/**
 * @file test_mentions.cpp
 * @brief What a primary text names, and the two ways a date can lie.
 *
 * @warning Every shape asserted here was MEASURED in the Perseus corpus before it was written down,
 * because a date parser tested against dates its author imagined is a parser tested against its
 * author. The shapes and their counts: `-0480` a whole year (3841 of them), `1863-05` a whole
 * month (103), `1863-05-01` one day (941), and `when-custom` -- an editor saying "this is not the
 * calendar you think" -- 11.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Mentions.hpp>
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
    using namespace lpl::harvest;

    std::printf("-- an authority key is a key; a bare name is not\n");
    {
        const AuthorityKey tgn = splitAuthorityKey("tgn,7003867");
        check("a keyed mention splits", tgn.present && tgn.authority == "tgn" && tgn.entry == "7003867");

        const AuthorityKey perseus = splitAuthorityKey("perseus,Aetna");
        check("so does a non-numeric entry", perseus.present && perseus.entry == "Aetna");

        // Measured: entries do contain further commas. Consuming them all truncates whichever
        // editor used one.
        const AuthorityKey commas = splitAuthorityKey("tgn,700,3867");
        check("only the FIRST comma separates", commas.present && commas.entry == "700,3867");

        check("a value with no comma is a name, not a key", !splitAuthorityKey("Aetna").present);
        check("and so is an empty one", !splitAuthorityKey("").present);
        check("a key with nothing after the comma is not a key", !splitAuthorityKey("tgn,").present);
        check("nor is one with nothing before it", !splitAuthorityKey(",7003867").present);

        // @warning Two authorities number their entries independently, so the identifier must be
        // built from the WHOLE key. Folding these together would merge two unrelated entities on
        // a coincidence of digits -- and both authorities really are present in the corpus:
        // 24 661 `tgn` mentions and 15 558 `perseus` ones.
        const core::u32 a = authorityIdentifier(splitAuthorityKey("tgn,7003867"));
        const core::u32 b = authorityIdentifier(splitAuthorityKey("perseus,7003867"));
        check("the same entry under two authorities is two identities", a != b);
        check("and an absent key has no identity at all", authorityIdentifier(AuthorityKey{}) == 0u);
    }

    std::printf("-- the precision of a date is the WIDTH of its window\n");
    {
        DateWindow window;

        check("a bare year reads", parseTeiDate("1863", window));
        check("and covers the whole of it",
              window.fromDay == history::firstDayOfYear(1863) && window.toDay == history::lastDayOfYear(1863));
        check("which is 365 days", window.toDay - window.fromDay + 1 == 365);

        check("a year-month reads", parseTeiDate("1863-05", window));
        check("and covers that month",
              window.fromDay == history::dayOfDate(1863, 5u, 1u) && window.toDay == history::dayOfDate(1863, 5u, 31u));

        check("a full date reads", parseTeiDate("1863-05-01", window));
        check("and covers one day",
              window.fromDay == window.toDay && window.fromDay == history::dayOfDate(1863, 5u, 1u));

        // @warning A leading minus with leading zeroes is the corpus's most common shape, and reading
        // the minus as a separator would put 480 BCE in 480 CE -- the same digits, the wrong
        // millennium, and nothing downstream able to tell.
        check("a negative year reads", parseTeiDate("-0480", window));
        check("and is before the era", window.fromDay < 0);
        check("and is the whole year -480",
              window.fromDay == history::firstDayOfYear(-480) && window.toDay == history::lastDayOfYear(-480));
        check("a negative full date is one day", parseTeiDate("-0480-09-20", window) && window.fromDay == window.toDay);

        // February, without February appearing anywhere in the implementation.
        check("a leap February ends on the 29th",
              parseTeiDate("2000-02", window) && window.toDay == history::dayOfDate(2000, 2u, 29u));
        check("and a common one on the 28th",
              parseTeiDate("1900-02", window) && window.toDay == history::dayOfDate(1900, 2u, 28u));

        check("something with no digits is refused", !parseTeiDate("circa", window));
        check("and so is an empty value", !parseTeiDate("", window));
    }

    std::printf("-- the three ways TEI says when, and the one it says do not guess\n");
    {
        DateWindow window;

        check("a `when` is read", dateWindowOf("<date when=\"1863-05-01\">", window));
        check("as one day", window.fromDay == window.toDay);

        // @warning The OUTER edges. Taking the inner ones would narrow a span to a point exactly when
        // an editor took the trouble to date both ends -- making the most carefully dated entries
        // look like the most precise, which is the reverse of what they say.
        check("a from/to span is read", dateWindowOf("<date from=\"1861\" to=\"1865\">", window));
        check("and covers both ends",
              window.fromDay == history::firstDayOfYear(1861) && window.toDay == history::lastDayOfYear(1865));

        check("a notBefore/notAfter span is read",
              dateWindowOf("<date notBefore=\"-0500\" notAfter=\"-0400\">", window));
        check("and covers what the evidence allows",
              window.fromDay == history::firstDayOfYear(-500) && window.toDay == history::lastDayOfYear(-400));

        check("one end alone still reads", dateWindowOf("<date from=\"1861\">", window));
        check("an element with no date attribute is refused", !dateWindowOf("<date type=\"publication\">", window));

        // @warning **The calendar refusal, and it is the whole reason this reader can be trusted with
        // ancient material.** Reading a Julian date as a proleptic Gregorian one shifts it by ten
        // days in 1582 and by more the further back one goes: silent, plausible and wrong.
        // Measured: the corpus declares no alternative calendar anywhere, so the editors really
        // did normalise -- and the 11 `when-custom` elements are exactly where they said they had
        // not.
        check("a date in a declared other calendar is REFUSED",
              !dateWindowOf("<date when=\"1700\" datingMethod=\"#julian\">", window));
        // @warning `when-custom` needs no rule of its own, and asserting that it did was a check
        // satisfied for the wrong reason: an element carrying ONLY a custom reckoning has nothing
        // this parser can read anyway. What matters is that a normalised value BESIDE one is
        // still read -- an editor who records both has done the work properly, and a first
        // version discarded their `from`/`to` for it.
        check("a custom reckoning alone yields nothing to read",
              !dateWindowOf("<date when-custom=\"Ol. 75.1\">", window));
        check("but a normalised range beside one is still read",
              dateWindowOf("<date when-custom=\"Ol. 75.1\" from=\"-0480\" to=\"-0479\">", window));
        check("and covers what the editor normalised",
              window.fromDay == history::firstDayOfYear(-480) && window.toDay == history::lastDayOfYear(-479));
        // But a custom reckoning ALONGSIDE a normalised one is the editor doing the work properly,
        // and refusing it would discard a date that was correctly given.
        check("a custom reckoning beside a normalised one is still read",
              dateWindowOf("<date when=\"-0480\" when-custom=\"Ol. 75.1\">", window));
        check("using the normalised value", window.fromDay == history::firstDayOfYear(-480));
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures,
                gChecks);
    return gFailures == 0 ? 0 : 1;
}
