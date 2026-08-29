/**
 * @file OaiPmh.cpp
 * @brief Implementation of OAI-PMH harvesting.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/OaiPmh.hpp>

#include <lpl/corpus/Urn.hpp>
#include <lpl/harvest/Catalogue.hpp>
#include <lpl/harvest/Xml.hpp>

#include <cstdlib>

namespace lpl::harvest {

namespace {

/// How many times one page may be retried before the harvest gives up on it.
constexpr core::u32 kMaxRetries = 5u;

/// Longest a server's `Retry-After` is obeyed, so a hostile value cannot park the process.
constexpr int kMaxRetrySeconds = 120;

/**
 * @brief Whether a string looks like an address rather than a bare identifier.
 *
 * `dc:identifier` is repeated and heterogeneous — a DOI, a handle, a URL, sometimes an ISBN.
 * The catalogue wants the one a reader could follow.
 *
 * @param text The candidate.
 * @return true when it is fetchable as written.
 */
[[nodiscard]] bool looksFetchable(std::string_view text) noexcept
{
    return text.rfind("http://", 0u) == 0u || text.rfind("https://", 0u) == 0u ||
           text.rfind("doi:", 0u) == 0u || text.rfind("urn:", 0u) == 0u;
}

/**
 * @brief Reads the fields of one `<record>` span.
 *
 * @param body   The whole response.
 * @param begin  First byte of the record.
 * @param end    One past its last.
 * @param out    Receives the record.
 */
void readRecord(std::string_view body, std::size_t begin, std::size_t end, OaiRecord &out)
{
    const std::string_view span = body.substr(begin, end - begin);

    XmlElement element{};
    std::size_t cursor = 0u;
    std::size_t depth = 0u;

    while (xmlNextElement(span, cursor, element))
    {
        cursor = element.end;
        if (element.closing || element.selfClosing)
            continue;

        // @warning The tombstone marker lives on <header>, not on the metadata — a deleted record has
        // no metadata at all, so a reader looking only at dc: fields sees an empty record and
        // cannot tell it from one whose repository simply describes it badly.
        if (element.name == "header")
        {
            std::string status;
            if (xmlAttribute(element.tag, "status", status) && status == "deleted")
                out.deleted = true;
            continue;
        }

        // Find this element's text by walking to its matching close.
        depth = 1u;
        XmlElement next{};
        std::size_t inner = element.end;
        std::size_t closeAt = span.size();
        while (depth != 0u && xmlNextElement(span, inner, next))
        {
            inner = next.end;
            if (next.selfClosing)
                continue;
            if (next.closing)
            {
                --depth;
                if (depth == 0u)
                    closeAt = next.begin;
                continue;
            }
            if (next.name == element.name)
                ++depth;
        }
        const std::string text = xmlTextOf(span, element.end, closeAt);
        if (text.empty())
            continue;

        if (element.name == "identifier" && out.identifier.empty() && !looksFetchable(text))
            out.identifier = text;
        else if (element.name == "identifier")
        {
            // dc:identifier repeats; keep the first one a reader could actually follow, and
            // fall back to the header's identifier for naming.
            if (out.address.empty() && looksFetchable(text))
                out.address = text;
            else if (out.identifier.empty())
                out.identifier = text;
        }
        else if (element.name == "datestamp" && out.datestamp.empty())
            out.datestamp = text;
        else if (element.name == "title" && out.title.empty())
            out.title = text;
        else if (element.name == "creator" && out.creator.empty())
            out.creator = text;
        else if (element.name == "date" && out.date.empty())
            out.date = text;
        else if (element.name == "language" && out.language.empty())
            out.language = text;
        else if (element.name == "rights" && out.rights.empty())
            out.rights = text;
        else if (element.name == "type" && out.type.empty())
            out.type = text;
    }
}

} // namespace

std::string urlEncode(std::string_view value)
{
    static const char *kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() + 8u);
    for (const char c : value)
    {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                          c == '-' || c == '_' || c == '.' || c == '~';
        if (safe)
        {
            out.push_back(c);
            continue;
        }
        out.push_back('%');
        out.push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 0x0Fu]);
        out.push_back(kHex[static_cast<unsigned char>(c) & 0x0Fu]);
    }
    return out;
}

std::string oaiListRecordsUrl(const OaiRequest &request, const std::string &resumptionToken)
{
    std::string url = request.endpoint;
    url += url.find('?') == std::string::npos ? '?' : '&';
    url += "verb=ListRecords";

    // @warning Rule 1, and it is the whole reason this function exists rather than a string built at
    // the call site. A continuation carries the verb and the token and nothing else; a server
    // that also receives `metadataPrefix` answers `badArgument`, so the harvest ends after one
    // page and reports an empty repository rather than a protocol mistake.
    if (!resumptionToken.empty())
    {
        url += "&resumptionToken=" + urlEncode(resumptionToken);
        return url;
    }

    url += "&metadataPrefix=" + urlEncode(request.metadataPrefix);
    if (!request.set.empty())
        url += "&set=" + urlEncode(request.set);
    if (!request.from.empty())
        url += "&from=" + urlEncode(request.from);
    if (!request.until.empty())
        url += "&until=" + urlEncode(request.until);
    return url;
}

bool parseOaiPage(std::string_view xml, OaiPage &out)
{
    out = OaiPage{};
    if (xml.find("OAI-PMH") == std::string_view::npos)
        return false;

    // An error response carries no records, and reading it as an empty list would turn a
    // protocol mistake into "this repository holds nothing".
    const std::size_t errorAt = xml.find("<error");
    if (errorAt != std::string_view::npos)
    {
        XmlElement element{};
        if (xmlNextElement(xml, errorAt, element) && element.name == "error")
        {
            (void) xmlAttribute(element.tag, "code", out.errorCode);
            const std::size_t close = xml.find("</", element.end);
            out.errorText = xmlTextOf(xml, element.end, close == std::string_view::npos ? xml.size() : close);
            if (out.errorCode.empty())
                out.errorCode = "unknown";
            return true;
        }
    }

    XmlElement element{};
    std::size_t cursor = 0u;
    while (xmlNextElement(xml, cursor, element))
    {
        cursor = element.end;
        if (element.closing)
            continue;

        if (element.name == "resumptionToken")
        {
            std::string size;
            if (xmlAttribute(element.tag, "completeListSize", size))
                out.completeListSize = static_cast<core::u32>(std::strtoul(size.c_str(), nullptr, 10));
            if (element.selfClosing)
                continue;
            const std::size_t close = xml.find("</", element.end);
            // @warning An EMPTY token element is how a repository says "that was the last page". It is
            // not a missing token and not an error; treating it as either would make the last
            // page of every harvest look like a failure.
            out.resumptionToken =
                xmlTextOf(xml, element.end, close == std::string_view::npos ? xml.size() : close);
            continue;
        }

        if (element.name != "record" || element.selfClosing)
            continue;

        std::size_t depth = 1u;
        std::size_t inner = element.end;
        std::size_t closeAt = xml.size();
        XmlElement next{};
        while (depth != 0u && xmlNextElement(xml, inner, next))
        {
            inner = next.end;
            if (next.selfClosing || next.name != "record")
                continue;
            if (next.closing)
            {
                --depth;
                if (depth == 0u)
                    closeAt = next.begin;
            }
            else
                ++depth;
        }

        OaiRecord record{};
        readRecord(xml, element.end, closeAt, record);
        out.records.push_back(record);
        cursor = closeAt;
    }
    return true;
}

bool harvestOaiPmh(IHttpFetcher &fetcher, const OaiRequest &request, ICatalogueSink &sink,
                   core::u32 holder, OaiHarvestReport &out, core::u32 maxPages)
{
    out = OaiHarvestReport{};
    std::string token;
    std::string previousToken;

    for (;;)
    {
        const std::string url = oaiListRecordsUrl(request, token);

        HttpResponse response{};
        core::u32 attempt = 0u;
        for (;;)
        {
            if (!fetcher.get(url, response))
            {
                out.failure = "the request could not be made";
                out.lastToken = token;
                return false;
            }
            // @warning Rule 2. 503 means "later", and the server says how much later. Retrying at once
            // is how an address gets banned rather than throttled — and the wait is CAPPED, so a
            // server answering `Retry-After: 86400` cannot park the process for a day.
            if (response.status != 503 || attempt >= kMaxRetries)
                break;
            int wait = response.retryAfterSeconds > 0 ? response.retryAfterSeconds : 5;
            if (wait > kMaxRetrySeconds)
                wait = kMaxRetrySeconds;
            fetcher.sleepFor(wait);
            ++attempt;
            ++out.retries;
        }

        if (response.status != 200)
        {
            out.failure = "the repository answered with status " + std::to_string(response.status);
            out.lastToken = token;
            return false;
        }

        OaiPage page{};
        if (!parseOaiPage(response.body, page))
        {
            out.failure = "the response was not OAI-PMH";
            out.lastToken = token;
            return false;
        }
        if (!page.errorCode.empty())
        {
            // `noRecordsMatch` is the repository saying "nothing changed in that window", which
            // is a normal answer to an incremental harvest and not a failure.
            out.errorCode = page.errorCode;
            if (page.errorCode == "noRecordsMatch")
                return true;
            out.failure = page.errorText.empty() ? page.errorCode : page.errorText;
            out.lastToken = token;
            return false;
        }

        ++out.pages;

        for (const OaiRecord &record : page.records)
        {
            if (record.datestamp > out.lastDatestamp)
                out.lastDatestamp = record.datestamp;

            // A tombstone. Counted, never written: a withdrawn record ingested as a holding is
            // a book in the catalogue that is not there.
            if (record.deleted)
            {
                ++out.deleted;
                continue;
            }

            sink.beginRow();

            knowledge::CatalogueEntryV1 entry{};
            if (record.title.empty())
            {
                ++out.untitled;
                entry.title = knowledge::kNoIdentifier;
            }
            else
                entry.title = sink.addText(record.title) + 1u;

            entry.creator = record.creator.empty() ? knowledge::kNoIdentifier
                                                   : sink.addText(record.creator) + 1u;

            const std::string &where = record.address.empty() ? record.identifier : record.address;
            entry.address = where.empty() ? knowledge::kNoIdentifier : sink.addText(where) + 1u;

            entry.holder = holder;
            entry.year = readYear(record.date);
            entry.language = languageOf(record.language);
            // @warning Rights are NOT read out of `dc:rights`. It is free text — "© the authors", a
            // licence URL, a sentence — and mapping a sentence onto a flag would be writing a
            // legal opinion into a data field. The HathiTrust reader takes rights from a coded
            // column for exactly this reason; an OAI record has no such column, so the honest
            // answer is to claim nothing.
            entry.flags = 0u;
            entry.cluster = knowledge::kNoIdentifier;

            sink.addCatalogueEntry(entry);
            ++out.records;
        }

        // @warning A server that hands back the token it was given makes this loop infinite while
        // looking, from outside, exactly like a very large repository.
        if (!page.resumptionToken.empty() && page.resumptionToken == previousToken)
        {
            out.failure = "the repository repeated its resumption token";
            out.lastToken = page.resumptionToken;
            return false;
        }

        if (page.resumptionToken.empty())
            return true;

        previousToken = token;
        token = page.resumptionToken;
        out.lastToken = token;

        if (maxPages != 0u && out.pages >= maxPages)
            return true;
    }
}

} // namespace lpl::harvest
