/**
 * @file Xml.hpp
 * @brief The small XML scanner two readers share.
 *
 * @warning **Promoted out of `Tei.cpp` rather than written a second time.** It lived in an anonymous
 * namespace there, which was right while TEI was the only XML this library read; OAI-PMH is the
 * second, and a second scanner would be two answers to "where does this tag end" — free to
 * disagree about comments, about CDATA, about a `>` inside an attribute value. The TEI reader
 * already paid for one of those disagreements: a comment leaked into a passage of Caesar,
 * correctly cited, with an editor's note inside the quotation.
 *
 * **Deliberately not a DOM, and not a validating parser.** It finds tags, reads attributes and
 * flattens text. Everything above that — which elements matter, how they nest, what they mean —
 * belongs to the reader that knows the vocabulary, because that is the part where TEI and
 * OAI-PMH have nothing in common.
 *
 * @warning Host only. A heap, `std::string` and text parsing; `harvest/` is the hosted half.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_XML_HPP
#    define LPL_LPL_HARVEST_XML_HPP

#    include <lpl/Foundation.hpp>

#    include <string>
#    include <string_view>

namespace lpl::harvest {

/**
 * @struct XmlElement
 * @brief One tag found by the scanner.
 */
struct XmlElement {
    std::string_view tag;    ///< The whole tag, brackets included.
    std::string_view name;   ///< Local name, namespace prefix stripped.
    std::size_t begin{0u};   ///< Offset of '<'.
    std::size_t end{0u};     ///< Offset just past '>'.
    bool closing{false};     ///< `</x>`.
    bool selfClosing{false}; ///< `<x/>`.
};

/**
 * @brief The local name of an element, namespace prefix stripped.
 *
 * `<tei:div>` and `<div>` are the same element, as are `<oai:record>` and `<record>`. Which one
 * a file uses is a serialisation choice its author made and no reader should depend on.
 *
 * @param tag The whole start tag.
 * @return The local name. Case is NOT folded — XML is case-sensitive.
 */
[[nodiscard]] std::string_view xmlElementName(std::string_view tag);

/**
 * @brief Reads one attribute out of a start tag.
 *
 * @warning **Attribute ORDER is not significant**, and this is the one place that has to know it.
 * Measured on real corpora: Herodotus writes `<div n="urn:…" type="edition">` and Caesar writes
 * `<div type="edition" xml:lang="lat" n="urn:…">`. A first version that expected one order
 * reported that Caesar had no identity — which reads exactly like a file that has none.
 *
 * @param tag  The whole start tag.
 * @param name The attribute, matched on its local name.
 * @param out  Receives the value, entities decoded.
 * @return false when the tag does not carry it.
 */
[[nodiscard]] bool xmlAttribute(std::string_view tag, std::string_view name, std::string &out);

/**
 * @brief Finds the next tag at or after a position, skipping comments and declarations.
 *
 * @param body The document.
 * @param from Where to start.
 * @param out  Receives the tag.
 * @return false when there is none left.
 */
[[nodiscard]] bool xmlNextElement(std::string_view body, std::size_t from, XmlElement &out);

/**
 * @brief Strips every tag from a span, leaving its text.
 *
 * @param body  The document.
 * @param begin First byte.
 * @param end   One past the last.
 * @return The text, entities decoded and whitespace flattened.
 */
[[nodiscard]] std::string xmlTextOf(std::string_view body, std::size_t begin, std::size_t end);

/**
 * @brief The text of the first occurrence of an element within a span.
 *
 * @param body The span to search.
 * @param name Local element name.
 * @param out  Receives the text.
 * @return false when the element is not there.
 */
[[nodiscard]] bool xmlFirstElementText(std::string_view body, std::string_view name, std::string &out);

/**
 * @brief Decodes the XML entities a document actually contains.
 *
 * The five predefined ones plus numeric references. Anything else is left VERBATIM: an unknown
 * entity is a fact about the document, and silently dropping it would quietly change the text a
 * citation points at.
 *
 * @param text The raw bytes.
 * @return The decoded text.
 */
[[nodiscard]] std::string xmlDecodeEntities(std::string_view text);

/**
 * @brief Collapses runs of whitespace and trims the ends.
 *
 * @param text The text.
 * @return The flattened text.
 */
[[nodiscard]] std::string xmlFlatten(std::string_view text);

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_XML_HPP
