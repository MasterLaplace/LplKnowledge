/**
 * @file Tei.cpp
 * @brief Implementation of reading TEI-XML into an addressable corpus.
 *
 * A scanner rather than a general XML parser, and the boundary is deliberate: what is needed
 * is the element structure and a handful of attributes, and a general parser would be a
 * dependency this repository does not have and a surface this reading does not use.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Tei.hpp>

#include <lpl/history/Fact.hpp>

#include <lpl/harvest/AuthorDates.hpp>
#include <lpl/harvest/EntityResolution.hpp>
#include <lpl/harvest/Mentions.hpp>
#include <lpl/harvest/Xml.hpp>

#include <lpl/corpus/Urn.hpp>

#include <cstdio>
#include <map>
#include <set>

namespace lpl::harvest {

namespace {

/// Confidence 1.0 as a raw Q16.16 word. That a document SAYS this is not in doubt.
constexpr core::u32 kCertain = 65536u;

/// Namespace under which a TEI work with no CTS URN of its own is cited.
constexpr const char *kFallbackPrefix = "urn:cts:lplTei:";

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
    char chunk[65536];
    std::size_t read = 0u;
    while ((read = std::fread(chunk, 1u, sizeof(chunk), file)) > 0u)
        out.append(chunk, read);
    return std::fclose(file) == 0;
}


/**
 * @brief Records what one passage names, and any date its editor marked up.
 *
 * @warning Called ONLY for a leaf textpart. A container's span contains its children's, so scanning
 * every level would record one editor's single tag once per ancestor -- and a chapter, its book
 * and the work all "naming" the same place reads downstream as three sources agreeing.
 *
 * @param view      The whole document.
 * @param begin     First byte of the passage.
 * @param end       One past the last.
 * @param work      The work, which is the subject: a mention is a fact about the TEXT.
 * @param where     The passage's locus.
 * @param baker     Where to put it.
 * @param outReport Receives the tally.
 */
void scanMentions(std::string_view view, std::size_t begin, std::size_t end, core::u32 work, core::u32 where,
                  Baker &baker, TeiIngestReport &outReport)
{
    XmlElement element;
    std::size_t cursor = begin;
    while (cursor < end && xmlNextElement(view, cursor, element) && element.begin < end)
    {
        cursor = element.end;
        if (element.closing)
            continue;

        const bool place = element.name == "placeName";
        const bool person = element.name == "persName";
        if (place || person)
        {
            std::string key;
            const AuthorityKey split =
                xmlAttribute(element.tag, "key", key) ? splitAuthorityKey(key) : AuthorityKey{};

            knowledge::FactV1 fact{};
            fact.subject = work;
            fact.predicate = place ? kPredicateNamesPlace : kPredicateNamesPerson;
            fact.source = work;
            fact.locus = where;
            fact.confidenceRaw = kCertain;

            if (split.present)
            {
                // An editor's authority key: a HARD key, which may fuse. The identifier is built
                // from the whole key because two authorities number independently.
                fact.object = authorityIdentifier(split);
                baker.name(fact.object, key);
            }
            else
            {
                // No key: the word itself, as a line of text rather than an identifier. A name is
                // not an identity -- ten places are called Alexandria -- so this must never be
                // matched on, and giving it an identifier is how something later would.
                std::string word = xmlTextOf(view, element.end, end);
                const std::size_t stop = word.find('\n');
                if (stop != std::string::npos)
                    word.resize(stop);
                if (word.size() > 64u)
                    word.resize(64u);
                fact.object = baker.addText(word);
                if (place)
                    ++outReport.unkeyedPlaces;
            }

            baker.addFact(fact);
            if (place)
                ++outReport.placeMentions;
            else
                ++outReport.personMentions;
            continue;
        }

        if (element.name == "date")
        {
            DateWindow window;
            if (!dateWindowOf(element.tag, window))
                continue;
            knowledge::FactV1 fact{};
            fact.subject = work;
            fact.predicate = kPredicateDatedHere;
            // The object is the work: the DATE is the window, not an entity to be named. Minting
            // identifiers for dates would repeat exactly what retired predicate 1204.
            fact.object = work;
            fact.fromDay = window.fromDay;
            fact.toDay = window.toDay;
            fact.source = work;
            fact.locus = where;
            fact.confidenceRaw = kCertain;
            baker.addFact(fact);
            ++outReport.datedPassages;
        }
    }
}

} // namespace









bool looksLikeTei(std::string_view head) noexcept
{
    return head.find("http://www.tei-c.org/ns/1.0") != std::string_view::npos;
}

corpus::LanguageTag teiLanguage(std::string_view code) noexcept
{
    if (code == "grc")
        return corpus::LanguageTag::AncientGreek;
    if (code == "lat" || code == "la")
        return corpus::LanguageTag::Latin;
    if (code == "fro")
        return corpus::LanguageTag::OldFrench;
    if (code == "enm")
        return corpus::LanguageTag::MiddleEnglish;
    if (code == "fra" || code == "fre" || code == "fr")
        return corpus::LanguageTag::ModernFrench;
    if (code == "eng" || code == "en")
        return corpus::LanguageTag::ModernEnglish;
    return corpus::LanguageTag::Unknown;
}

bool ingestTei(const std::vector<TeiSource> &sources, const TeiOptions &options, Baker &baker,
               TeiIngestReport &outReport)
{
    outReport = TeiIngestReport{};

    if (!baker.name(kPredicateAttributedTo, "attributed-to") || !baker.name(kPredicateWrittenIn, "written-in") ||
        !baker.name(kPredicateWorkTitle, "work-title") ||
        !baker.name(kPredicatePassageText, "passage-text") || !baker.name(kPredicateEditedBy, "edited-by") ||
        !baker.name(kPredicateCitationScheme, "citation-scheme") ||
        !baker.name(kPredicateNamesPlace, "names-place") ||
        !baker.name(kPredicateNamesPerson, "names-person") ||
        !baker.name(kPredicateDatedHere, "dated-here"))
        return false;

    std::set<core::u32> works;
    std::set<std::string> authors;

    for (const TeiSource &source : sources)
    {
        std::string body;
        if (!readFile(source.path, body))
            return false;
        ++outReport.documents;

        const std::string_view view{body};

        // @warning **The licence the DOCUMENT declares, not one this reader assumes.** Measured across
        // Perseus: 1345 files carry their own `<licence>` and 1344 of them are CC-BY-SA 4.0 --
        // share-alike, so a derivative must carry the same terms. Reading it beats hard-coding it
        // twice over: a corpus with mixed terms is described accurately, and a corpus that changes
        // its terms is followed rather than misreported.
        {
            std::string availability;
            if (xmlFirstElementText(view, "licence", availability) && !availability.empty())
            {
                baker.addAttribution(availability);
            }
            else if (xmlFirstElementText(view, "availability", availability) && !availability.empty())
            {
                baker.addAttribution(availability);
            }
            std::string licenceTag;
            XmlElement licenceElement;
            std::size_t at = 0u;
            while (xmlNextElement(view, at, licenceElement))
            {
                at = licenceElement.end;
                if (licenceElement.name != "licence" || licenceElement.closing)
                    continue;
                // The URL is the licence's identity; the prose beside it is a paraphrase.
                if (xmlAttribute(licenceElement.tag, "target", licenceTag) && !licenceTag.empty())
                    baker.addAttribution(licenceTag);
                break;
            }
        }

        // ── Identity ──────────────────────────────────────────────────────────
        // The edition div's `n`, which is where Perseus puts the CTS URN. Read by scanning
        // for the element and then asking for the attribute BY NAME — never by expecting it
        // at a position, because two files in one project order their attributes differently.
        std::string urnText;
        std::string editionLanguage;
        {
            XmlElement element{};
            std::size_t cursor = 0u;
            while (xmlNextElement(view, cursor, element))
            {
                cursor = element.end;
                if (element.closing || element.name != "div")
                    continue;
                std::string type;
                if (!xmlAttribute(element.tag, "type", type))
                    continue;
                if (type != "edition" && type != "translation")
                    continue;
                std::string candidate;
                if (xmlAttribute(element.tag, "n", candidate) && candidate.rfind("urn:cts:", 0u) == 0u)
                    urnText = candidate;
                (void) xmlAttribute(element.tag, "lang", editionLanguage);
                break;
            }
        }

        if (urnText.empty())
        {
            // No CTS URN: identified by where it came from instead. Counted, because a
            // corpus in which some works are citable by the world's scheme and others only
            // by this machine's paths is a corpus with two identity systems, and that is
            // worth knowing rather than discovering later.
            ++outReport.withoutUrn;
            urnText = std::string{kFallbackPrefix} + source.canonical;
        }

        const core::u32 work = corpus::nameIdentifier(urnText.data(), static_cast<core::u32>(urnText.size()));
        if (!works.insert(work).second)
        {
            outReport.firstCollision = urnText + "  (" + source.path + ")";
            // Two works on one identifier. `Baker` would catch a name collision, but two
            // files legitimately carrying the same URN is a corpus error it cannot see —
            // and merging them would fuse two editions into one work.
            return false;
        }
        if (!baker.name(work, urnText))
        {
            outReport.firstCollision = baker.firstCollision();
            return false;
        }

        knowledge::DocumentV1 document{};
        document.urn = work;

        std::string title;
        std::string author;
        std::string editor;
        {
            // The header, bounded to itself: a `<title>` also occurs inside `<sourceDesc>`
            // and inside a `<series>`, and taking the wrong one silently retitles the work.
            const std::size_t headerEnd = view.find("</teiHeader>");
            const std::string_view header =
                headerEnd == std::string_view::npos ? view.substr(0u, 0u) : view.substr(0u, headerEnd);
            std::string statement;
            const std::size_t titleStatement = header.find("<titleStmt>");
            const std::string_view scope =
                titleStatement == std::string_view::npos ? header : header.substr(titleStatement);
            (void) xmlFirstElementText(scope, "title", title);
            (void) xmlFirstElementText(scope, "author", author);
            (void) xmlFirstElementText(scope, "editor", editor);
        }

        const std::string languageCode =
            editionLanguage.empty() ? std::string{} : editionLanguage;
        document.language = static_cast<core::u32>(teiLanguage(languageCode));
        // Not plain text: this is addressed by book, chapter and line, which is the whole
        // reason `LocusV1` documents two addressing schemes rather than one.
        document.flags = knowledge::kDocumentFlagPublicDomain;
        const core::u32 titleId =
            title.empty() ? work : corpus::nameIdentifier(title.data(), static_cast<core::u32>(title.size()));
        document.title = titleId;
        if (!title.empty() && !baker.name(titleId, title))
        {
            outReport.firstCollision = baker.firstCollision();
            return false;
        }
        const core::u32 documentIndex = baker.addDocument(document);

        // The edition is the source of everything read out of it.
        knowledge::SourceV1 wire{};
        wire.id = work;
        // Chronicle: a contemporary account, honest and partial. It is the closest posture in
        // an enumeration written for archives, and it is the edition being described rather
        // than the ancient author — see the header on why that distinction is kept.
        wire.kind = static_cast<core::u32>(knowledge::SourceKindV1::Chronicle);
        // @warning UNKNOWN, not zero, and this was a real defect rather than a tidy-up. Zero means
        // "written the year it happened" -- an eyewitness, the strongest thing a source can be --
        // and a TEI file carries no composition date at all, so every classical work ingested
        // here was being scored as though its author had been present. Herodotus writing three
        // generations after Croesus got full temporal credit. The trust score now grants that
        // credit only where it is evidenced.
        wire.yearsAfterEvent = history::kUnknownYearsAfterEvent;
        // @warning **The soft join, and the two things it is allowed to do.** A TEI file carries no
        // composition date -- measured across Perseus -- but a bibliographic catalogue knows when
        // the author lived, and the two share no identifier. So:
        //
        //  - an EXACTLY matching name fills the window and says `WindowFromNameMatch`, because a
        //    marked hypothesis is usable and an unmarked one is indistinguishable from evidence;
        //  - a merely SIMILAR name fills nothing and records a candidate, because a score
        //    proposes and never decides;
        //  - and neither ever merges two records, since splitting is reversible and merging is
        //    not.
        //
        // With nowhere to look, the flag says somebody looked and found nothing -- a different
        // fact from nobody having looked, and one an empty window cannot express.
        bool dated = false;
        if (options.authorDates != nullptr && !author.empty())
        {
            const AuthorDateMatch found = options.authorDates->find(author, options.proposeThreshold);
            if (found.exact)
            {
                wire.composedFrom = found.fromDay;
                wire.composedTo = found.toDay;
                wire.flags |= knowledge::kSourceFlagWindowFromNameMatch;
                ++outReport.datedByNameMatch;
                dated = true;
            }
            else if (found.proposed)
            {
                // Recorded so the suspicion outlives the run. Two men called Jean Martin resemble
                // each other completely, so this is a question for somebody to answer rather than
                // a date to write.
                const core::u32 mine =
                    corpus::nameIdentifier(author.data(), static_cast<core::u32>(author.size()));
                const std::string &theirs = options.authorDates->at(found.index).name;
                baker.addCandidate(mine,
                                   corpus::nameIdentifier(theirs.data(),
                                                          static_cast<core::u32>(theirs.size())),
                                   found.scoreRaw,
                                   static_cast<core::u32>(Evidence::SimilarName));
                ++outReport.proposedByName;
            }
        }
        if (!dated)
            wire.flags |= knowledge::kSourceFlagUndated;
        wire.agreements = 0u;
        wire.name = titleId;
        wire.document = documentIndex;
        baker.addSource(wire);
        ++outReport.works;

        const core::u32 wholeWork = baker.addLocus(documentIndex, corpus::Locus{});

        if (!title.empty())
        {
            knowledge::FactV1 fact{};
            fact.subject = work;
            fact.predicate = kPredicateWorkTitle;
            fact.object = baker.addText(title);
            fact.source = work;
            fact.locus = wholeWork;
            fact.confidenceRaw = kCertain;
            baker.addFact(fact);
        }
        if (!author.empty())
        {
            const core::u32 who =
                corpus::nameIdentifier(author.data(), static_cast<core::u32>(author.size()));
            if (!baker.name(who, author))
            {
                outReport.firstCollision = baker.firstCollision();
                return false;
            }
            if (authors.insert(author).second)
                ++outReport.authors;
            knowledge::FactV1 fact{};
            fact.subject = work;
            fact.predicate = kPredicateAttributedTo;
            fact.object = who;
            fact.source = work;
            fact.locus = wholeWork;
            fact.confidenceRaw = kCertain;
            baker.addFact(fact);
        }
        if (!editor.empty())
        {
            const core::u32 who =
                corpus::nameIdentifier(editor.data(), static_cast<core::u32>(editor.size()));
            if (!baker.name(who, editor))
            {
                outReport.firstCollision = baker.firstCollision();
                return false;
            }
            knowledge::FactV1 fact{};
            fact.subject = work;
            fact.predicate = kPredicateEditedBy;
            fact.object = who;
            fact.source = work;
            fact.locus = wholeWork;
            fact.confidenceRaw = kCertain;
            baker.addFact(fact);
        }
        if (document.language != static_cast<core::u32>(corpus::LanguageTag::Unknown))
        {
            knowledge::FactV1 fact{};
            fact.subject = work;
            fact.predicate = kPredicateWrittenIn;
            fact.object = document.language;
            fact.source = work;
            fact.locus = wholeWork;
            fact.confidenceRaw = kCertain;
            baker.addFact(fact);
        }

        // ── The citation scheme the file DECLARES ─────────────────────────────
        // Read rather than inferred from the divs: `refsDecl n="CTS"` is the document saying
        // what its own levels are called, and a scheme guessed from the nesting would be this
        // reader's opinion of a book's citation system instead of the book's.
        {
            std::string scheme;
            XmlElement element{};
            std::size_t cursor = 0u;
            while (xmlNextElement(view, cursor, element))
            {
                cursor = element.end;
                if (element.closing || element.name != "cRefPattern")
                    continue;
                std::string level;
                if (xmlAttribute(element.tag, "n", level) && !level.empty())
                    scheme = scheme.empty() ? level : level + "/" + scheme;
            }
            if (!scheme.empty())
            {
                knowledge::FactV1 fact{};
                fact.subject = work;
                fact.predicate = kPredicateCitationScheme;
                fact.object = baker.addText(scheme);
                fact.source = work;
                fact.locus = wholeWork;
                fact.confidenceRaw = kCertain;
                baker.addFact(fact);
            }
        }

        // ── The passages ──────────────────────────────────────────────────────
        // A stack of `@n` values, one per open textpart. The citation of a passage is the
        // path from the root, which is exactly what CTS means by a passage reference.
        struct Open {
            std::string number;
            std::size_t contentBegin;
            std::size_t depth;
            /**
             * Whether a textpart was opened inside this one.
             *
             * @warning What tells a citable CONTAINER from a citable LEAF, and the distinction is
             * load-bearing for text: `textOf` over a chapter returns its sections' words too,
             * so carrying words for containers stores Herodotus once per book, once per
             * chapter and once per section — the same sentences asserted at three different
             * citations, and an image that grows with the square of the nesting.
             */
            bool hasChild{false};
        };
        std::vector<Open> stack;
        std::size_t depth = 0u;
        std::size_t cursor = 0u;
        XmlElement element{};

        while (xmlNextElement(view, cursor, element))
        {
            cursor = element.end;
            if (element.name != "div" || element.selfClosing)
                continue;

            if (element.closing)
            {
                if (depth != 0u)
                    --depth;
                if (!stack.empty() && stack.back().depth == depth)
                {
                    const Open closed = stack.back();
                    stack.pop_back();

                    std::string reference;
                    bool named = !closed.number.empty();
                    for (const Open &level : stack)
                    {
                        named = named && !level.number.empty();
                        reference += level.number + ".";
                    }
                    reference += closed.number;
                    if (!named)
                        ++outReport.unnumbered;

                    corpus::Locus locus{};
                    const bool addressable =
                        named &&
                        corpus::parsePassage(reference.data(), static_cast<core::u32>(reference.size()), locus);
                    if (!addressable)
                    {
                        ++outReport.unaddressable;
                        if (outReport.firstUnaddressable.empty())
                            outReport.firstUnaddressable = reference;
                    }

                    const core::u32 levels = static_cast<core::u32>(stack.size() + 1u);
                    if (levels > outReport.deepest)
                        outReport.deepest = levels;
                    if (levels > 3u)
                        ++outReport.tooDeep;

                    // Emitted for every textpart, container or leaf: a chapter is a citable
                    // passage in its own right, and a reader asking for book 1 chapter 10
                    // must find it whether or not the edition also subdivides it.
                    ++outReport.passages;
                    // @warning No identifier is minted for the passage, and that is the correction
                    // rather than an omission. A passage is a POSITION in a work, which is
                    // exactly what `FactV1::locus` says; giving it a subject of its own
                    // duplicated that, and the duplicate is what broke — `nameIdentifier` is
                    // 32 bits, Perseus carries some 700 000 passages, and two of them
                    // collided within seconds of the first full run. The subject is the WORK
                    // and the locus is the passage, so the vocabulary now holds works,
                    // authors and titles only: thousands of names instead of a million.
                    const core::u32 passage = baker.addLocus(documentIndex, locus);
                    const core::u32 where = addressable ? passage : wholeWork;

                    // Every textpart is CITABLE — a reader asking for book 1 chapter 2 must
                    // find it — but only a leaf carries words, for the reason on Open::hasChild.
                    if (options.carryMentions && !closed.hasChild)
                        scanMentions(view, closed.contentBegin, element.begin, work, where, baker, outReport);

                    if (options.carryText && !closed.hasChild)
                    {
                        std::string words = xmlTextOf(view, closed.contentBegin, element.begin);
                        if (words.size() > options.maxPassageBytes)
                        {
                            words.resize(options.maxPassageBytes);
                            ++outReport.truncated;
                        }
                        if (!words.empty())
                        {
                            knowledge::FactV1 text{};
                            text.subject = work;
                            text.predicate = kPredicatePassageText;
                            text.object = baker.addText(words);
                            text.source = work;
                            text.locus = where;
                            text.confidenceRaw = kCertain;
                            baker.addFact(text);
                            ++outReport.textLines;
                        }
                    }
                }
                continue;
            }

            std::string type;
            const bool textpart = xmlAttribute(element.tag, "type", type) && type == "textpart";
            ++depth;
            if (!textpart)
                continue;

            std::string number;
            // @warning A textpart with no `@n` is NOT ordinal zero. It was given `"0"` here at first,
            // which invented a citation for 2725 divisions of the real Perseus corpus —
            // prefaces, arguments, front matter — that their editions deliberately do not
            // number. An empty reference is carried through and refused downstream by
            // `parsePassage`, which is the same treatment `10A` gets and for the same reason:
            // a citation nobody wrote is worse than no citation.
            if (!xmlAttribute(element.tag, "n", number))
                number.clear();
            if (!stack.empty())
                stack.back().hasChild = true;
            stack.push_back(Open{number, element.end, depth - 1u, false});
        }
    }

    return true;
}

} // namespace lpl::harvest
