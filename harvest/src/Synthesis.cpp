/**
 * @file Synthesis.cpp
 * @brief Implementation of reading prose into claims, and refusing what it did not say.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Synthesis.hpp>

#include <lpl/corpus/Urn.hpp>

#include <set>

namespace lpl::harvest {

namespace {

/// One as a raw Q16.16 word.
constexpr core::u32 kOne = 65536u;

/**
 * @brief Lower-cases an ASCII byte.
 *
 * @param c The byte.
 * @return Its lower-case form when it is an ASCII letter, otherwise itself.
 */
[[nodiscard]] char lowered(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/**
 * @brief Is this byte whitespace?
 *
 * @param c The byte.
 * @return true for space, tab, carriage return and newline.
 */
[[nodiscard]] bool space(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/**
 * @brief Lower-cases and collapses runs of whitespace to one space.
 *
 * The whole of the normalisation, and it stops exactly there. Re-wrapping a line or changing
 * its case is not fabrication, so those differences are forgiven; anything more — stemming,
 * synonyms, dropped punctuation — starts forgiving differences that ARE fabrication, and a
 * grounding check that can be talked into a match is not one.
 *
 * @param text The text.
 * @return Its normalised form.
 */
[[nodiscard]] std::string normalised(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    bool pending = false;
    for (char c : text)
    {
        if (space(c))
        {
            pending = !out.empty();
            continue;
        }
        if (pending)
        {
            out.push_back(' ');
            pending = false;
        }
        out.push_back(lowered(c));
    }
    return out;
}

} // namespace

const char *groundingText(Grounding grounding) noexcept
{
    switch (grounding)
    {
    case Grounding::Admitted:
        return "admitted";
    case Grounding::NoQuotation:
        return "no quotation";
    case Grounding::QuotationAbsent:
        return "quotation absent";
    case Grounding::SubjectAbsent:
        return "subject absent";
    case Grounding::ObjectAbsent:
        return "object absent";
    case Grounding::Incomplete:
        return "incomplete";
    case Grounding::ImpossibleWindow:
        return "impossible window";
    case Grounding::OverConfident:
        return "over confident";
    }
    return "unknown";
}

Grounding groundClaim(const Passage &passage, const ProposedClaim &proposal) noexcept
{
    if (proposal.subject.empty() || proposal.predicate.empty() || proposal.object.empty())
        return Grounding::Incomplete;
    if (proposal.toDay < proposal.fromDay)
        return Grounding::ImpossibleWindow;
    if (proposal.confidenceRaw > kOne)
        return Grounding::OverConfident;
    if (proposal.quotation.empty())
        return Grounding::NoQuotation;

    const std::string haystack = normalised(passage.text);
    const std::string quotation = normalised(proposal.quotation);
    if (quotation.empty() || haystack.find(quotation) == std::string::npos)
        return Grounding::QuotationAbsent;

    // Both ends of the claim must sit inside the quoted span, not merely somewhere in the
    // passage. A passage naming two people lets a reader quote a sentence about one and
    // attribute it to the other, and the result is a perfectly-cited fact about the wrong
    // man — which nothing downstream can detect.
    if (quotation.find(normalised(proposal.subject)) == std::string::npos)
        return Grounding::SubjectAbsent;
    if (quotation.find(normalised(proposal.object)) == std::string::npos)
        return Grounding::ObjectAbsent;

    return Grounding::Admitted;
}

bool synthesise(const std::vector<Passage> &passages, IClaimReader &reader, std::vector<SynthesisedClaim> &out,
                SynthesisReport &report)
{
    out.clear();
    report = SynthesisReport{};

    std::set<core::u32> works;
    std::vector<ProposedClaim> proposals;

    for (const Passage &passage : passages)
    {
        ++report.passages;

        // One passage at a time, with nothing carried over. A reader that accumulated
        // context between passages would make the corpus a function of the walk order, and
        // two machines harvesting the same tree would then disagree about history.
        proposals.clear();
        reader.read(passage, proposals);

        if (proposals.size() > kMaxProposalsPerPassage)
            return false;

        for (const ProposedClaim &proposal : proposals)
        {
            ++report.proposed;
            const Grounding grounding = groundClaim(passage, proposal);
            ++report.refusals[static_cast<core::u32>(grounding)];
            if (grounding != Grounding::Admitted)
            {
                ++report.refused;
                continue;
            }
            ++report.admitted;
            works.insert(passage.work);
            out.push_back(SynthesisedClaim{proposal, passage.work, passage.line, passage.heading});
        }
    }

    report.works = static_cast<core::u32>(works.size());
    return true;
}

void testimoniesFor(const std::vector<SynthesisedClaim> &claims, std::vector<Testimony> &out)
{
    out.clear();
    out.reserve(claims.size());
    for (const SynthesisedClaim &entry : claims)
    {
        // The claim's identity is its three parts together, joined by a byte that cannot
        // occur in any of them. Hashing them one after another without a separator would let
        // ("ab", "c", …) and ("a", "bc", …) collide, which is a claim about one thing
        // silently corroborating a claim about another.
        const std::string key = normalised(entry.claim.subject) + '\x1f' + normalised(entry.claim.predicate) +
                                '\x1f' + normalised(entry.claim.object);
        out.push_back(Testimony{corpus::nameIdentifier(key.data(), static_cast<core::u32>(key.size())),
                                entry.work});
    }
}

} // namespace lpl::harvest
