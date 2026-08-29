/**
 * @file Changefile.hpp
 * @brief FOLDED — the protocol already carries this.
 *
 * @warning **Deliberately empty. Do not fill it in.** What this file described — daily differential
 * synchronisation, upsert on a stable identifier — is a parameter of OAI-PMH rather than a
 * mechanism to build on top of it. @ref lpl::harvest::OaiRequest carries `from` and `until`, the
 * repository answers with only what moved inside that window, and
 * @ref lpl::harvest::OaiHarvestReport hands back the newest datestamp it saw so the next run can
 * open where this one closed. `lpl-ingest` prints that date. That is the whole of it.
 *
 * The one thing a changefile would add is the case where a source CANNOT be re-read whole and
 * publishes deltas instead — a bulk dump with nightly patches. No such source is wired here, and
 * the two that are argue against writing it early: HathiTrust ships a full file plus daily
 * updates that are themselves full rows, and the four-repository markdown corpus re-bakes
 * completely in 0.103 s. Incrementality solves a cost that does not exist yet at this size.
 *
 * @warning And a tombstone is NOT a changefile concern: OAI publishes deleted records in-band, and
 * @ref lpl::harvest::harvestOaiPmh counts them and refuses to write them, because a withdrawn
 * record ingested as a holding is a book in the catalogue that is not there.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_CHANGEFILE_HPP
#    define LPL_LPL_HARVEST_CHANGEFILE_HPP

#endif // LPL_LPL_HARVEST_CHANGEFILE_HPP
