/**
 * @file test_corpus_identity.cpp
 * @brief Identity, addressing and language tags — the half that needs no foundation.
 *
 * Runs in a STANDALONE checkout, and that is the point rather than a side effect: this
 * repository claims it builds and is tested on its own, and a claim whose only test needs
 * a sibling checkout present is not that claim.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/corpus/Language.hpp>
#include <lpl/corpus/Locus.hpp>
#include <lpl/corpus/TextView.hpp>
#include <lpl/corpus/Urn.hpp>
#include <lpl/knowledge/FactStore.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>
#include <lpl/history/Calendar.hpp>
#include <lpl/knowledge/Query.hpp>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int gChecks = 0;
int gFailures = 0;

/**
 * @brief Records one check.
 *
 * @param label What was checked.
 * @param ok    Whether it held.
 */
void check(const char *label, bool ok)
{
    ++gChecks;
    if (!ok)
    {
        ++gFailures;
        std::printf("  (fail) %s\n", label);
    }
}

/**
 * @brief Length of a NUL-terminated literal.
 *
 * @param text The literal.
 * @return Its length.
 */
lpl::core::u32 length(const char *text) { return static_cast<lpl::core::u32>(std::strlen(text)); }

/**
 * @brief Exercises CTS URN parsing and identity derivation.
 */
void testUrn()
{
    std::printf("── urn\n");

    const char *full = "urn:cts:greekLit:tlg0016.tlg001.perseus-grc2:1.5";
    lpl::corpus::Urn urn{};
    check("a full CTS URN parses", lpl::corpus::parseUrn(full, length(full), urn));
    check("namespace is greekLit",
          urn.namespaceBytes == 8u && std::strncmp(urn.namespaceText, "greekLit", 8u) == 0);
    check("work is the middle component",
          urn.workBytes == 27u && std::strncmp(urn.workText, "tlg0016.tlg001.perseus-grc2", 27u) == 0);
    check("passage is 1.5", urn.passageBytes == 3u && std::strncmp(urn.passageText, "1.5", 3u) == 0);

    const char *noPassage = "urn:cts:latinLit:phi0448.phi001";
    lpl::corpus::Urn bare{};
    check("a URN with no passage parses", lpl::corpus::parseUrn(noPassage, length(noPassage), bare));
    check("and carries no passage", bare.passageBytes == 0u);

    // Two exemplars of the SAME work must be different works only if their work component
    // differs — the passage must never enter the identity, or every cited line would look
    // like a separate document and corroboration would count one source as many.
    const char *sameWorkOtherLine = "urn:cts:greekLit:tlg0016.tlg001.perseus-grc2:9.2";
    lpl::corpus::Urn other{};
    (void) lpl::corpus::parseUrn(sameWorkOtherLine, length(sameWorkOtherLine), other);
    check("the passage does not enter the work identity",
          lpl::corpus::workIdentifier(urn) == lpl::corpus::workIdentifier(other));

    const char *otherWork = "urn:cts:greekLit:tlg0016.tlg002.perseus-grc2:1.5";
    lpl::corpus::Urn second{};
    (void) lpl::corpus::parseUrn(otherWork, length(otherWork), second);
    check("a different work is a different identity",
          lpl::corpus::workIdentifier(urn) != lpl::corpus::workIdentifier(second));

    // The namespace has to be part of the identity: two corpora are free to reuse a local
    // work name, and dropping the namespace would merge them silently.
    const char *sameWorkOtherNamespace = "urn:cts:latinLit:tlg0016.tlg001.perseus-grc2:1.5";
    lpl::corpus::Urn third{};
    (void) lpl::corpus::parseUrn(sameWorkOtherNamespace, length(sameWorkOtherNamespace), third);
    check("the namespace is part of the identity",
          lpl::corpus::workIdentifier(urn) != lpl::corpus::workIdentifier(third));

    check("an identifier is never zero", lpl::corpus::nameIdentifier("", 0u) != 0u);

    const char *notCts = "https://example.org/text/1";
    lpl::corpus::Urn refused{};
    check("a non-CTS string is refused", !lpl::corpus::parseUrn(notCts, length(notCts), refused));
    const char *truncated = "urn:cts:greekLit";
    check("a URN with a namespace and no work is refused",
          !lpl::corpus::parseUrn(truncated, length(truncated), refused));
}

/**
 * @brief Exercises the passage-to-locus mapping.
 */
void testLocus()
{
    std::printf("── locus\n");

    lpl::corpus::Locus locus{};
    check("one level names a LINE", lpl::corpus::parsePassage("5", 1u, locus) && locus.line == 5u &&
                                       locus.section == 0u && locus.part == 0u);
    check("two levels name section and line",
          lpl::corpus::parsePassage("3.12", 4u, locus) && locus.section == 3u && locus.line == 12u &&
              locus.part == 0u);
    check("three levels name all of them", lpl::corpus::parsePassage("3.12.4", 6u, locus) && locus.part == 3u &&
                                               locus.section == 12u && locus.line == 4u);

    // Refused rather than truncated: dropping the finest level of a four-level citation
    // would silently move it to a different position in the text.
    check("four levels are refused", !lpl::corpus::parsePassage("1.2.3.4", 7u, locus));
    check("an empty level is refused", !lpl::corpus::parsePassage("1..3", 4u, locus));
    check("a trailing separator is refused", !lpl::corpus::parsePassage("1.", 2u, locus));
    check("a citation range is refused", !lpl::corpus::parsePassage("1-5", 3u, locus));
    check("an overflowing ordinal is refused", !lpl::corpus::parsePassage("99999999999", 11u, locus));

    // A round trip must not change the DEPTH, because the depth is what the citation means.
    char rendered[32];
    (void) lpl::corpus::parsePassage("3.12", 4u, locus);
    (void) lpl::corpus::renderLocus(locus, false, rendered, 32u);
    check("a two-level passage renders back as two levels", std::strcmp(rendered, "3.12") == 0);
    (void) lpl::corpus::parsePassage("7", 1u, locus);
    (void) lpl::corpus::renderLocus(locus, false, rendered, 32u);
    check("a one-level passage renders back as one level", std::strcmp(rendered, "7") == 0);

    const lpl::corpus::Locus fileLine = lpl::corpus::lineLocus(4u, 1207u);
    check("a plain-text locus has no part", fileLine.part == 0u && fileLine.section == 4u &&
                                                fileLine.line == 1207u);
    (void) lpl::corpus::renderLocus(fileLine, true, rendered, 32u);
    check("a plain-text locus renders as its line", std::strcmp(rendered, "1207") == 0);

    check("reading order is part, then section, then line",
          lpl::corpus::locusPrecedes({1u, 9u, 9u}, {2u, 0u, 0u}) &&
              lpl::corpus::locusPrecedes({1u, 1u, 9u}, {1u, 2u, 0u}) &&
              !lpl::corpus::locusPrecedes({1u, 1u, 2u}, {1u, 1u, 1u}));
}

/**
 * @brief Exercises language tags.
 */
void testLanguage()
{
    std::printf("── language\n");

    lpl::corpus::LanguageTag tag{};
    check("a registered subtag resolves", lpl::corpus::languageByName("grc", 3u, tag) &&
                                              tag == lpl::corpus::LanguageTag::AncientGreek);
    check("the ecclesiastical extension resolves",
          lpl::corpus::languageByName("la-eccl", 7u, tag) && tag == lpl::corpus::LanguageTag::EcclesiasticalLatin);
    check("a word nothing carries is REFUSED, not defaulted",
          !lpl::corpus::languageByName("klingon", 7u, tag) && tag == lpl::corpus::LanguageTag::Unknown);
    check("every tag round-trips through its word", [] {
        for (lpl::core::u32 i = 1u; i < static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::Count); ++i)
        {
            const auto original = static_cast<lpl::corpus::LanguageTag>(i);
            const char *word = lpl::corpus::languageName(original);
            lpl::corpus::LanguageTag parsed{};
            if (!lpl::corpus::languageByName(word, length(word), parsed) || parsed != original)
                return false;
        }
        return true;
    }());

    check("a modern rendering is flagged as one",
          lpl::corpus::isModernRendering(lpl::corpus::LanguageTag::ModernFrench) &&
              !lpl::corpus::isModernRendering(lpl::corpus::LanguageTag::OldFrench));
}

/**
 * @brief Exercises the reader's refusals on malformed images.
 *
 * No foundation needed: a bad image is bytes, and refusing it is arithmetic-free.
 */
void testReaderRefusals()
{
    std::printf("── reader refusals\n");

    lpl::knowledge::KnowledgePack pack;
    check("a short buffer is too small",
          pack.open(reinterpret_cast<const lpl::core::u8 *>("LPLKNOW"), 7u) ==
              lpl::knowledge::OpenStatus::TooSmall);

    // A well-formed header for an image with no sections, built by hand so the test does not
    // depend on the baker — a reader test that needs the writer cannot tell which of the two
    // is wrong.
    std::vector<lpl::core::u8> image(32u, 0u);
    const char magic[] = {'L', 'P', 'L', 'K', 'N', 'O', 'W', '\0'};
    for (int i = 0; i < 8; ++i)
        image[static_cast<std::size_t>(i)] = static_cast<lpl::core::u8>(magic[i]);
    const auto put = [&image](std::size_t at, lpl::core::u32 value) {
        image[at] = static_cast<lpl::core::u8>(value & 0xFFu);
        image[at + 1u] = static_cast<lpl::core::u8>((value >> 8) & 0xFFu);
        image[at + 2u] = static_cast<lpl::core::u8>((value >> 16) & 0xFFu);
        image[at + 3u] = static_cast<lpl::core::u8>((value >> 24) & 0xFFu);
    };
    put(8u, lpl::knowledge::kFormatVersion);
    put(12u, 32u); // totalSize
    put(16u, 0u);  // sectionCount
    put(20u, lpl::knowledge::kFnv1aOffsetBasis); // hash over zero bytes
    check("an empty but well-formed image opens",
          pack.open(image.data(), 32u) == lpl::knowledge::OpenStatus::Ok);
    check("and holds nothing", pack.factCount() == 0u && pack.sourceCount() == 0u);
    check("an absent vocabulary is not an error", pack.textFor(1u) == nullptr);

    image[0] = 'X';
    check("bad magic is bad magic", pack.open(image.data(), 32u) == lpl::knowledge::OpenStatus::BadMagic);
    image[0] = 'L';

    put(8u, lpl::knowledge::kFormatVersion + 1u);
    check("an unknown version is refused", pack.open(image.data(), 32u) == lpl::knowledge::OpenStatus::BadVersion);
    put(8u, lpl::knowledge::kFormatVersion);

    put(12u, 64u);
    check("a declared size that is not the buffer size is refused",
          pack.open(image.data(), 32u) == lpl::knowledge::OpenStatus::SizeMismatch);
    put(12u, 32u);

    put(16u, 1u);
    check("a section table that does not fit is refused",
          pack.open(image.data(), 32u) == lpl::knowledge::OpenStatus::TableOutOfRange);
    put(16u, 0u);

    put(20u, 0u);
    check("a wrong content hash is refused", pack.open(image.data(), 32u) == lpl::knowledge::OpenStatus::HashMismatch);
    put(20u, lpl::knowledge::kFnv1aOffsetBasis);

    // A refusal must leave NOTHING adopted. The alternative — a reader still holding the
    // previous image, or the sections of the new one that happened to validate — is a reader
    // that answers questions about a file it rejected.
    check("the image is adopted again after the field is restored",
          pack.open(image.data(), 32u) == lpl::knowledge::OpenStatus::Ok);
    image[0] = 'X';
    (void) pack.open(image.data(), 32u);
    check("a rejection leaves nothing adopted", !pack.ready() && pack.size() == 0u);
}

/**
 * @brief Exercises query semantics that need no corpus of substance.
 */
void testQuery()
{
    std::printf("── query\n");

    lpl::knowledge::FactV1 fact{};
    fact.subject = 7u;
    fact.predicate = 11u;
    fact.object = 22u;
    // @warning DAYS, and this fixture held year numbers until the query started converting properly.
    // It passed all along -- both sides were wrong the same way, so `during(1200)` compared 1200
    // against 1200 and matched. A test can be green because its bug is symmetric with the code's.
    fact.fromDay = lpl::history::firstDayOfYear(1200);
    fact.toDay = lpl::history::lastDayOfYear(1250);
    fact.source = 100u;
    fact.confidenceRaw = 32768u;

    lpl::knowledge::Query query;
    check("a default query matches anything", lpl::knowledge::matches(query, fact));
    check("a window is inclusive at its start",
          lpl::knowledge::matches(lpl::knowledge::Query{}.during(1200), fact));
    check("and at its end", lpl::knowledge::matches(lpl::knowledge::Query{}.during(1250), fact));
    check("a year outside it does not match",
          !lpl::knowledge::matches(lpl::knowledge::Query{}.during(1199), fact));

    // An INSTANT -- one day -- must still answer a question asked about its year. That is the
    // asymmetry the unit exists for: sources are vague and a precise claim must not fall through
    // the gaps between their windows. Containment would drop exactly the sharpest facts.
    lpl::knowledge::FactV1 instant = fact;
    instant.fromDay = lpl::history::dayOfDate(1204, 6u, 18u);
    instant.toDay = instant.fromDay;
    check("an instant covers its own year", lpl::knowledge::matches(lpl::knowledge::Query{}.during(1204), instant));
    check("and is findable by its own day",
          lpl::knowledge::matches(lpl::knowledge::Query{}.onDay(instant.fromDay), instant));
    check("but not by the day before",
          !lpl::knowledge::matches(lpl::knowledge::Query{}.onDay(instant.fromDay - 1), instant));

    check("a confidence floor bites",
          !lpl::knowledge::matches(lpl::knowledge::Query{}.atLeast(40000u), fact) &&
              lpl::knowledge::matches(lpl::knowledge::Query{}.atLeast(30000u), fact));

    char described[128];
    const lpl::core::u32 written =
        lpl::knowledge::describeQuery(lpl::knowledge::Query{}.about(7u).during(1204), described, 128u);
    check("a question describes itself", written > 0u && std::strstr(described, "subject=7") != nullptr &&
                                             std::strstr(described, "year=1204") != nullptr);
    (void) lpl::knowledge::describeQuery(lpl::knowledge::Query{}, described, 128u);
    check("an unconstrained question says so", std::strstr(described, "everything") != nullptr);
}

/**
 * @brief Exercises the baked text window.
 */
void testTextView()
{
    std::printf("── text view\n");

    // Two lines, laid out the way the baker would: header, offset table with lineCount + 1
    // entries, then the bytes.
    const char *body = "alphabravo";
    std::vector<lpl::core::u8> section;
    const auto push = [&section](lpl::core::u32 value) {
        section.push_back(static_cast<lpl::core::u8>(value & 0xFFu));
        section.push_back(static_cast<lpl::core::u8>((value >> 8) & 0xFFu));
        section.push_back(static_cast<lpl::core::u8>((value >> 16) & 0xFFu));
        section.push_back(static_cast<lpl::core::u8>((value >> 24) & 0xFFu));
    };
    push(2u);  // lineCount
    push(10u); // byteCount
    push(0u);
    push(5u);
    push(10u);
    for (const char *c = body; *c != '\0'; ++c)
        section.push_back(static_cast<lpl::core::u8>(*c));

    lpl::corpus::TextView view;
    check("a well-formed text section opens", view.open(section.data(), static_cast<lpl::core::u32>(section.size())));
    const char *line = nullptr;
    lpl::core::u32 size = 0u;
    check("the first line is alpha", view.line(0u, line, size) && size == 5u && std::strncmp(line, "alpha", 5u) == 0);
    check("the LAST line needs no special rule",
          view.line(1u, line, size) && size == 5u && std::strncmp(line, "bravo", 5u) == 0);
    check("a line past the end is refused", !view.line(2u, line, size));

    // A non-monotonic table would let a "line" have a negative length.
    std::vector<lpl::core::u8> broken = section;
    broken[12] = 9u; // second offset now 9, third still 10 — but first was 0, second must be >= 0
    broken[8] = 6u;  // first offset 6 > second 9? no: make first 6, second 9 -> still monotonic
    broken[8] = 7u;
    broken[12] = 3u; // 7 then 3: not monotonic
    check("a non-monotonic offset table is refused",
          !view.open(broken.data(), static_cast<lpl::core::u32>(broken.size())));
}

} // namespace

int main()
{
    std::printf("test-corpus-identity — identity, addressing, and a reader's refusals\n");

    testUrn();
    testLocus();
    testLanguage();
    testReaderRefusals();
    testQuery();
    testTextView();

    // The project's verdict format, character for character: the full validation greps for
    // "ALL PASS (0 failure", so a line that says the same thing in another order is a
    // test that passes and is recorded as a failure.
    std::printf("-- one table for every language, and the aliases a real catalogue writes\n");
    {
        using lpl::corpus::LanguageTag;
        using lpl::corpus::LanguageEra;

        // @warning Every tag must have exactly ONE canonical row. Zero would make `languageName`
        // answer "unknown" for a language the enumeration names -- and that word goes into an
        // image, so a corpus would be baked with its language spelled as the absence of one.
        // Two would make the answer depend on table order.
        bool everyTagIsNamed = true;
        bool everyNameParsesBack = true;
        for (lpl::core::u32 i = 1u; i < static_cast<lpl::core::u32>(LanguageTag::Count); ++i)
        {
            const LanguageTag tag = static_cast<LanguageTag>(i);
            const char *name = lpl::corpus::languageName(tag);
            if (name == nullptr || std::strcmp(name, "unknown") == 0)
            {
                everyTagIsNamed = false;
                continue;
            }
            LanguageTag back = LanguageTag::Unknown;
            if (!lpl::corpus::languageByName(name, static_cast<lpl::core::u32>(std::strlen(name)), back) ||
                back != tag)
                everyNameParsesBack = false;
        }
        check("every tag in the enumeration has a canonical word", everyTagIsNamed);
        check("and every word parses back to the tag it names", everyNameParsesBack);

        // The aliases a real catalogue writes. Measured: HathiTrust and Gutenberg disagree about
        // which of these they use, and a reader that knew one would drop the other's corpus.
        struct Alias { const char *word; LanguageTag tag; };
        static constexpr Alias kAliases[] = {
            {"lat", LanguageTag::Latin},        {"la", LanguageTag::Latin},
            {"eng", LanguageTag::ModernEnglish},{"en", LanguageTag::ModernEnglish},
            {"fre", LanguageTag::ModernFrench}, {"fra", LanguageTag::ModernFrench},
            {"ger", LanguageTag::German},       {"deu", LanguageTag::German},
            {"chi", LanguageTag::Chinese},      {"zho", LanguageTag::Chinese},
        };
        bool everyAliasResolves = true;
        for (const Alias &alias : kAliases)
        {
            LanguageTag got = LanguageTag::Unknown;
            if (!lpl::corpus::languageByName(alias.word, static_cast<lpl::core::u32>(std::strlen(alias.word)),
                                             got) ||
                got != alias.tag)
                everyAliasResolves = false;
        }
        check("the three-letter codes a catalogue writes resolve too", everyAliasResolves);

        // @warning Fifteen centuries apart, told apart by two table rows. A first version mapped `el`
        // to AncientGreek, and the catalogue then claimed 216 works of ancient Greek from a source
        // that declares `grc` zero times.
        LanguageTag modern = LanguageTag::Unknown;
        LanguageTag ancient = LanguageTag::Unknown;
        check("modern Greek is read", lpl::corpus::languageByName("el", 2u, modern));
        check("ancient Greek is read", lpl::corpus::languageByName("grc", 3u, ancient));
        check("and they are not the same language",
              modern == LanguageTag::ModernGreek && ancient == LanguageTag::AncientGreek);

        // @warning The consequence of the era, and the reason it is derived rather than listed: a tag
        // added without an era would read as "not necessarily a modern rendering", which is the
        // answer that licenses redistributing a copyrighted translation.
        check("a modern vernacular is a modern rendering", lpl::corpus::isModernRendering(LanguageTag::German));
        check("an ancient language is not", !lpl::corpus::isModernRendering(LanguageTag::Latin));
        // A living language with an ancient literature is neither, and that is the honest answer.
        check("and a living language with an ancient literature claims neither",
              lpl::corpus::languageEra(LanguageTag::Arabic) == LanguageEra::Unspecified &&
                  !lpl::corpus::isModernRendering(LanguageTag::Arabic));

        check("an unknown word is refused, never defaulted",
              !lpl::corpus::languageByName("qqq", 3u, modern));
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
