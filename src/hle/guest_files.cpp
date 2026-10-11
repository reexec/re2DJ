#include "re2dj/hle/guest_files.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>

#include "re2dj/hle/win32_errors.h"

namespace re2dj::hle
{
namespace
{

// A path below the root as the deleted list keys it: case does not count.
std::string DeletedKey(std::string_view relative)
{
    std::string key(relative);
    for (char& c : key)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return key;
}

// The CHD's FAT32 volume as a file source.
class Fat32FileSource final : public GuestFileSource
{
public:
    explicit Fat32FileSource(std::unique_ptr<storage::Fat32Volume> volume) : volume_(std::move(volume)) {}

    bool Find(std::string_view relative_path, bool* directory, std::uint64_t* size) const override
    {
        storage::Fat32Entry entry;
        std::string error;
        if (!volume_->Find(relative_path, &entry, &error))
        {
            return false;
        }
        *directory = entry.directory;
        *size = entry.size;
        return true;
    }
    bool ReadRange(std::string_view relative_path,
                   std::uint64_t offset,
                   void* destination,
                   std::size_t length) const override
    {
        std::string error;
        return volume_->ReadFileRange(relative_path, offset, destination, length, &error);
    }
    bool Materialize(std::string_view relative_path, const std::filesystem::path& output) const override
    {
        std::string error;
        return volume_->MaterializeFile(relative_path, output, &error);
    }
    bool ListDirectory(std::string_view relative_path, std::vector<storage::Fat32Entry>* entries) const override
    {
        std::string error;
        return volume_->ReadDirectory(relative_path, entries, &error);
    }

private:
    std::unique_ptr<storage::Fat32Volume> volume_;
};

// The guest's own calls into the image source, marked so the prefetcher's
// worker steps aside while one is under way (#9) and kept in its journal.
class ForegroundSource final : public GuestFileSource
{
public:
    ForegroundSource(std::unique_ptr<GuestFileSource> inner, GuestFilePrefetcher* prefetcher)
        : inner_(std::move(inner)), prefetcher_(prefetcher)
    {
    }

    bool Find(std::string_view relative_path, bool* directory, std::uint64_t* size) const override
    {
        const Scope scope(prefetcher_);
        prefetcher_->Record(GuestFileReadJournal::Kind::kFind, relative_path, 0);
        return inner_->Find(relative_path, directory, size);
    }
    bool ReadRange(std::string_view relative_path,
                   std::uint64_t offset,
                   void* destination,
                   std::size_t length) const override
    {
        const Scope scope(prefetcher_);
        prefetcher_->Record(GuestFileReadJournal::Kind::kRead, relative_path, offset + length);
        return inner_->ReadRange(relative_path, offset, destination, length);
    }
    bool Materialize(std::string_view relative_path, const std::filesystem::path& output) const override
    {
        const Scope scope(prefetcher_);
        return inner_->Materialize(relative_path, output);
    }
    bool ListDirectory(std::string_view relative_path, std::vector<storage::Fat32Entry>* entries) const override
    {
        const Scope scope(prefetcher_);
        prefetcher_->Record(GuestFileReadJournal::Kind::kList, relative_path, 0);
        return inner_->ListDirectory(relative_path, entries);
    }

private:
    class Scope
    {
    public:
        explicit Scope(GuestFilePrefetcher* prefetcher) : prefetcher_(prefetcher) { prefetcher_->BeginForeground(); }
        ~Scope() { prefetcher_->EndForeground(); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        GuestFilePrefetcher* prefetcher_;
    };

    std::unique_ptr<GuestFileSource> inner_;
    GuestFilePrefetcher* prefetcher_;
};

// A directory dump on the host as a file source. A host file system may tell
// case apart where the guest's does not, so each component is matched
// without case when its exact spelling is missing. Entries list in the order
// NTFS gives the Windows product, names compared without case; they carry no
// dates, since a dump's host times are when it was copied, not the image's.
class HostDirectorySource final : public GuestFileSource
{
public:
    explicit HostDirectorySource(std::filesystem::path root) : root_(std::move(root)) {}

    bool Find(std::string_view relative_path, bool* directory, std::uint64_t* size) const override
    {
        std::filesystem::path path;
        std::error_code code;
        if (!Resolve(relative_path, &path))
        {
            return false;
        }
        *directory = std::filesystem::is_directory(path, code);
        *size = *directory ? 0 : static_cast<std::uint64_t>(std::filesystem::file_size(path, code));
        return !code;
    }
    bool ReadRange(std::string_view relative_path,
                   std::uint64_t offset,
                   void* destination,
                   std::size_t length) const override
    {
        std::filesystem::path path;
        if (!Resolve(relative_path, &path))
        {
            return false;
        }
        std::ifstream stream(path, std::ios::binary);
        stream.seekg(static_cast<std::streamoff>(offset));
        stream.read(static_cast<char*>(destination), static_cast<std::streamsize>(length));
        return static_cast<std::size_t>(stream.gcount()) == length;
    }
    bool Materialize(std::string_view relative_path, const std::filesystem::path& output) const override
    {
        std::filesystem::path path;
        std::error_code code;
        return Resolve(relative_path, &path) &&
               std::filesystem::copy_file(path, output, std::filesystem::copy_options::overwrite_existing, code) &&
               !code;
    }
    bool ListDirectory(std::string_view relative_path, std::vector<storage::Fat32Entry>* entries) const override
    {
        std::filesystem::path path;
        std::error_code code;
        if (!Resolve(relative_path, &path) || !std::filesystem::is_directory(path, code))
        {
            return false;
        }
        entries->clear();
        for (const auto& item : std::filesystem::directory_iterator(path, code))
        {
            storage::Fat32Entry entry;
            entry.name = item.path().filename().string();
            entry.directory = item.is_directory(code);
            entry.size = entry.directory ? 0 : static_cast<std::uint32_t>(item.file_size(code));
            entries->push_back(std::move(entry));
        }
        const auto upper_less = [](char left, char right) {
            return std::toupper(static_cast<unsigned char>(left)) < std::toupper(static_cast<unsigned char>(right));
        };
        std::sort(entries->begin(), entries->end(),
                  [&](const storage::Fat32Entry& left, const storage::Fat32Entry& right) {
                      return std::lexicographical_compare(left.name.begin(), left.name.end(), right.name.begin(),
                                                          right.name.end(), upper_less);
                  });
        return !code;
    }

private:
    bool Resolve(std::string_view relative_path, std::filesystem::path* resolved) const
    {
        std::filesystem::path path = root_;
        std::size_t start = 0;
        while (start <= relative_path.size())
        {
            const std::size_t slash = relative_path.find('/', start);
            const std::size_t end = slash == std::string_view::npos ? relative_path.size() : slash;
            const std::string_view component = relative_path.substr(start, end - start);
            if (!component.empty())
            {
                std::error_code code;
                std::filesystem::path exact = path / std::string(component);
                if (std::filesystem::exists(exact, code))
                {
                    path = std::move(exact);
                }
                else
                {
                    bool found = false;
                    for (const auto& item : std::filesystem::directory_iterator(path, code))
                    {
                        if (storage::EqualsIgnoreAsciiCase(item.path().filename().string(), component))
                        {
                            path = item.path();
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        return false;
                    }
                }
            }
            if (slash == std::string_view::npos)
            {
                break;
            }
            start = slash + 1;
        }
        *resolved = std::move(path);
        return true;
    }

    std::filesystem::path root_;
};

}  // namespace

GuestFiles::~GuestFiles()
{
    for (auto& [handle, file] : files_)
    {
        if (file.host != nullptr)
        {
            std::fclose(file.host);
        }
    }
}

bool GuestFiles::Configure(GuestFileConfig config, std::string* error)
{
    if (config.chd_image.empty())
    {
        if (!config.hdd_directory.empty())
        {
            std::filesystem::path directory = config.hdd_directory;
            return Configure(std::move(config), std::make_unique<HostDirectorySource>(std::move(directory)), error);
        }
        config_ = std::move(config);
        source_.reset();
        return true;
    }
    std::unique_ptr<storage::Fat32Volume> volume;
    if (!storage::Fat32Volume::Open(config.chd_image, &volume, error))
    {
        return false;
    }
    return Configure(std::move(config), std::make_unique<Fat32FileSource>(std::move(volume)), error);
}

bool GuestFiles::Configure(GuestFileConfig config,
                           std::unique_ptr<GuestFileSource> source,
                           std::string* error)
{
    config_ = std::move(config);
    prefetcher_.reset();
    source_.reset();
    if (!storage::ParseGuestPath(config_.guest_root, &root_) ||
        root_.kind != storage::GuestPathKind::kDriveAbsolute || !storage::NormalizeGuestPath(&root_))
    {
        if (error != nullptr) *error = "guest root is not a drive-absolute path: " + config_.guest_root;
        return false;
    }
    current_ = root_;
    // The worker reads the source itself; the guest's calls go through the
    // wrapper that makes it step aside. prefetcher_ goes before source_, which
    // owns the source it reads.
    prefetcher_ = std::make_unique<GuestFilePrefetcher>(source.get());
    source_ = std::make_unique<ForegroundSource>(std::move(source), prefetcher_.get());
    LoadDeletedList();
    if (!config_.overlay_root.empty())
    {
        prefetcher_->Learn(config_.overlay_root / kReadJournalName);
    }
    return true;
}

std::filesystem::path GuestFiles::OverlayPath(const std::string& relative) const
{
    std::filesystem::path path = config_.overlay_root;
    bool resolving = true;
    std::size_t start = 0;
    while (start <= relative.size())
    {
        const std::size_t slash = relative.find('/', start);
        const std::size_t end = slash == std::string::npos ? relative.size() : slash;
        const std::string component = relative.substr(start, end - start);
        if (!component.empty())
        {
            std::error_code code;
            std::filesystem::path exact = path / component;
            if (resolving && !std::filesystem::exists(exact, code))
            {
                // NTFS ignores case: an existing entry of another spelling is
                // the same one. Past the first missing component everything
                // takes the spelling given.
                resolving = false;
                for (const auto& item : std::filesystem::directory_iterator(path, code))
                {
                    if (storage::EqualsIgnoreAsciiCase(item.path().filename().string(), component))
                    {
                        exact = item.path();
                        resolving = true;
                        break;
                    }
                }
            }
            path = std::move(exact);
        }
        if (slash == std::string::npos)
        {
            break;
        }
        start = slash + 1;
    }
    return path;
}

bool GuestFiles::ListedDeleted(const std::string& relative) const
{
    return !deleted_.empty() && deleted_.count(DeletedKey(relative)) == 1;
}

bool GuestFiles::InImage(const std::string& relative, bool* directory, std::uint64_t* size) const
{
    const std::string chd_relative = config_.chd_root.empty() ? relative : config_.chd_root + "/" + relative;
    return source_ != nullptr && source_->Find(chd_relative, directory, size) &&
           (*directory || !ListedDeleted(relative));
}

void GuestFiles::LoadDeletedList()
{
    deleted_.clear();
    if (config_.overlay_root.empty())
    {
        return;
    }
    std::ifstream stream(config_.overlay_root / kDeletedListName);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (!line.empty())
        {
            deleted_.insert(DeletedKey(line));
        }
    }
}

bool GuestFiles::SaveDeletedList() const
{
    std::error_code code;
    std::filesystem::create_directories(config_.overlay_root, code);
    std::ofstream stream(config_.overlay_root / kDeletedListName, std::ios::binary | std::ios::trunc);
    for (const std::string& key : deleted_)
    {
        stream << key << '\n';
    }
    return static_cast<bool>(stream);
}

bool GuestFiles::BelowRoot(const storage::GuestPath& combined,
                           bool allow_root,
                           std::vector<std::string>* below) const
{
    if (combined.drive_letter != root_.drive_letter || combined.components.size() < root_.components.size() ||
        (!allow_root && combined.components.size() == root_.components.size()))
    {
        return false;
    }
    for (std::size_t index = 0; index < root_.components.size(); ++index)
    {
        if (!storage::EqualsIgnoreAsciiCase(combined.components[index], root_.components[index]))
        {
            return false;
        }
    }
    below->assign(combined.components.begin() + static_cast<std::ptrdiff_t>(root_.components.size()),
                  combined.components.end());
    return true;
}

bool GuestFiles::RelativeToRoot(std::string_view guest_path, std::string* relative) const
{
    storage::GuestPath parsed;
    storage::GuestPath combined;
    storage::GuestPath below;
    if (!storage::ParseGuestPath(guest_path, &parsed) || !storage::CombineGuestPath(current_, parsed, &combined) ||
        !BelowRoot(combined, false, &below.components))
    {
        return false;
    }
    *relative = storage::GuestPathToRelativeString(below);
    return true;
}

std::uint32_t GuestFiles::ImagePath(std::string_view guest_path, std::string* image_path, bool* outside_root) const
{
    *outside_root = false;
    std::string relative;
    if (!RelativeToRoot(guest_path, &relative))
    {
        *outside_root = true;
        return kWin32ErrorFileNotFound;
    }
    const std::string chd_relative = config_.chd_root.empty() ? relative : config_.chd_root + "/" + relative;
    bool directory = false;
    std::uint64_t size = 0;
    if (!InImage(relative, &directory, &size) || directory)
    {
        return kWin32ErrorFileNotFound;
    }
    *image_path = chd_relative;
    return kWin32ErrorSuccess;
}

GuestFiles::Entry GuestFiles::Lookup(const std::string& relative) const
{
    if (!config_.overlay_root.empty())
    {
        std::error_code code;
        const std::filesystem::path overlay = OverlayPath(relative);
        if (std::filesystem::is_directory(overlay, code))
        {
            return Entry::kDirectory;
        }
        if (std::filesystem::is_regular_file(overlay, code))
        {
            return Entry::kFile;
        }
    }
    bool directory = false;
    std::uint64_t size = 0;
    if (!InImage(relative, &directory, &size))
    {
        return Entry::kMissing;
    }
    return directory ? Entry::kDirectory : Entry::kFile;
}

GuestFiles::FindResult GuestFiles::FindFirst(std::string_view guest_pattern)
{
    FindResult result;
    if (guest_pattern.empty())
    {
        result.error = kWin32ErrorPathNotFound;
        return result;
    }
    // The pattern is split off first: '*' and '?' are not path characters.
    const std::size_t separator = guest_pattern.find_last_of("\\/");
    const std::string_view directory =
        separator == std::string_view::npos ? std::string_view(".") : guest_pattern.substr(0, separator);
    const std::string_view pattern =
        separator == std::string_view::npos ? guest_pattern : guest_pattern.substr(separator + 1);
    if (pattern.empty())
    {
        result.error = kWin32ErrorInvalidParameter;
        return result;
    }
    storage::GuestPath parsed;
    storage::GuestPath combined;
    std::vector<std::string> below;
    if (directory.empty())
    {
        // "\name" searches the drive's root, which the model does not serve.
        result.outside_root = true;
        return result;
    }
    if (!storage::ParseGuestPath(directory, &parsed))
    {
        result.error = kWin32ErrorPathNotFound;
        return result;
    }
    if (!storage::CombineGuestPath(current_, parsed, &combined) || !BelowRoot(combined, true, &below))
    {
        result.outside_root = true;
        return result;
    }
    storage::GuestPath relative;
    relative.components = below;
    const std::string below_root = storage::GuestPathToRelativeString(relative);
    std::string chd_relative = config_.chd_root;
    if (!below_root.empty())
    {
        chd_relative += (chd_relative.empty() ? "" : "/") + below_root;
    }
    std::vector<storage::Fat32Entry> entries;
    if (source_ == nullptr || !source_->ListDirectory(chd_relative, &entries))
    {
        result.error = kWin32ErrorPathNotFound;
        return result;
    }
    Search search;
    for (storage::Fat32Entry& entry : entries)
    {
        // An image file the guest deleted is gone from listings too.
        if (storage::MatchesFindPattern(pattern, entry.name) &&
            (entry.directory || !ListedDeleted(below_root.empty() ? entry.name : below_root + "/" + entry.name)))
        {
            search.matches.push_back(std::move(entry));
        }
    }
    if (search.matches.empty())
    {
        result.error = kWin32ErrorFileNotFound;
        return result;
    }
    GuestHandleAllocator* handles = handles_ != nullptr ? handles_ : &own_handles_;
    result.handle = handles->Allocate();
    result.first = storage::DescribeFindEntry(search.matches.front());
    search.next = 1;
    searches_[result.handle] = std::move(search);
    return result;
}

std::uint32_t GuestFiles::FindNext(std::uint32_t handle, storage::GuestFindData* data)
{
    const auto found = searches_.find(handle);
    if (found == searches_.end())
    {
        return kWin32ErrorInvalidHandle;
    }
    Search& search = found->second;
    if (search.next >= search.matches.size())
    {
        return kWin32ErrorNoMoreFiles;
    }
    *data = storage::DescribeFindEntry(search.matches[search.next++]);
    return kWin32ErrorSuccess;
}

bool GuestFiles::FindClose(std::uint32_t handle)
{
    return searches_.erase(handle) != 0;
}

std::string GuestFiles::CurrentDirectory() const
{
    return storage::GuestPathToString(current_);
}

bool GuestFiles::FullPath(std::string_view guest_path, std::string* full) const
{
    storage::GuestPath parsed;
    storage::GuestPath combined;
    if (guest_path.empty() || !storage::ParseGuestPath(guest_path, &parsed) ||
        parsed.kind == storage::GuestPathKind::kUnc || !storage::CombineGuestPath(current_, parsed, &combined))
    {
        return false;
    }
    *full = storage::GuestPathToString(combined);
    const char last = guest_path.back();
    if ((last == '\\' || last == '/') && !full->empty() && full->back() != '\\')
    {
        full->push_back('\\');
    }
    return true;
}

std::uint32_t GuestFiles::SetCurrentDirectory(std::string_view guest_path, bool* outside_root)
{
    *outside_root = false;
    storage::GuestPath parsed;
    storage::GuestPath combined;
    if (!storage::ParseGuestPath(guest_path, &parsed) || parsed.kind == storage::GuestPathKind::kUnc)
    {
        return kWin32ErrorInvalidName;
    }
    std::vector<std::string> below;
    if (!storage::CombineGuestPath(current_, parsed, &combined) || !BelowRoot(combined, true, &below))
    {
        *outside_root = true;
        return kWin32ErrorSuccess;
    }
    std::string relative;
    for (std::size_t index = 0; index < below.size(); ++index)
    {
        relative += (index == 0 ? "" : "/") + below[index];
        const bool last = index + 1 == below.size();
        switch (Lookup(relative))
        {
        case Entry::kMissing:
            return last ? kWin32ErrorFileNotFound : kWin32ErrorPathNotFound;
        case Entry::kFile:
            // A file in the middle of the path was not measured; Windows
            // reports a missing path for the like.
            return last ? kWin32ErrorDirectory : kWin32ErrorPathNotFound;
        case Entry::kDirectory:
            break;
        }
    }
    current_ = std::move(combined);
    return kWin32ErrorSuccess;
}

std::uint32_t GuestFiles::Delete(std::string_view guest_path, bool* outside_root)
{
    *outside_root = false;
    std::string relative;
    if (source_ == nullptr || !RelativeToRoot(guest_path, &relative))
    {
        *outside_root = true;
        return kWin32ErrorFileNotFound;
    }
    switch (Lookup(relative))
    {
    case Entry::kDirectory:
        return kWin32ErrorAccessDenied;
    case Entry::kMissing:
    {
        const std::size_t slash = relative.rfind('/');
        const bool parent_missing = slash != std::string::npos && Lookup(relative.substr(0, slash)) != Entry::kDirectory;
        return parent_missing ? kWin32ErrorPathNotFound : kWin32ErrorFileNotFound;
    }
    case Entry::kFile:
        break;
    }
    const std::string key = DeletedKey(relative);
    for (const auto& [handle, file] : files_)
    {
        if (file.relative_key == key)
        {
            return kWin32ErrorSharingViolation;
        }
    }
    std::error_code code;
    const std::filesystem::path overlay = OverlayPath(relative);
    if (std::filesystem::is_regular_file(overlay, code) && !std::filesystem::remove(overlay, code))
    {
        return kWin32ErrorAccessDenied;
    }
    // The image is never changed: its copy is listed as deleted instead.
    bool directory = false;
    std::uint64_t size = 0;
    if (InImage(relative, &directory, &size))
    {
        deleted_.insert(key);
        if (!SaveDeletedList())
        {
            deleted_.erase(key);
            return kWin32ErrorAccessDenied;
        }
    }
    return kWin32ErrorSuccess;
}

storage::GuestFileAttributes GuestFiles::Attributes(std::string_view guest_path, bool* outside_root) const
{
    *outside_root = false;
    storage::GuestFileAttributes result;
    if (guest_path.empty())
    {
        result.error = kWin32ErrorPathNotFound;
        return result;
    }
    storage::GuestPath parsed;
    storage::GuestPath combined;
    if (!storage::ParseGuestPath(guest_path, &parsed) || parsed.kind == storage::GuestPathKind::kUnc)
    {
        result.error = kWin32ErrorInvalidName;
        return result;
    }
    std::vector<std::string> below;
    if (!storage::CombineGuestPath(current_, parsed, &combined) || !BelowRoot(combined, true, &below))
    {
        *outside_root = true;
        return result;
    }
    const bool trailing_separator = guest_path.back() == '\\' || guest_path.back() == '/';
    return storage::DescribeGuestFileAttributes(below, trailing_separator, [this](const std::string& relative) {
        switch (Lookup(relative))
        {
        case Entry::kFile:
            return storage::GuestEntryKind::kFile;
        case Entry::kDirectory:
            return storage::GuestEntryKind::kDirectory;
        case Entry::kMissing:
            break;
        }
        return storage::GuestEntryKind::kMissing;
    });
}

std::uint32_t GuestFiles::OpenHost(const std::filesystem::path& path,
                                   bool write,
                                   bool truncate,
                                   File* file) const
{
    const char* mode = !write ? "rb" : truncate ? "w+b" : "r+b";
    std::FILE* stream = std::fopen(path.string().c_str(), mode);
    if (stream == nullptr)
    {
        return kWin32ErrorAccessDenied;
    }
    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    file->host = stream;
    file->size = code ? 0 : static_cast<std::uint64_t>(size);
    return kWin32ErrorSuccess;
}

GuestFiles::OpenResult GuestFiles::Open(std::string_view guest_path,
                                        bool read,
                                        bool write,
                                        std::uint32_t disposition)
{
    OpenResult result;
    std::string relative;
    if (source_ == nullptr || !RelativeToRoot(guest_path, &relative))
    {
        result.outside_root = true;
        return result;
    }
    const std::string chd_relative = config_.chd_root.empty() ? relative : config_.chd_root + "/" + relative;
    bool chd_directory = false;
    std::uint64_t chd_size = 0;
    const bool in_chd = InImage(relative, &chd_directory, &chd_size);
    const std::filesystem::path overlay = OverlayPath(relative);
    std::error_code code;
    const bool in_overlay = std::filesystem::is_regular_file(overlay, code);
    if ((in_chd && chd_directory) || std::filesystem::is_directory(overlay, code))
    {
        result.error = kWin32ErrorAccessDenied;
        return result;
    }
    const bool exists = in_overlay || in_chd;
    if (disposition < kCreateNew || disposition > kTruncateExisting)
    {
        result.error = kWin32ErrorInvalidParameter;
        return result;
    }
    if (disposition == kCreateNew && exists)
    {
        result.error = kWin32ErrorFileExists;
        return result;
    }
    if ((disposition == kOpenExisting || disposition == kTruncateExisting) && !exists)
    {
        // A missing directory on the way is ERROR_PATH_NOT_FOUND, as on Windows.
        const std::size_t slash = relative.rfind('/');
        const bool parent_missing = slash != std::string::npos && Lookup(relative.substr(0, slash)) != Entry::kDirectory;
        result.error = parent_missing ? kWin32ErrorPathNotFound : kWin32ErrorFileNotFound;
        return result;
    }

    File file;
    file.relative_key = DeletedKey(relative);
    file.read = read;
    file.write = write;
    const bool truncate = disposition == kCreateAlways || disposition == kTruncateExisting;
    if (!write && !truncate)
    {
        if (in_overlay)
        {
            result.error = OpenHost(overlay, false, false, &file);
        }
        else if (in_chd)
        {
            file.chd_relative = chd_relative;
            file.size = chd_size;
        }
        else
        {
            // OPEN_ALWAYS or CREATE_NEW without write access still creates.
            std::filesystem::create_directories(overlay.parent_path(), code);
            result.error = OpenHost(overlay, true, true, &file);
        }
    }
    else
    {
        std::filesystem::create_directories(overlay.parent_path(), code);
        // Copy on write: the overlay starts as the CHD's copy.
        if (!in_overlay && in_chd && !truncate && !source_->Materialize(chd_relative, overlay))
        {
            result.error = kWin32ErrorAccessDenied;
            return result;
        }
        result.error = OpenHost(overlay, true, truncate || !exists, &file);
    }
    if (result.error != kWin32ErrorSuccess)
    {
        return result;
    }
    result.handle = handles_ != nullptr ? handles_->Allocate() : own_handles_.Allocate();
    files_.emplace(result.handle, std::move(file));
    // CREATE_ALWAYS and OPEN_ALWAYS on an existing file succeed with
    // ERROR_ALREADY_EXISTS as the last error.
    result.error = exists && (disposition == kCreateAlways || disposition == kOpenAlways)
                       ? kWin32ErrorAlreadyExists
                       : kWin32ErrorSuccess;
    return result;
}

bool GuestFiles::IsOpen(std::uint32_t handle) const
{
    return files_.count(handle) == 1;
}

std::uint32_t GuestFiles::Read(std::uint32_t handle,
                               std::uint32_t size,
                               std::vector<std::uint8_t>* bytes)
{
    const auto found = files_.find(handle);
    if (found == files_.end())
    {
        return kWin32ErrorInvalidHandle;
    }
    File& file = found->second;
    if (!file.read)
    {
        return kWin32ErrorAccessDenied;
    }
    const std::uint64_t available = file.position >= file.size ? 0 : file.size - file.position;
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(size, available));
    bytes->assign(count, 0);
    if (count != 0)
    {
        bool read = false;
        if (file.host != nullptr)
        {
            read = std::fseek(file.host, static_cast<long>(file.position), SEEK_SET) == 0 &&
                   std::fread(bytes->data(), 1, count, file.host) == count;
        }
        else
        {
            read = (file.prefetch != nullptr &&
                    GuestFilePrefetcher::Copy(*file.prefetch, file.position, bytes->data(), count)) ||
                   source_->ReadRange(file.chd_relative, file.position, bytes->data(), count);
        }
        if (!read)
        {
            bytes->clear();
            return kWin32ErrorReadFault;
        }
    }
    file.position += count;
    if (file.host == nullptr && !file.prefetch_considered)
    {
        file.prefetch_considered = true;
        const std::uint64_t remaining = file.position >= file.size ? 0 : file.size - file.position;
        if (prefetcher_ != nullptr && remaining >= kPrefetchMinimumRemaining)
        {
            file.prefetch = prefetcher_->Start(file.chd_relative, file.size);
        }
    }
    return kWin32ErrorSuccess;
}

std::uint32_t GuestFiles::Write(std::uint32_t handle, std::span<const std::uint8_t> bytes)
{
    const auto found = files_.find(handle);
    if (found == files_.end())
    {
        return kWin32ErrorInvalidHandle;
    }
    File& file = found->second;
    if (!file.write || file.host == nullptr)
    {
        return kWin32ErrorAccessDenied;
    }
    if (!bytes.empty() &&
        (std::fseek(file.host, static_cast<long>(file.position), SEEK_SET) != 0 ||
         std::fwrite(bytes.data(), 1, bytes.size(), file.host) != bytes.size() ||
         std::fflush(file.host) != 0))
    {
        return kWin32ErrorWriteFault;
    }
    file.position += bytes.size();
    file.size = std::max(file.size, file.position);
    return kWin32ErrorSuccess;
}

std::uint32_t GuestFiles::Seek(std::uint32_t handle,
                               std::int64_t distance,
                               std::uint32_t method,
                               std::uint64_t* position)
{
    const auto found = files_.find(handle);
    if (found == files_.end())
    {
        return kWin32ErrorInvalidHandle;
    }
    File& file = found->second;
    std::int64_t base = 0;
    switch (method)
    {
    case 0:
        break;
    case 1:
        base = static_cast<std::int64_t>(file.position);
        break;
    case 2:
        base = static_cast<std::int64_t>(file.size);
        break;
    default:
        return kWin32ErrorInvalidParameter;
    }
    if (base + distance < 0)
    {
        return kWin32ErrorNegativeSeek;
    }
    file.position = static_cast<std::uint64_t>(base + distance);
    *position = file.position;
    return kWin32ErrorSuccess;
}

std::uint32_t GuestFiles::Size(std::uint32_t handle, std::uint64_t* size) const
{
    const auto found = files_.find(handle);
    if (found == files_.end())
    {
        return kWin32ErrorInvalidHandle;
    }
    *size = found->second.size;
    return kWin32ErrorSuccess;
}

bool GuestFiles::Close(std::uint32_t handle)
{
    const auto found = files_.find(handle);
    if (found == files_.end())
    {
        return false;
    }
    if (found->second.host != nullptr)
    {
        std::fclose(found->second.host);
    }
    if (found->second.prefetch != nullptr && prefetcher_ != nullptr)
    {
        prefetcher_->Cancel(found->second.prefetch);
    }
    files_.erase(found);
    return true;
}

std::uint64_t GuestFiles::PrefetchedBytes(std::uint32_t handle) const
{
    const auto found = files_.find(handle);
    return found == files_.end() || found->second.prefetch == nullptr ? 0 : found->second.prefetch->loaded();
}

void GuestFiles::WaitForPrefetch()
{
    if (prefetcher_ != nullptr)
    {
        prefetcher_->WaitWarmed();
        prefetcher_->WaitIdle();
    }
}

}  // namespace re2dj::hle
