/**
 * @file Types.cpp
 * @brief Implementation of fixed-width identifiers shared by every knowledge artefact.
 *
 * Everything else in `Types.hpp` is a layout or a `constexpr` fold, so this translation
 * unit holds the one thing that is neither: the mapping from a section type to its word.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/knowledge/Types.hpp>

namespace lpl::knowledge {

const char *sectionTypeName(SectionType type) noexcept
{
    switch (type)
    {
    case SectionType::Unknown: return "unknown";
    case SectionType::Vocabulary: return "vocabulary";
    case SectionType::Sources: return "sources";
    case SectionType::Facts: return "facts";
    case SectionType::Documents: return "documents";
    case SectionType::Loci: return "loci";
    case SectionType::Texts: return "texts";
    case SectionType::Ecc: return "ecc";
    case SectionType::Catalogue: return "catalogue";
    case SectionType::Gazetteer: return "gazetteer";
    case SectionType::Candidate: return "candidate";
    case SectionType::Attribution: return "attribution";
    case SectionType::PlaceLink: return "place-link";
    case SectionType::Relief: return "relief";
    }
    // Not a fallthrough for tidiness: a section type this build has never heard of is the
    // NORMAL case for an image from a newer writer, and reporting it as unknown is how the
    // skip-rather-than-fail rule reads in a log.
    return "unknown";
}

} // namespace lpl::knowledge
