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
 * @brief One tag, one word that names it, and what the word settles about its era.
 *
 * @warning **One table, and it used to be two.** `corpus::kTags` held seven BCP-47 subtags while
 * `harvest::languageOf` held its own if-chain that knew `lat`, `fre`, `eng` -- three-letter
 * ISO 639-2 codes the first had never heard of. Two answers to "what language is this", already
 * disagreeing about which words are acceptable, and a catalogue read through one and queried
 * through the other. Everything now reads this.
 */
struct NamedTag {
    const char *name;
    LanguageTag tag;
    LanguageEra era;
    bool canonical; ///< The word @ref languageName gives back; aliases are read, never written.
};

/**
 * Every word this project accepts for a language, and what it implies.
 *
 * @warning **Aliases are accepted and never emitted.** A catalogue writes `lat`, another writes
 * `la`, a third writes `latin`; all three name one language, and a reader that knew only one of
 * them would silently drop two thirds of a corpus. But an image must carry ONE spelling or two
 * runs over the same source would bake different bytes -- so exactly one row per tag is
 * canonical, and `languageName` returns that one.
 *
 * @warning `el` is MODERN Greek and `grc` is ancient. Fifteen centuries apart, told apart by
 * nothing but these two rows: a first version mapped `el` to @ref LanguageTag::AncientGreek and
 * the catalogue then claimed 216 works of ancient Greek from a source that declares `grc` zero
 * times.
 */
constexpr NamedTag kTags[] = {
    {"la", LanguageTag::Latin, LanguageEra::Ancient, true},
    {"lat", LanguageTag::Latin, LanguageEra::Ancient, false},
    {"la-eccl", LanguageTag::EcclesiasticalLatin, LanguageEra::Medieval, true},
    {"grc", LanguageTag::AncientGreek, LanguageEra::Ancient, true},
    {"fro", LanguageTag::OldFrench, LanguageEra::Medieval, true},
    {"enm", LanguageTag::MiddleEnglish, LanguageEra::Medieval, true},
    {"fr", LanguageTag::ModernFrench, LanguageEra::Modern, true},
    {"fre", LanguageTag::ModernFrench, LanguageEra::Modern, false},
    {"fra", LanguageTag::ModernFrench, LanguageEra::Modern, false},
    {"en", LanguageTag::ModernEnglish, LanguageEra::Modern, true},
    {"eng", LanguageTag::ModernEnglish, LanguageEra::Modern, false},
    {"sa", LanguageTag::Sanskrit, LanguageEra::Ancient, true},
    {"san", LanguageTag::Sanskrit, LanguageEra::Ancient, false},
    {"pi", LanguageTag::Pali, LanguageEra::Ancient, true},
    {"pli", LanguageTag::Pali, LanguageEra::Ancient, false},
    {"lzh", LanguageTag::ClassicalChinese, LanguageEra::Ancient, true},
    {"cop", LanguageTag::Coptic, LanguageEra::Ancient, true},
    {"syc", LanguageTag::Syriac, LanguageEra::Ancient, true},
    {"syr", LanguageTag::Syriac, LanguageEra::Ancient, false},
    {"arc", LanguageTag::Aramaic, LanguageEra::Ancient, true},
    {"akk", LanguageTag::Akkadian, LanguageEra::Ancient, true},
    {"gez", LanguageTag::Geez, LanguageEra::Ancient, true},
    {"hbo", LanguageTag::AncientHebrew, LanguageEra::Ancient, true},
    {"peo", LanguageTag::OldPersian, LanguageEra::Ancient, true},
    {"got", LanguageTag::Gothic, LanguageEra::Ancient, true},
    {"ang", LanguageTag::OldEnglish, LanguageEra::Medieval, true},
    {"non", LanguageTag::OldNorse, LanguageEra::Medieval, true},
    {"sga", LanguageTag::OldIrish, LanguageEra::Medieval, true},
    {"cu", LanguageTag::ChurchSlavonic, LanguageEra::Medieval, true},
    {"chu", LanguageTag::ChurchSlavonic, LanguageEra::Medieval, false},
    {"he", LanguageTag::Hebrew, LanguageEra::Unspecified, true},
    {"heb", LanguageTag::Hebrew, LanguageEra::Unspecified, false},
    {"ar", LanguageTag::Arabic, LanguageEra::Unspecified, true},
    {"ara", LanguageTag::Arabic, LanguageEra::Unspecified, false},
    {"arb", LanguageTag::Arabic, LanguageEra::Unspecified, false},
    {"fa", LanguageTag::Persian, LanguageEra::Unspecified, true},
    {"per", LanguageTag::Persian, LanguageEra::Unspecified, false},
    {"fas", LanguageTag::Persian, LanguageEra::Unspecified, false},
    {"zh", LanguageTag::Chinese, LanguageEra::Unspecified, true},
    {"chi", LanguageTag::Chinese, LanguageEra::Unspecified, false},
    {"zho", LanguageTag::Chinese, LanguageEra::Unspecified, false},
    {"bo", LanguageTag::Tibetan, LanguageEra::Unspecified, true},
    {"tib", LanguageTag::Tibetan, LanguageEra::Unspecified, false},
    {"bod", LanguageTag::Tibetan, LanguageEra::Unspecified, false},
    {"hy", LanguageTag::Armenian, LanguageEra::Unspecified, true},
    {"arm", LanguageTag::Armenian, LanguageEra::Unspecified, false},
    {"hye", LanguageTag::Armenian, LanguageEra::Unspecified, false},
    {"ka", LanguageTag::Georgian, LanguageEra::Unspecified, true},
    {"geo", LanguageTag::Georgian, LanguageEra::Unspecified, false},
    {"kat", LanguageTag::Georgian, LanguageEra::Unspecified, false},
    {"tr", LanguageTag::Turkish, LanguageEra::Unspecified, true},
    {"tur", LanguageTag::Turkish, LanguageEra::Unspecified, false},
    {"ota", LanguageTag::Turkish, LanguageEra::Unspecified, false},
    {"ja", LanguageTag::Japanese, LanguageEra::Unspecified, true},
    {"jpn", LanguageTag::Japanese, LanguageEra::Unspecified, false},
    {"ko", LanguageTag::Korean, LanguageEra::Unspecified, true},
    {"kor", LanguageTag::Korean, LanguageEra::Unspecified, false},
    {"de", LanguageTag::German, LanguageEra::Modern, true},
    {"ger", LanguageTag::German, LanguageEra::Modern, false},
    {"deu", LanguageTag::German, LanguageEra::Modern, false},
    {"fi", LanguageTag::Finnish, LanguageEra::Modern, true},
    {"fin", LanguageTag::Finnish, LanguageEra::Modern, false},
    {"it", LanguageTag::Italian, LanguageEra::Modern, true},
    {"ita", LanguageTag::Italian, LanguageEra::Modern, false},
    {"nl", LanguageTag::Dutch, LanguageEra::Modern, true},
    {"dut", LanguageTag::Dutch, LanguageEra::Modern, false},
    {"nld", LanguageTag::Dutch, LanguageEra::Modern, false},
    {"es", LanguageTag::Spanish, LanguageEra::Modern, true},
    {"spa", LanguageTag::Spanish, LanguageEra::Modern, false},
    {"hu", LanguageTag::Hungarian, LanguageEra::Modern, true},
    {"hun", LanguageTag::Hungarian, LanguageEra::Modern, false},
    {"pt", LanguageTag::Portuguese, LanguageEra::Modern, true},
    {"por", LanguageTag::Portuguese, LanguageEra::Modern, false},
    {"sv", LanguageTag::Swedish, LanguageEra::Modern, true},
    {"swe", LanguageTag::Swedish, LanguageEra::Modern, false},
    {"el", LanguageTag::ModernGreek, LanguageEra::Modern, true},
    {"gre", LanguageTag::ModernGreek, LanguageEra::Modern, false},
    {"ell", LanguageTag::ModernGreek, LanguageEra::Modern, false},
    {"eo", LanguageTag::Esperanto, LanguageEra::Modern, true},
    {"epo", LanguageTag::Esperanto, LanguageEra::Modern, false},
    {"ca", LanguageTag::Catalan, LanguageEra::Modern, true},
    {"cat", LanguageTag::Catalan, LanguageEra::Modern, false},
    {"da", LanguageTag::Danish, LanguageEra::Modern, true},
    {"dan", LanguageTag::Danish, LanguageEra::Modern, false},
    {"tl", LanguageTag::Tagalog, LanguageEra::Modern, true},
    {"tgl", LanguageTag::Tagalog, LanguageEra::Modern, false},
    {"pl", LanguageTag::Polish, LanguageEra::Modern, true},
    {"pol", LanguageTag::Polish, LanguageEra::Modern, false},
    {"no", LanguageTag::Norwegian, LanguageEra::Modern, true},
    {"nor", LanguageTag::Norwegian, LanguageEra::Modern, false},
    {"nb", LanguageTag::Norwegian, LanguageEra::Modern, false},
    {"nn", LanguageTag::Norwegian, LanguageEra::Modern, false},
    {"fy", LanguageTag::Frisian, LanguageEra::Modern, true},
    {"fry", LanguageTag::Frisian, LanguageEra::Modern, false},
    {"af", LanguageTag::Afrikaans, LanguageEra::Modern, true},
    {"afr", LanguageTag::Afrikaans, LanguageEra::Modern, false},
    {"cy", LanguageTag::Welsh, LanguageEra::Modern, true},
    {"wel", LanguageTag::Welsh, LanguageEra::Modern, false},
    {"cym", LanguageTag::Welsh, LanguageEra::Modern, false},
    {"cs", LanguageTag::Czech, LanguageEra::Modern, true},
    {"cze", LanguageTag::Czech, LanguageEra::Modern, false},
    {"ces", LanguageTag::Czech, LanguageEra::Modern, false},
    {"ru", LanguageTag::Russian, LanguageEra::Modern, true},
    {"rus", LanguageTag::Russian, LanguageEra::Modern, false},
    {"is", LanguageTag::Icelandic, LanguageEra::Modern, true},
    {"ice", LanguageTag::Icelandic, LanguageEra::Modern, false},
    {"isl", LanguageTag::Icelandic, LanguageEra::Modern, false},
    {"te", LanguageTag::Telugu, LanguageEra::Modern, true},
    {"tel", LanguageTag::Telugu, LanguageEra::Modern, false},
    {"fur", LanguageTag::Friulian, LanguageEra::Modern, true},
    {"bg", LanguageTag::Bulgarian, LanguageEra::Modern, true},
    {"bul", LanguageTag::Bulgarian, LanguageEra::Modern, false},
    {"sl", LanguageTag::Slovenian, LanguageEra::Modern, true},
    {"slv", LanguageTag::Slovenian, LanguageEra::Modern, false},
    {"ro", LanguageTag::Romanian, LanguageEra::Modern, true},
    {"rum", LanguageTag::Romanian, LanguageEra::Modern, false},
    {"ron", LanguageTag::Romanian, LanguageEra::Modern, false},
    {"ga", LanguageTag::Irish, LanguageEra::Modern, true},
    {"gle", LanguageTag::Irish, LanguageEra::Modern, false},
    {"sr", LanguageTag::Serbian, LanguageEra::Modern, true},
    {"srp", LanguageTag::Serbian, LanguageEra::Modern, false},
    {"gl", LanguageTag::Galician, LanguageEra::Modern, true},
    {"glg", LanguageTag::Galician, LanguageEra::Modern, false},
    {"ceb", LanguageTag::Cebuano, LanguageEra::Modern, true},
    {"ilo", LanguageTag::Ilocano, LanguageEra::Modern, true},
    {"gd", LanguageTag::ScottishGaelic, LanguageEra::Modern, true},
    {"gla", LanguageTag::ScottishGaelic, LanguageEra::Modern, false},
};

constexpr core::u32 kTagCount = sizeof(kTags) / sizeof(kTags[0]);

} // namespace

const char *languageName(LanguageTag tag) noexcept
{
    for (core::u32 i = 0u; i < kTagCount; ++i)
        if (kTags[i].tag == tag && kTags[i].canonical)
            return kTags[i].name;
    return "unknown";
}

LanguageEra languageEra(LanguageTag tag) noexcept
{
    for (core::u32 i = 0u; i < kTagCount; ++i)
        if (kTags[i].tag == tag && kTags[i].canonical)
            return kTags[i].era;
    return LanguageEra::Unspecified;
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
