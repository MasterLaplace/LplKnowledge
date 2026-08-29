/**
 * @file OaiPmh.hpp
 * @brief Harvesting repositories that speak OAI-PMH.
 *
 * The protocol every library, archive and preprint server already exposes, and the reason the
 * immense corpus is reachable at all: a repository publishes its catalogue in pages, each page
 * handing back a token for the next. Nothing is downloaded but metadata — which is the whole
 * thread this module pulls on, that a catalogue row is the GENERATOR of a text rather than the
 * text.
 *
 * @warning **The fetch is behind @ref IHttpFetcher, and that is not decoration.** The part of this
 * file with bugs in it is the LOOP — tokens, retries, when to stop — and a loop that can only be
 * exercised by talking to a live server is a loop nobody tests. The parser takes bytes and the
 * harvester takes a fetcher, so both run with the network unplugged; the socket lives in one
 * small translation unit, @ref CurlFetcher, which nothing but a real run touches.
 *
 * **Three rules keep a harvester from being blocked**, and each is a real failure rather than
 * etiquette:
 *  1. A continuation request carries the verb and the resumption token AND NOTHING ELSE. Adding
 *     `metadataPrefix` back is the classic mistake and every conforming server answers
 *     `badArgument` — the harvest stops one page in, looking like an empty repository.
 *  2. `503` means "later", with `Retry-After` saying how much later. Retrying immediately is how
 *     an address gets banned rather than throttled.
 *  3. Pages are taken as the server sizes them. The client does not get to ask for more.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_OAIPMH_HPP
#    define LPL_LPL_HARVEST_OAIPMH_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/harvest/Baker.hpp>

#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

/**
 * @struct HttpResponse
 * @brief What came back.
 */
struct HttpResponse {
    int status{0};             ///< HTTP status, or 0 when the request never completed.
    std::string body;          ///< The payload.
    int retryAfterSeconds{0};  ///< `Retry-After` when the server sent one; 0 otherwise.
};

/**
 * @class IHttpFetcher
 * @brief Where bytes come from.
 *
 * The seam, and the same shape and the same reason as `agent::IDecider` and
 * `harvest::IClaimReader`: the interesting half is the policy above it, and a policy that can
 * only be run against the live thing is a policy that never gets a test. A fake implementation
 * of this is how the resumption loop, the retry rule and the deleted-record rule are checked.
 */
class IHttpFetcher {
public:
    virtual ~IHttpFetcher() = default;

    /**
     * @brief Fetches a URL.
     *
     * @param url The address.
     * @param out Receives the response.
     * @return false when the request could not be made at all, which is different from a
     *         response that carries an error status.
     */
    [[nodiscard]] virtual bool get(const std::string &url, HttpResponse &out) = 0;

    /**
     * @brief Waits, because a server asked to be left alone.
     *
     * On the interface rather than inside the harvester so a test can run the retry path
     * without spending the seconds. @warning A default that does nothing would make every test of the
     * retry rule pass while the real harvester hammered the server.
     *
     * @param seconds How long.
     */
    virtual void sleepFor(int seconds) = 0;
};

/**
 * @class CurlFetcher
 * @brief The real one: libcurl.
 *
 * @warning **Not cpp-httplib, and the reason is measured rather than preferred.** LplAssistant already
 * links cpp-httplib, so reaching for it here looked obvious — but that build has no TLS backend,
 * and its own `WebFetch.cpp` says so in a comment: every remote or `https` request in this
 * project is already handed to curl, because httplib without TLS cannot make one and cannot
 * follow the redirect that a plain `http` host answers with. Every OAI endpoint worth harvesting
 * is `https`.
 *
 * @warning **The LIBRARY, not the binary**, and that changed after the first version shipped as a child
 * process. Three differences, none of them style: no process per request, where a harvest is
 * thousands of them; ONE reused connection to a repository instead of a fresh TCP and TLS
 * handshake per page, which is both faster and the polite way to walk a server that agreed to be
 * walked; and headers read from the response rather than reconstructed out of `--write-out`.
 * Running a shell also had to be avoided by hand, because a URL carries a resumption token the
 * REPOSITORY chose — with a library there is no shell in the picture at all.
 */
class CurlFetcher final : public IHttpFetcher {
public:
    /**
     * @brief Builds a fetcher.
     *
     * @param timeoutSeconds Wall-clock cap on one request.
     * @param userAgent      Sent as `User-Agent`. Repositories block anonymous harvesters, and
     *                       being identifiable is also simply the polite thing.
     */
    explicit CurlFetcher(int timeoutSeconds = 60,
                         std::string userAgent = "LplKnowledge/0.1 (+https://github.com/MasterLaplace)");

    ~CurlFetcher() override;

    CurlFetcher(const CurlFetcher &) = delete;
    CurlFetcher &operator=(const CurlFetcher &) = delete;

    /**
     * @brief Fetches a URL.
     *
     * @param url The address.
     * @param out Receives the response.
     * @return false when the transfer never completed — a name that did not resolve, a refused
     *         connection, a timeout. @warning Distinct from a response carrying an error STATUS: one
     *         means the address or the network is wrong, the other means the repository
     *         answered and said no, and a harvester treats them differently.
     */
    [[nodiscard]] bool get(const std::string &url, HttpResponse &out) override;

    /**
     * @brief Sleeps.
     *
     * @param seconds How long.
     */
    void sleepFor(int seconds) override;

private:
    int _timeoutSeconds;
    std::string _userAgent;
    /// The `CURL *`, held opaquely so that `curl.h` stays out of every file that includes this.
    void *_handle{nullptr};
};

/**
 * @struct OaiRecord
 * @brief One record, in the fields Dublin Core promises.
 */
struct OaiRecord {
    std::string identifier; ///< The repository's own, e.g. `oai:arXiv.org:2301.00001`.
    std::string datestamp;  ///< When the repository last changed it.
    bool deleted{false};    ///< The record is a TOMBSTONE. See @ref harvestOaiPmh.
    std::string title;
    std::string creator;
    std::string date;     ///< As written; @ref readYear turns it into a year.
    std::string language; ///< Two- or three-letter; @ref languageOf maps it.
    std::string rights;   ///< Free text. See @ref harvestOaiPmh on why it is not trusted.
    std::string address;  ///< `dc:identifier`, preferring one that looks like a URL.
    std::string type;
};

/**
 * @struct OaiPage
 * @brief One response.
 */
struct OaiPage {
    std::vector<OaiRecord> records;
    std::string resumptionToken; ///< Empty when this was the last page.
    std::string errorCode;       ///< OAI-PMH `<error code>`, empty when there was none.
    std::string errorText;
    core::u32 completeListSize{0u}; ///< Server's own total, when it declares one; 0 otherwise.
};

/**
 * @brief Reads one OAI-PMH response.
 *
 * @param xml The response body.
 * @param out Receives the page.
 * @return false when the body is not an OAI-PMH response at all.
 */
[[nodiscard]] bool parseOaiPage(std::string_view xml, OaiPage &out);

/**
 * @struct OaiRequest
 * @brief What to ask a repository for.
 */
struct OaiRequest {
    std::string endpoint;                ///< Base URL of the OAI-PMH interface.
    std::string metadataPrefix{"oai_dc"}; ///< Every conforming repository must offer `oai_dc`.
    std::string set;                     ///< Optional subset the repository defines.

    /**
     * Selective harvesting window, `YYYY-MM-DD` or empty.
     *
     * @warning This IS incremental synchronisation, and it is why `Changefile` describes no separate
     * mechanism: "everything that changed since I last looked" is a parameter of the protocol,
     * not a thing to build on top of it. Carry @ref OaiHarvestReport::lastDatestamp forward and
     * the next run collects only what moved.
     */
    std::string from;
    std::string until;
};

/**
 * @brief Builds the URL for one request.
 *
 * @param request The query.
 * @param resumptionToken Continuation token, or empty for the first page.
 * @return The URL. @warning With a token, it carries the verb and the token and NOTHING else — rule 1.
 */
[[nodiscard]] std::string oaiListRecordsUrl(const OaiRequest &request,
                                            const std::string &resumptionToken);

/**
 * @struct OaiHarvestReport
 * @brief What a harvest did.
 */
struct OaiHarvestReport {
    core::u32 pages{0u};     ///< Responses read.
    core::u32 records{0u};   ///< Rows written.
    core::u32 deleted{0u};   ///< Tombstones seen and NOT written.
    core::u32 untitled{0u};  ///< Records with no title. Counted, not invented.
    core::u32 retries{0u};   ///< Times a server said "later" and was obeyed.
    std::string lastToken;   ///< The token in flight when it stopped, for a resumable run.
    std::string lastDatestamp; ///< Newest datestamp seen; the `from` of the next run.
    std::string errorCode;   ///< The repository's own error, when it sent one.
    std::string failure;     ///< Why it stopped, when it stopped badly.
};

/**
 * @brief Walks a repository, writing its catalogue.
 *
 * @warning **A deleted record is not written**, and that is not a detail: OAI publishes tombstones so
 * that a mirror can REMOVE what a repository withdrew. Ingesting one as a holding puts a book in
 * the catalogue that is not there, and nothing downstream can tell it from a real one.
 *
 * @warning **Rights are recorded, never inferred.** `dc:rights` is free text; a repository saying
 * "public domain" in a sentence is not the same as a rights code, and reading the year to decide
 * would be writing a legal opinion into a data field. The same rule the HathiTrust reader
 * already follows.
 *
 * @warning **A repeated resumption token stops the harvest.** A server that hands back the token it was
 * given is a real failure mode, and without this the loop is infinite — while looking, from the
 * outside, exactly like a very large repository.
 *
 * @param fetcher   Where bytes come from.
 * @param request   What to ask for.
 * @param sink      Where rows go.
 * @param holder    Identifier naming the repository.
 * @param out       Receives the tally.
 * @param maxPages  Stop after this many responses; 0 means until the repository runs out.
 * @return false when the harvest stopped on an error rather than on the end of the list.
 */
[[nodiscard]] bool harvestOaiPmh(IHttpFetcher &fetcher, const OaiRequest &request,
                                 ICatalogueSink &sink, core::u32 holder, OaiHarvestReport &out,
                                 core::u32 maxPages = 0u);

/**
 * @brief Percent-encodes a query parameter value.
 *
 * Exposed because a resumption token is opaque and routinely contains `/`, `:`, `+` and `=` —
 * a token pasted raw into a URL is a harvest that stops at page two on some servers and not on
 * others, which is the worst kind of bug to chase.
 *
 * @param value The value.
 * @return The encoded form.
 */
[[nodiscard]] std::string urlEncode(std::string_view value);

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_OAIPMH_HPP
