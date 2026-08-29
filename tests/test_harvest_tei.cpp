/**
 * @file test_harvest_tei.cpp
 * @brief Reading a canonical text, and refusing to invent a citation for it.
 *
 * The fixtures are written by the test, but their SHAPE is not invented: every construct
 * below was observed in the real Perseus files this reader was built against — Herodotus
 * (`tlg0016.tlg001.perseus-grc2`, 2.9 MB, 9 books / 1578 chapters / 4338 sections) and Caesar
 * (`phi0448.phi001.perseus-lat2`, 8 / 404 / 2150). The two of them disagree about attribute
 * order and about whether ordinals are numeric, and both disagreements are reproduced here
 * because both of them would otherwise ship as silent defects.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/corpus/Locus.hpp>
#include <lpl/corpus/TextView.hpp>
#include <lpl/corpus/Urn.hpp>
#include <lpl/harvest/Baker.hpp>
#include <lpl/harvest/Predicates.hpp>
#include <lpl/harvest/AuthorDates.hpp>
#include <lpl/harvest/Tei.hpp>
#include <lpl/harvest/Xml.hpp>
#include <lpl/history/Calendar.hpp>
#include <lpl/history/Fact.hpp>
#include <lpl/knowledge/FactStore.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>
#include <lpl/knowledge/Provenance.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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
 * @brief Writes a fixture document.
 *
 * @param path Where.
 * @param body What.
 */
void writeDocument(const std::filesystem::path &path, const std::string &body)
{
    std::ofstream file{path, std::ios::binary};
    file << body;
}

/**
 * A work in Perseus's shape.
 *
 * @warning Deliberately awkward in three ways that the real files are:
 *  - the edition div writes `type` BEFORE `n`, as Caesar does and Herodotus does not;
 *  - one chapter is numbered `10A`, as 45 of Herodotus's are;
 *  - `<sourceDesc>` carries a second `<title>`, which is the one a naive reader picks up.
 */
const char *const kWork =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<TEI xmlns=\"http://www.tei-c.org/ns/1.0\">\n"
    "<teiHeader xml:lang=\"eng\">\n"
    "<fileDesc>\n"
    "<titleStmt>\n"
    "<title xml:lang=\"lat\">De bello Gallico</title>\n"
    "<author>Julius Caesar</author>\n"
    "<editor>T. Rice Holmes</editor>\n"
    "</titleStmt>\n"
    "<sourceDesc><biblStruct><monogr><title>THE WRONG TITLE</title>\n"
    "<author>THE WRONG AUTHOR</author></monogr></biblStruct></sourceDesc>\n"
    "</fileDesc>\n"
    "<encodingDesc>\n"
    "<refsDecl n=\"CTS\">\n"
    "<cRefPattern n=\"section\" matchPattern=\"(\\w+).(\\w+).(\\w+)\"/>\n"
    "<cRefPattern n=\"chapter\" matchPattern=\"(\\w+).(\\w+)\"/>\n"
    "<cRefPattern n=\"book\" matchPattern=\"(\\w+)\"/>\n"
    "</refsDecl>\n"
    "</encodingDesc>\n"
    "</teiHeader>\n"
    "<text>\n"
    "<body>\n"
    "<div type=\"edition\"  xml:lang=\"lat\" n=\"urn:cts:latinLit:phi0448.phi001.perseus-lat2\">\n"
    "<div type=\"textpart\" subtype=\"book\" n=\"1\">\n"
    "<div type=\"textpart\" subtype=\"chapter\" n=\"2\">\n"
    "<div type=\"textpart\" subtype=\"section\" n=\"3\">\n"
    "<p>Gallia est omnis divisa in partes tres &amp; reliqua.</p>\n"
    "</div>\n"
    "<div type=\"textpart\" subtype=\"section\" n=\"4\">\n"
    "<!-- a comment holding <div type=\"textpart\" n=\"999\"> which must not be read -->\n"
    "<p>Horum omnia&#x0020;fortissimi sunt Belgae.</p>\n"
    "</div>\n"
    "</div>\n"
    "<div type=\"textpart\" subtype=\"chapter\" n=\"10A\">\n"
    "<div type=\"textpart\" subtype=\"section\" n=\"1\">\n"
    "<p>A subdivided chapter, numbered after the fact.</p>\n"
    "</div>\n"
    "</div>\n"
    "</div>\n"
    "</div>\n"
    "</body>\n"
    "</text>\n"
    "</TEI>\n";

} // namespace

int main()
{
    std::printf("test-harvest-tei — a canonical text, cited the way its edition cites it\n");

    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lplknow-tei-fixture";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    // ── Recognition ───────────────────────────────────────────────────────────
    std::printf("── recognition\n");
    check("a TEI document is recognised", lpl::harvest::looksLikeTei(kWork));
    // Keyed on the namespace, not the root tag: `<TEI>` is a plausible element name elsewhere,
    // and the namespace is what the standard actually pins down.
    check("a bare <TEI> element without the namespace is not TEI",
          !lpl::harvest::looksLikeTei("<TEI><body>not tei</body></TEI>"));
    check("markdown is not TEI", !lpl::harvest::looksLikeTei("# A heading\n\nsome prose\n"));

    // ── @warning Attribute order, the trap the real corpus set ───────────────────────
    std::printf("── attributes\n");
    {
        std::string value;
        check("an attribute is found after others",
              lpl::harvest::xmlAttribute("<div type=\"edition\"  xml:lang=\"lat\" n=\"urn:x\">", "n", value) &&
                  value == "urn:x");
        check("and before others",
              lpl::harvest::xmlAttribute("<div n=\"urn:y\" type=\"edition\">", "n", value) && value == "urn:y");
        // Herodotus writes `n` first and Caesar writes it third. A reader that scans for a
        // position finds one and silently reports the other as having no identity.
        check("so the two orders give the same answer",
              lpl::harvest::xmlAttribute("<div n=\"urn:z\" type=\"edition\">", "n", value) &&
                  value == "urn:z");
        check("a namespaced attribute matches its local name",
              lpl::harvest::xmlAttribute("<div xml:lang=\"grc\">", "lang", value) && value == "grc");
        check("single quotes are accepted",
              lpl::harvest::xmlAttribute("<div n='1'>", "n", value) && value == "1");
        check("an absent attribute is absent",
              !lpl::harvest::xmlAttribute("<div type=\"edition\">", "n", value));
        // A value containing the other attribute's name must not be mistaken for it.
        check("a value is not mistaken for an attribute",
              lpl::harvest::xmlAttribute("<div rend=\"n=7\" n=\"3\">", "n", value) && value == "3");
    }

    check("a Greek language code maps", lpl::harvest::teiLanguage("grc") == lpl::corpus::LanguageTag::AncientGreek);
    check("a Latin one too", lpl::harvest::teiLanguage("lat") == lpl::corpus::LanguageTag::Latin);
    check("an unknown one is Unknown", lpl::harvest::teiLanguage("xyz") == lpl::corpus::LanguageTag::Unknown);

    // ── A whole work ──────────────────────────────────────────────────────────
    std::printf("── a work\n");
    writeDocument(root / "caesar.xml", kWork);
    std::vector<lpl::harvest::TeiSource> sources{{(root / "caesar.xml").string(), "fixture/caesar.xml"}};

    lpl::harvest::Baker baker;
    lpl::harvest::TeiOptions options;
    options.carryText = true;
    lpl::harvest::TeiIngestReport report{};
    check("the work is read", lpl::harvest::ingestTei(sources, options, baker, report));
    check("one work was identified", report.works == 1u);
    check("its author was named", report.authors == 1u);
    check("it names a CTS URN of its own", report.withoutUrn == 0u);

    // Five textparts: book 1, chapters 2 and 10A, sections 3, 4 and 1. Every one of them is
    // citable in its own right — a reader asking for book 1 chapter 2 must find it whether or
    // not the edition also subdivides it.
    check("every textpart is a citable passage", report.passages == 6u);
    check("the deepest citation is three levels", report.deepest == 3u);

    // @warning The one that cannot be fudged. `10A` is not expressible as three ordinals, and
    // `parsePassage` already refuses it rather than truncating. Both the chapter and the
    // section under it are therefore unaddressable.
    check("a non-numeric ordinal is counted, not invented", report.unaddressable == 2u);
    check("and the first one is named", report.firstUnaddressable == "1.10A" ||
                                            report.firstUnaddressable == "1.10A.1");

    std::vector<lpl::core::u8> image;
    lpl::harvest::BakeReport bake{};
    check("the work bakes", baker.build(image, bake));
    check("every claim has a described source", bake.unsourced == 0u);

    // @warning **A TEI edition carries no composition date, so it must not pass as an eyewitness.**
    // This reader used to write zero into `yearsAfterEvent`, and zero is not "unknown" -- it is
    // the strongest claim a source can make about itself. Every classical work ingested here was
    // therefore scored with full temporal-proximity credit: Herodotus writing three generations
    // after Croesus rated as though he had been standing there. Asserted rather than merely
    // fixed, because a value nothing checks is a value that drifts back.
    {
        lpl::knowledge::KnowledgePack pack;
        check("the baked work reopens",
              pack.open(image.data(), image.size()) == lpl::knowledge::OpenStatus::Ok);
        bool everySourceDeclaresItsDistanceUnknown = pack.sourceCount() != 0u;
        for (lpl::core::u32 i = 0u; i < pack.sourceCount(); ++i)
        {
            lpl::knowledge::SourceV1 wire{};
            if (!pack.sourceAt(i, wire) || wire.yearsAfterEvent != lpl::history::kUnknownYearsAfterEvent)
                everySourceDeclaresItsDistanceUnknown = false;
        }
        check("a TEI edition declares its distance from the events UNKNOWN, not zero",
              everySourceDeclaresItsDistanceUnknown);

        // @warning "Nobody looked" and "somebody looked and found nothing" are different facts, and
        // an empty window cannot tell them apart. The flag can, which is what lets a caller sort
        // the established from the merely unfilled instead of treating both as missing.
        bool everySourceSaysItIsUndated = pack.sourceCount() != 0u;
        for (lpl::core::u32 i = 0u; i < pack.sourceCount(); ++i)
        {
            lpl::knowledge::SourceV1 wire{};
            if (!pack.sourceAt(i, wire) ||
                (wire.flags & lpl::knowledge::kSourceFlagUndated) == 0u || wire.composedFrom != 0 ||
                wire.composedTo != 0)
                everySourceSaysItIsUndated = false;
        }
        check("and says its dating was looked for and not found", everySourceSaysItIsUndated);

        // And the consequence, measured rather than assumed: the unknown must cost the source
        // its proximity credit, or the sentinel would be a label with no effect.
        lpl::history::SourceProfile unknown;
        unknown.kind = lpl::history::SourceKind::Chronicle;
        unknown.yearsAfterEvent = lpl::history::kUnknownYearsAfterEvent;
        lpl::history::SourceProfile witness = unknown;
        witness.yearsAfterEvent = 0u;
        check("and an unknown distance scores strictly below an eyewitness",
              lpl::history::trustworthiness(unknown) < lpl::history::trustworthiness(witness));
    }

    lpl::knowledge::KnowledgePack pack;
    check("the image opens", pack.open(image.data(), static_cast<lpl::core::u32>(image.size())) ==
                                 lpl::knowledge::OpenStatus::Ok);

    const lpl::knowledge::FactStore store{pack};
    lpl::knowledge::Page page;

    const std::string urnText = "urn:cts:latinLit:phi0448.phi001.perseus-lat2";
    const lpl::core::u32 work =
        lpl::corpus::nameIdentifier(urnText.data(), static_cast<lpl::core::u32>(urnText.size()));

    // @warning The title comes from <titleStmt>, never from <sourceDesc>. A `<title>` also occurs
    // inside the bibliographic description of the printed book this was scanned from, and
    // taking that one silently retitles the work.
    lpl::knowledge::Query title;
    title.about(work).asserting(lpl::harvest::kPredicateWorkTitle);
    store.run(title, page);
    check("the work has a title", page.matched == 1u);
    {
        const lpl::core::u8 *section = nullptr;
        lpl::core::u32 bytes = 0u;
        lpl::corpus::TextView view;
        const char *line = nullptr;
        lpl::core::u32 size = 0u;
        check("the image carries its texts",
              pack.section(lpl::knowledge::SectionType::Texts, section, bytes) && view.open(section, bytes));
        check("and it is the titleStmt title, not the sourceDesc one",
              page.count == 1u && view.line(page.rows[0].object, line, size) &&
                  std::string(line, size) == "De bello Gallico");
    }

    lpl::knowledge::Query author;
    author.about(work).asserting(lpl::harvest::kPredicateAttributedTo);
    store.run(author, page);
    check("the work is attributed", page.matched == 1u);
    check("to the titleStmt author",
          page.count == 1u && pack.textFor(page.rows[0].object) != nullptr &&
              std::strcmp(pack.textFor(page.rows[0].object), "Julius Caesar") == 0);

    lpl::knowledge::Query editor;
    editor.about(work).asserting(lpl::harvest::kPredicateEditedBy);
    store.run(editor, page);
    check("the modern editor is recorded apart from the author", page.matched == 1u);

    lpl::knowledge::Query scheme;
    scheme.about(work).asserting(lpl::harvest::kPredicateCitationScheme);
    store.run(scheme, page);
    check("the citation scheme the file DECLARES is recorded", page.matched == 1u);

    // ── The three-level locus, which nothing had ever written ─────────────────
    std::printf("── citation\n");
    {
        // @warning A passage is a POSITION, not an entity. The first version minted an identifier
        // per passage and it did not survive the real corpus: `nameIdentifier` is 32 bits,
        // Perseus carries 395 470 passages, and two of them collided on one identifier within
        // seconds. `FactV1` already had a locus, and that was the field that meant "which
        // passage" all along — so the subject is the WORK and the locus says where.
        lpl::knowledge::Query words;
        words.about(work).asserting(lpl::harvest::kPredicatePassageText);
        words.take(64u);
        store.run(words, page);
        check("the work's passages carry their words", page.matched == 3u);

        // Find the one at book 1, chapter 2, section 3 by its CITATION.
        bool foundThree = false;
        std::string quoted;
        for (lpl::core::u32 i = 0u; i < page.count; ++i)
        {
            lpl::knowledge::Citation citation{};
            if (!lpl::knowledge::cite(pack, page.rows[i], citation) || citation.hasLocus == 0u)
                continue;
            if (citation.locus.part == 1u && citation.locus.section == 2u && citation.locus.line == 3u)
            {
                foundThree = true;
                const lpl::core::u8 *section = nullptr;
                lpl::core::u32 bytes = 0u;
                lpl::corpus::TextView view;
                const char *line = nullptr;
                lpl::core::u32 size = 0u;
                if (pack.section(lpl::knowledge::SectionType::Texts, section, bytes) &&
                    view.open(section, bytes) && view.line(page.rows[i].object, line, size))
                    quoted.assign(line, size);
            }
        }
        check("a passage is addressable at all three levels", foundThree);
        check("with entities decoded and markup stripped",
              quoted == "Gallia est omnis divisa in partes tres & reliqua.");

        // And it is retrievable BY that position, which is what `Query::at` exists for.
        lpl::knowledge::Query byPlace;
        byPlace.about(work).asserting(lpl::harvest::kPredicatePassageText);
        for (lpl::core::u32 i = 0u; i < page.count; ++i)
        {
            lpl::knowledge::Citation citation{};
            if (lpl::knowledge::cite(pack, page.rows[i], citation) && citation.locus.part == 1u &&
                citation.locus.section == 2u && citation.locus.line == 4u)
                byPlace.at(page.rows[i].locus);
        }
        lpl::knowledge::Page one;
        store.run(byPlace, one);
        check("a query narrowed to one position returns one passage", one.matched == 1u);

        const lpl::core::u8 *section = nullptr;
        lpl::core::u32 bytes = 0u;
        lpl::corpus::TextView view;
        const char *line = nullptr;
        lpl::core::u32 size = 0u;
        (void) pack.section(lpl::knowledge::SectionType::Texts, section, bytes);
        (void) view.open(section, bytes);
        // A numeric character reference becomes UTF-8, because these corpora are Greek and
        // Syriac and a codepoint above 127 is the normal case rather than the exception.
        check("a numeric character reference is decoded",
              one.count == 1u && view.line(one.rows[0].object, line, size) &&
                  std::string(line, size) == "Horum omnia fortissimi sunt Belgae.");
        // @warning And the comment in that passage held a well-formed <div> that must not have been
        // read as structure. Six passages, not seven, is what says it was skipped.
        check("a div inside a comment was not read as structure", report.passages == 6u);
    }

    // ── An unaddressable passage is cited at the work, never at a made-up locus ─
    {
        // The `10A` chapter and its section. Neither may carry ordinals nobody wrote.
        lpl::knowledge::Query all;
        all.about(work).asserting(lpl::harvest::kPredicatePassageText);
        all.take(64u);
        store.run(all, page);
        lpl::core::u32 atOrigin = 0u;
        for (lpl::core::u32 i = 0u; i < page.count; ++i)
        {
            lpl::knowledge::Citation citation{};
            if (lpl::knowledge::cite(pack, page.rows[i], citation) && citation.locus.part == 0u &&
                citation.locus.section == 0u && citation.locus.line == 0u)
                ++atOrigin;
        }
        check("the unaddressable passage is cited at the work, with no ordinals invented",
              atOrigin == 1u);
    }

    // ── Text is opt-in ────────────────────────────────────────────────────────
    std::printf("── refusals and defaults\n");
    {
        lpl::harvest::Baker b;
        lpl::harvest::TeiOptions plain; // carryText defaults to false
        lpl::harvest::TeiIngestReport r{};
        check("the same work reads as a catalogue", lpl::harvest::ingestTei(sources, plain, b, r));
        check("the structure is still there", r.passages == 6u && r.works == 1u);
        check("but the words are not", r.textLines == 0u);
    }

    // A passage longer than the bound is CUT and counted, never silently kept whole: the
    // input is a file whose structure this reader did not choose.
    {
        lpl::harvest::Baker b;
        lpl::harvest::TeiOptions tight;
        tight.carryText = true;
        tight.maxPassageBytes = 8u;
        lpl::harvest::TeiIngestReport r{};
        check("a tight bound still reads", lpl::harvest::ingestTei(sources, tight, b, r));
        check("and reports what it cut", r.truncated >= 1u);
    }

    // @warning Two files carrying one URN would fuse two editions into one work. Baker cannot see
    // it — it sees names, and both files agree on the name — so this reader refuses.
    {
        writeDocument(root / "twin.xml", kWork);
        std::vector<lpl::harvest::TeiSource> twins{
            {(root / "caesar.xml").string(), "fixture/caesar.xml"},
            {(root / "twin.xml").string(), "fixture/twin.xml"},
        };
        lpl::harvest::Baker b;
        lpl::harvest::TeiIngestReport r{};
        check("two works on one URN are refused", !lpl::harvest::ingestTei(twins, options, b, r));
    }

    // A file with no CTS URN is identified by where it came from, and says so.
    {
        std::string anonymous{kWork};
        const std::size_t at = anonymous.find(" n=\"urn:cts:latinLit:phi0448.phi001.perseus-lat2\"");
        anonymous.erase(at, std::strlen(" n=\"urn:cts:latinLit:phi0448.phi001.perseus-lat2\""));
        writeDocument(root / "anon.xml", anonymous);
        std::vector<lpl::harvest::TeiSource> only{{(root / "anon.xml").string(), "fixture/anon.xml"}};
        lpl::harvest::Baker b;
        lpl::harvest::TeiIngestReport r{};
        check("a work with no URN is still read", lpl::harvest::ingestTei(only, options, b, r));
        check("and counted apart", r.withoutUrn == 1u && r.works == 1u);
    }

    // ── The predicate table that keeps a library from printing #N ─────────────
    check("a text-valued predicate is known to be one",
          lpl::harvest::objectIsTextLine(lpl::harvest::kPredicatePassageText));
    check("and an identifier-valued one is not",
          !lpl::harvest::objectIsTextLine(lpl::harvest::kPredicateAttributedTo));

    std::printf("-- the soft join: a value may be filled, an identity may not\n");
    {
        // @warning A TEI file has no composition date and a bibliographic catalogue has the author's
        // dates, and the two share no identifier. The join is therefore a guess about identity --
        // admissible only because of what it is allowed to do.
        lpl::harvest::AuthorDates dates;
        // The fixture's own author, so the EXACT path actually fires. A table full of names the
        // corpus does not contain exercises only the refusal, and a check that can only ever see
        // the negative half proves nothing about the feature.
        dates.add("Julius Caesar", lpl::history::firstDayOfYear(-100), lpl::history::lastDayOfYear(-44));
        // And one that merely resembles somebody, to exercise the other half.
        dates.add("Julius Caesarion", lpl::history::firstDayOfYear(-47), lpl::history::lastDayOfYear(-30));
        dates.finalise();

        lpl::harvest::TeiOptions joined;
        joined.authorDates = &dates;
        lpl::harvest::Baker joinedBaker;
        lpl::harvest::TeiIngestReport joinedReport{};
        check("the work reads with a date table", lpl::harvest::ingestTei(sources, joined, joinedBaker,
                                                                          joinedReport));

        std::vector<lpl::core::u8> joinedImage;
        lpl::harvest::BakeReport joinedBake{};
        check("and bakes", joinedBaker.build(joinedImage, joinedBake));

        lpl::knowledge::KnowledgePack joinedPack;
        check("and reopens",
              joinedPack.open(joinedImage.data(), joinedImage.size()) == lpl::knowledge::OpenStatus::Ok);

        // The fixture's author is not "Caesar, Julius", so nothing may be filled from this table.
        // What matters is that the ABSENCE is the honest one: undated, not dated wrongly.
        bool nothingWasInvented = joinedPack.sourceCount() != 0u;
        for (lpl::core::u32 i = 0u; i < joinedPack.sourceCount(); ++i)
        {
            lpl::knowledge::SourceV1 wire{};
            if (!joinedPack.sourceAt(i, wire))
                continue;
            const bool soft = (wire.flags & lpl::knowledge::kSourceFlagWindowFromNameMatch) != 0u;
            // A window may exist ONLY alongside the flag that says where it came from. An unmarked
            // window is indistinguishable from one an editor stated, and that is the whole of what
            // provenance means here.
            if ((wire.composedFrom != 0 || wire.composedTo != 0) && !soft)
                nothingWasInvented = false;
        }
        check("no window exists without the flag that says it came from a name", nothingWasInvented);

        // The positive half: an exact name DID fill a window, and said so.
        check("an exact author name dates the edition", joinedReport.datedByNameMatch == 1u);
        bool oneSourceCarriesTheWindow = false;
        for (lpl::core::u32 i = 0u; i < joinedPack.sourceCount(); ++i)
        {
            lpl::knowledge::SourceV1 wire{};
            if (!joinedPack.sourceAt(i, wire))
                continue;
            if ((wire.flags & lpl::knowledge::kSourceFlagWindowFromNameMatch) != 0u &&
                wire.composedFrom == lpl::history::firstDayOfYear(-100) &&
                wire.composedTo == lpl::history::lastDayOfYear(-44))
                oneSourceCarriesTheWindow = true;
        }
        check("with the window the table stated", oneSourceCarriesTheWindow);
        // @warning And it is NOT also marked undated: the two flags answer the same question and
        // both being set would mean the record contradicts itself about its own provenance.
        bool neverBothFlags = true;
        for (lpl::core::u32 i = 0u; i < joinedPack.sourceCount(); ++i)
        {
            lpl::knowledge::SourceV1 wire{};
            if (!joinedPack.sourceAt(i, wire))
                continue;
            if ((wire.flags & lpl::knowledge::kSourceFlagWindowFromNameMatch) != 0u &&
                (wire.flags & lpl::knowledge::kSourceFlagUndated) != 0u)
                neverBothFlags = false;
        }
        check("a dated source is not also marked undated", neverBothFlags);
    }

    std::filesystem::remove_all(root, error);


    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
