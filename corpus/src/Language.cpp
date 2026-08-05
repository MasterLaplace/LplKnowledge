/**
 * @file Language.cpp
 * @brief Implementation of ancient and medieval language tags, and what they imply.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/corpus/Language.hpp>

namespace lpl::corpus {

namespace {

/**
 * @struct NamedTag
 * @brief One tag and the word that names it on the wire.
 */
struct NamedTag {
    const char *name;
    LanguageTag tag;
};

/**
 * The word for each tag.
 *
 * A table rather than a switch, because both directions are needed and a switch would
 * have been written twice — which is how a name and its parse drift apart. The words are
 * BCP-47 subtags where one exists (`la`, `grc`, `fro`, `enm`), so a document already
 * tagged by a library catalogue needs no translation table of ours; `la-eccl` is an
 * extension, and it is one because no registered subtag distinguishes the stage.
 */
constexpr NamedTag kTags[] = {
    {"la", LanguageTag::Latin},
    {"la-eccl", LanguageTag::EcclesiasticalLatin},
    {"grc", LanguageTag::AncientGreek},
    {"fro", LanguageTag::OldFrench},
    {"enm", LanguageTag::MiddleEnglish},
    {"fr", LanguageTag::ModernFrench},
    {"en", LanguageTag::ModernEnglish},
};

constexpr core::u32 kTagCount = sizeof(kTags) / sizeof(kTags[0]);

} // namespace

const char *languageName(LanguageTag tag) noexcept
{
    for (core::u32 i = 0u; i < kTagCount; ++i)
        if (kTags[i].tag == tag)
            return kTags[i].name;
    return "unknown";
}

bool languageByName(const char *text, core::u32 bytes, LanguageTag &out) noexcept
{
    out = LanguageTag::Unknown;
    if (text == nullptr || bytes == 0u)
        return false;

    for (core::u32 i = 0u; i < kTagCount; ++i)
    {
        const char *name = kTags[i].name;
        core::u32 length = 0u;
        while (name[length] != '\0')
            ++length;
        if (length != bytes)
            continue;

        bool same = true;
        for (core::u32 j = 0u; same && j < bytes; ++j)
            same = text[j] == name[j];
        if (same)
        {
            out = kTags[i].tag;
            return true;
        }
    }
    return false;
}

} // namespace lpl::corpus
