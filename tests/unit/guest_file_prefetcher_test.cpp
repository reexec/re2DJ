#include "re2dj/hle/guest_file_prefetcher.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "re2dj/hle/guest_files.h"
#include "re2dj/hle/win32_errors.h"
#include "re2dj/storage/guest_path.h"

#include "test_support.h"

namespace
{

namespace hle = re2dj::hle;

constexpr std::uint64_t kLargeBytes = 1024 * 1024;
constexpr std::uint64_t kSmallBytes = 100 * 1024;

std::uint8_t PatternByte(std::uint64_t offset)
{
    return static_cast<std::uint8_t>((offset * 7 + offset / 251) & 0xFF);
}

// Two patterned files. Reads from threads other than the one that made the
// source (the prefetcher's worker) are counted apart and can be held back.
class StreamSource final : public hle::GuestFileSource
{
public:
    StreamSource() : owner_(std::this_thread::get_id()) {}

    bool Find(std::string_view relative_path, bool* directory, std::uint64_t* size) const override
    {
        const std::uint64_t found = SizeOf(relative_path);
        if (found == 0)
        {
            return false;
        }
        *directory = false;
        *size = found;
        return true;
    }
    bool ReadRange(std::string_view relative_path, std::uint64_t offset, void* destination,
                   std::size_t length) const override
    {
        const std::uint64_t size = SizeOf(relative_path);
        if (size == 0 || offset + length > size)
        {
            return false;
        }
        if (std::this_thread::get_id() == owner_)
        {
            ++owner_reads;
        }
        else
        {
            std::unique_lock<std::mutex> guard(lock_);
            ++worker_waiting_;
            changed_.notify_all();
            changed_.wait(guard, [this] { return !held_; });
            --worker_waiting_;
            ++worker_reads;
        }
        auto* bytes = static_cast<std::uint8_t*>(destination);
        for (std::size_t index = 0; index < length; ++index)
        {
            bytes[index] = PatternByte(offset + index);
        }
        return true;
    }
    bool Materialize(std::string_view relative_path, const std::filesystem::path& output) const override
    {
        (void)relative_path;
        (void)output;
        return false;
    }
    bool ListDirectory(std::string_view relative_path, std::vector<re2dj::storage::Fat32Entry>* entries) const override
    {
        (void)relative_path;
        (void)entries;
        return false;
    }

    void Hold()
    {
        const std::lock_guard<std::mutex> guard(lock_);
        held_ = true;
    }
    // Notifies under the lock: a test may destroy the source as soon as the
    // worker runs on.
    void Release()
    {
        const std::lock_guard<std::mutex> guard(lock_);
        held_ = false;
        changed_.notify_all();
    }
    // Waits until the worker sits in a held read.
    void WaitForHeldWorker()
    {
        std::unique_lock<std::mutex> guard(lock_);
        changed_.wait(guard, [this] { return worker_waiting_ != 0; });
    }

    mutable std::atomic<int> owner_reads{0};
    mutable std::atomic<int> worker_reads{0};

private:
    static std::uint64_t SizeOf(std::string_view relative_path)
    {
        if (re2dj::storage::EqualsIgnoreAsciiCase(relative_path, "EZ2DJ/BGM.EZW"))
        {
            return kLargeBytes;
        }
        if (re2dj::storage::EqualsIgnoreAsciiCase(relative_path, "EZ2DJ/KEY.EZW"))
        {
            return kSmallBytes;
        }
        return 0;
    }

    std::thread::id owner_;
    mutable std::mutex lock_;
    mutable std::condition_variable changed_;
    mutable int worker_waiting_ = 0;
    bool held_ = false;
};

struct Fixture
{
    Fixture()
    {
        overlay = std::filesystem::temp_directory_path() /
                  ("re2dj-prefetch-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::error_code code;
        std::filesystem::remove_all(overlay, code);
        auto owned = std::make_unique<StreamSource>();
        source = owned.get();
        hle::GuestFileConfig config;
        config.chd_root = "EZ2DJ";
        config.guest_root = "D:\\ez2dj";
        config.overlay_root = overlay;
        std::string error;
        configured = files.Configure(config, std::move(owned), &error);
    }
    ~Fixture()
    {
        files.WaitForPrefetch();
        std::error_code code;
        std::filesystem::remove_all(overlay, code);
    }

    // Reads size bytes and checks they are the pattern at the position.
    bool ReadMatches(std::uint32_t handle, std::uint64_t position, std::uint32_t size)
    {
        std::vector<std::uint8_t> bytes;
        if (files.Read(handle, size, &bytes) != hle::kWin32ErrorSuccess || bytes.size() != size)
        {
            return false;
        }
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            if (bytes[index] != PatternByte(position + index))
            {
                return false;
            }
        }
        return true;
    }

    std::filesystem::path overlay;
    hle::GuestFiles files;
    StreamSource* source = nullptr;
    bool configured = false;
};

// A file streamed in small reads is read ahead; once it is, reads no longer
// reach the source and still return the file's bytes.
void CheckStreamedFile(re2dj::test::Context& context)
{
    Fixture fixture;
    RE2DJ_CHECK(context, fixture.configured);
    const auto open = fixture.files.Open("BGM.EZW", true, false, hle::kOpenExisting);
    RE2DJ_CHECK(context, open.handle != 0);
    RE2DJ_CHECK(context, fixture.ReadMatches(open.handle, 0, 4096));
    fixture.files.WaitForPrefetch();
    RE2DJ_CHECK_EQ(context, fixture.files.PrefetchedBytes(open.handle), kLargeBytes);
    const int direct = fixture.source->owner_reads.load();
    std::uint64_t position = 0;
    RE2DJ_CHECK_EQ(context, fixture.files.Seek(open.handle, 0x42012, 0, &position), hle::kWin32ErrorSuccess);
    bool matches = true;
    while (position + 22528 <= kLargeBytes)
    {
        matches = matches && fixture.ReadMatches(open.handle, position, 22528);
        position += 22528;
    }
    RE2DJ_CHECK(context, matches);
    RE2DJ_CHECK_EQ(context, fixture.source->owner_reads.load(), direct);
    // The worker read the file once, in pieces.
    RE2DJ_CHECK_EQ(context, fixture.source->worker_reads.load(),
                   static_cast<int>(kLargeBytes / hle::GuestFilePrefetcher::kPieceBytes));
    RE2DJ_CHECK(context, fixture.files.Close(open.handle));
}

// A file read whole at once, and a small one, are not read ahead.
void CheckFilesNotPrefetched(re2dj::test::Context& context)
{
    Fixture fixture;
    const auto whole = fixture.files.Open("BGM.EZW", true, false, hle::kOpenExisting);
    RE2DJ_CHECK(context, fixture.ReadMatches(whole.handle, 0, static_cast<std::uint32_t>(kLargeBytes)));
    const auto small = fixture.files.Open("KEY.EZW", true, false, hle::kOpenExisting);
    RE2DJ_CHECK(context, fixture.ReadMatches(small.handle, 0, 4096));
    fixture.files.WaitForPrefetch();
    RE2DJ_CHECK_EQ(context, fixture.files.PrefetchedBytes(whole.handle), std::uint64_t{0});
    RE2DJ_CHECK_EQ(context, fixture.files.PrefetchedBytes(small.handle), std::uint64_t{0});
    RE2DJ_CHECK_EQ(context, fixture.source->worker_reads.load(), 0);
}

// While the worker is held, reads go to the source on the calling thread and
// return the same bytes; closing the handle stops the worker after its piece.
void CheckReadsWhilePrefetching(re2dj::test::Context& context)
{
    Fixture fixture;
    fixture.source->Hold();
    const auto open = fixture.files.Open("BGM.EZW", true, false, hle::kOpenExisting);
    RE2DJ_CHECK(context, fixture.ReadMatches(open.handle, 0, 4096));
    fixture.source->WaitForHeldWorker();
    const int direct = fixture.source->owner_reads.load();
    RE2DJ_CHECK(context, fixture.ReadMatches(open.handle, 4096, 22528));
    RE2DJ_CHECK_EQ(context, fixture.source->owner_reads.load(), direct + 1);
    RE2DJ_CHECK_EQ(context, fixture.files.PrefetchedBytes(open.handle), std::uint64_t{0});
    RE2DJ_CHECK(context, fixture.files.Close(open.handle));
    fixture.source->Release();
    fixture.files.WaitForPrefetch();
    RE2DJ_CHECK_EQ(context, fixture.source->worker_reads.load(), 1);
}

// Jobs past the budget are refused until a cancelled one gives memory back.
void CheckBudget(re2dj::test::Context& context)
{
    StreamSource source;
    hle::GuestFilePrefetcher prefetcher(&source, kLargeBytes + kLargeBytes / 2);
    const auto first = prefetcher.Start("EZ2DJ/BGM.EZW", kLargeBytes);
    RE2DJ_CHECK(context, first != nullptr);
    RE2DJ_CHECK(context, prefetcher.Start("EZ2DJ/BGM.EZW", kLargeBytes) == nullptr);
    prefetcher.WaitIdle();
    RE2DJ_CHECK(context, first->finished() && !first->failed());
    std::vector<std::uint8_t> bytes(16);
    RE2DJ_CHECK(context, hle::GuestFilePrefetcher::Copy(*first, kLargeBytes - 16, bytes.data(), bytes.size()));
    RE2DJ_CHECK_EQ(context, bytes[0], PatternByte(kLargeBytes - 16));
    RE2DJ_CHECK(context, !hle::GuestFilePrefetcher::Copy(*first, kLargeBytes - 8, bytes.data(), bytes.size()));
    prefetcher.Cancel(first);
    prefetcher.Cancel(first);
    const auto second = prefetcher.Start("EZ2DJ/BGM.EZW", kLargeBytes);
    RE2DJ_CHECK(context, second != nullptr);
    prefetcher.WaitIdle();
    // A read the source refuses ends the job as failed.
    const auto missing = prefetcher.Start("EZ2DJ/NONE.EZW", 64);
    prefetcher.WaitIdle();
    RE2DJ_CHECK(context, missing != nullptr && missing->finished() && missing->failed());
    RE2DJ_CHECK_EQ(context, missing->loaded(), std::uint64_t{0});
}

// The worker starts no piece while a guest call into the source is under
// way, and goes on once it ends.
void CheckWorkerStepsAside(re2dj::test::Context& context)
{
    StreamSource source;
    hle::GuestFilePrefetcher prefetcher(&source);
    prefetcher.BeginForeground();
    const auto job = prefetcher.Start("EZ2DJ/BGM.EZW", kLargeBytes);
    RE2DJ_CHECK(context, job != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    RE2DJ_CHECK_EQ(context, source.worker_reads.load(), 0);
    RE2DJ_CHECK_EQ(context, job->loaded(), std::uint64_t{0});
    prefetcher.EndForeground();
    prefetcher.WaitIdle();
    RE2DJ_CHECK_EQ(context, job->loaded(), kLargeBytes);
}

// A journal left by earlier runs is read ahead: only the paths used twice.
// The guest's own calls go into it, and the run's end merges them into the
// file.
void CheckLearnedReadAhead(re2dj::test::Context& context)
{
    const std::filesystem::path overlay =
        std::filesystem::temp_directory_path() / ("re2dj-learn-" + std::to_string(reinterpret_cast<std::uintptr_t>(&context)));
    std::error_code code;
    std::filesystem::remove_all(overlay, code);
    std::filesystem::create_directories(overlay, code);
    {
        std::ofstream journal(overlay / hle::kReadJournalName, std::ios::binary);
        journal << "2\tr\t65536\tEZ2DJ/BGM.EZW\n1\tr\t4096\tEZ2DJ/KEY.EZW\n";
    }
    StreamSource* source = nullptr;
    {
        auto owned = std::make_unique<StreamSource>();
        source = owned.get();
        hle::GuestFileConfig config;
        config.chd_root = "EZ2DJ";
        config.guest_root = "D:\\ez2dj";
        config.overlay_root = overlay;
        hle::GuestFiles files;
        std::string error;
        RE2DJ_CHECK(context, files.Configure(config, std::move(owned), &error));
        files.WaitForPrefetch();
        RE2DJ_CHECK_EQ(context, source->worker_reads.load(),
                       static_cast<int>(65536 / hle::GuestFilePrefetcher::kPieceBytes));
        const auto open = files.Open("KEY.EZW", true, false, hle::kOpenExisting);
        std::vector<std::uint8_t> bytes;
        RE2DJ_CHECK_EQ(context, files.Read(open.handle, 4096, &bytes), hle::kWin32ErrorSuccess);
        RE2DJ_CHECK(context, files.Close(open.handle));
    }
    re2dj::hle::GuestFileReadJournal saved;
    {
        std::ifstream journal(overlay / hle::kReadJournalName, std::ios::binary);
        saved.Load(journal);
    }
    bool key_used_twice = false;
    bool key_found = false;
    for (const auto& entry : saved.entries())
    {
        key_used_twice = key_used_twice || (entry.kind == hle::GuestFileReadJournal::Kind::kRead &&
                                            entry.path == "EZ2DJ/KEY.EZW" && entry.uses == 2);
        key_found = key_found || (entry.kind == hle::GuestFileReadJournal::Kind::kFind && entry.path == "EZ2DJ/KEY.EZW");
    }
    RE2DJ_CHECK(context, key_used_twice);
    RE2DJ_CHECK(context, key_found);
    RE2DJ_CHECK(context, !std::filesystem::exists(overlay / (std::string(hle::kReadJournalName) + ".tmp"), code));
    std::filesystem::remove_all(overlay, code);
}

// Closing with a held job, then destroying everything, does not hang.
void CheckShutdownWhileHeld(re2dj::test::Context& context)
{
    auto fixture = std::make_unique<Fixture>();
    fixture->source->Hold();
    const auto open = fixture->files.Open("BGM.EZW", true, false, hle::kOpenExisting);
    RE2DJ_CHECK(context, fixture->ReadMatches(open.handle, 0, 4096));
    fixture->source->WaitForHeldWorker();
    std::thread release([source = fixture->source] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        source->Release();
    });
    fixture.reset();
    release.join();
    RE2DJ_CHECK(context, true);
}

}  // namespace

void RunGuestFilePrefetcherTests(re2dj::test::Context& context)
{
    CheckStreamedFile(context);
    CheckFilesNotPrefetched(context);
    CheckReadsWhilePrefetching(context);
    CheckBudget(context);
    CheckWorkerStepsAside(context);
    CheckLearnedReadAhead(context);
    CheckShutdownWhileHeld(context);
}
