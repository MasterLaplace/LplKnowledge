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
 * The list is deliberately short — it holds the stages this project has a corpus for, and
 * grows when one arrives rather than in anticipation.
 */
enum class LanguageTag : core::u32 {
    Unknown = 0u,
    Latin = 1u,               ///< Classical Latin.
    EcclesiasticalLatin = 2u, ///< Medieval church Latin: different orthography, heavy abbreviation.
    AncientGreek = 3u,        ///< Polytonic, and the reason a byte offset is a poor citation.
    OldFrench = 4u,
    MiddleEnglish = 5u,
    ModernFrench = 6u,
    ModernEnglish = 7u,
    Count = 8u,
};

/**
 * @brief Is a text in this language a modern derivative rather than a source?
 *
 * The question a harvester has to answer before it redistributes anything. Answered from
 * the tag and nothing else, so it can be answered about a document nobody has read.
 *
 * ⚠ It is a NECESSARY condition, not a sufficient one: an ancient-language text can still
 * be an edition someone holds rights over. This tells a caller when the answer is
 * certainly "derivative"; it never licenses the opposite conclusion, and the
 * `kDocumentFlagPublicDomain` bit exists because that judgement is made per document by
 * someone who looked.
 *
 * @param tag The language.
 * @return true when a text in this language is necessarily a modern rendering.
 */
[[nodiscard]] constexpr bool isModernRendering(LanguageTag tag) noexcept
{
    return tag == LanguageTag::ModernFrench || tag == LanguageTag::ModernEnglish;
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
