#ifndef RE2DJ_HLE_GUEST_FILE_PREFETCHER_H_
#define RE2DJ_HLE_GUEST_FILE_PREFETCHER_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "re2dj/hle/guest_file_read_journal.h"

namespace re2dj::hle
{

class GuestFileSource;

// Reads image files into memory on a thread of its own (#9), so a guest that
// streams a file in small reads from its main thread (6th's music, 22,528
// bytes every ~133 ms) does not wait on the disk behind the CHD. The worker
// reads each file from the start in pieces and publishes how much it holds;
// a read inside that prefix is served from memory, one past it goes to the
// source as before. The source must allow calls from another thread.
//
// It also learns what to read before the guest asks (#24). The
// guest's calls into the source are kept in a GuestFileReadJournal saved
// beside the overlay; a run started with one reads the paths used more than
// once into the host's cache, on a worker of its own and only while no
// guest call is under way.
class GuestFilePrefetcher
{
public:
    // One file being read ahead. Shared by the worker and the open handle;
    // the memory goes when both let go.
    class Job
    {
    public:
        const std::string& path() const { return path_; }
        std::uint64_t size() const { return size_; }
        // How many bytes from the start are in memory.
        std::uint64_t loaded() const { return loaded_.load(std::memory_order_acquire); }
        bool failed() const { return failed_.load(std::memory_order_acquire); }
        bool finished() const { return finished_.load(std::memory_order_acquire); }

    private:
        friend class GuestFilePrefetcher;
        std::string path_;
        std::uint64_t size_ = 0;
        std::unique_ptr<std::uint8_t[]> bytes_;
        std::atomic<std::uint64_t> loaded_{0};
        std::atomic<bool> cancelled_{false};
        std::atomic<bool> failed_{false};
        std::atomic<bool> finished_{false};
        // Set once the job's bytes are given back to the budget.
        bool released_ = false;
    };

    // The piece the worker reads at a time: one 6th cluster, so a guest read
    // that needs the same source lock waits for one piece at most.
    static constexpr std::size_t kPieceBytes = 16 * 1024;
    // The memory all unfinished or still open jobs may hold together.
    static constexpr std::uint64_t kDefaultBudgetBytes = 256ull * 1024 * 1024;
    // How much the journal's paths may come to that a run reads ahead: the
    // paths every song loads come to a few MiB.
    static constexpr std::uint64_t kWarmBudgetBytes = 64ull * 1024 * 1024;
    // How often the journal is saved while it changes. A run may end without
    // destructors, through ExitProcess or a killed process.
    static constexpr std::chrono::seconds kJournalSaveInterval{15};

    explicit GuestFilePrefetcher(const GuestFileSource* source,
                                 std::uint64_t budget_bytes = kDefaultBudgetBytes);
    ~GuestFilePrefetcher();

    GuestFilePrefetcher(const GuestFilePrefetcher&) = delete;
    GuestFilePrefetcher& operator=(const GuestFilePrefetcher&) = delete;

    // Queues path (size bytes) for reading ahead, or nullptr when it would
    // take the jobs past the budget.
    std::shared_ptr<Job> Start(const std::string& path, std::uint64_t size);
    // Copies [offset, offset + length) when the job holds it.
    static bool Copy(const Job& job, std::uint64_t offset, void* destination, std::size_t length);
    // Stops reading at the next piece and gives the budget back.
    void Cancel(const std::shared_ptr<Job>& job);
    // Waits until the queue is empty and the worker idle (tests).
    void WaitIdle();
    // Brackets a guest call into the source. The worker does not start a
    // piece while one is under way, so the guest waits for one piece at most
    // instead of losing the source's lock to the worker piece after piece.
    void BeginForeground();
    void EndForeground();

    // Loads the journal at path, if there is one, starts reading its paths
    // ahead, and saves it there from then on. Once per prefetcher.
    void Learn(std::filesystem::path journal_path);
    // A guest call into the source, for the journal. extent is how far into
    // the file a read reached.
    void Record(GuestFileReadJournal::Kind kind, std::string_view path, std::uint64_t extent);
    // Waits until the journal's paths are read ahead (tests).
    void WaitWarmed();
    // Saves the journal now (tests; the learning worker saves on its own).
    void SaveJournal();

private:
    void Run();
    void Release(Job* job);
    void RunLearned(std::vector<GuestFileReadJournal::Entry> list);
    // Waits until no guest call or file job is under way; false when
    // stopping.
    bool WaitForQuiet();

    const GuestFileSource* source_ = nullptr;
    std::uint64_t budget_bytes_ = 0;
    std::mutex lock_;
    std::condition_variable changed_;
    std::deque<std::shared_ptr<Job>> queue_;
    // The job the worker is reading.
    std::shared_ptr<Job> current_;
    // Bytes held by jobs not yet cancelled.
    std::uint64_t held_bytes_ = 0;
    // Guest calls into the source under way.
    int foreground_ = 0;
    bool busy_ = false;
    bool stopping_ = false;
    std::thread worker_;

    // The journal and where it is saved, under journal_lock_; warmed_ under
    // lock_.
    std::mutex journal_lock_;
    GuestFileReadJournal journal_;
    std::filesystem::path journal_path_;
    bool warmed_ = false;
    std::thread learn_worker_;
};

}  // namespace re2dj::hle

#endif  // RE2DJ_HLE_GUEST_FILE_PREFETCHER_H_
