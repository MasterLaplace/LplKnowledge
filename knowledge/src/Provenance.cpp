/**
 * @file Provenance.cpp
 * @brief Implementation of who asserted this, and how we came to hold it.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/knowledge/Provenance.hpp>

namespace lpl::knowledge {

namespace {

/// A word per source kind, in the same order the enum declares them.
constexpr const char *kKindText[] = {"notarial", "archaeology", "administrative", "chronicle", "panegyric"};

/**
 * @brief A word for a wire source kind.
 *
 * Bounds-checked rather than indexed, because the value comes from an untrusted image: a
 * byte naming a sixth kind must read as unknown, not index past the table.
 *
 * @param kind The wire value.
 * @return A short, stable string.
 */
[[nodiscard]] const char *kindText(core::u32 kind) noexcept
{
    if (kind >= static_cast<core::u32>(SourceKindV1::Count))
        return "unknown";
    return kKindText[kind];
}

/**
 * @brief Appends a NUL-terminated literal, stopping at the cap.
 *
 * @param out      Destination.
 * @param capacity Room in @p out, NUL included.
 * @param cursor   Current length, advanced in place.
 * @param text     What to append.
 */
void appendText(char *out, core::u32 capacity, core::u32 &cursor, const char *text) noexcept
{
    while (text != nullptr && *text != '\0' && cursor + 1u < capacity)
        out[cursor++] = *text++;
}

/**
 * @brief Appends an unsigned decimal, stopping at the cap.
 *
 * @param out      Destination.
 * @param capacity Room in @p out, NUL included.
 * @param cursor   Current length, advanced in place.
 * @param value    What to append.
 */
void appendNumber(char *out, core::u32 capacity, core::u32 &cursor, core::u32 value) noexcept
{
    char digits[10];
    core::u32 count = 0u;
    do
    {
        digits[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < 10u);

    while (count > 0u && cursor + 1u < capacity)
        out[cursor++] = digits[--count];
}

} // namespace

bool cite(const KnowledgePack &pack, const FactV1 &fact, Citation &out) noexcept
{
    out = Citation{};

    if (!pack.sourceById(fact.source, out.source))
        return false;

    out.sourceName = pack.textFor(out.source.name);

    if (fact.locus != kNoIdentifier)
    {
        // A locus index is one-based on the wire so that zero can mean "none" — the same
        // reason kNoIdentifier is zero. Converting here, once, is what keeps that off
        // every call site.
        const core::u32 index = fact.locus - 1u;
        if (pack.locusAt(index, out.locus))
        {
            out.hasLocus = 1u;
            if (pack.documentAt(out.locus.document, out.document))
            {
                out.hasDocument = 1u;
                out.documentName = pack.textFor(out.document.urn);
            }
        }
    }

    // A source may name a work without any individual claim naming a line in it, which is
    // how a whole-document citation is expressed. Only consulted when the locus did not
    // already answer the question.
    if (out.hasDocument == 0u && out.source.document != kNoIdentifier)
    {
        if (pack.documentAt(out.source.document - 1u, out.document))
        {
            out.hasDocument = 1u;
            out.documentName = pack.textFor(out.document.urn);
        }
    }

    return true;
}

core::u32 renderCitation(const Citation &citation, char *out, core::u32 capacity) noexcept
{
    if (out == nullptr || capacity == 0u)
        return 0u;

    core::u32 cursor = 0u;

    appendText(out, capacity, cursor, citation.sourceName != nullptr ? citation.sourceName : "source#");
    if (citation.sourceName == nullptr)
        appendNumber(out, capacity, cursor, citation.source.id);

    appendText(out, capacity, cursor, " (");
    appendText(out, capacity, cursor, kindText(citation.source.kind));
    appendText(out, capacity, cursor, ", +");
    appendNumber(out, capacity, cursor, citation.source.yearsAfterEvent);
    appendText(out, capacity, cursor, "y)");

    if (citation.hasDocument != 0u)
    {
        appendText(out, capacity, cursor, " — ");
        appendText(out, capacity, cursor, citation.documentName != nullptr ? citation.documentName : "document");
        if ((citation.document.flags & kDocumentFlagTranslation) != 0u)
            appendText(out, capacity, cursor, " [translation]");
    }

    if (citation.hasLocus != 0u)
    {
        // Rendered the way the corpus is cited: part.section.line for a work, and just
        // the line for a plain-text document, whose part and section are not ordinals a
        // reader would recognise.
        appendText(out, capacity, cursor, " ");
        if ((citation.document.flags & kDocumentFlagPlainText) == 0u)
        {
            appendNumber(out, capacity, cursor, citation.locus.part);
            appendText(out, capacity, cursor, ".");
            appendNumber(out, capacity, cursor, citation.locus.section);
            appendText(out, capacity, cursor, ".");
        }
        else
        {
            appendText(out, capacity, cursor, "line ");
        }
        appendNumber(out, capacity, cursor, citation.locus.line);
    }

    out[cursor] = '\0';
    return cursor;
}

bool auditProvenance(const KnowledgePack &pack, ProvenanceAudit &out) noexcept
{
    out = ProvenanceAudit{};

    const core::u32 documents = pack.documentCount();
    const core::u32 loci = pack.locusCount();

    out.facts = pack.factCount();
    for (core::u32 i = 0u; i < out.facts; ++i)
    {
        FactV1 fact{};
        if (!pack.factAt(i, fact))
            continue;

        SourceV1 source{};
        if (!pack.sourceById(fact.source, source))
        {
            ++out.unsourced;
            continue;
        }

        if (fact.locus == kNoIdentifier)
        {
            ++out.unlocated;
            continue;
        }

        const core::u32 index = fact.locus - 1u;
        if (index >= loci)
        {
            ++out.danglingLocus;
            continue;
        }

        LocusV1 locus{};
        if (pack.locusAt(index, locus) && locus.document >= documents)
            ++out.danglingDocument;
    }

    for (core::u32 i = 0u; i < pack.sourceCount(); ++i)
    {
        SourceV1 source{};
        if (!pack.sourceAt(i, source))
            continue;
        if (pack.textFor(source.name) == nullptr)
            ++out.unnamed;
        if (source.document != kNoIdentifier && source.document - 1u >= documents)
            ++out.danglingDocument;
    }

    return out.unsourced == 0u && out.danglingLocus == 0u && out.danglingDocument == 0u;
}

core::u32 foldAudit(const ProvenanceAudit &audit) noexcept
{
    core::u32 hash = kFnv1aOffsetBasis;
    foldWord(hash, audit.facts);
    foldWord(hash, audit.unsourced);
    foldWord(hash, audit.unlocated);
    foldWord(hash, audit.danglingLocus);
    foldWord(hash, audit.danglingDocument);
    foldWord(hash, audit.unnamed);
    return hash;
}

} // namespace lpl::knowledge
