/**
 * @file test_gazetteer.cpp
 * @brief The ancient-places reader, and the four ways a CSV silently lies.
 *
 * @warning Written because the reader had been verified against 42 400 real places and guarded against
 * nothing. That is exactly the debt that made five wrong hypotheses possible elsewhere in this
 * repository the same day: a component with no test is the one every future bug gets blamed on.
 *
 * The fixtures here are small and deliberately nasty. Real Pleiades rows carry commas inside
 * quoted titles, whole GeoJSON documents in a column, newlines inside descriptions, and dates two
 * millennia before the era — each of which breaks a different naive reader, and each of which
 * broke mine at some point.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Baker.hpp>
#include <lpl/harvest/Pleiades.hpp>
#include <lpl/knowledge/PlaceResolver.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

/**
 * @brief Writes a file.
 *
 * @param path  Where.
 * @param bytes What.
 */
void writeFile(const std::filesystem::path &path, const std::string &bytes)
{
    std::ofstream out{path, std::ios::binary};
    out << bytes;
}

/**
 * @brief Finds a place by its identifier among what a baker collected.
 *
 * @param baker Where the rows are.
 * @param place The identifier.
 * @param out   Receives it.
 * @return false when no row carries it.
 */
[[nodiscard]] bool find(const lpl::harvest::Baker &baker, lpl::core::u32 place,
                        lpl::knowledge::GazetteerEntryV1 &out)
{
    for (const lpl::knowledge::GazetteerEntryV1 &entry : baker.gazetteer())
    {
        if (entry.place != place)
            continue;
        out = entry;
        return true;
    }
    return false;
}

} // namespace

int main()
{
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lpl-gazetteer-test";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    std::printf("── splitting a record\n");
    {
        std::vector<std::string> fields;

        // @warning A comma inside quotes is CONTENT. Splitting on commas returns a different number of
        // fields for some rows than for others, so every column after the first quoted one is
        // read from the wrong place — on exactly the rows whose data is richest.
        lpl::harvest::splitCsvRecord(R"(1,"Becker, J., T. Elliott",settlement)", fields);
        check("a quoted comma does not split a field", fields.size() == 3u);
        check("and the quotes are removed", fields.size() == 3u && fields[1] == "Becker, J., T. Elliott");

        // A doubled quote inside a quoted field is an escaped quote, not a close. Getting this
        // wrong flips the quoting state and everything after it lands one column off.
        lpl::harvest::splitCsvRecord(R"(a,"he said ""no""",b)", fields);
        check("a doubled quote is one quote", fields.size() == 3u && fields[1] == "he said \"no\"");

        // @warning A trailing empty field IS a field. Dropping it shortens the row, so every
        // index-based read after it is wrong.
        lpl::harvest::splitCsvRecord("a,b,", fields);
        check("a trailing empty field is a field", fields.size() == 3u && fields[2].empty());

        lpl::harvest::splitCsvRecord("a,,c", fields);
        check("and so is an empty one in the middle", fields.size() == 3u && fields[1].empty());

        // The `extent` column is a whole GeoJSON document, braces and commas and all.
        lpl::harvest::splitCsvRecord(R"(1,"{""type"": ""Point"", ""coordinates"": [13.4, 42.0]}",x)",
                                     fields);
        check("a JSON document survives as one field", fields.size() == 3u);
    }

    std::printf("── a record is not a line\n");
    {
        // @warning Pleiades descriptions contain newlines inside quoted fields. A reader that stops at
        // every '\n' cuts records in half and reports twice as many, all malformed.
        const std::string body = "a,\"two\nlines\",c\nsecond,record,here\n";
        std::size_t cursor = 0u;
        std::string record;
        check("the first record is read whole", lpl::harvest::nextCsvRecord(body, cursor, record));
        check("newline and all", record == "a,\"two\nlines\",c");
        check("and the second follows", lpl::harvest::nextCsvRecord(body, cursor, record) &&
                                            record == "second,record,here");
        check("then the input ends", !lpl::harvest::nextCsvRecord(body, cursor, record));
    }

    std::printf("── degrees, exactly\n");
    {
        lpl::core::i32 raw = 0;
        check("a whole degree", lpl::harvest::degreesToRaw("37", raw) && raw == 37 * 65536);
        check("a negative one", lpl::harvest::degreesToRaw("-118", raw) && raw == -118 * 65536);
        check("a half", lpl::harvest::degreesToRaw("0.5", raw) && raw == 32768);
        // @warning Parsed with integers throughout, never through a double: the same decimal string must
        // land on the same word on every target, and a coordinate that rounds differently is a
        // body that walks somewhere else.
        lpl::core::i32 again = 0;
        check("the same text gives the same word twice",
              lpl::harvest::degreesToRaw("27.4221505", raw) &&
                  lpl::harvest::degreesToRaw("27.4221505", again) && raw == again);
        // @warning Computed INDEPENDENTLY (exact decimal, 27.4221505 x 65536 = 1797138.055), never read
        // off the parser: an expected value taken from the thing under test asserts only that the
        // code agrees with itself. My first hand-arithmetic here was wrong by 401 units and the
        // test caught it, which is the whole point of writing the number twice.
        check("and it is the right one", raw == 1797138);
        check("text that is not a number is refused", !lpl::harvest::degreesToRaw("", raw));
        check("and so is a word", !lpl::harvest::degreesToRaw("unlocated", raw));
        // @warning Beyond any longitude: refused rather than wrapped. A silently wrapped coordinate is
        // a place on the other side of the world.
        check("a value past any coordinate is refused", !lpl::harvest::degreesToRaw("999", raw));
    }

    std::printf("── reading a dump\n");
    {
        // Columns deliberately NOT in the order the real dump uses, because the reader must find
        // them by name: the order is the repository's business and has changed before, and a
        // reader pinned to indices reads a description as a latitude the day a column moves.
        std::string csv =
            "title,minDate,id,featureTypes,reprLong,locationPrecision,maxDate,reprLat,extent\n";
        csv += R"("Halicarnassus/Halikarnassos",-550,599636,"settlement, station",27.4221505,precise,2100,37.0404005,"{""type"": ""Point""}")" "\n";
        // @warning A date two millennia before the era. `readYear` wants four digits and would refuse it;
        // `degreesToRaw` is bounded at 400 and would ALSO refuse it — which is how Babylon came
        // back undated from a reader that reused the coordinate parser for years.
        csv += R"("Babylon",-2000,893951,"urban, settlement",44.4247,precise,1599,32.5372,"{}")" "\n";
        // Known from texts, never found on the ground. It must be KEPT, and must not receive a
        // position: giving it one turns "nobody knows where this was" into a map pin.
        csv += R"("Cimmeria",-700,991377,unlocated,,unlocated,300,,"{}")" "\n";
        writeFile(root / "places.csv", csv);

        lpl::harvest::Baker baker;
        lpl::harvest::GazetteerIngestReport report{};
        check("the dump is read", lpl::harvest::ingestPleiades((root / "places.csv").string(), baker, report));
        check("three places", report.places == 3u && report.malformed == 0u);
        check("two of them located", report.located == 2u);
        check("and one not, counted rather than dropped", report.unlocated == 1u);
        check("all three dated", report.dated == 3u);
        check("two are settlements", report.settlements == 2u);

        lpl::knowledge::GazetteerEntryV1 entry{};
        check("Halicarnassus is there", find(baker, 599636u, entry));
        check("with the coordinates the file states",
              entry.latRaw == 2427480 && entry.lonRaw == 1797138);
        check("its window", entry.minYear == -550 && entry.maxYear == 2100);
        check("located, precise and dated",
              (entry.flags & lpl::knowledge::kGazetteerFlagLocated) != 0u &&
                  (entry.flags & lpl::knowledge::kGazetteerFlagPrecise) != 0u &&
                  (entry.flags & lpl::knowledge::kGazetteerFlagDated) != 0u);
        check("and a settlement on a route",
              (entry.kinds & lpl::knowledge::kPlaceKindSettlement) != 0u &&
                  (entry.kinds & lpl::knowledge::kPlaceKindRoute) != 0u);

        // @warning THE year check. Two thousand years before the era is the half of history this whole
        // module exists for, and two different parsers in this file would both have refused it.
        check("Babylon is there", find(baker, 893951u, entry));
        check("and is attested from -2000", entry.minYear == -2000 && entry.maxYear == 1599);
        check("an urban place is also a settlement",
              (entry.kinds & lpl::knowledge::kPlaceKindUrban) != 0u &&
                  (entry.kinds & lpl::knowledge::kPlaceKindSettlement) != 0u);

        check("the unlocated place is kept", find(baker, 991377u, entry));
        check("without a position", (entry.flags & lpl::knowledge::kGazetteerFlagLocated) == 0u);
        check("and without inventing one", entry.latRaw == 0 && entry.lonRaw == 0);
        check("but still dated", (entry.flags & lpl::knowledge::kGazetteerFlagDated) != 0u);
    }

    std::printf("── what it refuses\n");
    {
        lpl::harvest::Baker baker;
        lpl::harvest::GazetteerIngestReport report{};
        check("a missing file is refused",
              !lpl::harvest::ingestPleiades((root / "absent.csv").string(), baker, report));

        // @warning Not a Pleiades dump: refused whole rather than half-read. A reader that accepted a
        // header it does not understand would bake rows built from whichever columns happened to
        // line up.
        writeFile(root / "other.csv", "name,value\nfoo,1\n");
        check("a file whose columns it does not know is refused",
              !lpl::harvest::ingestPleiades((root / "other.csv").string(), baker, report));
    }

    std::printf("── it reaches an image, and comes back\n");
    {
        std::string csv = "id,title,reprLat,reprLong,minDate,maxDate,featureTypes,locationPrecision\n";
        csv += "599636,Halicarnassus,37.0404005,27.4221505,-550,2100,settlement,precise\n";
        writeFile(root / "one.csv", csv);

        lpl::harvest::Baker baker;
        lpl::harvest::GazetteerIngestReport report{};
        check("it reads", lpl::harvest::ingestPleiades((root / "one.csv").string(), baker, report));

        std::vector<lpl::core::u8> image;
        lpl::harvest::BakeReport bake{};
        check("it bakes", baker.build(image, bake));
        check("and the bake counts its places", bake.gazetteer == 1u);

        lpl::knowledge::KnowledgePack pack;
        check("the image opens", pack.open(image.data(), static_cast<lpl::core::u32>(image.size())) ==
                                     lpl::knowledge::OpenStatus::Ok);
        check("carrying one place", pack.gazetteerCount() == 1u);

        lpl::knowledge::GazetteerEntryV1 held{};
        // @warning By identifier, never by name: ten distinct places in the real dump are called
        // "Alexandria", from Egypt to Afghanistan, and matching on a title puts the Library in
        // Kabul. This is the same rule `EntityResolution` states — a hard key merges, a score
        // only proposes.
        check("and it is found by its identifier", pack.placeById(599636u, held));
        check("with its coordinates intact through the wire",
              held.latRaw == 2427480 && held.lonRaw == 1797138);
        check("and its window", held.minYear == -550 && held.maxYear == 2100);
        check("a place the image does not hold is not invented", !pack.placeById(4242u, held));
    }

    std::filesystem::remove_all(root, error);

    std::printf("-- a walking body asks the corpus where a place is\n");
    {
        // @warning **The seam's first real implementation.** `history::IPlaceResolver` was declared so
        // a walk could ask where a place is without knowing what a gazetteer is, and until now its
        // only implementations were fixtures -- five places typed into a parity test. This
        // answers from a baked corpus.
        lpl::harvest::Baker baker;
        const lpl::core::u32 halicarnassus = 599636u;
        const lpl::core::u32 miletus = 599799u;
        const lpl::core::u32 cimmeria = 111111u;

        lpl::knowledge::GazetteerEntryV1 hali{};
        hali.place = halicarnassus;
        hali.latRaw = lpl::math::Fixed32::fromFloat(37.0404f).raw();
        hali.lonRaw = lpl::math::Fixed32::fromFloat(27.4222f).raw();
        hali.minYear = -550;
        hali.maxYear = 2100;
        hali.flags = lpl::knowledge::kGazetteerFlagLocated | lpl::knowledge::kGazetteerFlagDated;
        baker.addGazetteerEntry(hali);

        lpl::knowledge::GazetteerEntryV1 mil{};
        mil.place = miletus;
        mil.latRaw = lpl::math::Fixed32::fromFloat(37.5300f).raw();
        mil.lonRaw = lpl::math::Fixed32::fromFloat(27.2775f).raw();
        mil.flags = lpl::knowledge::kGazetteerFlagLocated;
        baker.addGazetteerEntry(mil);

        // Known from the texts, never found on the ground. Carried WITHOUT a position.
        lpl::knowledge::GazetteerEntryV1 lost{};
        lost.place = cimmeria;
        lost.flags = 0u;
        baker.addGazetteerEntry(lost);

        baker.addPlaceLink(halicarnassus, miletus);
        // A road to somewhere this corpus does not carry: stated, and unusable.
        baker.addPlaceLink(halicarnassus, 424242u);

        std::vector<lpl::core::u8> image;
        lpl::harvest::BakeReport report{};
        check("the gazetteer bakes", baker.build(image, report));

        lpl::knowledge::KnowledgePack pack;
        check("and reopens", pack.open(image.data(), image.size()) == lpl::knowledge::OpenStatus::Ok);

        lpl::knowledge::PackPlaceResolver resolver;
        resolver.bind(pack);
        check("the resolver sees every place", resolver.placeCount() == 3u);

        lpl::history::Place place{};
        check("a located place resolves", resolver.resolve(halicarnassus, place));
        check("and is located", place.located);
        // @warning The RAW words, verbatim. Converting through a float here would put a rounding
        // between the corpus and the walk, and two targets could then disagree about where a body
        // ends up -- which is the whole reason a coordinate travels as a Q16.16 word.
        check("with the coordinates the corpus stated, bit for bit",
              place.z.raw() == hali.latRaw && place.x.raw() == hali.lonRaw);
        check("and the window it was attested in", place.minYear == -550 && place.maxYear == 2100);

        // @warning An UNDATED place answers true for every year. Absence of a window is absence of
        // knowledge, not a claim that the place never existed.
        lpl::history::Place undated{};
        check("an undated place resolves", resolver.resolve(miletus, undated));
        check("with no window claimed", undated.minYear == 0 && undated.maxYear == 0);
        check("so it exists in every year", lpl::history::existsInYear(undated, -400) &&
                                                lpl::history::existsInYear(undated, 1900));

        // @warning Carried, not positioned. Inventing coordinates for a place nobody has found turns
        // "nobody knows where Cimmeria was" into somewhere a body walks to.
        lpl::history::Place unfound{};
        check("an unlocated place is still carried", resolver.resolve(cimmeria, unfound));
        check("and is NOT located", !unfound.located);

        check("a place the corpus does not carry resolves to nothing",
              !resolver.resolve(987654u, place));

        // The attested road, and only the usable half of it.
        lpl::core::u32 neighbours[8]{};
        const lpl::core::u32 count = resolver.linkedPlaces(halicarnassus, neighbours, 8u);
        check("the attested road is found", count == 1u && neighbours[0] == miletus);
        // @warning A link to a place this corpus lacks is skipped rather than returned: the walk
        // would ask for it a moment later, get nothing, and treat a stated road as a dead end --
        // which reads as the corpus being silent when it was specific.
        check("and a road to nowhere this corpus knows is not offered", count == 1u);
        // Stored both ways, because a road is walked both ways.
        const lpl::core::u32 back = resolver.linkedPlaces(miletus, neighbours, 8u);
        check("and it runs in the other direction too", back == 1u && neighbours[0] == halicarnassus);
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures,
                gChecks);
    return gFailures == 0 ? 0 : 1;
}
