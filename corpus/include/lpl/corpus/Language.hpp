/**
 * @file Language.hpp
 * @brief Ancient and medieval language tags, and what they imply.
 *
 * Old French, Middle English, ecclesiastical Latin: the tag drives tokenisation
 * and, more importantly, warns that a modern translation is a copyrighted
 * derivative and not the source.
 *
 * A tag crosses into an image as a WORD, never as an index. That rule is already paid
 * for elsewhere in the project — `caveKindName`/`caveKindByName` exist because an index
 * means whatever the enumeration happened to be worth the day the document was written,
 * so reordering an enum silently reinterprets everything already on disk. An unknown
 * word is REFUSED rather than defaulted, for the same reason: a corpus quietly relabelled
 * from Old French to modern French has lost the one fact that says whether its text may
 * be redistributed.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_CORPUS_LANGUAGE_HPP
#    define LPL_LPL_CORPUS_LANGUAGE_HPP

#    include <lpl/Foundation.hpp>

namespace lpl::corpus {

/**
 * @enum LanguageTag
 * @brief What a text is written in.
 *
 * Historical stages are separate values rather than one "Latin" with a date, because the
 * stage is what changes how a text is read: ecclesiastical Latin spells and abbreviates
 * differently from classical, and a tokeniser that treats them alike mis-segments both.
 * @warning The list holds what this project has a corpus for, and it GREW because the corpus did.
 * Measured on Project Gutenberg: 79 179 documents declare a language, in **58 distinct codes**,
 * and eight tags could name six of them -- so 12 042 works came back "unknown", including every
 * work in Finnish, German, Italian, Dutch, Spanish and Chinese. An enumeration too small does not
 * fail loudly; it answers "unknown" to a question the source already answered.
 *
 * @warning Values 0 to 7 are FROZEN in their original order. A tag travels into an image as a word
 * rather than an index, so reordering would not corrupt a document -- but the parity blob and the
 * gate P18 fold read these, and there is no reason to move a number that is already correct.
 *
 * The ancient stages are taken from this project's own inventory of repositories
 * (`store/LplKnowledge/Inventaire_Depots_Textes_Anciens.md`) rather than invented: Syriac,
 * Sanskrit, Persian, Georgian, Coptic, Chinese, Armenian, Arabic, Tibetan, Hebrew, Ge'ez,
 * Aramaic, Pali. They are named before their corpus lands because a missing tag is silent --
 * an unnamed language reads exactly like an untagged document.
 */
enum class LanguageTag : core::u32 {
    Unknown = 0u,

    // ── Frozen: the original eight ──────────────────────────────────────────
    Latin = 1u,               ///< Classical Latin.
    EcclesiasticalLatin = 2u, ///< Medieval church Latin: different orthography, heavy abbreviation.
    AncientGreek = 3u,        ///< Polytonic, and the reason a byte offset is a poor citation.
    OldFrench = 4u,
    MiddleEnglish = 5u,
    ModernFrench = 6u,
    ModernEnglish = 7u,

    // ── Ancient and classical: what the historical corpus is written in ─────
    Sanskrit = 8u,
    Pali = 9u,
    ClassicalChinese = 10u, ///< `lzh`. Distinct from modern Chinese by more than orthography.
    Coptic = 11u,
    Syriac = 12u,
    Aramaic = 13u,
    Akkadian = 14u,
    Geez = 15u,      ///< Classical Ethiopic.
    AncientHebrew = 16u,
    OldPersian = 17u,
    Gothic = 18u,
    OldEnglish = 19u,
    OldNorse = 20u,
    OldIrish = 21u,
    ChurchSlavonic = 22u,

    // ── Living languages whose historical stage this project does not separate ──
    // @warning No era is claimed for these: Arabic, Hebrew, Persian, Chinese and the rest are
    // written today AND were written two thousand years ago, and one tag cannot say which. See
    // @ref LanguageEra::Unspecified -- guessing here would be a licence judgement made by a
    // lookup table.
    Hebrew = 23u,
    Arabic = 24u,
    Persian = 25u,
    Chinese = 26u,
    Tibetan = 27u,
    Armenian = 28u,
    Georgian = 29u,
    Turkish = 30u,
    Japanese = 31u,
    Korean = 32u,

    // ── Modern vernaculars, ordered by what the measured corpus actually holds ──
    German = 33u,
    Finnish = 34u,
    Italian = 35u,
    Dutch = 36u,
    Spanish = 37u,
    Hungarian = 38u,
    Portuguese = 39u,
    Swedish = 40u,
    ModernGreek = 41u, ///< `el`. Fifteen centuries from @ref AncientGreek; see Language.cpp.
    Esperanto = 42u,
    Catalan = 43u,
    Danish = 44u,
    Tagalog = 45u,
    Polish = 46u,
    Norwegian = 47u,
    Frisian = 48u,
    Afrikaans = 49u,
    Welsh = 50u,
    Czech = 51u,
    Russian = 52u,
    Icelandic = 53u,
    Telugu = 54u,
    Friulian = 55u,
    Bulgarian = 56u,
    Slovenian = 57u,
    Romanian = 58u,
    Irish = 59u,
    Serbian = 60u,
    Galician = 61u,
    Cebuano = 62u,
    Ilocano = 63u,
    ScottishGaelic = 64u,

    Count = 65u,
};

/**
 * @enum LanguageEra
 * @brief Whether a text in a language is necessarily modern, necessarily not, or neither.
 *
 * @warning **Three values, not two, and the third is the important one.** A living language with an
 * ancient literature -- Arabic, Hebrew, Persian, Chinese, Greek -- cannot be classified by its
 * name alone, and forcing it either way would make a lookup table pronounce on whether a text
 * may be redistributed. @ref Unspecified is the honest answer, and it is treated as "not
 * necessarily modern", which is the conservative direction: it never licenses redistribution.
 */
enum class LanguageEra : core::u32 {
    Unspecified = 0u, ///< The name does not settle it. Never treated as a licence.
    Ancient = 1u,     ///< A text in this is a source, not a rendering of one.
    Medieval = 2u,    ///< Likewise, and with its own orthography.
    Modern = 3u,      ///< A text in this is necessarily a modern rendering.
};

/**
 * @brief What era a language places a text in.
 *
 * @param tag The language.
 * @return Its era; @ref LanguageEra::Unspecified for anything the name does not settle.
 */
[[nodiscard]] LanguageEra languageEra(LanguageTag tag) noexcept;

/**
 * @brief Is a text in this language a modern derivative rather than a source?
 *
 * The question a harvester has to answer before it redistributes anything. Answered from
 * the tag and nothing else, so it can be answered about a document nobody has read.
 *
 * @warning It is a NECESSARY condition, not a sufficient one: an ancient-language text can still
 * be an edition someone holds rights over. This tells a caller when the answer is
 * certainly "derivative"; it never licenses the opposite conclusion, and the
 * `kDocumentFlagPublicDomain` bit exists because that judgement is made per document by
 * someone who looked.
 *
 * @param tag The language.
 * @return true when a text in this language is necessarily a modern rendering.
 */
[[nodiscard]] inline bool isModernRendering(LanguageTag tag) noexcept
{
    // @warning Derived from the era rather than listing tags, and that is what makes widening the
    // enumeration safe. The old form named two tags explicitly; adding thirty modern vernaculars
    // beside it would have left every one of them reading as "not necessarily a rendering" --
    // which is precisely the answer that licenses redistributing a copyrighted translation.
    return languageEra(tag) == LanguageEra::Modern;
}

/**
 * @brief The stable word for a tag.
 *
 * @param tag The language.
 * @return A short, stable string; "unknown" for anything outside the enumeration.
 */
[[nodiscard]] const char *languageName(LanguageTag tag) noexcept;

/**
 * @brief The tag a word names.
 *
 * @param text  First byte of the word.
 * @param bytes Its length.
 * @param out   Receives the tag.
 * @return false when no tag carries that word — a refusal, never a default.
 */
[[nodiscard]] bool languageByName(const char *text, core::u32 bytes, LanguageTag &out) noexcept;

} // namespace lpl::corpus

#endif // LPL_LPL_CORPUS_LANGUAGE_HPP
