/**
 * @file ResearchReport.cpp
 * @brief Implementation of the structural reading of a deep-research run.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/history/Calendar.hpp>
#include <lpl/harvest/ResearchReport.hpp>

#include <lpl/corpus/Language.hpp>
#include <lpl/corpus/Urn.hpp>
#include <lpl/harvest/EntityResolution.hpp>
#include <lpl/harvest/Markdown.hpp>

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace lpl::harvest {

namespace {

/// Namespace under which this project's own documents are cited, reports included.
constexpr const char *kUrnPrefix = "urn:cts:lplDoc:";

/**
 * The byline every report this engine writes carries, and nothing else does.
 *
 * Matched as a whole phrase rather than a prefix: it names the writer, so a document
 * containing it is either a report or is quoting one, and quoting one costs a reader that
 * finds nothing and says so.
 */
constexpr const char *kBylineMarker = "Rapport Laplace deep research";

/// The heading under which a report lists its findings. Contract with `research::writeReport`.
constexpr const char *kFindingsHeading = "Constats";

/// The heading under which a report lists what it consulted.
constexpr const char *kSourcesHeading = "Sources";

/// Confidence 1.0 as a raw Q16.16 word.
constexpr core::u32 kCertain = 65536u;

/**
 * @struct ParsedSource
 * @brief One entry of a report's source list.
 */
struct ParsedSource {
    core::u32 tag{0u};    ///< The run-local `S<id>` number. NOT an identity; see @ref identityOf.
    std::string url;      ///< Where it is.
    std::string title;    ///< What it is called; the URL when the run had no title.
    std::string provider; ///< Which search provider yielded it.
    bool readable{false}; ///< Whether the run managed to fetch it.
    bool reachable{false}; ///< Whether the run confirmed the URL still answered.
    core::u32 line{0u};   ///< Where in the report it is listed.
    core::u32 heading{0u}; ///< Which section that line falls under.
};

/**
 * @struct ParsedFinding
 * @brief One line of a report's findings list.
 */
struct ParsedFinding {
    core::u32 tag{0u};   ///< Which source backs it.
    std::string text;    ///< What it says, verbatim.
    core::u32 line{0u};  ///< Where in the report it is stated.
    core::u32 heading{0u}; ///< Which section that line falls under.
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
    return (depth < line.size() && line[depth] == ' ') ? depth : 0u;
}

/**
 * @brief Reads an `[S<id>]` tag at a position.
 *
 * @param line   The line.
 * @param at     Offset of the '['.
 * @param outTag Receives the number.
 * @param outEnd Receives the offset just past the ']'.
 * @return false when there is no tag there.
 */
[[nodiscard]] bool readSourceTag(std::string_view line, std::size_t at, core::u32 &outTag,
                                 std::size_t &outEnd) noexcept
{
    if (at + 3u > line.size() || line[at] != '[' || line[at + 1u] != 'S')
        return false;
    std::size_t i = at + 2u;
    core::u32 value = 0u;
    std::size_t digits = 0u;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9')
    {
        // Bounded rather than left to wrap: the tag comes from a file, and a run of forty
        // digits must read as "not a tag" instead of as some aliased small number.
        if (digits >= 6u)
            return false;
        value = value * 10u + static_cast<core::u32>(line[i] - '0');
        ++digits;
        ++i;
    }
    if (digits == 0u || i >= line.size() || line[i] != ']')
        return false;
    outTag = value;
    outEnd = i + 1u;
    return true;
}

/**
 * @brief Trims ASCII spaces from both ends.
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

/**
 * @brief Is this line a list bullet?
 *
 * @param line The line.
 * @return The offset just past the bullet marker, or npos.
 */
[[nodiscard]] std::size_t bulletBody(std::string_view line) noexcept
{
    const std::string_view t = trimmed(line);
    if (t.size() < 2u || t[0] != '-' || t[1] != ' ')
        return std::string_view::npos;
    return static_cast<std::size_t>(t.data() - line.data()) + 2u;
}

/**
 * @brief Parses one entry of the source list.
 *
 * The shape is `writeReport`'s, and the two forms it emits are handled apart because they
 * carry different evidence rather than different formatting: the struck-through form means
 * the run TRIED to read the page and failed, so it names no provider and backs nothing.
 *
 * @param line The line, bullet included.
 * @param out  Receives the entry.
 * @return false when the line is not a source entry.
 */
[[nodiscard]] bool parseSourceLine(std::string_view line, ParsedSource &out)
{
    const std::size_t body = bulletBody(line);
    if (body == std::string_view::npos)
        return false;
    std::size_t cursor = 0u;
    if (!readSourceTag(line, body, out.tag, cursor))
        return false;

    std::string_view rest = trimmed(line.substr(cursor));

    // `~~url~~ (lecture échouée)`: listed, fetched, failed.
    if (rest.size() > 4u && rest.substr(0u, 2u) == "~~")
    {
        const std::size_t close = rest.find("~~", 2u);
        if (close == std::string_view::npos)
            return false;
        out.url = std::string{rest.substr(2u, close - 2u)};
        out.title = out.url;
        out.readable = false;
        out.reachable = false;
        return !out.url.empty();
    }

    // `<title> — <url> (provider)<mark>`. Located from the RIGHT: a title may legitimately
    // contain the em dash this format uses as a separator, and the URL never contains the
    // angle brackets that delimit it.
    const std::size_t close = rest.rfind("> (");
    if (close == std::string_view::npos)
        return false;
    const std::size_t open = rest.rfind('<', close);
    if (open == std::string_view::npos)
        return false;

    out.url = std::string{rest.substr(open + 1u, close - open - 1u)};
    if (out.url.empty())
        return false;

    std::string_view head = trimmed(rest.substr(0u, open));
    // Drop the separator the writer put before the URL, whatever remains of it.
    if (head.size() >= 3u && head.substr(head.size() - 3u) == "\xE2\x80\x94")
        head = trimmed(head.substr(0u, head.size() - 3u));
    out.title = head.empty() ? out.url : std::string{head};

    const std::string_view tail = rest.substr(close + 3u);
    const std::size_t providerEnd = tail.find(')');
    if (providerEnd == std::string_view::npos)
        return false;
    out.provider = std::string{tail.substr(0u, providerEnd)};

    // The mark, and only the affirmative one is evidence. See kPredicateReachable.
    const std::string_view mark = trimmed(tail.substr(providerEnd + 1u));
    out.reachable = (mark == "\xE2\x9C\x93");
    out.readable = true;
    return true;
}

} // namespace

bool looksLikeResearchReport(std::string_view body) noexcept
{
    return body.find(kBylineMarker) != std::string_view::npos;
}

ProviderProfile providerProfile(std::string_view provider) noexcept
{
    // 0.80, 0.65, 0.55, 0.50, 0.40 and 0.35 as raw Q16.16 words. Written out rather than
    // computed from a float so that a build with no floating point reads the same numbers.
    constexpr core::u32 kPeerReviewed = 52429u;
    constexpr core::u32 kPreprint = 42598u;
    constexpr core::u32 kEncyclopaedia = 36045u;
    constexpr core::u32 kRegistry = 32768u;
    constexpr core::u32 kForum = 26214u;
    constexpr core::u32 kUnknown = 22938u;

    if (provider == "openalex")
        return ProviderProfile{knowledge::SourceKindV1::Notarial, kPeerReviewed};
    if (provider == "arxiv")
        return ProviderProfile{knowledge::SourceKindV1::Chronicle, kPreprint};
    if (provider == "wikipedia")
        return ProviderProfile{knowledge::SourceKindV1::Chronicle, kEncyclopaedia};
    if (provider == "github")
        return ProviderProfile{knowledge::SourceKindV1::Administrative, kRegistry};
    if (provider == "stackexchange")
        return ProviderProfile{knowledge::SourceKindV1::Chronicle, kForum};
    return ProviderProfile{knowledge::SourceKindV1::Chronicle, kUnknown};
}

bool ingestResearchReports(const std::vector<ResearchDocument> &documents, Baker &baker,
                           ResearchIngestReport &outReport)
{
    outReport = ResearchIngestReport{};

    if (!baker.name(kPredicateStatedIn, "stated-in") || !baker.name(kPredicateRestsOn, "rests-on") ||
        !baker.name(kPredicateFindingText, "finding-text") || !baker.name(kPredicateSourceUrl, "source-url") ||
        !baker.name(kPredicateResearches, "researches") || !baker.name(kPredicateReachable, "reachable") ||
        // `cited-in` is REUSED from the markdown reader rather than given a number of its
        // own: "X is referred to here" means the same thing whether X is a project
        // identifier or a paper, and two predicates for one meaning would split every query
        // that asks it. Naming it here as well is not redundant — a batch of reports with no
        // markdown alongside them would otherwise bake a predicate the vocabulary cannot
        // resolve, and `lpl-ask` would print `#1002`. Measured, not feared: it did.
        !baker.name(kPredicateCitedIn, "cited-in"))
        return false;

    // @warning Everything is parsed BEFORE anything is emitted, and the reason is corroboration.
    // How much a source is worth depends on how many OTHER independent witnesses back the
    // same claim, which is a fact about the whole batch — so a `SourceV1` written while its
    // report is still being read could only ever carry a zero there. `agreements` was that
    // zero until this pass existed.
    struct ParsedReport {
        core::u32 documentIndex{0u};
        core::u32 urn{0u};
        core::i32 year{0};
        std::string topic;
        core::u32 topicLine{0u};
        std::vector<ParsedSource> sources;
        std::vector<ParsedFinding> findings;
    };
    std::vector<ParsedReport> parsed;
    parsed.reserve(documents.size());

    for (const ResearchDocument &document : documents)
    {
        std::string body;
        if (!readFile(document.path, body))
            return false;

        const std::string urnText = std::string{kUrnPrefix} + document.canonical;
        const core::u32 urn = corpus::nameIdentifier(urnText.data(), static_cast<core::u32>(urnText.size()));
        const core::u32 title =
            corpus::nameIdentifier(document.canonical.data(), static_cast<core::u32>(document.canonical.size()));

        knowledge::DocumentV1 record{};
        record.urn = urn;
        record.title = title;
        record.language = static_cast<core::u32>(corpus::LanguageTag::ModernFrench);
        record.flags = knowledge::kDocumentFlagPlainText | knowledge::kDocumentFlagPublicDomain;
        const core::u32 documentIndex = baker.addDocument(record);

        if (!baker.name(urn, urnText) || !baker.name(title, document.canonical))
            return false;

        // The report itself is a source: it is what asserts that a finding was stated and
        // that a source was consulted. Chronicle, and yearsAfterEvent zero, for the reason a
        // working journal is: it was written as the work happened.
        knowledge::SourceV1 reportSource{};
        reportSource.id = urn;
        reportSource.kind = static_cast<core::u32>(knowledge::SourceKindV1::Chronicle);
        reportSource.yearsAfterEvent = 0u;
        reportSource.agreements = 0u;
        reportSource.name = title;
        reportSource.document = documentIndex;
        baker.addSource(reportSource);
        ++outReport.reports;

        std::vector<ParsedSource> sources;
        std::vector<ParsedFinding> findings;
        struct Citation {
            core::u32 tag;
            core::u32 line;
            core::u32 heading;
        };
        std::vector<Citation> citations;

        core::i32 year = 0;
        bool dated = false;
        std::string topic;
        core::u32 topicLine = 0u;
        core::u32 heading = 0u;
        core::u32 lineNumber = 0u;
        bool inFence = false;
        enum class Section { Other, Findings, Sources } section = Section::Other;

        std::size_t cursor = 0u;
        while (cursor <= body.size())
        {
            const std::size_t end = body.find('\n', cursor);
            const std::string_view line{body.data() + cursor,
                                        (end == std::string::npos ? body.size() : end) - cursor};
            cursor = (end == std::string::npos) ? body.size() + 1u : end + 1u;
            ++lineNumber;
            ++outReport.lines;

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
                const std::string_view name = trimmed(line.substr(depth));
                if (depth == 1u && topic.empty())
                {
                    topic = std::string{name};
                    topicLine = lineNumber;
                }
                if (name.substr(0u, std::char_traits<char>::length(kFindingsHeading)) == kFindingsHeading)
                    section = Section::Findings;
                else if (name.substr(0u, std::char_traits<char>::length(kSourcesHeading)) == kSourcesHeading)
                    section = Section::Sources;
                else
                    section = Section::Other;
                continue;
            }

            // The byline, which is also where the run's date lives. Reused rather than
            // reimplemented: `extractIsoYear` already refuses a ten-digit run that merely sits
            // inside a longer number, and a second date parser would be a second chance to
            // disagree about what a heading means.
            if (!dated && line.find(kBylineMarker) != std::string_view::npos)
            {
                if (extractIsoYear(line.data(), static_cast<core::u32>(line.size()), year))
                {
                    dated = true;
                    ++outReport.dated;
                }
            }

            if (section == Section::Sources)
            {
                ParsedSource parsed{};
                if (parseSourceLine(line, parsed))
                {
                    parsed.line = lineNumber;
                    parsed.heading = heading;
                    sources.push_back(std::move(parsed));
                    continue;
                }
            }

            if (section == Section::Findings)
            {
                const std::size_t bullet = bulletBody(line);
                std::size_t after = 0u;
                core::u32 tag = 0u;
                if (bullet != std::string_view::npos && readSourceTag(line, bullet, tag, after))
                {
                    const std::string_view text = trimmed(line.substr(after));
                    if (!text.empty())
                    {
                        findings.push_back(ParsedFinding{tag, std::string{text}, lineNumber, heading});
                        continue;
                    }
                }
            }

            // Everywhere else, an `[S<id>]` is the prose leaning on a source.
            for (std::size_t i = 0u; i < line.size(); ++i)
            {
                core::u32 tag = 0u;
                std::size_t after = 0u;
                if (line[i] == '[' && readSourceTag(line, i, tag, after))
                {
                    citations.push_back(Citation{tag, lineNumber, heading});
                    i = after - 1u;
                }
            }
        }

        parsed.push_back(ParsedReport{documentIndex, urn, year, std::move(topic), topicLine,
                                      std::move(sources), std::move(findings)});

        for (const Citation &citation : citations)
        {
            bool listed = false;
            for (const ParsedSource &candidate : parsed.back().sources)
                listed = listed || candidate.tag == citation.tag;
            if (listed)
                ++outReport.citations;
        }
    }

    // ── How much corroboration this batch actually contains ───────────────────
    //
    // A source is identified by its URL across every report, so a paper three runs cite is
    // one source with three citations rather than three sources that happen to agree. But
    // exact-URL equality is only the floor: `normaliseReference` also folds together the
    // spellings of one address that a search provider hands back as different rows — a DOI
    // with and without its resolver, a preprint's `/abs/` and `/pdf/` faces, a version
    // suffix. Getting that wrong is not a lost lookup. `history::fuseConfidence` would treat
    // one document as several concurring witnesses and manufacture certainty nobody earned.
    std::vector<core::u32> sourceIds;
    std::vector<std::string> sourceReferences;
    std::map<core::u32, core::u32> tagIdentity; // per report, rebuilt below
    std::map<core::u32, std::string> urlOf;
    std::map<core::u32, std::string> titleOf;
    std::map<core::u32, std::string> providerOf;

    for (const ParsedReport &report : parsed)
    {
        for (const ParsedSource &source : report.sources)
        {
            const core::u32 identity =
                corpus::nameIdentifier(source.url.data(), static_cast<core::u32>(source.url.size()));
            if (urlOf.find(identity) != urlOf.end())
                continue;
            urlOf[identity] = source.url;
            titleOf[identity] = source.title;
            providerOf[identity] = source.provider;
            sourceIds.push_back(identity);
            sourceReferences.push_back(source.url);
        }
    }

    // A claim is identified by the EXACT wording of the finding. Grouping near-duplicates
    // would find more agreement, and would find it by deciding that two differently-worded
    // sentences say the same thing — a judgement that inflates confidence when it is wrong.
    // The error this way round is CONSERVATIVE: two sources phrasing one finding differently
    // read as two claims and corroborate nothing, which understates the corpus instead of
    // overstating it. That is the direction to be wrong in.
    std::vector<Testimony> testimonies;
    for (const ParsedReport &report : parsed)
    {
        std::map<core::u32, core::u32> identityByTag;
        for (const ParsedSource &source : report.sources)
            identityByTag[source.tag] =
                corpus::nameIdentifier(source.url.data(), static_cast<core::u32>(source.url.size()));

        for (const ParsedFinding &finding : report.findings)
        {
            const auto backing = identityByTag.find(finding.tag);
            if (backing == identityByTag.end())
                continue;
            testimonies.push_back(
                Testimony{corpus::nameIdentifier(finding.text.data(), static_cast<core::u32>(finding.text.size())),
                          backing->second});
        }
    }

    AgreementReport agreement{};
    // No declared derivations: a citation inside a report says a run READ a page, not that
    // one page copied another, and inferring the second from the first would invent exactly
    // the relation this count exists to be honest about. `Derivation` is the seam for a
    // reader that can actually establish it.
    if (!countIndependentAgreements(sourceIds, sourceReferences, testimonies, {}, agreement))
        return false;
    outReport.witnesses = agreement.witnesses;
    outReport.collapsed = agreement.collapsed;
    outReport.corroborated = agreement.corroborated;
    outReport.inflated = agreement.inflated;

    std::map<core::u32, core::u32> agreementsOf;
    for (std::size_t i = 0u; i < sourceIds.size(); ++i)
        agreementsOf[sourceIds[i]] = agreement.agreements[i];

    // ── Emission ──────────────────────────────────────────────────────────────
    std::set<core::u32> described;
    for (const ParsedReport &report : parsed)
    {
        std::map<core::u32, core::u32> identityByTag;

        for (const ParsedSource &source : report.sources)
        {
            const core::u32 identity =
                corpus::nameIdentifier(source.url.data(), static_cast<core::u32>(source.url.size()));
            identityByTag[source.tag] = identity;

            const core::u32 nameId =
                corpus::nameIdentifier(source.title.data(), static_cast<core::u32>(source.title.size()));

            if (described.insert(identity).second)
            {
                if (!baker.name(identity, source.url) || !baker.name(nameId, source.title))
                    return false;

                const ProviderProfile profile = providerProfile(source.provider);
                knowledge::SourceV1 wire{};
                wire.id = identity;
                wire.kind = static_cast<core::u32>(profile.kind);
                wire.yearsAfterEvent = 0u;
                wire.agreements = agreementsOf[identity];
                wire.name = nameId;
                // No document: the image does not carry the page's text, and a document
                // nothing can address a locus into is a record with no reader.
                wire.document = knowledge::kNoIdentifier;
                baker.addSource(wire);
                ++outReport.sources;
            }

            if (!source.readable)
                ++outReport.unreadable;

            const core::u32 locus =
                baker.addLocus(report.documentIndex, corpus::lineLocus(source.heading, source.line));

            knowledge::FactV1 where{};
            where.subject = identity;
            where.predicate = kPredicateSourceUrl;
            where.object = baker.addText(source.url);
            where.fromDay = lpl::history::firstDayOfYear(report.year);
            where.toDay = lpl::history::lastDayOfYear(report.year);
            where.source = report.urn;
            where.locus = locus;
            where.confidenceRaw = kCertain;
            baker.addFact(where);

            knowledge::FactV1 cited{};
            cited.subject = identity;
            cited.predicate = kPredicateCitedIn;
            cited.object = report.urn;
            cited.fromDay = lpl::history::firstDayOfYear(report.year);
            cited.toDay = lpl::history::lastDayOfYear(report.year);
            cited.source = report.urn;
            cited.locus = locus;
            cited.confidenceRaw = kCertain;
            baker.addFact(cited);

            if (source.reachable)
            {
                ++outReport.reachable;
                knowledge::FactV1 alive{};
                alive.subject = identity;
                alive.predicate = kPredicateReachable;
                alive.object = report.urn;
                // Windowed to the year the run checked, because that is exactly how long the
                // observation is good for. A URL confirmed alive in 2026 says nothing about
                // 2030, and a claim with no window would say it does.
                alive.fromDay = lpl::history::firstDayOfYear(report.year);
                alive.toDay = lpl::history::lastDayOfYear(report.year);
                alive.source = report.urn;
                alive.locus = locus;
                alive.confidenceRaw = kCertain;
                baker.addFact(alive);
            }
        }

        if (!report.topic.empty())
        {
            knowledge::FactV1 about{};
            about.subject = report.urn;
            about.predicate = kPredicateResearches;
            about.object = baker.addText(report.topic);
            about.fromDay = lpl::history::firstDayOfYear(report.year);
            about.toDay = lpl::history::lastDayOfYear(report.year);
            about.source = report.urn;
            about.locus = baker.addLocus(report.documentIndex, corpus::lineLocus(1u, report.topicLine));
            about.confidenceRaw = kCertain;
            baker.addFact(about);
        }

        for (const ParsedFinding &finding : report.findings)
        {
            const auto backing = identityByTag.find(finding.tag);
            if (backing == identityByTag.end())
            {
                // A finding tagged with a source the report does not list. Counted and NOT
                // written, the same way `ingestMarkdown` refuses a dangling citation: baking
                // it would make the image assert that something was learned from a source
                // nothing can name, which is a claim with no provenance — and provenance is
                // the one property this format exists to keep.
                ++outReport.orphanFindings;
                if (outReport.firstOrphan.empty())
                {
                    char tag[16];
                    std::snprintf(tag, sizeof(tag), "S%u", finding.tag);
                    outReport.firstOrphan = tag;
                }
                continue;
            }

            const core::u32 identity =
                corpus::nameIdentifier(finding.text.data(), static_cast<core::u32>(finding.text.size()));
            if (!baker.name(identity, finding.text))
                return false;

            const core::u32 locus =
                baker.addLocus(report.documentIndex, corpus::lineLocus(finding.heading, finding.line));
            const core::u32 textIndex = baker.addText(finding.text);

            // That the REPORT states this is not in doubt — the words are on the line. The
            // confidence of the underlying claim is carried by `rests-on`, where it belongs,
            // because it is a property of the source and not of the transcription.
            knowledge::FactV1 stated{};
            stated.subject = identity;
            stated.predicate = kPredicateStatedIn;
            stated.object = report.urn;
            stated.fromDay = lpl::history::firstDayOfYear(report.year);
            stated.toDay = lpl::history::lastDayOfYear(report.year);
            stated.source = report.urn;
            stated.locus = locus;
            stated.confidenceRaw = kCertain;
            baker.addFact(stated);

            knowledge::FactV1 words{};
            words.subject = identity;
            words.predicate = kPredicateFindingText;
            words.object = textIndex;
            words.fromDay = lpl::history::firstDayOfYear(report.year);
            words.toDay = lpl::history::lastDayOfYear(report.year);
            words.source = report.urn;
            words.locus = locus;
            words.confidenceRaw = kCertain;
            baker.addFact(words);

            knowledge::FactV1 rests{};
            rests.subject = identity;
            rests.predicate = kPredicateRestsOn;
            rests.object = backing->second;
            rests.fromDay = lpl::history::firstDayOfYear(report.year);
            rests.toDay = lpl::history::lastDayOfYear(report.year);
            // The SOURCE of this claim is the source itself, not the report: the report is
            // the witness that the claim was made, the paper is what makes it worth
            // believing. Attributing it to the report would let a run manufacture confidence
            // by restating its own findings.
            rests.source = backing->second;
            rests.locus = locus;
            rests.confidenceRaw = providerProfile(providerOf[backing->second]).confidenceRaw;
            baker.addFact(rests);

            ++outReport.findings;
        }
    }

    return true;
}

} // namespace lpl::harvest
