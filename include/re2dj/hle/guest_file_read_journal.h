#ifndef RE2DJ_HLE_GUEST_FILE_READ_JOURNAL_H_
#define RE2DJ_HLE_GUEST_FILE_READ_JOURNAL_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace re2dj::hle
{

// What the guest read from the image, kept across runs so the next run can
// read it ahead (#24). On a CHD on a hard disk a song's loading
// waits on a seek for nearly every file it touches for the first time, and
// most of those files (panels, effects, rankings, the directories' own
// entries) are the same for every song. A path the guest comes back to,
// after a pause or in a later run, is one worth having in the host's cache
// before the guest asks for it.
//
// Not thread-safe: the owner serializes it.
class GuestFileReadJournal
{
public:
    enum class Kind : char
    {
        kFind = 'f',
        kList = 'l',
        kRead = 'r',
    };

    struct Entry
    {
        Kind kind = Kind::kRead;
        std::string path;
        // For a read, how far into the file the guest read.
        std::uint64_t extent = 0;
        // How many times the guest came to it: once per run, and once more
        // each time it comes back after kUseGap without it.
        std::uint32_t uses = 0;
    };

    // Entries past this are not added; the file stays a few hundred KiB.
    static constexpr std::size_t kMaximumEntries = 8192;
    // A path untouched this long and read again counts as another use: a
    // song's loading reads its files within a few seconds, and the next
    // song's comes tens of seconds later.
    static constexpr std::chrono::seconds kUseGap{10};

    // Adds the entries of a journal file; lines that do not parse are left
    // out.
    void Load(std::istream& stream);
    // One line per entry, in the order the guest first came to them.
    void Save(std::ostream& stream) const;
    // Adds what this run recorded since the last MarkSaved to target: the
    // uses gained, longer extents and new entries. Saving merges into the
    // file as it is on disk, since another run on the same overlay (6th's
    // bootstrap waits on the game) may have saved since this one loaded.
    void MergeInto(GuestFileReadJournal* target) const;

    void Record(Kind kind, std::string_view path, std::uint64_t extent, std::chrono::steady_clock::time_point now);

    // The entries used at least twice, the most used first while their reads
    // fit in budget_bytes, given back in journal order.
    std::vector<Entry> WarmList(std::uint64_t budget_bytes) const;

    const std::vector<Entry>& entries() const { return entries_; }
    // Whether anything changed since the last MarkSaved.
    bool dirty() const { return dirty_; }
    void MarkSaved();

private:
    std::vector<Entry> entries_;
    // When this run last came to each entry, a default value for none yet,
    // and the uses gained since the last MarkSaved.
    std::vector<std::chrono::steady_clock::time_point> last_used_;
    std::vector<std::uint32_t> pending_uses_;
    // Kind and upper-cased path to the entry's index.
    std::unordered_map<std::string, std::size_t> index_;
    bool dirty_ = false;
};

}  // namespace re2dj::hle

#endif  // RE2DJ_HLE_GUEST_FILE_READ_JOURNAL_H_
