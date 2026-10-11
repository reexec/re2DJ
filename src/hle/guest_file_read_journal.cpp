#include "re2dj/hle/guest_file_read_journal.h"

#include <algorithm>
#include <cctype>
#include <numeric>

namespace re2dj::hle
{
namespace
{

std::string Key(GuestFileReadJournal::Kind kind, std::string_view path)
{
    std::string key(1, static_cast<char>(kind));
    key.reserve(path.size() + 1);
    for (const char c : path)
    {
        key.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return key;
}

bool ParseKind(std::string_view text, GuestFileReadJournal::Kind* kind)
{
    if (text.size() != 1)
    {
        return false;
    }
    switch (text[0])
    {
    case 'f':
        *kind = GuestFileReadJournal::Kind::kFind;
        return true;
    case 'l':
        *kind = GuestFileReadJournal::Kind::kList;
        return true;
    case 'r':
        *kind = GuestFileReadJournal::Kind::kRead;
        return true;
    default:
        return false;
    }
}

bool ParseNumber(std::string_view text, std::uint64_t* value)
{
    if (text.empty() || text.size() > 19)
    {
        return false;
    }
    std::uint64_t parsed = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
        parsed = parsed * 10 + static_cast<std::uint64_t>(c - '0');
    }
    *value = parsed;
    return true;
}

}  // namespace

// A line is "uses<TAB>kind<TAB>extent<TAB>path".
void GuestFileReadJournal::Load(std::istream& stream)
{
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        std::string_view rest(line);
        std::string_view fields[3];
        bool parsed = true;
        for (std::string_view& field : fields)
        {
            const std::size_t tab = rest.find('\t');
            if (tab == std::string_view::npos)
            {
                parsed = false;
                break;
            }
            field = rest.substr(0, tab);
            rest.remove_prefix(tab + 1);
        }
        Entry entry;
        std::uint64_t uses = 0;
        if (!parsed || rest.empty() || !ParseNumber(fields[0], &uses) || !ParseKind(fields[1], &entry.kind) ||
            !ParseNumber(fields[2], &entry.extent) || entries_.size() >= kMaximumEntries)
        {
            continue;
        }
        entry.uses = static_cast<std::uint32_t>(std::min<std::uint64_t>(uses, UINT32_MAX));
        entry.path = std::string(rest);
        const auto [found, inserted] = index_.emplace(Key(entry.kind, entry.path), entries_.size());
        if (!inserted)
        {
            continue;
        }
        entries_.push_back(std::move(entry));
        last_used_.emplace_back();
        pending_uses_.push_back(0);
    }
}

void GuestFileReadJournal::Save(std::ostream& stream) const
{
    for (const Entry& entry : entries_)
    {
        stream << entry.uses << '\t' << static_cast<char>(entry.kind) << '\t' << entry.extent << '\t' << entry.path
               << '\n';
    }
}

void GuestFileReadJournal::Record(Kind kind,
                                  std::string_view path,
                                  std::uint64_t extent,
                                  std::chrono::steady_clock::time_point now)
{
    const std::string key = Key(kind, path);
    auto found = index_.find(key);
    if (found == index_.end())
    {
        if (entries_.size() >= kMaximumEntries)
        {
            return;
        }
        found = index_.emplace(key, entries_.size()).first;
        Entry entry;
        entry.kind = kind;
        entry.path = std::string(path);
        entries_.push_back(std::move(entry));
        last_used_.emplace_back();
        pending_uses_.push_back(0);
        dirty_ = true;
    }
    Entry& entry = entries_[found->second];
    std::chrono::steady_clock::time_point& last = last_used_[found->second];
    if (last == std::chrono::steady_clock::time_point{} || now - last >= kUseGap)
    {
        ++entry.uses;
        ++pending_uses_[found->second];
        dirty_ = true;
    }
    last = now;
    if (extent > entry.extent)
    {
        entry.extent = extent;
        dirty_ = true;
    }
}

void GuestFileReadJournal::MergeInto(GuestFileReadJournal* target) const
{
    for (std::size_t index = 0; index < entries_.size(); ++index)
    {
        const Entry& entry = entries_[index];
        const std::uint32_t gained = pending_uses_[index];
        auto found = target->index_.find(Key(entry.kind, entry.path));
        if (found == target->index_.end())
        {
            if (gained == 0 || target->entries_.size() >= kMaximumEntries)
            {
                continue;
            }
            found = target->index_.emplace(Key(entry.kind, entry.path), target->entries_.size()).first;
            Entry added = entry;
            added.uses = 0;
            target->entries_.push_back(std::move(added));
            target->last_used_.emplace_back();
            target->pending_uses_.push_back(0);
        }
        Entry& merged = target->entries_[found->second];
        merged.uses = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{merged.uses} + gained, UINT32_MAX));
        merged.extent = std::max(merged.extent, entry.extent);
    }
}

void GuestFileReadJournal::MarkSaved()
{
    std::fill(pending_uses_.begin(), pending_uses_.end(), 0u);
    dirty_ = false;
}

std::vector<GuestFileReadJournal::Entry> GuestFileReadJournal::WarmList(std::uint64_t budget_bytes) const
{
    std::vector<std::size_t> order(entries_.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [this](std::size_t left, std::size_t right) { return entries_[left].uses > entries_[right].uses; });
    std::vector<std::size_t> chosen;
    std::uint64_t total = 0;
    for (const std::size_t index : order)
    {
        const Entry& entry = entries_[index];
        if (entry.uses < 2)
        {
            break;
        }
        if (entry.extent > budget_bytes - total)
        {
            continue;
        }
        total += entry.extent;
        chosen.push_back(index);
    }
    std::sort(chosen.begin(), chosen.end());
    std::vector<Entry> list;
    list.reserve(chosen.size());
    for (const std::size_t index : chosen)
    {
        list.push_back(entries_[index]);
    }
    return list;
}

}  // namespace re2dj::hle
