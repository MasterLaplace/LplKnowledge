/**
 * @file Catalogue.cpp
 * @brief Implementation of indexing what exists without fetching it.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Catalogue.hpp>

#include <lpl/history/Calendar.hpp>

#include <lpl/corpus/Language.hpp>

#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace lpl::harvest {

namespace {

/**
 * @brief Reads a whole file into a string.
 *
 * @param path Where.
 * @param out  Receives the bytes.
 * @return false when the file could not be opened.
 */
[[nodiscard]] bool readFile(const std::string &path, std::string &out)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    char chunk[262144];
    std::size_t read = 0u;
    while ((read = std::fread(chunk, 1u, sizeof(chunk), file)) > 0u)
        out.append(chunk, read);
    return std::fclose(file) == 0;
}

/**
 * @brief Trims ASCII whitespace from both ends.
 *
 * @param text The text.
 * @return The trimmed view.
 */
[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        text.remove_prefix(1u);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1u);
    return text;
}

} // namespace

core::i32 readYear(std::string_view field) noexcept
{
    for (std::size_t i = 0u; i + 4u <= field.size(); ++i)
    {
        bool digits = true;
        for (std::size_t k = 0u; k < 4u; ++k)
            digits = digits && field[i + k] >= '0' && field[i + k] <= '9';
        if (!digits)
            continue;
        if (i > 0u && field[i - 1u] >= '0' && field[i - 1u] <= '9')
            continue;
        if (i + 4u < field.size() && field[i + 4u] >= '0' && field[i + 4u] <= '9')
            continue;
        return static_cast<core::i32>((field[i] - '0') * 1000 + (field[i + 1u] - '0') * 100 +
                                      (field[i + 2u] - '0') * 10 + (field[i + 3u] - '0'));
    }
    return 0;
}

core::u32 languageOf(std::string_view code) noexcept
{
    // @warning Delegated, and it used to be a second if-chain here. This function knew `lat`, `fre`
    // and `eng` -- three-letter ISO 639-2 codes -- while `corpus::kTags` knew only the two-letter
    // subtags, so the same corpus read one way through a catalogue and another through a query.
    // One table now answers both, and adding a language is one row rather than two edits that
    // must agree.
    corpus::LanguageTag tag = corpus::LanguageTag::Unknown;
    if (!corpus::languageByName(code.data(), static_cast<core::u32>(code.size()), tag))
        return static_cast<core::u32>(corpus::LanguageTag::Unknown);
    return static_cast<core::u32>(tag);
}

namespace {

/**
 * @brief The text between the first `<tag>` and its close.
 *
 * A scan rather than an XML parse, and bounded to one element: Gutenberg's RDF is machine
 * written and its shape is a contract, but the descriptions inside it contain arbitrary text
 * including angle brackets, so nothing here may assume well-formedness beyond the tag sought.
 *
 * @param body The document.
 * @param tag  The element name, e.g. "dcterms:title".
 * @param out  Receives the text.
 * @return false when the element is absent.
 */
/**
 * @brief The bytes between `<tag ...>` and `</tag>`.
 *
 * @warning Exists because a Gutenberg record names SEVERAL people — a creator, a translator, an
 * illustrator, an editor — each in its own agent block, and every one of them carries a
 * `pgterms:name` and a `pgterms:deathdate`. Reading those from the whole file takes whichever
 * agent happens to come first. Measured on the full catalogue: **2947 of 79 179 records** list a
 * non-creator first, so that many rows would be credited to their translator, silently and
 * plausibly.
 *
 * @param body The document.
 * @param tag  The element name.
 * @param out  Receives the span between the tags.
 * @return false when the element is absent or unterminated.
 */
[[nodiscard]] bool elementSpan(std::string_view body, std::string_view tag, std::string_view &out)
{
    const std::string open = "<" + std::string{tag};
    const std::string close = "</" + std::string{tag} + ">";
    const std::size_t at = body.find(open);
    if (at == std::string_view::npos)
        return false;
    const std::size_t gt = body.find('>', at);
    if (gt == std::string_view::npos)
        return false;
    const std::size_t stop = body.find(close, gt);
    if (stop == std::string_view::npos)
        return false; // self-closing, or an rdf:resource reference with no body
    out = body.substr(gt + 1u, stop - gt - 1u);
    return true;
}



/**
 * @brief Reads a possibly negative integer year.
 *
 * @warning Separate from @ref readYear, which wants four digits and would refuse `-430` — and refusing
 * is exactly wrong here, because a year before the era is the case this whole field exists to
 * carry. Gutenberg writes these as XML-schema integers, so the form is exact rather than prose.
 *
 * @param field The field.
 * @param out   Receives the year.
 * @return false when the field holds no integer.
 */
[[nodiscard]] bool readSignedYear(std::string_view field, core::i32 &out) noexcept
{
    std::size_t i = 0u;
    while (i < field.size() && (field[i] == ' ' || field[i] == '\t' || field[i] == '\n' || field[i] == '\r'))
        ++i;
    const bool negative = i < field.size() && field[i] == '-';
    if (negative)
        ++i;
    if (i >= field.size() || field[i] < '0' || field[i] > '9')
        return false;
    core::i32 value = 0;
    while (i < field.size() && field[i] >= '0' && field[i] <= '9')
    {
        value = value * 10 + static_cast<core::i32>(field[i] - '0');
        ++i;
    }
    out = negative ? -value : value;
    return true;
}


[[nodiscard]] bool elementText(std::string_view body, std::string_view tag, std::string &out)
{
    const std::string open = "<" + std::string{tag};
    const std::string close = "</" + std::string{tag} + ">";
    const std::size_t at = body.find(open);
    if (at == std::string_view::npos)
        return false;
    const std::size_t gt = body.find('>', at);
    if (gt == std::string_view::npos)
        return false;
    // A self-closing element carries no text; treating it as opening one would swallow the
    // whole rest of the document up to some unrelated close tag.
    if (gt > 0u && body[gt - 1u] == '/')
        return false;
    const std::size_t stop = body.find(close, gt);
    if (stop == std::string_view::npos)
        return false;

    std::string raw{body.substr(gt + 1u, stop - gt - 1u)};
    // The five predefined entities, plus the carriage returns Gutenberg encodes numerically.
    const std::pair<const char *, const char *> replacements[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&#13;", " "}};
    for (const auto &pair : replacements)
    {
        std::size_t found = 0u;
        while ((found = raw.find(pair.first, found)) != std::string::npos)
            raw.replace(found, std::char_traits<char>::length(pair.first), pair.second);
    }
    out.assign(trimmed(raw));
    return !out.empty();
}

/**
 * @brief The window in which any NAMED CREATOR of a work could have written it.
 *
 * @warning **The union of their lives, not the first one's.** Measured on Project Gutenberg: 2767
 * records (3 %) name more than one `dcterms:creator`, and their death dates are a median 23 years
 * apart -- noise against a proximity term that decays as 1/(1+y/32) -- but the ninetieth
 * percentile is 102 years and the widest is **2589**, which is an ancient author recorded beside
 * a modern editor. Taking the first would credit a work to whichever the file happened to list
 * first, and be wrong by two and a half millennia in the tail.
 *
 * Widening is the CONSERVATIVE direction and that is why it is the answer: a wider window means a
 * greater distance from any given event, which means less temporal credit. A credit that is not
 * evidenced must not be granted, so where the evidence is ambiguous the score should suffer.
 *
 * @warning What this bounds is when a named person could have WRITTEN something -- not when the text
 * reached its present form. A work assembled, published or continued after its author's death has
 * a later history that this window says nothing about, and deliberately so: the trust score wants
 * to know how close the WITNESS stood to the event. Publication is a different question, and
 * `dcterms:issued` is refused for it below for a related reason.
 *
 * @param body      The whole record.
 * @param outFrom   Receives the earliest birth, as a day.
 * @param outTo     Receives the latest death, as a day.
 * @param outShared Set when more than one creator was named, so the caller can count it.
 * @return false when no creator carries a date at all.
 */
[[nodiscard]] bool creatorLifeWindow(std::string_view body, core::i32 &outFrom, core::i32 &outTo,
                                     bool &outShared)
{
    outFrom = 0;
    outTo = 0;
    outShared = false;

    const std::string open = "<dcterms:creator";
    const std::string close = "</dcterms:creator>";
    bool any = false;
    core::u32 creators = 0u;
    std::size_t cursor = 0u;

    while (cursor < body.size())
    {
        const std::size_t at = body.find(open, cursor);
        if (at == std::string_view::npos)
            break;
        const std::size_t gt = body.find('>', at);
        if (gt == std::string_view::npos)
            break;
        const std::size_t stop = body.find(close, gt);
        if (stop == std::string_view::npos)
            break;
        const std::string_view span = body.substr(gt + 1u, stop - gt - 1u);
        cursor = stop + close.size();
        ++creators;

        std::string birth;
        std::string death;
        const bool hasBirth = elementText(span, "pgterms:birthdate", birth);
        const bool hasDeath = elementText(span, "pgterms:deathdate", death);
        core::i32 birthYear = 0;
        core::i32 deathYear = 0;
        const bool readBirth = hasBirth && readSignedYear(birth, birthYear);
        const bool readDeath = hasDeath && readSignedYear(death, deathYear);
        if (!readBirth && !readDeath)
            continue;

        // One bound alone still says something: an author known only by a death date could not
        // have written after it. The missing side stays open rather than being invented.
        const core::i32 from = readBirth ? lpl::history::firstDayOfYear(birthYear) : 0;
        const core::i32 to = readDeath ? lpl::history::lastDayOfYear(deathYear) : 0;
        if (!any)
        {
            outFrom = from;
            outTo = to;
            any = true;
            continue;
        }
        if (from != 0 && (outFrom == 0 || from < outFrom))
            outFrom = from;
        if (to != 0 && (outTo == 0 || to > outTo))
            outTo = to;
    }

    outShared = creators > 1u;
    return any;
}

} // namespace

void splitTabs(std::string_view line, std::vector<std::string_view> &out)
{
    out.clear();
    std::size_t cursor = 0u;
    while (true)
    {
        const std::size_t tab = line.find('\t', cursor);
        if (tab == std::string_view::npos)
        {
            // The last field, even when empty. Stopping at the last tab instead would drop a
            // trailing empty column, and every column index after it would be off by one on
            // exactly the rows whose data is missing.
            out.push_back(line.substr(cursor));
            return;
        }
        out.push_back(line.substr(cursor, tab - cursor));
        cursor = tab + 1u;
    }
}

bool ingestHathiFile(const std::string &path, core::u32 holder, ICatalogueSink &sink,
                     CatalogueIngestReport &outReport, std::vector<std::string> *outKeys)
{
    outReport = CatalogueIngestReport{};

    // @warning A buffer at a time, never the whole file. Streaming the OUTPUT while slurping the input
    // would only move the ceiling: HathiTrust's full holdings file is 5.6 GB uncompressed, so the
    // slurp alone would cost more memory than the in-memory baker this path exists to replace.
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;

    // Column positions confirmed against a real update file, not taken from documentation.
    constexpr std::size_t kIdentifier = 0u;
    constexpr std::size_t kAccess = 1u;
    constexpr std::size_t kRights = 2u;
    constexpr std::size_t kTitle = 11u;
    constexpr std::size_t kYear = 16u;
    constexpr std::size_t kLanguage = 18u;
    constexpr std::size_t kOclc = 7u;
    constexpr std::size_t kAuthor = 25u;
    constexpr std::size_t kMinimumColumns = 26u;

    std::vector<std::string_view> fields;
    std::vector<char> chunk(1u << 20);
    std::string carry;
    bool eof = false;

    while (!eof)
    {
        const std::size_t read = std::fread(chunk.data(), 1u, chunk.size(), file);
        eof = read == 0u;
        carry.append(chunk.data(), read);

        std::size_t from = 0u;
        while (true)
        {
            const std::size_t end = carry.find('\n', from);
            // @warning Without a newline the tail is a PARTIAL line, and parsing it would cut a row in
            // half — silently, usually in the middle of a title. It is carried to the next read.
            // At end of file the remainder IS a whole line, because a file need not end in one.
            if (end == std::string::npos)
            {
                if (eof && from < carry.size())
                {
                    const std::string_view line{carry.data() + from, carry.size() - from};
                    from = carry.size();
                    if (!line.empty())
                    {
                    splitTabs(line, fields);
                    if (fields.size() < kMinimumColumns || trimmed(fields[kIdentifier]).empty())
                    {
                        ++outReport.malformed;
                        continue;
                    }

                    sink.beginRow();
                    knowledge::CatalogueEntryV1 entry{};
                    entry.holder = holder;

                    const std::string_view title = trimmed(fields[kTitle]);
                    if (title.empty())
                        ++outReport.withoutTitle;
                    else
                        entry.title = sink.addText(title) + 1u;

                    const std::string_view author = trimmed(fields[kAuthor]);
                    if (author.empty())
                        ++outReport.withoutCreator;
                    else
                        entry.creator = sink.addText(author) + 1u;

                    // The address is derived from the identifier rather than stored: HathiTrust's page
                    // template is stable and one column is cheaper than a full URL on eighteen million
                    // rows. It is written out here so the row is self-sufficient once baked.
                    const std::string address = "https://babel.hathitrust.org/cgi/pt?id=" + std::string{trimmed(fields[kIdentifier])};
                    entry.address = sink.addText(address) + 1u;

                    entry.year = readYear(fields[kYear]);
                    // An edition was printed in one year, so its window is one year wide. Stated
                    // rather than left at zero: zero means UNKNOWN, and a printed book is not.
                    entry.yearTo = entry.year;
                    if (entry.year == 0)
                        ++outReport.withoutYear;
                    else if (entry.year < 1900)
                        ++outReport.beforeNineteenHundred;

                    entry.language = languageOf(trimmed(fields[kLanguage]));

                    // @warning Rights from the holder's own code, never inferred from the year. A 1905 book may
                    // be in copyright and a 1960 government document may not be; a harvester that guessed
                    // would be writing a legal opinion into a data field.
                    const std::string_view rights = trimmed(fields[kRights]);
                    if (rights == "pd" || rights == "pdus" || rights == "world" || rights == "cc-zero")
                    {
                        entry.flags |= knowledge::kCatalogueFlagPublicDomain;
                        ++outReport.publicDomain;
                    }
                    if (trimmed(fields[kAccess]) == "allow")
                    {
                        entry.flags |= knowledge::kCatalogueFlagFullView;
                        ++outReport.fullView;
                    }
                    // A HathiTrust volume is a scan with machine-read text over it. Saying so matters:
                    // it is the difference between a passage that can be quoted and one that can only be
                    // looked at, and a catalogue that flattened the two would promise words it has not got.
                    entry.flags |= knowledge::kCatalogueFlagOpticalCharacterRecognition;

                    sink.addCatalogueEntry(entry);
                    ++outReport.rows;

                    }
                }
                break;
            }
            const std::string_view line{carry.data() + from, end - from};
            from = end + 1u;
            if (line.empty())
                continue;

            splitTabs(line, fields);
            if (fields.size() < kMinimumColumns || trimmed(fields[kIdentifier]).empty())
            {
                ++outReport.malformed;
                continue;
            }

            sink.beginRow();
            knowledge::CatalogueEntryV1 entry{};
            entry.holder = holder;

            const std::string_view title = trimmed(fields[kTitle]);
            if (title.empty())
                ++outReport.withoutTitle;
            else
                entry.title = sink.addText(title) + 1u;

            const std::string_view author = trimmed(fields[kAuthor]);
            if (author.empty())
                ++outReport.withoutCreator;
            else
                entry.creator = sink.addText(author) + 1u;

            // The address is derived from the identifier rather than stored: HathiTrust's page
            // template is stable and one column is cheaper than a full URL on eighteen million
            // rows. It is written out here so the row is self-sufficient once baked.
            const std::string address = "https://babel.hathitrust.org/cgi/pt?id=" + std::string{trimmed(fields[kIdentifier])};
            entry.address = sink.addText(address) + 1u;

            entry.year = readYear(fields[kYear]);
            entry.yearTo = entry.year;
            if (entry.year == 0)
                ++outReport.withoutYear;
            else if (entry.year < 1900)
                ++outReport.beforeNineteenHundred;

            entry.language = languageOf(trimmed(fields[kLanguage]));

            // @warning Rights from the holder's own code, never inferred from the year. A 1905 book may
            // be in copyright and a 1960 government document may not be; a harvester that guessed
            // would be writing a legal opinion into a data field.
            const std::string_view rights = trimmed(fields[kRights]);
            if (rights == "pd" || rights == "pdus" || rights == "world" || rights == "cc-zero")
            {
                entry.flags |= knowledge::kCatalogueFlagPublicDomain;
                ++outReport.publicDomain;
            }
            if (trimmed(fields[kAccess]) == "allow")
            {
                entry.flags |= knowledge::kCatalogueFlagFullView;
                ++outReport.fullView;
            }
            // A HathiTrust volume is a scan with machine-read text over it. Saying so matters:
            // it is the difference between a passage that can be quoted and one that can only be
            // looked at, and a catalogue that flattened the two would promise words it has not got.
            entry.flags |= knowledge::kCatalogueFlagOpticalCharacterRecognition;

            sink.addCatalogueEntry(entry);
            if (outKeys != nullptr)
            {
                const std::string_view oclc = trimmed(fields[kOclc]);
                outKeys->emplace_back(oclc.empty() ? std::string{} : "oclc:" + std::string{oclc});
            }
            ++outReport.rows;

        }
        carry.erase(0u, from);
    }

    return std::fclose(file) == 0;
}


bool ingestGutenbergRdf(const std::vector<std::string> &paths, core::u32 holder, ICatalogueSink &sink,
                        CatalogueIngestReport &outReport)
{
    outReport = CatalogueIngestReport{};

    for (const std::string &path : paths)
    {
        std::string body;
        if (!readFile(path, body))
            return false;

        std::string title;
        std::string creator;
        std::string issued;
        std::string language;
        std::string rights;

        const bool haveTitle = elementText(body, "dcterms:title", title);
        // @warning Scoped to the creator's own block. See @ref elementSpan: 2947 records list a
        // translator or an illustrator first, and the whole-file read credited the work to them.
        std::string_view creatorSpan;
        const bool haveCreatorBlock = elementSpan(body, "dcterms:creator", creatorSpan);
        const bool haveCreator = haveCreatorBlock && elementText(creatorSpan, "pgterms:name", creator);
        (void) elementText(body, "dcterms:issued", issued);
        (void) elementText(body, "rdf:value", language);
        (void) elementText(body, "dcterms:rights", rights);

        // The ebook number, which is both the identity and the address.
        std::string number;
        const std::size_t about = body.find("rdf:about=\"ebooks/");
        if (about != std::string::npos)
        {
            const std::size_t start = about + std::char_traits<char>::length("rdf:about=\"ebooks/");
            const std::size_t stop = body.find('"', start);
            if (stop != std::string::npos)
                number = body.substr(start, stop - start);
        }
        if (number.empty())
        {
            ++outReport.malformed;
            continue;
        }

        sink.beginRow();
                knowledge::CatalogueEntryV1 entry{};
                entry.holder = holder;
        if (haveTitle)
            entry.title = sink.addText(title) + 1u;
        else
            ++outReport.withoutTitle;
        if (haveCreator)
            entry.creator = sink.addText(creator) + 1u;
        else
            ++outReport.withoutCreator;
        entry.address = sink.addText("https://www.gutenberg.org/ebooks/" + number) + 1u;

        // @warning `dcterms:issued` is the date Gutenberg PUBLISHED the transcription — 2006 for
        // Herodotus, 1971 for the Declaration of Independence — so it is NOT written here, and
        // the earlier version of this reader that did so made every ancient work in the
        // catalogue answer "2006" to a question about antiquity. Correctly, and uselessly: a run
        // over the full catalogue reported zero works published before 1900, out of 79 179.
        //
        // The creators' LIVES are what the file actually carries about the work's age, and they
        // are real bounds rather than a guess: a text cannot be written before its author was
        // born nor after they died. Written with @ref knowledge::kCatalogueFlagYearFromAuthor so
        // that nothing downstream can mistake it for a publication date. Absent -> zero, which
        // already means UNKNOWN.
        //
        // @warning A WINDOW, and an earlier version wrote only the death year -- so Herodotus was
        // dated "-430" when what is known is that he wrote somewhere across fifty-four years.
        // Collapsing a window to its end is not a rounding, it is a claim nobody made.
        core::i32 lifeFrom = 0;
        core::i32 lifeTo = 0;
        bool sharedCreators = false;
        if (creatorLifeWindow(body, lifeFrom, lifeTo, sharedCreators))
        {
            const core::i32 deathYear = lifeTo != 0 ? lpl::history::yearOfDay(lifeTo) : 0;
            entry.year = lifeFrom != 0 ? lpl::history::yearOfDay(lifeFrom) : deathYear;
            entry.yearTo = deathYear != 0 ? deathYear : entry.year;
            if (sharedCreators)
                ++outReport.sharedCreators;
            entry.flags |= knowledge::kCatalogueFlagYearFromAuthor;
            // @warning Counted here, and it was NOT before. The reader left this at zero while the
            // image itself held 19 541 such works, so the run printed "0 published before 1900"
            // — a number that reads as "none of this is old" when it meant "this reader does not
            // count that". A tally nobody fills is worse than one nobody prints.
            if (deathYear < 1900)
                ++outReport.beforeNineteenHundred;
        }
        else
        {
            entry.year = 0;
            entry.yearTo = 0;
            ++outReport.withoutYear;
        }
        (void) issued;

        entry.language = languageOf(language);
        if (rights.find("Public domain") != std::string::npos)
        {
            entry.flags |= knowledge::kCatalogueFlagPublicDomain;
            ++outReport.publicDomain;
        }
        // A transcription, not a scan: Gutenberg's whole point is that the words are typed.
        entry.flags |= knowledge::kCatalogueFlagTranscription | knowledge::kCatalogueFlagFullView;
        ++outReport.fullView;

        sink.addCatalogueEntry(entry);
        ++outReport.rows;
    }
    return true;
}

bool resolveCatalogue(std::vector<knowledge::CatalogueEntryV1> &entries, const std::vector<std::string> &keys,
                      CatalogueResolution &out)
{
    out = CatalogueResolution{};
    if (entries.size() != keys.size())
        return false;

    // @warning A key field holds a LIST, not one number. Measured on real data: the most-shared value
    // in a HathiTrust update is `1768512,2505035,27876602,35862481,429517699` — five OCLC
    // numbers a union catalogue has consolidated into one bibliographic record. Treating the
    // list as an opaque string was the first version, and it means two rows whose lists OVERLAP
    // but are not written identically never fold. Sharing ANY number is what makes two rows one
    // work, so the rows are united element by element.
    std::vector<std::size_t> parent(entries.size());
    for (std::size_t i = 0u; i < entries.size(); ++i)
        parent[i] = i;

    const std::function<std::size_t(std::size_t)> find = [&parent](std::size_t x) {
        while (parent[x] != x)
        {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    const auto unite = [&](std::size_t a, std::size_t b) {
        const std::size_t ra = find(a);
        const std::size_t rb = find(b);
        if (ra == rb)
            return;
        // Toward the smaller index, so the representative is the FIRST holding of the work and
        // two machines resolving the same catalogue agree on which row that is.
        if (ra < rb)
            parent[rb] = ra;
        else
            parent[ra] = rb;
    };

    std::map<std::string, std::size_t> firstWith;
    std::vector<bool> keyed(entries.size(), false);

    for (std::size_t i = 0u; i < entries.size(); ++i)
    {
        std::size_t from = 0u;
        while (from <= keys[i].size())
        {
            const std::size_t comma = keys[i].find(',', from);
            const std::string_view piece =
                std::string_view{keys[i]}.substr(from, (comma == std::string::npos ? keys[i].size() : comma) - from);
            from = (comma == std::string::npos) ? keys[i].size() + 1u : comma + 1u;
            const std::string_view number = trimmed(piece);
            if (number.empty())
                continue;
            keyed[i] = true;
            const std::string key{number};
            const auto at = firstWith.find(key);
            if (at == firstWith.end())
                firstWith.emplace(key, i);
            else
                unite(at->second, i);
        }
        if (!keyed[i])
            ++out.unkeyed;
    }

    std::map<std::size_t, core::u32> members;
    for (std::size_t i = 0u; i < entries.size(); ++i)
    {
        if (!keyed[i])
        {
            entries[i].cluster = knowledge::kNoIdentifier;
            continue;
        }
        const std::size_t root = find(i);
        // One-based, so that zero keeps meaning "unresolved" rather than "row zero".
        entries[i].cluster = static_cast<core::u32>(root + 1u);
        const core::u32 count = ++members[root];
        if (count > out.largest)
            out.largest = count;
    }
    out.clusters = static_cast<core::u32>(members.size());
    for (const auto &entry : members)
        out.merged += entry.second - 1u;
    return true;
}

} // namespace lpl::harvest
