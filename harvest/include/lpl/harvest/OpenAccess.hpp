/**
 * @file OpenAccess.hpp
 * @brief FOLDED — this work is done, on the researcher's side.
 *
 * @warning **Deliberately empty. Do not fill it in.** What this file described —  given a DOI or a
 * publisher URL, find a legally free copy — is implemented by `lpl::research::resolve_open_access`
 * in LplAssistant (`research/src/OpenAccess.cpp`), against Unpaywall, with the rule that only a
 * copy in a REPOSITORY counts and a publisher's own page never does.
 *
 * Writing a second one here would be this repository's most-repeated mistake, and the argument
 * that it belongs to the librarian does not survive contact with the facts:
 *
 *  - **The consumer is on the other side.** Resolution happens at the moment something is
 *    FETCHED, and fetching is what a research run does. A catalogue row is a generator, not a
 *    download; deciding where a free copy lives is a question asked when someone wants to read.
 *  - **The input is not here.** Resolution needs a DOI. HathiTrust rows carry an OCLC number,
 *    OAI records carry whatever `dc:identifier` the repository chose. The rows this module bakes
 *    mostly have no DOI at all, so a librarian-side resolver would spend its life declining.
 *  - **Two resolvers would disagree.** Unpaywall's answer changes over time; two callers asking
 *    separately would record two different "best free copies" for one work, and nothing would
 *    say which was meant.
 *
 * The seam that WOULD be right, the day a run needs it, is the one already here:
 * @ref lpl::harvest::IHttpFetcher. A resolver is a policy over a fetcher, and the fetcher is
 * shared. Until a caller exists, writing it would be an orphan.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_OPENACCESS_HPP
#    define LPL_LPL_HARVEST_OPENACCESS_HPP

#endif // LPL_LPL_HARVEST_OPENACCESS_HPP
