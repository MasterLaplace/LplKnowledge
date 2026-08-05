/**
 * @file Markdown.cpp
 * @brief Implementation of the structural reading of a markdown corpus.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Markdown.hpp>

#include <lpl/corpus/Language.hpp>
#include <lpl/corpus/Urn.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace lpl::harvest {

namespace {

/// Namespace under which this project's own documents are cited.
constexpr const char *kUrnPrefix = "urn:cts:lplDoc:";

/**
 * @struct Definition
 * @brief Where an identifier was defined, and what the line said.
 */
struct Definition {
    core::u32 document{0u}; ///< The document's URN identifier, never its array index.
    core::u32 locus{0u};
    core::i32 year{0};
    std::string line;
};

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
    char chunk[8192];
    std::size_t read = 0u;
    while ((read = std::fread(chunk, 1u, sizeof(chunk), file)) > 0u)
        out.append(chunk, read);
    return std::fclose(file) == 0;
}

/**
 * @brief Is this byte part of an identifier token?
 *
 * @param c The byte.
 * @return true for the characters an identifier may contain.
 */
[[nodiscard]] bool identifierByte(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

/**
 * @brief How deep a heading is, or zero when the line is not one.
 *
 * @param line The line.
 * @return The number of leading '#', capped at six.
 */
[[nodiscard]] core::u32 headingDepth(std::string_view line) noexcept
{
    core::u32 depth = 0u;
    while (depth < line.size() && line[depth] == '#')
        ++depth;
    if (depth == 0u || depth > 6u)
        return 0u;
    // A run of hashes with no space after it is not a heading — it is a comment marker, a
    // shell line inside a fenced block, or a C preprocessor directive.
    return (depth < line.size() && line[depth] == ' ') ? depth : 0u;
}

} // namespace

bool looksLikeIdentifier(const char *text, core::u32 bytes) noexcept
{
    if (text == nullptr || bytes < 6u || bytes > 8u)
        return false;

    // The prefix may hold digits — `CT3-004` is a real scheme in this corpus — but it must
    // hold at least one letter, or every hyphenated pair of numbers becomes an identifier.
    core::u32 prefix = 0u;
    bool sawLetter = false;
    while (prefix < bytes && text[prefix] != '-')
    {
        const char c = text[prefix];
        if (c >= 'A' && c <= 'Z')
            sawLetter = true;
        else if (c < '0' || c > '9')
            return false;
        ++prefix;
    }
    if (!sawLetter || prefix < 2u || prefix > 4u || prefix >= bytes)
        return false;

    // Exactly three digits. Every scheme this corpus actually uses is three wide, and
    // allowing one or two admitted `FNV-1` — measured, 93 phantom citations of it.
    return bytes - prefix - 1u == 3u && text[prefix + 1u] >= '0' && text[prefix + 1u] <= '9' &&
           text[prefix + 2u] >= '0' && text[prefix + 2u] <= '9' && text[prefix + 3u] >= '0' &&
           text[prefix + 3u] <= '9';
}

bool extractIsoYear(const char *text, core::u32 bytes, core::i32 &outYear) noexcept
{
    outYear = 0;
    if (text == nullptr || bytes < 10u)
        return false;

    for (core::u32 i = 0u; i + 10u <= bytes; ++i)
    {
        const char *p = text + i;
        bool shaped = true;
        for (core::u32 d : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u})
            shaped = shaped && p[d] >= '0' && p[d] <= '9';
        if (!shaped || p[4] != '-' || p[7] != '-')
            continue;

        // Anchored: a ten-character run that happens to sit inside a longer number is not a
        // date, it is a fragment of one. Without this, "20260805-1234" would yield 2026.
        if (i > 0u && text[i - 1u] >= '0' && text[i - 1u] <= '9')
            continue;
        if (i + 10u < bytes && text[i + 10u] >= '0' && text[i + 10u] <= '9')
            continue;

        outYear = (p[0] - '0') * 1000 + (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
        return true;
    }
    return false;
}

bool ingestMarkdown(const std::vector<MarkdownSource> &sources, Baker &baker, IngestReport &outReport)
{
    outReport = IngestReport{};

    // Two passes, and the split is what makes the dangling check possible: a citation in the
    // first document may refer to an identifier defined in the last, so nothing can be
    // called unresolved until every document has been read.
    std::map<std::string, Definition> definitions;
    struct Citation {
        std::string identifier;
        core::u32 document;
        core::u32 locus;
    };
    std::vector<Citation> citations;

    struct Loaded {
        std::string body;
        core::u32 documentIndex;
        core::u32 sourceId;
    };
    std::vector<Loaded> loaded;
    loaded.reserve(sources.size());

    // The predicates are named once, so a reader of a query result sees "defined-in" rather
    // than a bare number. They belong to the vocabulary for the same reason every other
    // identifier does: the identifier is the identity, the word is for humans.
    if (!baker.name(kPredicateDefinedIn, "defined-in") || !baker.name(kPredicateCitedIn, "cited-in") ||
        !baker.name(kPredicateDefinitionText, "definition-text"))
        return false;

    for (const MarkdownSource &source : sources)
    {
        std::string body;
        if (!readFile(source.path, body))
            return false;

        const std::string urnText = std::string{kUrnPrefix} + source.canonical;
        const core::u32 urn = corpus::nameIdentifier(urnText.data(), static_cast<core::u32>(urnText.size()));
        const core::u32 title =
            corpus::nameIdentifier(source.canonical.data(), static_cast<core::u32>(source.canonical.size()));

        knowledge::DocumentV1 document{};
        document.urn = urn;
        document.title = title;
        document.language = static_cast<core::u32>(corpus::LanguageTag::ModernFrench);
        // PlainText, because this corpus is addressed by file and line rather than by book
        // and line — the one place that mapping is decided is LocusV1's own header.
        document.flags = knowledge::kDocumentFlagPlainText | knowledge::kDocumentFlagPublicDomain;
        const core::u32 documentIndex = baker.addDocument(document);

        if (!baker.name(urn, urnText) || !baker.name(title, source.canonical))
            return false;

        // Every document is a source, and its kind is `Chronicle` rather than something
        // flattering: "a contemporary account, honest and partial" is exactly what a working
        // journal is. yearsAfterEvent is zero because these were written as the work
        // happened, which is the one thing that makes them worth more than a later account.
        knowledge::SourceV1 wire{};
        wire.id = urn;
        wire.kind = static_cast<core::u32>(knowledge::SourceKindV1::Chronicle);
        wire.yearsAfterEvent = 0u;
        wire.agreements = 0u;
        wire.name = title;
        wire.document = documentIndex;
        baker.addSource(wire);

        loaded.push_back(Loaded{std::move(body), documentIndex, urn});
        ++outReport.documents;
    }

    for (const Loaded &entry : loaded)
    {
        core::u32 lineNumber = 0u;
        core::u32 heading = 0u;
        core::i32 sectionYear = 0;
        bool inFence = false;

        std::size_t cursor = 0u;
        while (cursor <= entry.body.size())
        {
            const std::size_t end = entry.body.find('\n', cursor);
            const std::string_view line{entry.body.data() + cursor,
                                        (end == std::string::npos ? entry.body.size() : end) - cursor};
            cursor = (end == std::string::npos) ? entry.body.size() + 1u : end + 1u;
            ++lineNumber;
            ++outReport.lines;

            // Fenced code is skipped entirely. It is full of tokens shaped like identifiers
            // — enum values, macro names, hex constants — and none of them is a citation.
            if (line.size() >= 3u && line.substr(0u, 3u) == "```")
            {
                inFence = !inFence;
                continue;
            }
            if (inFence)
                continue;

            if (const core::u32 depth = headingDepth(line); depth != 0u)
            {
                ++heading;
                ++outReport.headings;
                core::i32 year = 0;
                if (extractIsoYear(line.data(), static_cast<core::u32>(line.size()), year))
                {
                    sectionYear = year;
                    ++outReport.dated;
                }
                else
                {
                    // A heading with no date inherits nothing: a section under an undated
                    // heading is undated, and carrying the previous section's year forward
                    // would date claims by where they happen to sit in a file.
                    sectionYear = 0;
                }
            }

            for (std::size_t i = 0u; i < line.size();)
            {
                if (!identifierByte(line[i]))
                {
                    ++i;
                    continue;
                }
                const std::size_t start = i;
                while (i < line.size() && identifierByte(line[i]))
                    ++i;
                const std::size_t length = i - start;
                if (!looksLikeIdentifier(line.data() + start, static_cast<core::u32>(length)))
                    continue;

                const std::string identifier{line.substr(start, length)};
                const core::u32 locus =
                    baker.addLocus(entry.documentIndex, corpus::lineLocus(heading, lineNumber));

                // A DEFINITION is a table row that opens with the identifier, which is how
                // every index in this corpus is written. Anything else is a citation. The
                // rule is deliberately syntactic: deciding by meaning would need a reader.
                const std::size_t firstNonSpace = line.find_first_not_of(" \t");
                const bool definitionRow = firstNonSpace != std::string_view::npos && line[firstNonSpace] == '|' &&
                                           start <= firstNonSpace + 3u;

                if (definitionRow && definitions.find(identifier) == definitions.end())
                {
                    definitions[identifier] =
                        Definition{entry.sourceId, locus, sectionYear, std::string{line}};
                    ++outReport.definitions;
                }
                else if (definitionRow)
                {
                    ++outReport.duplicates;
                    if (outReport.firstDuplicate.empty())
                        outReport.firstDuplicate = identifier;
                }
                else
                {
                    citations.push_back(Citation{identifier, entry.sourceId, locus});
                    ++outReport.citations;
                }
            }
        }
    }

    for (const auto &entry : definitions)
    {
        const core::u32 subject =
            corpus::nameIdentifier(entry.first.data(), static_cast<core::u32>(entry.first.size()));
        if (!baker.name(subject, entry.first))
            return false;

        knowledge::FactV1 fact{};
        fact.subject = subject;
        fact.predicate = kPredicateDefinedIn;
        // The object is the document's URN, never its position in the wire array. A position
        // is a fact about how the image happened to be laid out; a URN is what the document
        // IS, so a claim naming one stays true if the image is rebaked with the files in
        // another order.
        fact.object = entry.second.document;
        fact.fromYear = entry.second.year;
        fact.toYear = entry.second.year;
        // The source is the document the definition sits in, which is the whole point of
        // provenance here: an identifier is worth exactly what the document defining it is
        // worth.
        fact.source = entry.second.document;
        fact.locus = entry.second.locus;
        fact.confidenceRaw = 65536u; // 1.0 — that the TEXT says this is not in doubt
        baker.addFact(fact);

        // The words themselves, carried alongside. This is what makes an index renderable:
        // a reader that has the image has the definition, and never has to go and open the
        // document to find out what an identifier means.
        knowledge::FactV1 words{};
        words.subject = subject;
        words.predicate = kPredicateDefinitionText;
        words.object = baker.addText(entry.second.line);
        words.fromYear = entry.second.year;
        words.toYear = entry.second.year;
        words.source = entry.second.document;
        words.locus = entry.second.locus;
        words.confidenceRaw = 65536u;
        baker.addFact(words);
    }

    // Which prefixes are real schemes, learned from what the corpus DEFINES. `SHA-256` has
    // the shape of an identifier and always will; what tells it apart from `SIM-016` is that
    // no document in this corpus defines a `SHA-` anything.
    std::set<std::string> schemes;
    for (const auto &entry : definitions)
        schemes.insert(entry.first.substr(0u, entry.first.find('-')));

    for (const Citation &citation : citations)
    {
        const core::u32 subject =
            corpus::nameIdentifier(citation.identifier.data(), static_cast<core::u32>(citation.identifier.size()));

        if (definitions.find(citation.identifier) == definitions.end())
        {
            const std::string prefix = citation.identifier.substr(0u, citation.identifier.find('-'));
            if (schemes.find(prefix) == schemes.end())
            {
                // Not an identifier at all: a standard, a codec, a version. Counted apart,
                // because folding it into `dangling` is what buries the handful of citations
                // that really are broken.
                ++outReport.unknownScheme;
                continue;
            }
            ++outReport.dangling;
            if (outReport.firstDangling.empty())
                outReport.firstDangling = citation.identifier;
            // Counted, and NOT written. A citation of something nothing defines is the defect
            // this reading exists to surface; baking it would make the image assert that an
            // identifier which does not exist is cited somewhere — true, and useless.
            continue;
        }

        if (!baker.name(subject, citation.identifier))
            return false;

        knowledge::FactV1 fact{};
        fact.subject = subject;
        fact.predicate = kPredicateCitedIn;
        fact.object = citation.document;
        fact.source = citation.document;
        fact.confidenceRaw = 65536u;
        fact.locus = citation.locus;
        baker.addFact(fact);
    }

    return true;
}

} // namespace lpl::harvest
