#include "re2dj/hle/guest_file_prefetcher.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <new>
#include <system_error>
#include <vector>

#include <spdlog/logger.h>

#include "re2dj/hle/guest_files.h"
#include "re2dj/logging/logging.h"

namespace re2dj::hle
{

GuestFilePrefetcher::GuestFilePrefetcher(const GuestFileSource* source, std::uint64_t budget_bytes)
    : source_(source), budget_bytes_(budget_bytes)
{
}

GuestFilePrefetcher::~GuestFilePrefetcher()
{
    {
        const std::lock_guard<std::mutex> guard(lock_);
        stopping_ = true;
        for (const std::shared_ptr<Job>& job : queue_)
        {
            job->cancelled_.store(true, std::memory_order_release);
        }
        queue_.clear();
        if (current_ != nullptr)
        {
            current_->cancelled_.store(true, std::memory_order_release);
        }
    }
    changed_.notify_all();
    if (worker_.joinable())
    {
        worker_.join();
    }
    if (learn_worker_.joinable())
    {
        learn_worker_.join();
    }
}

std::shared_ptr<GuestFilePrefetcher::Job> GuestFilePrefetcher::Start(const std::string& path, std::uint64_t size)
{
    if (source_ == nullptr || size == 0)
    {
        return nullptr;
    }
    auto job = std::make_shared<Job>();
    job->path_ = path;
    job->size_ = size;
    {
        const std::lock_guard<std::mutex> guard(lock_);
        if (stopping_ || size > budget_bytes_ - std::min(held_bytes_, budget_bytes_))
        {
            return nullptr;
        }
        held_bytes_ += size;
        queue_.push_back(job);
        if (!worker_.joinable())
        {
            worker_ = std::thread([this] { Run(); });
        }
    }
    changed_.notify_all();
    const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
    if (logger != nullptr)
    {
        logger->info("files: prefetch {} ({} bytes)", path, size);
    }
    return job;
}

bool GuestFilePrefetcher::Copy(const Job& job, std::uint64_t offset, void* destination, std::size_t length)
{
    const std::uint64_t loaded = job.loaded();
    if (offset > loaded || length > loaded - offset)
    {
        return false;
    }
    if (length != 0)
    {
        std::memcpy(destination, job.bytes_.get() + offset, length);
    }
    return true;
}

void GuestFilePrefetcher::Cancel(const std::shared_ptr<Job>& job)
{
    if (job == nullptr)
    {
        return;
    }
    job->cancelled_.store(true, std::memory_order_release);
    const std::lock_guard<std::mutex> guard(lock_);
    Release(job.get());
    changed_.notify_all();
}

void GuestFilePrefetcher::BeginForeground()
{
    const std::lock_guard<std::mutex> guard(lock_);
    ++foreground_;
}

void GuestFilePrefetcher::EndForeground()
{
    const std::lock_guard<std::mutex> guard(lock_);
    --foreground_;
    if (foreground_ == 0)
    {
        changed_.notify_all();
    }
}

void GuestFilePrefetcher::Learn(std::filesystem::path journal_path)
{
    if (source_ == nullptr || journal_path.empty())
    {
        return;
    }
    std::vector<GuestFileReadJournal::Entry> list;
    {
        const std::lock_guard<std::mutex> guard(journal_lock_);
        if (!journal_path_.empty())
        {
            return;
        }
        journal_path_ = std::move(journal_path);
        std::ifstream stream(journal_path_, std::ios::binary);
        journal_.Load(stream);
        list = journal_.WarmList(kWarmBudgetBytes);
    }
    const std::lock_guard<std::mutex> guard(lock_);
    if (!stopping_)
    {
        learn_worker_ = std::thread([this, list = std::move(list)]() mutable { RunLearned(std::move(list)); });
    }
}

void GuestFilePrefetcher::Record(GuestFileReadJournal::Kind kind, std::string_view path, std::uint64_t extent)
{
    const std::lock_guard<std::mutex> guard(journal_lock_);
    if (!journal_path_.empty())
    {
        journal_.Record(kind, path, extent, std::chrono::steady_clock::now());
    }
}

void GuestFilePrefetcher::WaitWarmed()
{
    std::unique_lock<std::mutex> guard(lock_);
    changed_.wait(guard, [this] { return warmed_ || !learn_worker_.joinable(); });
}

void GuestFilePrefetcher::SaveJournal()
{
    const std::lock_guard<std::mutex> guard(journal_lock_);
    if (journal_path_.empty() || !journal_.dirty())
    {
        return;
    }
    // Merged into the file as it is now: another run may have saved since.
    GuestFileReadJournal saved;
    {
        std::ifstream stream(journal_path_, std::ios::binary);
        saved.Load(stream);
    }
    journal_.MergeInto(&saved);
    // The overlay is made by the guest's first write; a run that never
    // writes keeps no journal.
    std::error_code code;
    if (!std::filesystem::is_directory(journal_path_.parent_path(), code))
    {
        return;
    }
    std::filesystem::path temporary = journal_path_;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        saved.Save(stream);
        if (!stream.flush())
        {
            return;
        }
    }
    std::filesystem::rename(temporary, journal_path_, code);
    if (!code)
    {
        journal_.MarkSaved();
    }
}

bool GuestFilePrefetcher::WaitForQuiet()
{
    std::unique_lock<std::mutex> guard(lock_);
    changed_.wait(guard, [this] { return stopping_ || (foreground_ == 0 && !busy_); });
    return !stopping_;
}

void GuestFilePrefetcher::RunLearned(std::vector<GuestFileReadJournal::Entry> list)
{
    const auto started = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> buffer(kPieceBytes);
    std::size_t done = 0;
    std::uint64_t bytes = 0;
    for (const GuestFileReadJournal::Entry& entry : list)
    {
        bool directory = false;
        std::uint64_t size = 0;
        if (!WaitForQuiet())
        {
            break;
        }
        if (entry.kind == GuestFileReadJournal::Kind::kList)
        {
            std::vector<storage::Fat32Entry> entries;
            static_cast<void>(source_->ListDirectory(entry.path, &entries));
        }
        else if (source_->Find(entry.path, &directory, &size) && entry.kind == GuestFileReadJournal::Kind::kRead &&
                 !directory)
        {
            const std::uint64_t end = std::min(entry.extent, size);
            for (std::uint64_t offset = 0; offset < end && WaitForQuiet(); offset += kPieceBytes)
            {
                const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(kPieceBytes, end - offset));
                if (!source_->ReadRange(entry.path, offset, buffer.data(), length))
                {
                    break;
                }
                bytes += length;
            }
        }
        ++done;
    }
    {
        const std::lock_guard<std::mutex> guard(lock_);
        warmed_ = true;
    }
    changed_.notify_all();
    const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
    if (logger != nullptr && !list.empty())
    {
        logger->info("files: read {} of {} learned paths ahead ({} bytes) in {} ms", done, list.size(), bytes,
                     std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                         .count());
    }
    for (;;)
    {
        bool stopping = false;
        {
            std::unique_lock<std::mutex> guard(lock_);
            stopping = changed_.wait_for(guard, kJournalSaveInterval, [this] { return stopping_; });
        }
        SaveJournal();
        if (stopping)
        {
            return;
        }
    }
}

void GuestFilePrefetcher::WaitIdle()
{
    std::unique_lock<std::mutex> guard(lock_);
    changed_.wait(guard, [this] { return queue_.empty() && !busy_; });
}

void GuestFilePrefetcher::Release(Job* job)
{
    if (!job->released_)
    {
        job->released_ = true;
        held_bytes_ -= std::min(held_bytes_, job->size_);
    }
}

void GuestFilePrefetcher::Run()
{
    for (;;)
    {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> guard(lock_);
            busy_ = false;
            current_.reset();
            changed_.notify_all();
            changed_.wait(guard, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_)
            {
                return;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
            current_ = job;
            busy_ = true;
        }
        if (job->cancelled_.load(std::memory_order_acquire))
        {
            continue;
        }
        const auto started = std::chrono::steady_clock::now();
        job->bytes_.reset(new (std::nothrow) std::uint8_t[static_cast<std::size_t>(job->size_)]);
        bool failed = job->bytes_ == nullptr;
        std::uint64_t offset = 0;
        while (!failed && offset < job->size_ && !job->cancelled_.load(std::memory_order_acquire))
        {
            {
                std::unique_lock<std::mutex> guard(lock_);
                changed_.wait(guard, [this, &job] {
                    return foreground_ == 0 || stopping_ || job->cancelled_.load(std::memory_order_acquire);
                });
                if (stopping_ || job->cancelled_.load(std::memory_order_acquire))
                {
                    break;
                }
            }
            const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(kPieceBytes, job->size_ - offset));
            if (!source_->ReadRange(job->path_, offset, job->bytes_.get() + offset, length))
            {
                failed = true;
                break;
            }
            offset += length;
            job->loaded_.store(offset, std::memory_order_release);
        }
        job->failed_.store(failed, std::memory_order_release);
        job->finished_.store(true, std::memory_order_release);
        const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
        if (logger == nullptr)
        {
            continue;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        if (failed)
        {
            logger->warn("files: prefetch of {} failed at byte {}; reads go to the image", job->path_, offset);
        }
        else if (offset < job->size_)
        {
            logger->info("files: prefetch of {} stopped at {} of {} bytes (closed)", job->path_, offset, job->size_);
        }
        else
        {
            logger->info("files: prefetched {} in {} ms", job->path_, elapsed.count());
        }
    }
}

}  // namespace re2dj::hle
