// DigitalNote v2.0.0.7 -- Wallet rebuild (compact) primitives.
//
// Replaces the deprecated -salvagewallet path. Operates at the BDB cursor
// level so every record type round-trips: watch-only addresses, A4 coin
// locks, stealth addresses, multisig redeem scripts, the BIP39 mnemonic
// master key, address book entries, transaction history -- all preserved
// verbatim, unlike dumpwallet+importwallet which only round-trips keys.
//
// Three-phase design:
//   1. DumpAllRecords  -- read-only cursor walk, writes a versioned
//                         dumpfile. Safe to run on a copy of wallet.dat.
//   2. CreateFromDump  -- reads the dumpfile, validates checksum and
//                         record count, builds a fresh wallet.dat at a
//                         given path. Refuses to overwrite an existing
//                         destination.
//   3. RebuildWallet   -- orchestrates the full pipeline (dump -> create
//                         -> verify -> swap) with marker files for crash
//                         recovery and outcome reporting. Invoked from
//                         init.cpp's -rebuildwallet handler.
//
// All functions assume the source wallet is NOT currently open by this
// process. The maintenance-mode workflow guarantees this by triggering
// a full process restart before invoking the rebuild.
//
// Dump format v1: see walletrebuild.cpp for the full spec.

#ifndef DIGITALNOTE_WALLETREBUILD_H
#define DIGITALNOTE_WALLETREBUILD_H

#include <string>
#include <boost/filesystem/path.hpp>

class CDBEnv;

// Walks every record in the source wallet via BDB cursor and writes them
// to dumpfilePath in the v1 dump format. Strict-mode: any non-DB_NOTFOUND
// cursor error aborts the dump and the partial file is unlinked.
//
// Returns true on success. On failure, populates strError with a human-
// readable diagnostic and ensures no partial dumpfile is left on disk.
//
// Does not modify the source wallet. Safe to invoke repeatedly.
bool DumpAllRecords(CDBEnv& dbenv,
                    const std::string& walletFilename,
                    const boost::filesystem::path& dumpfilePath,
                    std::string& strError);

// Reads dumpfilePath (v1 format) and writes every record into a fresh
// BDB file at newWalletFilename within the given env. Validates the
// dump's double-SHA256 checksum and record count BEFORE creating any
// destination state -- a malformed dump produces no partial output.
//
// Refuses to overwrite an existing destination. Caller is responsible
// for ensuring newWalletFilename does not already exist.
//
// Returns true on success, populates strError on failure. On any per-
// record write failure, deletes the partial destination file.
//
// Uses pdbCopy->put with no DB_NOOVERWRITE flag so cursor-ordered records
// from the source go in cleanly. The dump came from cursor iteration so
// by definition there are no key collisions; the absent NOOVERWRITE is
// defensive against corruption of the dump file in transit.
bool CreateFromDump(CDBEnv& dbenv,
                    const boost::filesystem::path& dumpfilePath,
                    const std::string& newWalletFilename,
                    std::string& strError);

// End-to-end orchestration of the rebuild pipeline. Invoked by init.cpp's
// -rebuildwallet handler with the live datadir already determined and the
// BDB environment opened. Performs:
//
//   * Pre-flight checks (no stale .bak/.new/.dump, source exists, free
//     space at least 2x source size).
//   * Dump source wallet to wallet.dat.dump.
//   * Close source from the env, flush, ensure no holders.
//   * CreateFromDump into wallet.dat.new.
//   * Cursor-walk verify of new file (record count match).
//   * File swap: rename wallet.dat -> wallet.dat.bak, then
//     wallet.dat.new -> wallet.dat.
//   * Delete wallet.dat.dump.
//   * Write outcome marker for the GUI to surface on next paint.
//
// Returns true if the wallet at the original path is now the rebuilt
// wallet (i.e. either the swap completed normally or a prior interrupted
// swap was completed in this call). Returns false if the rebuild aborted
// before the swap; the source wallet at the original path is unchanged
// in that case.
//
// strError is populated on failure with a user-suitable diagnostic.
bool RebuildWallet(CDBEnv& dbenv,
                   const std::string& walletFilename,
                   std::string& strError);

// Marker file machinery. The pending flag is written by the GUI before
// shutdown to request a rebuild on next launch; the result file is written
// by the handler after rebuild attempt and consumed by the GUI on first
// paint to surface the outcome to the user.
//
// All functions are no-throw. Bad I/O returns false (write helpers) or
// REBUILD_RESULT_NONE (read helper).

enum RebuildResultState
{
    REBUILD_RESULT_NONE = 0,            // no result file present
    REBUILD_RESULT_SUCCESS,             // rebuild completed cleanly
    REBUILD_RESULT_RECOVERED_FROM_CRASH,// prior interrupted swap completed
    REBUILD_RESULT_FAILED_PRESWAP,      // dump or create or verify failed; original intact
    REBUILD_RESULT_FAILED_FILESYSTEM    // pre-flight or rename failed; original intact
};

// Pending flag. Empty file in datadir whose presence signals "perform a
// rebuild on the next AppInit2 before LoadWallet". Removed by the handler.
bool RebuildPendingFlagWrite();
bool RebuildPendingFlagExists();
bool RebuildPendingFlagRemove();

// Result marker. Single-line text: "<state-token>\n<optional reason>\n".
// The state-token is one of: success, recovered_from_crash, failed_preswap,
// failed_filesystem.
bool RebuildResultWrite(RebuildResultState state, const std::string& reason);
RebuildResultState RebuildResultRead(std::string& reason);
bool RebuildResultRemove();

#endif // DIGITALNOTE_WALLETREBUILD_H
