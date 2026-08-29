/**
 * @file test_oaipmh.cpp
 * @brief What a harvester must get right, checked with the network unplugged.
 *
 * @warning Every response here is canned. That is the point rather than a compromise: the part of an
 * OAI-PMH client with bugs in it is the LOOP — the continuation rule, the retry rule, the
 * tombstone rule, the repeated-token guard — and each of those is a specific server behaviour
 * that cannot be summoned on demand from a live repository. A test that talks to arXiv exercises
 * whichever of them arXiv happens to do today, and goes red when the network is down.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Baker.hpp>
#include <lpl/harvest/Catalogue.hpp>
#include <lpl/harvest/OaiPmh.hpp>

#include <lpl/corpus/Language.hpp>

#include <cstdio>
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
 * @class ScriptedFetcher
 * @brief Answers with a prepared list of responses, and remembers what it was asked.
 */
class ScriptedFetcher final : public lpl::harvest::IHttpFetcher {
public:
    std::vector<lpl::harvest::HttpResponse> script;
    std::vector<std::string> requested;
    std::vector<int> slept;
    std::size_t next{0u};
    bool refuse{false};

    bool get(const std::string &url, lpl::harvest::HttpResponse &out) override
    {
        requested.push_back(url);
        if (refuse)
            return false;
        if (next >= script.size())
        {
            out = lpl::harvest::HttpResponse{200, "<OAI-PMH><ListRecords></ListRecords></OAI-PMH>", 0};
            return true;
        }
        out = script[next++];
        return true;
    }

    // @warning Recorded rather than performed. A sleep that actually slept would make this file take
    // minutes; a default that did nothing would let the retry rule pass here while the real
    // harvester hammered a server that had asked to be left alone.
    void sleepFor(int seconds) override { slept.push_back(seconds); }
};

/**
 * @brief One `<record>` in oai_dc.
 *
 * @param id       Repository identifier.
 * @param title    Title.
 * @param creator  Creator.
 * @param date     Date as written.
 * @param language Language code.
 * @return The XML.
 */
[[nodiscard]] std::string record(const char *id, const char *title, const char *creator,
                                 const char *date, const char *language)
{
    return std::string{"<record><header><identifier>"} + id +
           "</identifier><datestamp>2026-08-01</datestamp></header><metadata>"
           "<oai_dc:dc><dc:title>" +
           title + "</dc:title><dc:creator>" + creator + "</dc:creator><dc:date>" + date +
           "</dc:date><dc:language>" + language +
           "</dc:language><dc:identifier>https://example.org/" + id +
           "</dc:identifier></oai_dc:dc></metadata></record>";
}

/**
 * @brief Wraps records in a ListRecords response.
 *
 * @param body  The records.
 * @param token Resumption token, or nullptr for the last page.
 * @return The XML.
 */
[[nodiscard]] std::string page(const std::string &body, const char *token)
{
    std::string out = "<?xml version=\"1.0\"?><OAI-PMH xmlns=\"http://www.openarchives.org/OAI/2.0/\">"
                      "<responseDate>2026-08-14T00:00:00Z</responseDate><ListRecords>";
    out += body;
    if (token != nullptr)
        out += std::string{"<resumptionToken completeListSize=\"7\">"} + token + "</resumptionToken>";
    else
        out += "<resumptionToken completeListSize=\"7\"></resumptionToken>";
    out += "</ListRecords></OAI-PMH>";
    return out;
}

} // namespace

int main()
{
    std::printf("── requests\n");
    {
        lpl::harvest::OaiRequest request;
        request.endpoint = "https://export.arxiv.org/oai2";
        request.metadataPrefix = "oai_dc";
        request.from = "2026-08-01";
        request.set = "physics";

        const std::string first = lpl::harvest::oaiListRecordsUrl(request, "");
        check("a first request names the verb", first.find("verb=ListRecords") != std::string::npos);
        check("and the metadata format", first.find("metadataPrefix=oai_dc") != std::string::npos);
        check("and the window it wants", first.find("from=2026-08-01") != std::string::npos);
        check("and the set", first.find("set=physics") != std::string::npos);

        // @warning Rule 1, and the one mistake every naive harvester makes. A continuation that also
        // carries `metadataPrefix` gets `badArgument` from every conforming server — so the
        // harvest stops after one page and reports an empty repository rather than a protocol
        // error. Asserted in the NEGATIVE, because the bug is a parameter that should not be
        // there rather than one that is missing.
        const std::string more = lpl::harvest::oaiListRecordsUrl(request, "tok/1:2+3");
        check("a continuation carries the token", more.find("resumptionToken=") != std::string::npos);
        check("and NOT the metadata prefix", more.find("metadataPrefix") == std::string::npos);
        check("and NOT the window", more.find("from=") == std::string::npos);
        check("and NOT the set", more.find("set=") == std::string::npos);

        // A token is opaque and routinely holds `/`, `:` and `+`. Pasted raw it is a harvest
        // that stops at page two on some servers and not on others.
        check("the token is percent-encoded", more.find("tok%2F1%3A2%2B3") != std::string::npos);
        check("a safe value is left alone", lpl::harvest::urlEncode("oai_dc") == "oai_dc");
    }

    std::printf("── one response\n");
    {
        lpl::harvest::OaiPage parsed;
        const std::string xml = page(record("oai:x:1", "Histories", "Herodotus", "c1885", "grc") +
                                         record("oai:x:2", "De bello Gallico", "Caesar", "1832-04", "lat"),
                                     "tok1");
        check("the response parses", lpl::harvest::parseOaiPage(xml, parsed));
        check("both records are read", parsed.records.size() == 2u);
        check("with their titles", parsed.records.size() == 2u && parsed.records[0].title == "Histories");
        check("their creators", parsed.records.size() == 2u && parsed.records[1].creator == "Caesar");
        check("the header identifier, not the URL",
              parsed.records.size() == 2u && parsed.records[0].identifier == "oai:x:1");
        // dc:identifier repeats and is heterogeneous; the catalogue wants the one a reader
        // could follow, and the header's own identifier is what names the record.
        check("and the address a reader could follow",
              parsed.records.size() == 2u && parsed.records[0].address == "https://example.org/oai:x:1");
        check("the token comes back", parsed.resumptionToken == "tok1");
        check("so does the server's own total", parsed.completeListSize == 7u);

        // @warning An EMPTY token element is how a repository says "that was the last page". Reading it
        // as a missing token or as an error would make the last page of every harvest look like
        // a failure.
        lpl::harvest::OaiPage last;
        check("the last page parses", lpl::harvest::parseOaiPage(page(record("oai:x:3", "T", "C", "1900", "eng"), nullptr), last));
        check("and its empty token means the end", last.resumptionToken.empty());
        check("while still carrying its record", last.records.size() == 1u);

        check("something that is not OAI-PMH is refused",
              !lpl::harvest::parseOaiPage("<html><body>404</body></html>", parsed));
    }

    std::printf("── errors and tombstones\n");
    {
        lpl::harvest::OaiPage parsed;
        const std::string err = "<OAI-PMH><error code=\"badArgument\">unknown argument</error></OAI-PMH>";
        check("an error response parses", lpl::harvest::parseOaiPage(err, parsed));
        check("and names its code", parsed.errorCode == "badArgument");
        // @warning An error response carries no records. Reading it as an empty list would turn a
        // protocol mistake into "this repository holds nothing" — true-looking and wrong.
        check("and carries no records", parsed.records.empty());

        const std::string tomb = page("<record><header status=\"deleted\">"
                                      "<identifier>oai:x:9</identifier>"
                                      "<datestamp>2026-08-02</datestamp></header></record>",
                                      nullptr);
        check("a tombstone parses", lpl::harvest::parseOaiPage(tomb, parsed));
        check("and is marked deleted", parsed.records.size() == 1u && parsed.records[0].deleted);
    }

    std::printf("── the harvest loop\n");
    {
        ScriptedFetcher fetcher;
        fetcher.script = {
            {200, page(record("oai:x:1", "Histories", "Herodotus", "c1885", "grc"), "tok1"), 0},
            {200, page(record("oai:x:2", "De bello Gallico", "Caesar", "1832", "lat"), "tok2"), 0},
            {200,
             page(record("oai:x:3", "Annales", "Tacitus", "1900", "lat") +
                      "<record><header status=\"deleted\"><identifier>oai:x:4</identifier>"
                      "<datestamp>2026-08-09</datestamp></header></record>",
                  nullptr),
             0},
        };

        lpl::harvest::Baker baker;
        check("the holder is named", baker.name(42u, "TestRepository"));
        lpl::harvest::OaiRequest request;
        request.endpoint = "https://example.org/oai";

        lpl::harvest::OaiHarvestReport report{};
        check("the harvest completes", lpl::harvest::harvestOaiPmh(fetcher, request, baker, 42u, report));
        check("over three pages", report.pages == 3u);
        check("writing three rows", report.records == 3u);
        // @warning A withdrawn record ingested as a holding is a book in the catalogue that is not
        // there, and nothing downstream can tell it from a real one.
        check("and refusing the tombstone", report.deleted == 1u && baker.catalogue().size() == 3u);

        check("the first request asked for the format",
              fetcher.requested.size() == 3u && fetcher.requested[0].find("metadataPrefix") != std::string::npos);
        check("and the second asked only with the token",
              fetcher.requested.size() == 3u && fetcher.requested[1].find("metadataPrefix") == std::string::npos &&
                  fetcher.requested[1].find("resumptionToken=tok1") != std::string::npos);

        // The newest datestamp is what makes the NEXT run incremental — this is the whole of
        // what `Changefile` would otherwise have to invent.
        check("the newest datestamp is carried out", report.lastDatestamp == "2026-08-09");

        const std::vector<lpl::knowledge::CatalogueEntryV1> &rows = baker.catalogue();
        check("a year is read out of a written date", rows.size() == 3u && rows[0].year == 1885);
        check("and out of one with a month", rows.size() == 3u && rows[1].year == 1832);
        check("the language is mapped",
              rows.size() == 3u &&
                  rows[0].language == static_cast<lpl::core::u32>(lpl::corpus::LanguageTag::AncientGreek));
        // @warning dc:rights is free text. Claiming a rights flag from a sentence would be writing a
        // legal opinion into a data field, which is the rule the HathiTrust reader already keeps.
        check("and no rights are claimed from free text", rows.size() == 3u && rows[0].flags == 0u);
    }

    std::printf("── being told to come back later\n");
    {
        ScriptedFetcher fetcher;
        fetcher.script = {
            {503, "", 7},
            {503, "", 99999},
            {200, page(record("oai:x:1", "T", "C", "1900", "eng"), nullptr), 0},
        };
        lpl::harvest::Baker baker;
        lpl::harvest::OaiRequest request;
        request.endpoint = "https://example.org/oai";
        lpl::harvest::OaiHarvestReport report{};

        check("a throttled harvest still completes",
              lpl::harvest::harvestOaiPmh(fetcher, request, baker, 1u, report));
        check("having waited twice", report.retries == 2u && fetcher.slept.size() == 2u);
        check("for as long as it was asked", !fetcher.slept.empty() && fetcher.slept[0] == 7);
        // @warning Capped. A server answering `Retry-After: 99999` would otherwise park the process for
        // a day, which is a denial of service the client inflicts on itself.
        check("but never longer than the cap", fetcher.slept.size() == 2u && fetcher.slept[1] == 120);
        check("and the record arrived", report.records == 1u);
    }

    std::printf("── failure modes\n");
    {
        // @warning A server that hands back the token it was given makes the loop infinite while
        // looking, from outside, exactly like a very large repository.
        ScriptedFetcher fetcher;
        fetcher.script = {
            {200, page(record("oai:x:1", "T", "C", "1900", "eng"), "same"), 0},
            {200, page(record("oai:x:2", "T", "C", "1900", "eng"), "same"), 0},
            {200, page(record("oai:x:3", "T", "C", "1900", "eng"), "same"), 0},
        };
        lpl::harvest::Baker baker;
        lpl::harvest::OaiRequest request;
        request.endpoint = "https://example.org/oai";
        lpl::harvest::OaiHarvestReport report{};
        check("a repeated token stops the harvest",
              !lpl::harvest::harvestOaiPmh(fetcher, request, baker, 1u, report));
        check("and says so", report.failure.find("repeated") != std::string::npos);

        // `noRecordsMatch` is a repository saying "nothing changed in that window", which is the
        // NORMAL answer to an incremental harvest that found nothing — not a failure.
        ScriptedFetcher quiet;
        quiet.script = {{200, "<OAI-PMH><error code=\"noRecordsMatch\">nothing</error></OAI-PMH>", 0}};
        lpl::harvest::Baker empty;
        lpl::harvest::OaiHarvestReport quietReport{};
        check("an empty incremental window is not a failure",
              lpl::harvest::harvestOaiPmh(quiet, request, empty, 1u, quietReport));
        check("though it is reported", quietReport.errorCode == "noRecordsMatch");
        check("with nothing written", quietReport.records == 0u);

        ScriptedFetcher broken;
        broken.script = {{200, "<OAI-PMH><error code=\"badArgument\">bad</error></OAI-PMH>", 0}};
        lpl::harvest::Baker none;
        lpl::harvest::OaiHarvestReport brokenReport{};
        check("a real protocol error stops it",
              !lpl::harvest::harvestOaiPmh(broken, request, none, 1u, brokenReport));

        ScriptedFetcher offline;
        offline.refuse = true;
        lpl::harvest::Baker unused;
        lpl::harvest::OaiHarvestReport offlineReport{};
        check("and a request that cannot be made is a failure, not an empty repository",
              !lpl::harvest::harvestOaiPmh(offline, request, unused, 1u, offlineReport));

        // A page budget, for a caller sampling a repository rather than mirroring it.
        ScriptedFetcher long_;
        long_.script = {
            {200, page(record("oai:x:1", "T", "C", "1900", "eng"), "a"), 0},
            {200, page(record("oai:x:2", "T", "C", "1900", "eng"), "b"), 0},
            {200, page(record("oai:x:3", "T", "C", "1900", "eng"), "c"), 0},
        };
        lpl::harvest::Baker sample;
        lpl::harvest::OaiHarvestReport sampled{};
        check("a page budget is obeyed",
              lpl::harvest::harvestOaiPmh(long_, request, sample, 1u, sampled, 2u) && sampled.pages == 2u);
        check("and it leaves a token to resume from", !sampled.lastToken.empty());
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures,
                gChecks);
    return gFailures == 0 ? 0 : 1;
}
