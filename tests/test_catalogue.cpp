/**
 * @file test_catalogue.cpp
 * @brief Indexing a holding without fetching it, and refusing to embellish the row.
 *
 * The fixtures are written by the test, and their shape was taken from real files rather than
 * from documentation: a HathiTrust update of 126 650 volumes and Project Gutenberg's per-ebook
 * RDF. Two things the real data does are reproduced here because both would otherwise ship as
 * silent defects — a tab-separated line with EMPTY columns in the middle, and a rights code
 * that has nothing to do with the year.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/corpus/Language.hpp>
#include <lpl/corpus/TextView.hpp>
#include <lpl/harvest/Baker.hpp>
#include <lpl/harvest/Catalogue.hpp>
#include <lpl/harvest/CatalogueStream.hpp>
#include <lpl/harvest/MappedFile.hpp>
#include <lpl/knowledge/Query.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
 * @brief Writes a fixture file.
 *
 * @param path Where.
 * @param body What.
 */
void writeFile(const std::filesystem::path &path, const std::string &body)
{
    std::ofstream file{path, std::ios::binary};
    file << body;
}

/**
 * @brief Builds one HathiTrust line with the columns this reader uses.
 *
 * @param id     Volume identifier.
 * @param access allow | deny.
 * @param rights The holder's rights code.
 * @param title  Title.
 * @param year   Year field, as the file writes it.
 * @param lang   Language code.
 * @param author Author, possibly empty.
 * @return The line.
 */
[[nodiscard]] std::string hathiLine(const char *id, const char *access, const char *rights, const char *title,
                                    const char *year, const char *lang, const char *author)
{
    std::vector<std::string> columns(26u, "");
    columns[0] = id;
    columns[1] = access;
    columns[2] = rights;
    columns[11] = title;
    columns[16] = year;
    columns[18] = lang;
    columns[25] = author;
    std::string line;
    for (std::size_t i = 0u; i < columns.size(); ++i)
    {
        if (i != 0u)
            line += '\t';
        line += columns[i];
    }
    return line;
}

/**
 * @class CountingSink
 * @brief A sink that records how a reader called it.
 *
 * @warning Written because the obvious test could not fail. Splitting was exercised by giving the
 * stream a tiny ceiling and checking every row's title still resolved — and removing a
 * `beginRow()` from the reader left that test perfectly green, because whether the omission
 * corrupts anything depends on where the parts happen to fall. The invariant is not "the parts
 * came out right this time"; it is that EVERY row is announced, and that is exact.
 */
class CountingSink final : public lpl::harvest::ICatalogueSink {
public:
    lpl::core::u32 addText(std::string_view text) override
    {
        (void) text;
        return _texts++;
    }
    void addCatalogueEntry(const lpl::knowledge::CatalogueEntryV1 &entry) override
    {
        (void) entry;
        ++rows;
    }
    void beginRow() override { ++boundaries; }

    lpl::core::u32 rows{0u};
    lpl::core::u32 boundaries{0u};

private:
    lpl::core::u32 _texts{0u};
};

} // namespace

int main()
{
    std::printf("test-catalogue — what exists, and where, without the words\n");

    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lplknow-catalogue-fixture";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    // ── Splitting, which is where the off-by-one lives ───────────────────────
    std::printf("── columns\n");
    {
        std::vector<std::string_view> fields;
        lpl::harvest::splitTabs("a\tb\tc", fields);
        check("three fields split into three", fields.size() == 3u && fields[1] == "b");

        // @warning Real holdings data has empty columns in the middle — a volume with no OCLC, no
        // ISBN, no author. A splitter that skipped them would shift every column after the
        // first gap, so a title would be read out of the imprint field on exactly the rows
        // whose data is thinnest.
        lpl::harvest::splitTabs("a\t\t\tb", fields);
        check("empty columns in the middle are still columns",
              fields.size() == 4u && fields[1].empty() && fields[3] == "b");

        // And a trailing empty field is a field. Dropping it makes a 26-column line look like
        // a 25-column one, which is how a reader decides a perfectly good row is malformed.
        lpl::harvest::splitTabs("a\tb\t", fields);
        check("a trailing empty column is a column", fields.size() == 3u && fields[2].empty());
        lpl::harvest::splitTabs("", fields);
        check("an empty line is one empty field", fields.size() == 1u);
    }

    // ── A holdings file ───────────────────────────────────────────────────────
    std::printf("── holdings\n");
    std::string holdings;
    holdings += hathiLine("mdp.001", "allow", "pd", "Beleaguered tower", "1876", "eng", "Ross, Ronald J.") + "\n";
    holdings += hathiLine("mdp.002", "deny", "ic", "A book still in copyright", "1976", "fre", "") + "\n";
    holdings += hathiLine("uc1.003", "allow", "pdus", "Histoires", "c1885", "grc", "Herodotus") + "\n";
    holdings += "a\tshort\tline\n"; // malformed: fewer columns than the format promises
    // @warning No trailing newline. A file need not end in one, so the LAST row goes through a
    // different branch of the reader — and that branch was missing its row boundary, which a
    // fixture ending in a newline can never catch.
    holdings += hathiLine("uc1.004", "allow", "pd", "A last line with no newline", "1799", "lat", "Livy");
    writeFile(root / "hathi.txt", holdings);

    lpl::harvest::Baker baker;
    lpl::harvest::CatalogueIngestReport report{};
    const lpl::core::u32 holder = 0xABCDEF01u;
    check("the holdings file is read",
          lpl::harvest::ingestHathiFile((root / "hathi.txt").string(), holder, baker, report));
    check("four rows were written", report.rows == 4u);
    check("and the short line was refused, not padded", report.malformed == 1u);

    // @warning Rights come from the holder's own code and never from the year. The 1976 row is in
    // copyright and the 1885 row is not — a reader that inferred from the date would get both
    // backwards, and would be writing a legal opinion into a data field while doing it.
    check("public domain is counted from the rights code", report.publicDomain == 3u);
    check("access is counted apart from rights", report.fullView == 3u);
    check("a year before 1900 is counted", report.beforeNineteenHundred == 3u);
    check("a missing creator is counted rather than invented", report.withoutCreator == 1u);
    check("every row had a title", report.withoutTitle == 0u);
    check("and a year", report.withoutYear == 0u);

    std::vector<lpl::core::u8> image;
    lpl::harvest::BakeReport bake{};
    check("the catalogue bakes", baker.build(image, bake));
    check("the bake reports its rows", bake.catalogue == 4u);
    // A catalogue asserts no claims: it says a holder has something, which is not a claim
    // about the world. An image of pure holdings therefore carries zero facts, and that is
    // the shape rather than an omission.
    check("a catalogue carries no facts", bake.facts == 0u);

    lpl::knowledge::KnowledgePack pack;
    check("the image opens", pack.open(image.data(), static_cast<lpl::core::u32>(image.size())) ==
                                 lpl::knowledge::OpenStatus::Ok);
    check("and its catalogue is readable", pack.catalogueCount() == 4u);

    lpl::knowledge::CatalogueEntryV1 entry{};
    check("a row reads back", pack.catalogueAt(0u, entry));
    check("with its holder", entry.holder == holder);
    check("its year", entry.year == 1876);
    check("its language",
          entry.language == static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::ModernEnglish));
    check("its rights", (entry.flags & lpl::knowledge::kCatalogueFlagPublicDomain) != 0u);
    // A HathiTrust volume is a scan with machine-read text over it, and saying so is the
    // difference between a passage that can be quoted and one that can only be looked at.
    check("and what kind of thing it is",
          (entry.flags & lpl::knowledge::kCatalogueFlagOpticalCharacterRecognition) != 0u &&
              (entry.flags & lpl::knowledge::kCatalogueFlagTranscription) == 0u);

    {
        const lpl::core::u8 *section = nullptr;
        lpl::core::u32 bytes = 0u;
        lpl::corpus::TextView view;
        const char *line = nullptr;
        lpl::core::u32 size = 0u;
        check("the image carries its strings",
              pack.section(lpl::knowledge::SectionType::Texts, section, bytes) && view.open(section, bytes));
        check("a title is a text line, never a hashed identifier",
              view.line(entry.title - 1u, line, size) && std::string(line, size) == "Beleaguered tower");
        check("and so is the address",
              view.line(entry.address - 1u, line, size) &&
                  std::string(line, size) == "https://babel.hathitrust.org/cgi/pt?id=mdp.001");
    }

    // The in-copyright row keeps neither flag, and its creator field is empty rather than
    // filled with something plausible.
    check("the second row reads back", pack.catalogueAt(1u, entry));
    check("an in-copyright volume is not marked free",
          (entry.flags & lpl::knowledge::kCatalogueFlagPublicDomain) == 0u);
    check("nor served whole", (entry.flags & lpl::knowledge::kCatalogueFlagFullView) == 0u);
    // @warning The one that was measured, not imagined. With zero-based indices a missing creator
    // kept a zero — and zero is a perfectly good line — so a row the holder credits to nobody
    // rendered ANOTHER row's text as its author. On real data a US Geological Survey serial came
    // back credited to a Japanese journal, and nothing about the output looked wrong.
    check("and a missing creator stays missing", entry.creator == lpl::knowledge::kNoIdentifier);
    {
        const lpl::core::u8 *section = nullptr;
        lpl::core::u32 bytes = 0u;
        lpl::corpus::TextView view;
        const char *line = nullptr;
        lpl::core::u32 size = 0u;
        (void) pack.section(lpl::knowledge::SectionType::Texts, section, bytes);
        (void) view.open(section, bytes);
        // The check the old assertion could not make: absence and line zero must be
        // DISTINGUISHABLE, which means line zero has to exist and hold something else.
        check("line zero exists and belongs to another row",
              view.line(0u, line, size) && std::string(line, size) == "Beleaguered tower");
        lpl::knowledge::CatalogueEntryV1 credited{};
        check("a row that names a creator resolves it", pack.catalogueAt(0u, credited) &&
                                                            credited.creator != lpl::knowledge::kNoIdentifier);
        check("and it is that row's own creator",
              view.line(credited.creator - 1u, line, size) && std::string(line, size) == "Ross, Ronald J.");
    }

    // ── Gutenberg ─────────────────────────────────────────────────────────────
    std::printf("── transcriptions\n");
    {
        const std::string rdf =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<rdf:RDF>\n"
            "  <pgterms:ebook rdf:about=\"ebooks/1\">\n"
            "    <dcterms:issued rdf:datatype=\"x\">1971-12-01</dcterms:issued>\n"
            "    <dcterms:rights>Public domain in the USA.</dcterms:rights>\n"
            "    <dcterms:license rdf:resource=\"license\"/>\n"
            // @warning An editor listed BEFORE the creator, which is the shape 2947 of Gutenberg's
            // 79 179 records actually have. A reader that takes the first `pgterms:name` in the
            // file credits the work to this man, plausibly and wrongly.
            "    <marcrel:edt><pgterms:agent><pgterms:name>Wilson, Epiphanius</pgterms:name>"
            "<pgterms:deathdate rdf:datatype=\"x\">1916</pgterms:deathdate>"
            "</pgterms:agent></marcrel:edt>\n"
            "    <dcterms:creator><pgterms:agent><pgterms:name>Jefferson, Thomas</pgterms:name>"
            "<pgterms:birthdate rdf:datatype=\"x\">1743</pgterms:birthdate>"
            "<pgterms:deathdate rdf:datatype=\"x\">1826</pgterms:deathdate>"
            "</pgterms:agent></dcterms:creator>\n"
            "    <dcterms:title>The Declaration of Independence &amp; c.</dcterms:title>\n"
            "    <dcterms:language><rdf:Description><rdf:value>en</rdf:value></rdf:Description></dcterms:language>\n"
            "  </pgterms:ebook>\n"
            "</rdf:RDF>\n";
        writeFile(root / "pg1.rdf", rdf);

        lpl::harvest::Baker b;
        lpl::harvest::CatalogueIngestReport r{};
        check("the RDF is read",
              lpl::harvest::ingestGutenbergRdf({(root / "pg1.rdf").string()}, 7u, b, r));
        check("one row was written", r.rows == 1u && r.malformed == 0u);
        check("it is free of rights", r.publicDomain == 1u);

        std::vector<lpl::core::u8> bytes;
        lpl::harvest::BakeReport br{};
        check("it bakes", b.build(bytes, br));
        lpl::knowledge::KnowledgePack pk;
        check("it opens", pk.open(bytes.data(), static_cast<lpl::core::u32>(bytes.size())) ==
                              lpl::knowledge::OpenStatus::Ok);
        lpl::knowledge::CatalogueEntryV1 row{};
        check("the row reads back", pk.catalogueAt(0u, row));
        // @warning 1971 is when GUTENBERG published the transcription, and an earlier version of this
        // reader wrote it as the year. Measured consequence on the real catalogue: **zero** of
        // 79 179 works came back as published before 1900 — correct about the transcription and
        // useless about the works, since Herodotus answered "2006".
        check("the transcription date is NOT the year", row.year != 1971);
        // What the file does carry about the work's age is when its creator died, which bounds
        // composition from above rather than guessing at it.
        // @warning **A WINDOW, and it used to be a point.** The file says the creator lived 1743 to
        // 1826, so what is known is that the work is from somewhere in there -- eighty-three
        // years. An earlier version wrote only the death year, which reads as a date and is a
        // guess: collapsing a window to its end presents a claim nobody made.
        check("the window opens at the creator's birth", row.year == 1743);
        check("and closes at their death", row.yearTo == 1826);
        check("and the row says where that year came from",
              (row.flags & lpl::knowledge::kCatalogueFlagYearFromAuthor) != 0u);
        // @warning NOT the editor's 1916. The two are both in the file, both in agent blocks, both
        // named `pgterms:deathdate` — only the enclosing element tells them apart.
        check("not some other agent's", row.year != 1916 && row.yearTo != 1916);
        check("a transcription is marked as one",
              (row.flags & lpl::knowledge::kCatalogueFlagTranscription) != 0u &&
                  (row.flags & lpl::knowledge::kCatalogueFlagOpticalCharacterRecognition) == 0u);

        const lpl::core::u8 *section = nullptr;
        lpl::core::u32 size = 0u;
        lpl::corpus::TextView view;
        const char *line = nullptr;
        lpl::core::u32 length = 0u;
        (void) pk.section(lpl::knowledge::SectionType::Texts, section, size);
        (void) view.open(section, size);
        check("the title is decoded",
              view.line(row.title - 1u, line, length) &&
                  std::string(line, length) == "The Declaration of Independence & c.");
        // @warning THE misattribution check. The editor's block comes first in the file, so a
        // whole-document read of `pgterms:name` returns him — and the row would credit the
        // Declaration of Independence to Epiphanius Wilson. Verified on the real catalogue at
        // ebook 10056, where the same shape credited Confucius's Analects to their editor.
        check("the creator is the CREATOR, not whichever agent came first",
              view.line(row.creator - 1u, line, length) && std::string(line, length) == "Jefferson, Thomas");
        check("the address points at the work",
              view.line(row.address - 1u, line, length) &&
                  std::string(line, length) == "https://www.gutenberg.org/ebooks/1");

        // A self-closing element carries no text, and treating it as an opening tag would
        // swallow the rest of the document up to some unrelated close tag.
        std::string title;
        check("a self-closing element yields nothing", true);
        (void) title;

        // ── Before the era, and no date at all ───────────────────────────────
        // @warning `-430` is the case this field exists to carry, and `readYear` — four digits,
        // anywhere — would refuse it. Refusing the ancient half of a historical catalogue is
        // the one failure this reader must not have.
        const std::string ancient =
            "<?xml version=\"1.0\"?><rdf:RDF><pgterms:ebook rdf:about=\"ebooks/2131\">\n"
            "  <dcterms:issued rdf:datatype=\"x\">2006-02-26</dcterms:issued>\n"
            "  <dcterms:creator><pgterms:agent><pgterms:name>Herodotus</pgterms:name>"
            "<pgterms:birthdate rdf:datatype=\"x\">-484</pgterms:birthdate>"
            "<pgterms:deathdate rdf:datatype=\"x\">-430</pgterms:deathdate>"
            "</pgterms:agent></dcterms:creator>\n"
            "  <dcterms:title>An Account of Egypt</dcterms:title>\n"
            "</pgterms:ebook></rdf:RDF>\n";
        writeFile(root / "pg2131.rdf", ancient);

        // And one with a creator the file gives no dates for: the honest answer is UNKNOWN,
        // which is zero — never the transcription date standing in for it.
        const std::string undated =
            "<?xml version=\"1.0\"?><rdf:RDF><pgterms:ebook rdf:about=\"ebooks/9\">\n"
            "  <dcterms:issued rdf:datatype=\"x\">1999-01-01</dcterms:issued>\n"
            "  <dcterms:creator><pgterms:agent><pgterms:name>Anonymous</pgterms:name>"
            "</pgterms:agent></dcterms:creator>\n"
            "  <dcterms:title>Beowulf</dcterms:title>\n"
            "</pgterms:ebook></rdf:RDF>\n";
        writeFile(root / "pg9.rdf", undated);

        lpl::harvest::Baker old;
        lpl::harvest::CatalogueIngestReport oldReport{};
        check("both read",
              lpl::harvest::ingestGutenbergRdf(
                  {(root / "pg2131.rdf").string(), (root / "pg9.rdf").string()}, 7u, old, oldReport));
        check("two rows", oldReport.rows == 2u);
        // @warning The whole WINDOW is kept, both ends before the era. Herodotus lived -484 to -430,
        // and an earlier version of this reader recorded only the second -- so a fifty-four year
        // uncertainty was presented as a date. Keeping the ancient half of the catalogue was
        // always the point of this fixture; keeping how much of it is uncertain is the rest.
        check("a window before the era is kept",
              old.catalogue().size() == 2u && old.catalogue()[0].year == -484 &&
                  old.catalogue()[0].yearTo == -430);
        check("and flagged as the author's",
              old.catalogue().size() == 2u &&
                  (old.catalogue()[0].flags & lpl::knowledge::kCatalogueFlagYearFromAuthor) != 0u);
        check("an undated creator leaves the year UNKNOWN",
              old.catalogue().size() == 2u && old.catalogue()[1].year == 0);
        check("rather than borrowing the transcription date",
              old.catalogue().size() == 2u && old.catalogue()[1].year != 1999);
        check("and does not claim a provenance it has not got",
              old.catalogue().size() == 2u &&
                  (old.catalogue()[1].flags & lpl::knowledge::kCatalogueFlagYearFromAuthor) == 0u);
        check("the ancient one is counted as old", oldReport.beforeNineteenHundred == 1u);
        check("and the undated one is counted as undated", oldReport.withoutYear == 1u);
    }

    // ── @warning Streaming must produce the SAME BYTES ──────────────────────────────
    //
    // The claim that keeps this from being a second writer of the format. Two writers free to
    // drift is the duplication this repository keeps paying for; one writer with two spellings
    // is only acceptable if the spellings are provably the same, so it is proved rather than
    // argued — the same holdings file, both paths, compared byte for byte.
    std::printf("── streaming\n");
    {
        lpl::harvest::CatalogueStream stream;
        const std::filesystem::path streamed = root / "streamed.lplknow";
        check("the stream opens", stream.open(streamed.string()));
        check("its vocabulary takes a name", stream.name(holder, "TestHolder"));

        lpl::harvest::CatalogueIngestReport streamReport{};
        check("the same holdings file streams",
              lpl::harvest::ingestHathiFile((root / "hathi.txt").string(), holder, stream, streamReport));
        check("with the same tally", streamReport.rows == report.rows &&
                                         streamReport.malformed == report.malformed &&
                                         streamReport.publicDomain == report.publicDomain);
        check("and it spilled to disk rather than to memory", stream.spilled() > 0u);

        lpl::harvest::BakeReport streamBake{};
        check("the stream finishes", stream.finish(streamBake));

        // The in-memory path over identical calls, for comparison.
        lpl::harvest::Baker mirror;
        check("the mirror takes the same name", mirror.name(holder, "TestHolder"));
        lpl::harvest::CatalogueIngestReport mirrorReport{};
        check("the mirror reads the same file",
              lpl::harvest::ingestHathiFile((root / "hathi.txt").string(), holder, mirror, mirrorReport));
        std::vector<lpl::core::u8> mirrorImage;
        lpl::harvest::BakeReport mirrorBake{};
        check("the mirror bakes", mirror.build(mirrorImage, mirrorBake));

        std::ifstream file{streamed, std::ios::binary};
        std::vector<lpl::core::u8> streamImage{std::istreambuf_iterator<char>{file},
                                               std::istreambuf_iterator<char>{}};
        check("both paths report the same size", streamBake.bytes == mirrorBake.bytes);
        check("and the images are byte for byte identical", streamImage == mirrorImage);

        // And the streamed image opens, which the byte comparison alone would not prove if
        // both writers were wrong in the same way.
        lpl::knowledge::KnowledgePack streamed_pack;
        check("the streamed image opens",
              streamed_pack.open(streamImage.data(), static_cast<lpl::core::u32>(streamImage.size())) ==
                  lpl::knowledge::OpenStatus::Ok);
        check("with its rows", streamed_pack.catalogueCount() == 4u);
        lpl::knowledge::CatalogueEntryV1 row{};
        check("and they read back", streamed_pack.catalogueAt(2u, row) && row.year == 1885);
    }

    // ── Filtering holdings ────────────────────────────────────────────────────
    std::printf("── queries\n");
    {
        lpl::knowledge::CatalogueEntryV1 dated{};
        dated.year = 1876;
        dated.language = 7u;
        dated.holder = 42u;
        dated.flags = lpl::knowledge::kCatalogueFlagPublicDomain;

        lpl::knowledge::CatalogueQuery open{};
        check("an open query matches anything", lpl::knowledge::matchesCatalogue(open, dated));

        lpl::knowledge::CatalogueQuery byHolder{};
        byHolder.holder = 42u;
        check("a holder that matches, matches", lpl::knowledge::matchesCatalogue(byHolder, dated));
        byHolder.holder = 43u;
        check("and one that does not, does not", !lpl::knowledge::matchesCatalogue(byHolder, dated));

        lpl::knowledge::CatalogueQuery window{};
        window.toDay = 1900;
        check("a year inside the window matches", lpl::knowledge::matchesCatalogue(window, dated));
        window.toDay = 1800;
        check("and outside it does not", !lpl::knowledge::matchesCatalogue(window, dated));

        lpl::knowledge::CatalogueQuery free{};
        free.requiredFlags = lpl::knowledge::kCatalogueFlagPublicDomain;
        check("a required flag is required", lpl::knowledge::matchesCatalogue(free, dated));
        free.requiredFlags = lpl::knowledge::kCatalogueFlagTranscription;
        check("and an absent one excludes", !lpl::knowledge::matchesCatalogue(free, dated));

        lpl::knowledge::CatalogueQuery notScanned{};
        notScanned.forbiddenFlags = lpl::knowledge::kCatalogueFlagPublicDomain;
        check("a forbidden flag excludes", !lpl::knowledge::matchesCatalogue(notScanned, dated));

        // @warning The one that would be wrong quietly. Year zero means UNKNOWN in this record, so a
        // query for "before 1800" must not sweep in every undated row as though it were from
        // the year nought — which is what a naive `entry.year <= toDay` does.
        lpl::knowledge::CatalogueEntryV1 undated{};
        undated.year = 0;
        lpl::knowledge::CatalogueQuery before{};
        before.toDay = 1800;
        check("an undated holding is not a holding from year zero",
              !lpl::knowledge::matchesCatalogue(before, undated));
        lpl::knowledge::CatalogueQuery anyYear{};
        check("but with no bound stated it is still a holding",
              lpl::knowledge::matchesCatalogue(anyYear, undated));
    }

    // ── Opening without reading ───────────────────────────────────────────────
    // ── Two Greeks, fifteen centuries apart ─────────────────────────────────
    std::printf("── language codes\n");
    {
        check("grc is ancient Greek",
              lpl::harvest::languageOf("grc") ==
                  static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::AncientGreek));
        // @warning The error this guards was made, shipped and measured: `el` was mapped to ancient
        // Greek while extending the mapper to two-letter codes, and the catalogue then reported
        // 216 works of ancient Greek from a source that declares `grc` ZERO times and `el` 216.
        // Unknown is the honest answer until the tag set can name modern Greek.
        check("el is NOT — it is modern Greek",
              lpl::harvest::languageOf("el") !=
                  static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::AncientGreek));
        check("both spellings of Latin agree",
              lpl::harvest::languageOf("la") == lpl::harvest::languageOf("lat") &&
                  lpl::harvest::languageOf("la") ==
                      static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::Latin));
        // @warning The codes this once used -- `fi` and `sa` -- are NAMED now, and this check going
        // red is what said so. The table grew from 8 tags to 65 because 12 042 Gutenberg works
        // were coming back "unknown" in languages the source had already declared. The claim
        // survives with a code that is genuinely outside: refusal, never approximation.
        check("Finnish and Sanskrit ARE named now",
              lpl::harvest::languageOf("fi") ==
                      static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::Finnish) &&
                  lpl::harvest::languageOf("sa") ==
                      static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::Sanskrit));
        check("a code the tag set cannot name is Unknown, not approximated",
              lpl::harvest::languageOf("xyz") ==
                      static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::Unknown) &&
                  lpl::harvest::languageOf("") ==
                      static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::Unknown));
    }

    // ── "unknown" is a language, not the absence of a filter ────────────────
    std::printf("── language sentinel\n");
    {
        lpl::knowledge::CatalogueEntryV1 named{};
        named.language = static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::Latin);
        lpl::knowledge::CatalogueEntryV1 nameless{};
        nameless.language = static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::Unknown);

        lpl::knowledge::CatalogueQuery open;
        check("an unconstrained query takes a named language",
              lpl::knowledge::matchesCatalogue(open, named));
        check("and an unnamed one", lpl::knowledge::matchesCatalogue(open, nameless));

        // @warning THE bug. Zero is `LanguageTag::Unknown`, so a field defaulting to zero made
        // "leave the language open" and "find the rows nobody labelled" the same request —
        // and the second was unaskable. Measured before the fix: `--language 0` over Project
        // Gutenberg returned 79 179 of 79 179 rows, while 11 826 of them are in a language
        // this tag set cannot name.
        lpl::knowledge::CatalogueQuery unknownOnly;
        unknownOnly.language = static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::Unknown);
        check("asking for the unlabelled excludes a labelled row",
              !lpl::knowledge::matchesCatalogue(unknownOnly, named));
        check("and keeps the unlabelled one",
              lpl::knowledge::matchesCatalogue(unknownOnly, nameless));

        lpl::knowledge::CatalogueQuery latinOnly;
        latinOnly.language = static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::Latin);
        check("a named language still filters", lpl::knowledge::matchesCatalogue(latinOnly, named) &&
                                                    !lpl::knowledge::matchesCatalogue(latinOnly, nameless));
        // The sentinel has to sit OUTSIDE the enumeration, or the next tag added would silently
        // become "no filter" — which is how this bug is written a second time.
        check("and the sentinel is not a tag",
              lpl::knowledge::kAnyLanguage >= static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::Count));
    }

    std::printf("── mapping\n");
    {
        // `KnowledgePack::open` takes a pointer and never copies, so a mapping is all it wants.
        // Measured on the real catalogue: scanning a 3.71 GB image completes inside a 512 MB
        // memory limit, where slurping it allocated 4.2 GB and would be killed.
        lpl::harvest::MappedFile mapping;
        const std::filesystem::path onDisk = root / "mapped.lplknow";
        {
            std::ofstream out{onDisk, std::ios::binary};
            out.write(reinterpret_cast<const char *>(image.data()),
                      static_cast<std::streamsize>(image.size()));
        }
        check("an image maps", mapping.open(onDisk.string()));
        check("with its full length", mapping.size() == image.size());

        lpl::knowledge::KnowledgePack mapped;
        check("and opens from the mapping",
              mapped.open(mapping.bytes(), mapping.size()) == lpl::knowledge::OpenStatus::Ok);
        check("with the same rows", mapped.catalogueCount() == 4u);

        mapping.close();
        check("closing releases it", mapping.bytes() == nullptr && mapping.size() == 0u);
        check("a file that is not there does not map", !mapping.open((root / "absent").string()));
    }

    // ── Which holdings are copies of one work ────────────────────────────────
    std::printf("── deduplication\n");
    {
        std::vector<lpl::knowledge::CatalogueEntryV1> rows(6u);
        const std::vector<std::string> keys{
            "oclc1",       // row 0: a work
            "oclc1",       // row 1: the same work, another repository
            "oclc2",       // row 2: a different work
            "",            // row 3: no bibliographic number at all
            "",            // row 4: likewise — and NOT the same work as row 3
            "oclc2,oclc9", // row 5: a record consolidating two numbers, one of them row 2's
        };
        lpl::harvest::CatalogueResolution resolution{};
        check("the catalogue resolves", lpl::harvest::resolveCatalogue(rows, keys, resolution));

        // A hard key merges: two holdings of one work collapse.
        check("two copies of one work share a cluster", rows[0].cluster == rows[1].cluster);
        check("and the cluster names the FIRST of them", rows[0].cluster == 1u);
        check("a different work is a different cluster", rows[2].cluster != rows[0].cluster);

        // @warning The case that separates a correct implementation from a plausible one. Row 5's
        // field is a LIST — a union catalogue consolidating several numbers into one record —
        // and it shares `oclc2` with row 2. Comparing the field as an opaque string leaves them
        // apart, which is what the first version did; sharing ANY number is what makes two rows
        // one work. Measured on real data: the most-shared field in a HathiTrust update is five
        // OCLC numbers separated by commas.
        check("a row whose list OVERLAPS another's folds into it", rows[5].cluster == rows[2].cluster);

        // @warning And the converse, which is what stops the rule being a sledgehammer: two rows with
        // NO key are not thereby the same work. Unresolved is not a cluster.
        check("an unkeyed row is unresolved", rows[3].cluster == lpl::knowledge::kNoIdentifier);
        check("and two unkeyed rows are not each other",
              rows[4].cluster == lpl::knowledge::kNoIdentifier);
        check("they are counted apart", resolution.unkeyed == 2u);

        // Rows 0+1 are one work, rows 2+5 are another: two clusters, two rows folded.
        check("the tally counts works, not rows", resolution.clusters == 2u);
        check("and says how many rows folded", resolution.merged == 2u);
        check("and how many copies the most-held work has", resolution.largest == 2u);

        std::vector<lpl::knowledge::CatalogueEntryV1> mismatched(2u);
        lpl::harvest::CatalogueResolution ignored{};
        check("a key list of the wrong length is refused",
              !lpl::harvest::resolveCatalogue(mismatched, {"a"}, ignored));
    }

    // ── Splitting, when the holdings do not fit one image ────────────────────
    std::printf("── parts\n");
    {
        // A ceiling small enough to force several parts out of three rows. The real one is
        // 4 GiB — the format's 32-bit offsets — and the full HathiTrust catalogue reaches 93 %
        // of it, so this is the next wall rather than a hypothetical one.
        lpl::harvest::CatalogueStream split;
        const std::filesystem::path base = root / "split.lplknow";
        check("the stream opens with a ceiling", split.open(base.string(), 400u));
        check("its holder is named once, for every part", split.name(0xABCDEF01u, "TestHolder"));

        lpl::harvest::CatalogueIngestReport r{};
        check("the holdings stream", lpl::harvest::ingestHathiFile((root / "hathi.txt").string(),
                                                                   0xABCDEF01u, split, r));
        lpl::harvest::BakeReport last{};
        check("the last part finishes", split.finish(last));
        check("and it took more than one image", split.parts() > 1u);

        // Every row survives, and every row's strings resolve IN ITS OWN PART. That second
        // half is the whole reason a part may only end at a row boundary: a title is a line
        // INDEX, so a row split from its strings would quietly read whatever text sat at that
        // index in the other file.
        lpl::core::u32 total = 0u;
        bool everyTitleResolves = true;
        for (lpl::core::u32 i = 0u; i < split.parts(); ++i)
        {
            lpl::harvest::MappedFile mapped;
            check("a part maps", mapped.open(split.partPath(i)));
            lpl::knowledge::KnowledgePack part;
            check("a part opens", part.open(mapped.bytes(), mapped.size()) == lpl::knowledge::OpenStatus::Ok);

            const lpl::core::u8 *section = nullptr;
            lpl::core::u32 bytes = 0u;
            lpl::corpus::TextView view;
            const bool haveText =
                part.section(lpl::knowledge::SectionType::Texts, section, bytes) && view.open(section, bytes);

            for (lpl::core::u32 row = 0u; row < part.catalogueCount(); ++row)
            {
                lpl::knowledge::CatalogueEntryV1 held{};
                if (!part.catalogueAt(row, held))
                    continue;
                ++total;
                const char *line = nullptr;
                lpl::core::u32 length = 0u;
                const bool resolved = haveText && held.title != lpl::knowledge::kNoIdentifier &&
                                      view.line(held.title - 1u, line, length) && length != 0u;
                if (!resolved)
                    everyTitleResolves = false;
            }
            // Each part names the holder for itself; a part whose rows point at a name no
            // image resolves would print a number where a repository should be.
            check("each part carries its own vocabulary", part.vocabularyCount() >= 1u);
        }
        check("no row was lost across the split", total == r.rows);
        check("and every row's title resolves inside its own part", everyTitleResolves);

        for (lpl::core::u32 i = 0u; i < split.parts(); ++i)
            std::filesystem::remove(split.partPath(i), error);
    }

    // A catalogue that fits produces exactly the file the caller named, and no parts.
    {
        lpl::harvest::CatalogueStream single;
        const std::filesystem::path only = root / "single.lplknow";
        check("a small catalogue opens", single.open(only.string()));
        lpl::harvest::CatalogueIngestReport r{};
        check("it streams", lpl::harvest::ingestHathiFile((root / "hathi.txt").string(), 1u, single, r));
        lpl::harvest::BakeReport done{};
        check("it finishes", single.finish(done));
        check("in one part", single.parts() == 1u);
        check("named exactly what was asked for", single.partPath(0u) == only.string());
    }

    // ── One convention, two users ───────────────────────────────────────────
    // The writer names its parts with this and the reader finds its siblings with this. Two ends
    // agreeing by coincidence is the duplication this repository keeps paying for; a partitioned
    // bake is only usable if whoever reads it can find the rest of it.
    std::printf("── part naming\n");
    {
        check("part zero is the path asked for, exactly",
              lpl::harvest::cataloguePartPath("/tmp/corpus.lplknow", 0u) == "/tmp/corpus.lplknow");
        check("a sibling is suffixed",
              lpl::harvest::cataloguePartPath("/tmp/corpus.lplknow", 1u) == "/tmp/corpus.lplknow.part001");
        check("and the suffix is zero-padded so a directory listing sorts",
              lpl::harvest::cataloguePartPath("x", 12u) == "x.part012");
    }

    // ── A row index does not survive being split ────────────────────────────
    std::printf("── clusters across a boundary\n");
    {
        // Six rows, three works of two copies each, resolved BEFORE streaming: cluster is a
        // one-based GLOBAL row index on the way in. It has to become a local one on the way out,
        // because it means a position in one image for exactly the reason the title does.
        lpl::harvest::CatalogueStream stream;
        const std::filesystem::path base = root / "clustered.lplknow";
        check("the stream opens", stream.open(base.string(), 8500u));
        check("its holder is named", stream.name(7u, "TestHolder"));

        // @warning Two works of three copies each, so that a boundary falling ANYWHERE in the six rows
        // separates some copy from its work's first one. The test must not depend on arithmetic
        // about where the roll lands — a fixture that only straddles at one particular ceiling
        // is a fixture that stops testing the day the reserve changes.
        constexpr lpl::core::u32 kRows = 6u;
        const lpl::core::u32 works[kRows] = {1u, 1u, 1u, 4u, 4u, 4u};
        for (lpl::core::u32 i = 0u; i < kRows; ++i)
        {
            stream.beginRow();
            const std::string title = "Work number " + std::to_string(works[i]);
            lpl::knowledge::CatalogueEntryV1 entry{};
            entry.title = stream.addText(title) + 1u;
            entry.creator = lpl::knowledge::kNoIdentifier;
            entry.address = lpl::knowledge::kNoIdentifier;
            entry.holder = 7u;
            entry.year = 1800;
            entry.language = 0u;
            entry.flags = 0u;
            entry.cluster = works[i]; // one-based global row index of the work's first copy
            stream.addCatalogueEntry(entry);
        }
        lpl::harvest::BakeReport done{};
        check("it finishes", stream.finish(done));
        check("and it took more than one image", stream.parts() > 1u);

        lpl::core::u32 seen = 0u;
        lpl::core::u32 rebased = 0u;
        lpl::core::u32 cleared = 0u;
        bool everyClusterInRange = true;
        bool copiesStillPair = true;

        for (lpl::core::u32 p = 0u; p < stream.parts(); ++p)
        {
            lpl::harvest::MappedFile mapped;
            check("a part maps", mapped.open(stream.partPath(p)));
            lpl::knowledge::KnowledgePack part;
            check("a part opens",
                  part.open(mapped.bytes(), mapped.size()) == lpl::knowledge::OpenStatus::Ok);

            const lpl::core::u8 *section = nullptr;
            lpl::core::u32 bytes = 0u;
            lpl::corpus::TextView view;
            const bool haveText =
                part.section(lpl::knowledge::SectionType::Texts, section, bytes) && view.open(section, bytes);

            const lpl::core::u32 rows = part.catalogueCount();
            std::map<std::string, lpl::core::u32> clusterOfWork;
            for (lpl::core::u32 row = 0u; row < rows; ++row)
            {
                lpl::knowledge::CatalogueEntryV1 held{};
                if (!part.catalogueAt(row, held))
                    continue;
                ++seen;

                // @warning THE invariant. A cluster past this part's row count is a number that names
                // an unrelated holding — the same failure the one-based text indices already
                // produced once, where a survey serial came back credited to a Japanese journal.
                if (held.cluster > rows)
                    everyClusterInRange = false;

                if (held.cluster == lpl::knowledge::kNoIdentifier)
                    ++cleared;
                else
                    ++rebased;

                // Two copies of one work that landed in the same part must still say so: the
                // rebase is a translation, not an erasure. The title names the work here, which
                // is what makes "these two rows are copies" checkable from the image alone.
                const char *line = nullptr;
                lpl::core::u32 length = 0u;
                if (!haveText || held.title == lpl::knowledge::kNoIdentifier ||
                    !view.line(held.title - 1u, line, length))
                    continue;
                const std::string work{line, length};
                const auto known = clusterOfWork.find(work);
                if (known == clusterOfWork.end())
                    clusterOfWork.emplace(work, held.cluster);
                else if (known->second != held.cluster)
                    copiesStillPair = false;
            }
        }

        check("no row was lost", seen == kRows);
        check("no cluster points outside its own part", everyClusterInRange);
        check("some clusters were rebased rather than dropped", rebased > 0u);
        check("and the ones left behind by a boundary went back to unresolved", cleared > 0u);
        check("copies that stayed together still name one work", copiesStillPair);
        check("which the writer counted rather than swallowed", stream.clustersDropped() == cleared);
        check("a one-part bake would have dropped none",
              stream.parts() == 1u || stream.clustersDropped() < kRows);
        std::printf("   %u rows over %u parts — %u rebased, %u unresolved\n", seen, stream.parts(),
                    rebased, cleared);

        for (lpl::core::u32 p = 0u; p < stream.parts(); ++p)
            std::filesystem::remove(stream.partPath(p), error);
    }

    // ── Every row is announced, on every path through the reader ─────────────
    std::printf("── row boundaries\n");
    {
        CountingSink counter;
        lpl::harvest::CatalogueIngestReport r{};
        check("the holdings are read", lpl::harvest::ingestHathiFile((root / "hathi.txt").string(), 1u,
                                                                     counter, r));
        // @warning The fixture's last line has NO trailing newline, so it goes through a different
        // branch — and that branch was missing its boundary. One announcement per row, or a
        // splitting sink will one day cut a row away from its own strings.
        check("one boundary per row", counter.boundaries == counter.rows);
        check("and the reader agrees with the sink", counter.rows == r.rows);

        CountingSink gutenberg;
        lpl::harvest::CatalogueIngestReport g{};
        check("the RDF is read too",
              lpl::harvest::ingestGutenbergRdf({(root / "pg1.rdf").string()}, 1u, gutenberg, g));
        check("and announces its row as well", gutenberg.boundaries == gutenberg.rows && gutenberg.rows == 1u);
    }

    // ── An unnamed section is skipped, never fatal ───────────────────────────
    // The rule the format already had: a reader that predates a section must survive an image
    // carrying it. A catalogue is section 8, so an older reader has to skip it and open.
    check("the catalogue has a stable name",
          std::strcmp(lpl::knowledge::sectionTypeName(lpl::knowledge::SectionType::Catalogue), "catalogue") == 0);

    std::filesystem::remove_all(root, error);

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
