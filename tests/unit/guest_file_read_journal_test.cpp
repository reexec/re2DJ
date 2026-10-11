#include "re2dj/hle/guest_file_read_journal.h"

#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>

#include "test_support.h"

namespace
{

namespace hle = re2dj::hle;
using Journal = hle::GuestFileReadJournal;
using Kind = Journal::Kind;

const std::chrono::steady_clock::time_point kStart = std::chrono::steady_clock::now();

std::chrono::steady_clock::time_point At(int seconds)
{
    return kStart + std::chrono::seconds(seconds);
}

const Journal::Entry* FindEntry(const Journal& journal, Kind kind, const std::string& path)
{
    for (const Journal::Entry& entry : journal.entries())
    {
        if (entry.kind == kind && entry.path == path)
        {
            return &entry;
        }
    }
    return nullptr;
}

// A path counts once per stretch of use: again only after kUseGap without
// it. Paths match without case, and a read keeps its furthest extent.
void CheckRecordCountsUses(re2dj::test::Context& context)
{
    Journal journal;
    RE2DJ_CHECK(context, !journal.dirty());
    journal.Record(Kind::kRead, "EZ2DJ/System/Panel/A.ABM", 4096, At(0));
    journal.Record(Kind::kRead, "ez2dj/system/panel/a.abm", 9000, At(2));
    journal.Record(Kind::kRead, "EZ2DJ/System/Panel/A.ABM", 100, At(11));
    RE2DJ_CHECK(context, journal.dirty());
    RE2DJ_CHECK_EQ(context, journal.entries().size(), std::size_t{1});
    // Two seconds apart is one use; nine more seconds is still within the
    // gap of the last one.
    RE2DJ_CHECK_EQ(context, journal.entries()[0].uses, std::uint32_t{1});
    RE2DJ_CHECK_EQ(context, journal.entries()[0].extent, std::uint64_t{9000});
    journal.Record(Kind::kRead, "EZ2DJ/System/Panel/A.ABM", 100, At(30));
    RE2DJ_CHECK_EQ(context, journal.entries()[0].uses, std::uint32_t{2});
    // A find of the same path is an entry of its own.
    journal.Record(Kind::kFind, "EZ2DJ/System/Panel/A.ABM", 0, At(30));
    RE2DJ_CHECK_EQ(context, journal.entries().size(), std::size_t{2});
}

// Saved and loaded again, the entries come back in order; lines that do not
// parse are left out.
void CheckSaveAndLoad(re2dj::test::Context& context)
{
    Journal journal;
    journal.Record(Kind::kFind, "EZ2DJ/SOUND/6curseit", 0, At(0));
    journal.Record(Kind::kList, "EZ2DJ/System", 0, At(0));
    journal.Record(Kind::kRead, "EZ2DJ/System/Title/Title.ezw", 8609698, At(0));
    std::ostringstream saved;
    journal.Save(saved);
    std::istringstream input(saved.str() + "garbage\n3\tx\t0\tEZ2DJ/BAD\n2\tr\t12\t\n7\tr\t-1\tEZ2DJ/NEG\n");
    Journal loaded;
    loaded.Load(input);
    RE2DJ_CHECK_EQ(context, loaded.entries().size(), std::size_t{3});
    RE2DJ_CHECK(context, !loaded.dirty());
    RE2DJ_CHECK(context, loaded.entries()[0].kind == Kind::kFind);
    RE2DJ_CHECK_EQ(context, loaded.entries()[0].path, std::string("EZ2DJ/SOUND/6curseit"));
    RE2DJ_CHECK(context, loaded.entries()[1].kind == Kind::kList);
    RE2DJ_CHECK_EQ(context, loaded.entries()[2].extent, std::uint64_t{8609698});
    RE2DJ_CHECK_EQ(context, loaded.entries()[2].uses, std::uint32_t{1});
}

// Only paths used twice are read ahead, the most used first while they fit
// the budget, in the journal's order.
void CheckWarmList(re2dj::test::Context& context)
{
    std::istringstream input("1\tr\t100\tEZ2DJ/ONCE\n"
                             "2\tr\t600\tEZ2DJ/TWICE\n"
                             "5\tf\t0\tEZ2DJ/SOUND\n"
                             "4\tr\t500\tEZ2DJ/FOUR\n"
                             "3\tr\t300\tEZ2DJ/THREE\n");
    Journal journal;
    journal.Load(input);
    const auto list = journal.WarmList(800);
    // FOUR (500) and THREE (300) fill the budget; TWICE (600) no longer fits.
    RE2DJ_CHECK_EQ(context, list.size(), std::size_t{3});
    RE2DJ_CHECK_EQ(context, list[0].path, std::string("EZ2DJ/SOUND"));
    RE2DJ_CHECK_EQ(context, list[1].path, std::string("EZ2DJ/FOUR"));
    RE2DJ_CHECK_EQ(context, list[2].path, std::string("EZ2DJ/THREE"));
    RE2DJ_CHECK(context, Journal().WarmList(800).empty());
}

// Saving adds what this run gained to the file as another run left it, and
// a second save after MarkSaved adds nothing more.
void CheckMergeInto(re2dj::test::Context& context)
{
    std::istringstream on_load("2\tr\t100\tEZ2DJ/COMMON\n");
    Journal run;
    run.Load(on_load);
    run.Record(Kind::kRead, "EZ2DJ/COMMON", 400, At(0));
    run.Record(Kind::kRead, "EZ2DJ/NEW", 50, At(0));

    // Another run on the same overlay saved in between.
    std::istringstream on_disk("3\tr\t100\tEZ2DJ/COMMON\n1\tf\t0\tEZ2DJ/OTHER\n");
    Journal disk;
    disk.Load(on_disk);
    run.MergeInto(&disk);
    RE2DJ_CHECK_EQ(context, disk.entries().size(), std::size_t{3});
    RE2DJ_CHECK_EQ(context, FindEntry(disk, Kind::kRead, "EZ2DJ/COMMON")->uses, std::uint32_t{4});
    RE2DJ_CHECK_EQ(context, FindEntry(disk, Kind::kRead, "EZ2DJ/COMMON")->extent, std::uint64_t{400});
    RE2DJ_CHECK_EQ(context, FindEntry(disk, Kind::kRead, "EZ2DJ/NEW")->uses, std::uint32_t{1});
    RE2DJ_CHECK(context, FindEntry(disk, Kind::kFind, "EZ2DJ/OTHER") != nullptr);

    run.MarkSaved();
    RE2DJ_CHECK(context, !run.dirty());
    run.MergeInto(&disk);
    RE2DJ_CHECK_EQ(context, FindEntry(disk, Kind::kRead, "EZ2DJ/COMMON")->uses, std::uint32_t{4});
}

// The journal stops growing at its limit.
void CheckLimit(re2dj::test::Context& context)
{
    Journal journal;
    for (std::size_t index = 0; index <= Journal::kMaximumEntries; ++index)
    {
        journal.Record(Kind::kFind, "EZ2DJ/" + std::to_string(index), 0, At(0));
    }
    RE2DJ_CHECK_EQ(context, journal.entries().size(), Journal::kMaximumEntries);
}

}  // namespace

void RunGuestFileReadJournalTests(re2dj::test::Context& context)
{
    CheckRecordCountsUses(context);
    CheckSaveAndLoad(context);
    CheckWarmList(context);
    CheckMergeInto(context);
    CheckLimit(context);
}
