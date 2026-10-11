#ifndef RE2DJ_HLE_GUEST_FILES_H_
#define RE2DJ_HLE_GUEST_FILES_H_

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "re2dj/hle/guest_file_prefetcher.h"
#include "re2dj/hle/guest_handles.h"
#include "re2dj/storage/fat32_chd.h"
#include "re2dj/storage/guest_find.h"
#include "re2dj/storage/guest_path.h"

namespace re2dj::hle
{

// Where guest files come from: the CHD image, read-only, with writes going to
// a host overlay directory, as on the Windows path.
struct GuestFileConfig
{
    // The CHD the files come from; empty for a directory dump.
    std::filesystem::path chd_image;
    // A directory dump's host directory, used when there is no CHD. With
    // neither, the run provides no files.
    std::filesystem::path hdd_directory;
    // The product's directory inside the image, for example "EZ2DJ".
    std::string chd_root;
    // The guest's own name for that directory, for example "D:\ez2dj"; it is
    // also the guest's first current directory.
    std::string guest_root;
    // Host directory writes go to, mirroring the directory under chd_root.
    std::filesystem::path overlay_root;
};

// The read-only image guest files come from, below chd_root. Production uses
// the CHD's FAT32 volume; tests supply their own.
class GuestFileSource
{
public:
    virtual ~GuestFileSource() = default;
    // Case-insensitive lookup of a '/'-separated path.
    virtual bool Find(std::string_view relative_path, bool* directory, std::uint64_t* size) const = 0;
    virtual bool ReadRange(std::string_view relative_path,
                           std::uint64_t offset,
                           void* destination,
                           std::size_t length) const = 0;
    // Copies the whole file to a host path.
    virtual bool Materialize(std::string_view relative_path, const std::filesystem::path& output) const = 0;
    // A directory's entries in the image's own order, "." and ".." left out;
    // false when the path is not a directory.
    virtual bool ListDirectory(std::string_view relative_path, std::vector<storage::Fat32Entry>* entries) const = 0;
};

// The overlay file listing the image files the guest deleted.
inline constexpr const char* kDeletedListName = ".re2dj-deleted";
// The overlay file of the prefetcher's GuestFileReadJournal.
inline constexpr const char* kReadJournalName = ".re2dj-readahead";

// CreateFile dispositions (fileapi.h).
inline constexpr std::uint32_t kCreateNew = 1;
inline constexpr std::uint32_t kCreateAlways = 2;
inline constexpr std::uint32_t kOpenExisting = 3;
inline constexpr std::uint32_t kOpenAlways = 4;
inline constexpr std::uint32_t kTruncateExisting = 5;

// The guest's open files. Reads come from the overlay when it holds the file,
// otherwise from the CHD; opening for write first copies the CHD file into
// the overlay. Handles come from the guest's shared handle space. Results are
// Win32 error codes, 0 for success.
//
// Overlay paths ignore case as NTFS does: each component takes its exact
// spelling when present, else an existing entry matching without case. An
// image file the guest deletes is listed in the overlay's kDeletedListName
// and counts as missing from then on, for this run and the runs after it
// (task 437).
class GuestFiles
{
public:
    // What Open did: a handle, or 0 with a Win32 error; outside_root marks a
    // path this model does not serve, which the caller stops on.
    struct OpenResult
    {
        std::uint32_t handle = 0;
        std::uint32_t error = 0;
        bool outside_root = false;
    };

    GuestFiles() = default;
    ~GuestFiles();
    GuestFiles(const GuestFiles&) = delete;
    GuestFiles& operator=(const GuestFiles&) = delete;

    // Opens the CHD. With an empty chd_image the set provides no files.
    bool Configure(GuestFileConfig config, std::string* error);
    // Uses source in place of the CHD; config.chd_image is ignored.
    bool Configure(GuestFileConfig config, std::unique_ptr<GuestFileSource> source, std::string* error);
    bool configured() const { return source_ != nullptr; }
    void SetHandleAllocator(GuestHandleAllocator* allocator) { handles_ = allocator; }

    // The image path of a guest file, as the host starts a child process's
    // executable from it: chd_root joined with the path below the root, for
    // example "EZ2DJ/EZ2DJ6th.EXE". Relative paths resolve against the
    // current directory. ERROR_FILE_NOT_FOUND when the image holds no such
    // file; outside_root marks a path this model does not serve.
    std::uint32_t ImagePath(std::string_view guest_path, std::string* image_path, bool* outside_root) const;

    // Relative paths resolve against the current directory.
    OpenResult Open(std::string_view guest_path, bool read, bool write, std::uint32_t disposition);

    // The guest's current directory as GetCurrentDirectoryA renders it: the
    // components as the guest wrote them, with no trailing separator.
    std::string CurrentDirectory() const;
    // GetFullPathNameA's path: guest_path resolved against the current
    // directory, "." and ".." applied and '/' written as '\\', keeping a
    // trailing separator. It is a string operation: the file need not exist.
    // False for a path that does not parse or climbs above its drive's root.
    bool FullPath(std::string_view guest_path, std::string* full) const;
    // SetCurrentDirectoryA's rules, as Windows 11 applies them: the path
    // resolves against the current directory ('/' separates too, "." and
    // ".." apply); ERROR_INVALID_NAME for an empty or malformed path,
    // ERROR_FILE_NOT_FOUND when the last component is missing,
    // ERROR_PATH_NOT_FOUND when an earlier one is, and ERROR_DIRECTORY when
    // it names a file. 0 moves the current directory. A directory outside the
    // guest root is not served: outside_root is set and nothing changes.
    std::uint32_t SetCurrentDirectory(std::string_view guest_path, bool* outside_root);
    // DeleteFile: removes the overlay copy and lists an image file as
    // deleted. ERROR_FILE_NOT_FOUND or ERROR_PATH_NOT_FOUND for a missing
    // file, ERROR_ACCESS_DENIED for a directory, ERROR_SHARING_VIOLATION while
    // this process holds it open (share modes are not modelled).
    std::uint32_t Delete(std::string_view guest_path, bool* outside_root);

    // GetFileAttributesA: the path resolves like any other, then
    // storage::DescribeGuestFileAttributes walks it through the overlay and
    // the image. An empty name is ERROR_PATH_NOT_FOUND and a malformed one
    // ERROR_INVALID_NAME, as Windows 11 reports them. A path outside the
    // guest root is not served: outside_root is set.
    storage::GuestFileAttributes Attributes(std::string_view guest_path, bool* outside_root) const;

    // FindFirstFileA's search: the directory part resolves like any path;
    // the last part is the pattern (storage/guest_find.h), matched against
    // the image directory's entries in their order, as the Windows product's
    // VFS lists them. Errors as Windows 11 reports them: an empty name or a
    // missing directory is ERROR_PATH_NOT_FOUND, an empty pattern (a name
    // ending in a separator) ERROR_INVALID_PARAMETER, and no match
    // ERROR_FILE_NOT_FOUND. A directory outside the guest root is not served.
    struct FindResult
    {
        std::uint32_t handle = 0;
        std::uint32_t error = 0;
        bool outside_root = false;
        storage::GuestFindData first;
    };
    FindResult FindFirst(std::string_view guest_pattern);
    // The next match: 0, ERROR_NO_MORE_FILES at the end, or
    // ERROR_INVALID_HANDLE for a handle that is not an open search.
    std::uint32_t FindNext(std::uint32_t handle, storage::GuestFindData* data);
    bool FindClose(std::uint32_t handle);
    bool IsOpen(std::uint32_t handle) const;
    std::uint32_t Read(std::uint32_t handle, std::uint32_t size, std::vector<std::uint8_t>* bytes);
    std::uint32_t Write(std::uint32_t handle, std::span<const std::uint8_t> bytes);
    // SetFilePointer's move: method 0 begin, 1 current, 2 end.
    std::uint32_t Seek(std::uint32_t handle, std::int64_t distance, std::uint32_t method,
                       std::uint64_t* position);
    std::uint32_t Size(std::uint32_t handle, std::uint64_t* size) const;
    bool Close(std::uint32_t handle);

    // An image file opened read-only whose first Read leaves at least this
    // much unread is read ahead in the background (#9): a guest streaming it
    // in small reads then reads memory instead of waiting on the disk. A
    // file read whole at once is not, so its data is not read twice.
    static constexpr std::uint64_t kPrefetchMinimumRemaining = 256 * 1024;
    // How many bytes from the start of an open file are read ahead (tests).
    std::uint64_t PrefetchedBytes(std::uint32_t handle) const;
    // Waits until the background reads, the learned ones included, are done
    // (tests).
    void WaitForPrefetch();

private:
    struct File
    {
        // The path below the root, upper-cased, to refuse deleting it.
        std::string relative_key;
        // CHD files are read in place; overlay files are host FILE streams.
        std::string chd_relative;
        std::FILE* host = nullptr;
        std::uint64_t size = 0;
        std::uint64_t position = 0;
        bool read = false;
        bool write = false;
        // Whether the first Read decided on reading ahead, and the job.
        bool prefetch_considered = false;
        std::shared_ptr<GuestFilePrefetcher::Job> prefetch;
    };

    // The path below the guest root, '/'-separated, or false when the path
    // lies outside it.
    bool RelativeToRoot(std::string_view guest_path, std::string* relative) const;
    // The combined path's components below the root, or false when it lies
    // outside it; the root itself is allowed when allow_root is set.
    bool BelowRoot(const storage::GuestPath& combined, bool allow_root, std::vector<std::string>* below) const;
    // Whether a '/'-separated path below the root is a directory, a file, or
    // missing, in the overlay or the image.
    enum class Entry
    {
        kMissing,
        kFile,
        kDirectory,
    };
    Entry Lookup(const std::string& relative) const;
    // The overlay's host path for a path below the root, matching existing
    // components without case.
    std::filesystem::path OverlayPath(const std::string& relative) const;
    // Whether the image holds the path and it is not listed as deleted.
    bool InImage(const std::string& relative, bool* directory, std::uint64_t* size) const;
    bool ListedDeleted(const std::string& relative) const;
    void LoadDeletedList();
    bool SaveDeletedList() const;
    std::uint32_t OpenHost(const std::filesystem::path& path, bool write, bool truncate, File* file) const;

    GuestFileConfig config_;
    std::unique_ptr<GuestFileSource> source_;
    // Reads the source source_ wraps, so it is declared after it and goes
    // first.
    std::unique_ptr<GuestFilePrefetcher> prefetcher_;
    storage::GuestPath root_;
    storage::GuestPath current_;
    GuestHandleAllocator own_handles_;
    GuestHandleAllocator* handles_ = nullptr;
    std::map<std::uint32_t, File> files_;
    // Image files the guest deleted: paths below the root, upper-cased.
    std::set<std::string> deleted_;
    struct Search
    {
        std::vector<storage::Fat32Entry> matches;
        std::size_t next = 0;
    };
    std::map<std::uint32_t, Search> searches_;
};

}  // namespace re2dj::hle

#endif  // RE2DJ_HLE_GUEST_FILES_H_
