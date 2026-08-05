/**
 * @file Urn.cpp
 * @brief Implementation of canonical text identifiers (CTS URNs and friends).
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/corpus/Urn.hpp>

namespace lpl::corpus {

namespace {

constexpr core::u32 kFnv1aOffsetBasis = 0x811C9DC5u;
constexpr core::u32 kFnv1aPrime = 0x01000193u;

/**
 * @brief Does a byte range begin with a literal?
 *
 * @param text   First byte.
 * @param bytes  Its length.
 * @param prefix NUL-terminated literal.
 * @return Length of the prefix when it matches, 0 otherwise.
 */
[[nodiscard]] core::u32 startsWith(const char *text, core::u32 bytes, const char *prefix) noexcept
{
    core::u32 i = 0u;
    while (prefix[i] != '\0')
    {
        if (i >= bytes || text[i] != prefix[i])
            return 0u;
        ++i;
    }
    return i;
}

/**
 * @brief Folds a byte range into a running FNV-1a hash.
 *
 * @param hash  Running value, updated in place.
 * @param text  First byte.
 * @param bytes How many.
 */
void foldRange(core::u32 &hash, const char *text, core::u32 bytes) noexcept
{
    for (core::u32 i = 0u; i < bytes; ++i)
        hash = (hash ^ static_cast<core::u32>(static_cast<core::u8>(text[i]))) * kFnv1aPrime;
}

} // namespace

bool parseUrn(const char *text, core::u32 bytes, Urn &out) noexcept
{
    out = Urn{};
    if (text == nullptr || bytes == 0u)
        return false;

    // "urn:cts:" is matched literally, in lower case. The URN specification makes the
    // scheme and the namespace identifier case-insensitive, so a strict match rejects a
    // legal spelling — accepted deliberately, because case-folding here would need a
    // locale-independent lower-case that this module has no libc to borrow, and every
    // corpus this project reads writes them lower case.
    core::u32 cursor = startsWith(text, bytes, "urn:cts:");
    if (cursor == 0u)
        return false;

    const core::u32 namespaceStart = cursor;
    while (cursor < bytes && text[cursor] != ':')
        ++cursor;
    if (cursor == namespaceStart || cursor >= bytes)
        return false; // a namespace with no work after it names nothing

    out.namespaceText = text + namespaceStart;
    out.namespaceBytes = cursor - namespaceStart;
    ++cursor; // the colon

    const core::u32 workStart = cursor;
    while (cursor < bytes && text[cursor] != ':')
        ++cursor;
    if (cursor == workStart)
        return false;

    out.workText = text + workStart;
    out.workBytes = cursor - workStart;

    if (cursor < bytes)
    {
        ++cursor; // the colon
        out.passageText = text + cursor;
        out.passageBytes = bytes - cursor;
    }

    return true;
}

core::u32 workIdentifier(const Urn &urn) noexcept
{
    // The namespace is folded as well as the work, with the separator between them: two
    // corpora are free to use the same work identifier locally, and dropping the namespace
    // would silently merge them.
    core::u32 hash = kFnv1aOffsetBasis;
    foldRange(hash, urn.namespaceText, urn.namespaceBytes);
    foldRange(hash, ":", 1u);
    foldRange(hash, urn.workText, urn.workBytes);

    // Zero is reserved for "none", so it is displaced rather than allowed. One collision
    // in four billion is moved onto its neighbour, which is a smaller price than an
    // identifier that reads as absent.
    return hash == 0u ? 1u : hash;
}

core::u32 nameIdentifier(const char *text, core::u32 bytes) noexcept
{
    core::u32 hash = kFnv1aOffsetBasis;
    if (text != nullptr)
        foldRange(hash, text, bytes);
    return hash == 0u ? 1u : hash;
}

bool resolveUrn(const char *text, core::u32 bytes, core::u32 &outWork, Locus &outLocus) noexcept
{
    outWork = 0u;
    outLocus = Locus{};

    Urn urn{};
    if (!parseUrn(text, bytes, urn))
        return false;

    outWork = workIdentifier(urn);

    // A URN with no passage is a reference to the whole work, which is legitimate and
    // leaves the locus zeroed. A URN with a MALFORMED passage is not: it names a position
    // that cannot be found, and accepting it as a whole-work reference would quietly widen
    // a citation from one line to a whole chronicle.
    if (urn.passageBytes == 0u)
        return true;

    return parsePassage(urn.passageText, urn.passageBytes, outLocus);
}

} // namespace lpl::corpus
