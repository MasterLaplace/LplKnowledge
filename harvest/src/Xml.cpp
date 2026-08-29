/**
 * @file Xml.cpp
 * @brief Implementation of the shared XML scanner.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Xml.hpp>

namespace lpl::harvest {

namespace {

/**
 * @brief Whether a byte is XML whitespace.
 *
 * @param c The byte.
 * @return true for space, tab, carriage return and newline.
 */
bool space(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

} // namespace

/**
 * @brief The local name of an element, namespace prefix stripped.
 *
 * `<tei:div>` and `<div>` are the same element; which one a file uses is a serialisation
 * choice its author made and no reader should depend on.
 *
 * @param tag The whole start tag.
 * @return The local name, lower-cased is NOT applied — XML is case-sensitive.
 */
std::string_view xmlElementName(std::string_view tag)
{
    std::size_t i = 1u; // past '<'
    while (i < tag.size() && (tag[i] == '/' || space(tag[i])))
        ++i;
    const std::size_t start = i;
    while (i < tag.size() && !space(tag[i]) && tag[i] != '>' && tag[i] != '/')
        ++i;
    std::string_view name = tag.substr(start, i - start);
    const std::size_t colon = name.find(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1u);
}

/**
 * @brief Decodes the XML entities a TEI text actually contains.
 *
 * The five predefined ones plus numeric references. Anything else is left verbatim: an
 * unknown entity is a fact about the document, and silently dropping it would quietly change
 * the text a citation points at.
 *
 * @param text The raw text.
 * @return The decoded text.
 */
std::string xmlDecodeEntities(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0u; i < text.size();)
    {
        if (text[i] != '&')
        {
            out.push_back(text[i++]);
            continue;
        }
        const std::size_t semi = text.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10u)
        {
            out.push_back(text[i++]);
            continue;
        }
        const std::string_view name = text.substr(i + 1u, semi - i - 1u);
        if (name == "amp")
            out.push_back('&');
        else if (name == "lt")
            out.push_back('<');
        else if (name == "gt")
            out.push_back('>');
        else if (name == "quot")
            out.push_back('"');
        else if (name == "apos")
            out.push_back('\'');
        else if (name.size() > 1u && name[0] == '#')
        {
            // A numeric reference. Encoded back as UTF-8 rather than as a byte: these corpora
            // are Greek, Coptic and Syriac, so a codepoint above 127 is the normal case.
            core::u32 value = 0u;
            bool ok = true;
            const bool hex = name.size() > 2u && (name[1] == 'x' || name[1] == 'X');
            for (std::size_t k = hex ? 2u : 1u; k < name.size(); ++k)
            {
                const char c = name[k];
                core::u32 digit = 0u;
                if (c >= '0' && c <= '9')
                    digit = static_cast<core::u32>(c - '0');
                else if (hex && c >= 'a' && c <= 'f')
                    digit = static_cast<core::u32>(c - 'a' + 10);
                else if (hex && c >= 'A' && c <= 'F')
                    digit = static_cast<core::u32>(c - 'A' + 10);
                else
                {
                    ok = false;
                    break;
                }
                value = value * (hex ? 16u : 10u) + digit;
                if (value > 0x10FFFFu)
                {
                    ok = false;
                    break;
                }
            }
            if (!ok)
            {
                out.push_back(text[i++]);
                continue;
            }
            if (value < 0x80u)
                out.push_back(static_cast<char>(value));
            else if (value < 0x800u)
            {
                out.push_back(static_cast<char>(0xC0u | (value >> 6)));
                out.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
            }
            else if (value < 0x10000u)
            {
                out.push_back(static_cast<char>(0xE0u | (value >> 12)));
                out.push_back(static_cast<char>(0x80u | ((value >> 6) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0u | (value >> 18)));
                out.push_back(static_cast<char>(0x80u | ((value >> 12) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | ((value >> 6) & 0x3Fu)));
                out.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
            }
        }
        else
        {
            out.push_back(text[i++]);
            continue;
        }
        i = semi + 1u;
    }
    return out;
}

/**
 * @brief Collapses runs of whitespace and trims.
 *
 * TEI is indented for humans, so a passage's raw text arrives full of newlines that are
 * layout rather than content. Collapsing them is what makes a passage one line, which is what
 * a locus addresses.
 *
 * @param text The text.
 * @return The flattened text.
 */
std::string xmlFlatten(std::string_view text)
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
        out.push_back(c);
    }
    return out;
}

/**
 * @brief Finds the next tag at or after a position, skipping comments and declarations.
 *
 * @param body The document.
 * @param from Where to start.
 * @param out  Receives the tag.
 * @return false when there is none left.
 */
bool xmlNextElement(std::string_view body, std::size_t from, XmlElement &out)
{
    while (from < body.size())
    {
        const std::size_t open = body.find('<', from);
        if (open == std::string_view::npos)
            return false;

        // Comments, CDATA and processing instructions carry no structure this reader wants,
        // and their contents may hold anything at all — including text that looks like tags.
        if (body.compare(open, 4u, "<!--") == 0)
        {
            const std::size_t close = body.find("-->", open);
            if (close == std::string_view::npos)
                return false;
            from = close + 3u;
            continue;
        }
        if (body.compare(open, 9u, "<![CDATA[") == 0)
        {
            const std::size_t close = body.find("]]>", open);
            if (close == std::string_view::npos)
                return false;
            from = close + 3u;
            continue;
        }
        if (open + 1u < body.size() && (body[open + 1u] == '?' || body[open + 1u] == '!'))
        {
            const std::size_t close = body.find('>', open);
            if (close == std::string_view::npos)
                return false;
            from = close + 1u;
            continue;
        }

        std::size_t close = open + 1u;
        bool inQuote = false;
        char quote = '\0';
        while (close < body.size())
        {
            const char c = body[close];
            if (inQuote)
            {
                if (c == quote)
                    inQuote = false;
            }
            else if (c == '"' || c == '\'')
            {
                inQuote = true;
                quote = c;
            }
            else if (c == '>')
                break;
            ++close;
        }
        if (close >= body.size())
            return false;

        out.begin = open;
        out.end = close + 1u;
        out.tag = body.substr(open, out.end - open);
        out.name = xmlElementName(out.tag);
        out.closing = out.tag.size() > 1u && out.tag[1] == '/';
        out.selfClosing = out.tag.size() > 2u && out.tag[out.tag.size() - 2u] == '/';
        return true;
    }
    return false;
}

/**
 * @brief Strips every tag from a span, leaving its text.
 *
 * @param body  The document.
 * @param begin First byte.
 * @param end   One past the last.
 * @return The text, entities decoded and whitespace flattened.
 */
std::string xmlTextOf(std::string_view body, std::size_t begin, std::size_t end)
{
    // @warning Scanned here rather than driven by @ref nextElement, and a test caught why. That
    // helper skips comments, CDATA and processing instructions as STRUCTURE — it returns the
    // next real tag after them — so a caller appending everything between one tag and the
    // next silently appends the comment's bytes as if they were the text. A passage of Caesar
    // would then be quoted with an editor's note inside it, correctly cited, and nothing
    // downstream could tell.
    std::string raw;
    std::size_t cursor = begin;
    while (cursor < end)
    {
        const std::size_t open = body.find('<', cursor);
        if (open == std::string_view::npos || open >= end)
        {
            raw.append(body.substr(cursor, end - cursor));
            break;
        }
        if (open > cursor)
            raw.append(body.substr(cursor, open - cursor));

        std::size_t close = std::string_view::npos;
        if (body.compare(open, 4u, "<!--") == 0)
            close = body.find("-->", open) == std::string_view::npos ? close : body.find("-->", open) + 3u;
        else if (body.compare(open, 9u, "<![CDATA[") == 0)
        {
            // CDATA is the one skipped construct whose CONTENT is text: dropping it would
            // lose real words, where dropping a comment loses an editorial aside.
            const std::size_t stop = body.find("]]>", open);
            if (stop != std::string_view::npos)
            {
                raw.append(body.substr(open + 9u, stop - open - 9u));
                close = stop + 3u;
            }
        }
        else
        {
            XmlElement element{};
            if (xmlNextElement(body, open, element) && element.begin == open)
                close = element.end;
        }

        if (close == std::string_view::npos || close <= open)
            break; // unterminated: stop rather than guess where it ended
        cursor = close;
    }
    return xmlFlatten(xmlDecodeEntities(raw));
}

/**
 * @brief The text of the first occurrence of an element.
 *
 * @param body The document.
 * @param name Local element name.
 * @param out  Receives the text.
 * @return false when the element is absent.
 */
bool xmlFirstElementText(std::string_view body, std::string_view name, std::string &out)
{
    XmlElement element{};
    std::size_t cursor = 0u;
    while (xmlNextElement(body, cursor, element))
    {
        cursor = element.end;
        if (element.closing || element.selfClosing || element.name != name)
            continue;
        // Find its matching close, honouring nesting of the same name.
        std::size_t depth = 1u;
        std::size_t inner = element.end;
        XmlElement next{};
        while (depth != 0u && xmlNextElement(body, inner, next))
        {
            inner = next.end;
            if (next.name != name || next.selfClosing)
                continue;
            depth += next.closing ? static_cast<std::size_t>(-1) : 1u;
            if (depth == 0u)
            {
                out = xmlTextOf(body, element.end, next.begin);
                return true;
            }
        }
        return false;
    }
    return false;
}

bool xmlAttribute(std::string_view tag, std::string_view name, std::string &out)
{
    out.clear();
    std::size_t i = 1u;
    // Past the element name.
    while (i < tag.size() && !space(tag[i]))
        ++i;

    while (i < tag.size())
    {
        while (i < tag.size() && space(tag[i]))
            ++i;
        if (i >= tag.size() || tag[i] == '>' || tag[i] == '/')
            break;

        const std::size_t nameStart = i;
        while (i < tag.size() && tag[i] != '=' && !space(tag[i]) && tag[i] != '>')
            ++i;
        const std::string_view attribute = tag.substr(nameStart, i - nameStart);

        while (i < tag.size() && space(tag[i]))
            ++i;
        if (i >= tag.size() || tag[i] != '=')
            continue; // a valueless attribute; not something TEI uses, but not fatal either
        ++i;
        while (i < tag.size() && space(tag[i]))
            ++i;
        if (i >= tag.size() || (tag[i] != '"' && tag[i] != '\''))
            continue;
        const char quote = tag[i++];
        const std::size_t valueStart = i;
        while (i < tag.size() && tag[i] != quote)
            ++i;
        const std::string_view value = tag.substr(valueStart, i - valueStart);
        if (i < tag.size())
            ++i;

        // Compared on the LOCAL name, so `xml:lang` matches a request for `lang` and a file
        // that declares its own prefix is read the same as one that does not.
        std::string_view local = attribute;
        const std::size_t colon = local.find(':');
        if (colon != std::string_view::npos)
            local = local.substr(colon + 1u);

        if (attribute == name || local == name)
        {
            out.assign(value);
            return true;
        }
    }
    return false;
}

} // namespace lpl::harvest
