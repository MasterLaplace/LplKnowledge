/**
 * @file test_author_dates.cpp
 * @brief The soft join: it may fill a value, it may never decide an identity.
 *
 * @warning Two catalogues share no identifier for a person, so pairing "Herodotus" with "Herodotus"
 * is a guess. The guess is admissible because of what it is allowed to do -- fill a date window,
 * marked as soft -- and inadmissible in the one thing it must never do, which is decide that two
 * records are one man. Splitting is reversible and merging is not: two entries can be joined the
 * day evidence arrives, while a merge destroys the distinction and no filter recovers it.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/AuthorDates.hpp>
#include <lpl/history/Calendar.hpp>

#include <cstdio>

namespace {

int gChecks = 0;
int gFailures = 0;

/**
 * @brief Records one assertion.
 *
 * @param what Description.
 * @param ok   Whether it held.
 */
void check(const char *what, bool ok)
{
    ++gChecks;
    if (ok)
        return;
    ++gFailures;
    std::printf("  (fail) %s\n", what);
}

} // namespace

int main()
{
    using namespace lpl;

    // 0.55 in raw Q16.16, the same floor `ResolutionParams` uses for proposing.
    constexpr core::u32 kPropose = 36045u;

    harvest::AuthorDates table;
    table.add("Herodotus", history::firstDayOfYear(-484), history::lastDayOfYear(-430));
    table.add("Jefferson, Thomas", history::firstDayOfYear(1743), history::lastDayOfYear(1826));
    table.add("Macaulay, G. C.", history::firstDayOfYear(1852), history::lastDayOfYear(1915));
    table.finalise();

    std::printf("-- an exact name fills the window, and says it was a name\n");
    {
        const harvest::AuthorDateMatch hit = table.find("Herodotus", kPropose);
        check("an exact name is found", hit.exact);
        check("and fills the window",
              hit.fromDay == history::firstDayOfYear(-484) && hit.toDay == history::lastDayOfYear(-430));
        check("which is fifty-four years wide, not a date",
              history::yearOfDay(hit.toDay) - history::yearOfDay(hit.fromDay) == 54);

        // Case and punctuation are one spelling of one name; refusing to see that loses the join
        // for a reason no reader cares about.
        check("case does not break it", table.find("HERODOTUS", kPropose).exact);
        check("nor does trailing punctuation", table.find("Herodotus.", kPropose).exact);
        check("nor does extra spacing", table.find("  Herodotus  ", kPropose).exact);
    }

    std::printf("-- a resemblance proposes, and fills NOTHING\n");
    {
        // @warning The decisive one. "Jefferson, Thomas J." resembles "Jefferson, Thomas" strongly --
        // and might be another man. Filling a window from that would put a date on a person
        // nobody identified, and the record would afterwards be indistinguishable from one an
        // editor stated.
        const harvest::AuthorDateMatch near = table.find("Jefferson, Thomas J.", kPropose);
        check("a close name is not exact", !near.exact);
        check("it is proposed for a look", near.proposed);
        check("and NOTHING is filled", near.fromDay == 0 && near.toDay == 0);
        check("with the resemblance kept as a raw word", near.scoreRaw > kPropose && near.scoreRaw < 65536u);
        check("and it names which entry to look at", table.at(near.index).name == "Jefferson, Thomas");
    }

    std::printf("-- two different men who share a name are not told apart, and that is why the flag exists\n");
    {
        // @warning An exact match is still NOT an identity, and this is the case that proves it: two
        // notaries called Jean Martin, one at Rouen and one at Rennes. A table keyed on names
        // gives both the same window and cannot know better. The window is therefore a
        // hypothesis, and `kSourceFlagWindowFromNameMatch` is what says so downstream.
        harvest::AuthorDates ambiguous;
        ambiguous.add("Martin, Jean", history::firstDayOfYear(1600), history::lastDayOfYear(1660));
        ambiguous.add("Martin, Jean", history::firstDayOfYear(1710), history::lastDayOfYear(1775));
        ambiguous.finalise();

        const harvest::AuthorDateMatch hit = ambiguous.find("Martin, Jean", kPropose);
        check("both entries are kept, neither merged", ambiguous.size() == 2u);
        check("and a lookup resolves to one of them", hit.exact);
        // Which one is a rule rather than an accident: the first the caller added, after a stable
        // sort. Two runs over one catalogue must resolve the same way.
        check("the first one added, deterministically", hit.fromDay == history::firstDayOfYear(1600));
    }

    std::printf("-- a name nobody in the table carries fills nothing at all\n");
    {
        const harvest::AuthorDateMatch miss = table.find("Thucydides", kPropose);
        check("no exact match", !miss.exact);
        check("nothing proposed either", !miss.proposed);
        check("and no window invented", miss.fromDay == 0 && miss.toDay == 0);
        check("an empty name is refused", !table.find("", kPropose).exact);
        // A threshold of zero means "propose nothing", and it must not mean "propose everything".
        check("a zero threshold proposes nothing", !table.find("Jefferson, Thomas J.", 0u).proposed);
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures,
                gChecks);
    return gFailures == 0 ? 0 : 1;
}
