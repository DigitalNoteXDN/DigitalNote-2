// DigitalNote v2.0.0.7 -- Wallet rebuild (compact) primitives.
//
// Dump format v1
// --------------
//
//   # DigitalNote wallet rebuild dump created by DigitalNote 2.0.0.7 (<date>)
//   # * Created on <ISO timestamp>
//   # * Source wallet: <filename>
//   # * Best block at time of dump was <height> (<hash>),
//   #   mined on <ISO timestamp>
//   # * Format: bdb-raw-v1
//
//   <hex_key> <hex_value>
//   <hex_key> <hex_value>
//   ... (one record per line, in BDB cursor order, lowercase hex)
//
//   # checksum dsha256=<hex> records=<N>
//   # End of dump
//
// Rules:
//   * Lines starting with '#' are comments (matches importwallet convention).
//   * Blank lines are skipped by the parser.
//   * Data lines: lowercase hex key, single space, lowercase hex value.
//   * Records appear in BDB cursor order (non-deterministic across BDB
//     versions; not sorted).
//   * Checksum is the codebase-standard double-SHA256 (CHashWriter) over
//     the per-record stream: for each record, hash <varint_len><key_bytes>
//     <varint_len><value_bytes>. The varint length prefix is what
//     CHashWriter << std::vector<unsigned char> produces naturally; this
//     decouples the checksum from any future text-format changes.
//   * The "# checksum" line MUST be the second-to-last non-blank line and
//     "# End of dump" MUST be the last. CreateFromDump rejects files
//     missing either.
//   * Strict cursor error handling: any non-DB_NOTFOUND error from
//     ReadAtCursor aborts the dump and unlinks the partial file.

#include "walletrebuild.h"

#include "cdb.h"
#include "cdbenv.h"
#include "walletdb.h"
#include "cdatastream.h"
#include "chashwriter.h"
#include "enums/serialize_type.h"
#include "serialize/vector.h"
#include "uint/uint256.h"
#include "util.h"
#include "version.h"
#include "main_extern.h"
#include "cblockindex.h"
#include "clientversion.h"
#include "ui_interface.h"

#include <db_cxx.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/system/error_code.hpp>

#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

#ifndef WIN32
#include <sys/stat.h>
#endif

namespace {

// Format a unix timestamp as ISO-8601 UTC: 2026-05-02T07:23:45Z.
// Returns "unknown" for zero or invalid timestamps.
std::string FormatIsoUtc(int64_t nTime)
{
	if (nTime <= 0)
	{
		return "unknown";
	}
	std::time_t t = static_cast<std::time_t>(nTime);
	std::tm tm;
#ifdef WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
	return oss.str();
}

// Tighten dumpfile permissions to user-only on POSIX. No-op on Windows
// (datadir files default to user-only ACL there).
void TightenPermissions(const boost::filesystem::path& path)
{
#ifndef WIN32
	chmod(path.string().c_str(), S_IRUSR | S_IWUSR);  // 0600
#else
	(void)path;
#endif
}

}  // namespace

bool DumpAllRecords(CDBEnv& dbenv,
                    const std::string& walletFilename,
                    const boost::filesystem::path& dumpfilePath,
                    std::string& strError)
{
	// Implementation lives as a static method on CWalletDB so it can call
	// the protected GetCursor/ReadAtCursor on a CWalletDB instance. This
	// free function exists so callers (RPC handler, init.cpp -rebuildwallet
	// path) don't have to reach into CWalletDB to invoke it.
	return CWalletDB::DumpAllRecords(dbenv, walletFilename, dumpfilePath,
	                                 strError);
}

bool CWalletDB::DumpAllRecords(CDBEnv& dbenv,
                               const std::string& filename,
                               const boost::filesystem::path& dumpfilePath,
                               std::string& strError)
{
	(void)dbenv;  // implicit via the global bitdb that CDB constructor uses
	strError.clear();

	// Refuse to overwrite an existing dumpfile -- the caller chose this
	// path, if a previous dump is still there it's a bug or a stale
	// artefact and we should not silently clobber it.
	if (boost::filesystem::exists(dumpfilePath))
	{
		strError = strprintf("Refusing to overwrite existing dumpfile: %s",
		                     dumpfilePath.string());
		return false;
	}

	// Open the wallet read-only via CWalletDB. The "r" mode passes through
	// to CDB which sets fReadOnly=true; any accidental write attempt would
	// trigger an assert.
	CWalletDB walletdb(filename, "r");

	Dbc* pcursor = walletdb.GetCursor();
	if (pcursor == NULL)
	{
		strError = "Cannot open wallet cursor";
		return false;
	}

	// Open the dumpfile. If we fail mid-write we'll unlink it.
	boost::filesystem::ofstream out(dumpfilePath,
	                                std::ios::out | std::ios::trunc | std::ios::binary);
	if (!out.is_open())
	{
		pcursor->close();
		strError = strprintf("Cannot open dumpfile for writing: %s",
		                     dumpfilePath.string());
		return false;
	}
	TightenPermissions(dumpfilePath);

	// Header. Best-block info is best-effort: if the block index hasn't
	// been loaded yet at the time of dump (which is the case in the
	// maintenance-mode workflow where the dump runs early in init before
	// LoadBlockIndex), pindexBest will be NULL and we write "unknown".
	std::string strNowIso = FormatIsoUtc(GetTime());
	std::string strBestHeight = "unknown";
	std::string strBestHash = "unknown";
	std::string strBestTime = "unknown";
	if (pindexBest != NULL)
	{
		strBestHeight = strprintf("%d", pindexBest->nHeight);
		strBestHash = pindexBest->GetBlockHash().ToString();
		strBestTime = FormatIsoUtc(static_cast<int64_t>(pindexBest->nTime));
	}

	out << "# DigitalNote wallet rebuild dump created by DigitalNote "
	    << FormatFullVersion() << " (" << strNowIso.substr(0, 10) << ")\n";
	out << "# * Created on " << strNowIso << "\n";
	out << "# * Source wallet: " << filename << "\n";
	out << "# * Best block at time of dump was " << strBestHeight
	    << " (" << strBestHash << "),\n";
	out << "#   mined on " << strBestTime << "\n";
	out << "# * Format: bdb-raw-v1\n";
	out << "\n";

	// Cursor walk. Hash and write each record. DB_NEXT from a freshly-
	// opened cursor returns the first record (matches the codebase's
	// existing iteration convention in cdb.cpp and walletdb.cpp).
	CHashWriter hasher(SER_DISK, CLIENT_VERSION);
	uint64_t nRecords = 0;

	uiInterface.InitMessage("Dumping records...");

	while (true)
	{
		CDataStream ssKey(SER_DISK, CLIENT_VERSION);
		CDataStream ssValue(SER_DISK, CLIENT_VERSION);

		int ret = walletdb.ReadAtCursor(pcursor, ssKey, ssValue, DB_NEXT);

		if (ret == DB_NOTFOUND)
		{
			break;
		}
		if (ret != 0)
		{
			pcursor->close();
			out.close();
			boost::filesystem::remove(dumpfilePath);
			strError = strprintf("BDB cursor error %d at record %llu; "
			                     "partial dumpfile removed", ret,
			                     (unsigned long long)nRecords);
			return false;
		}

		// Hash: length-prefixed via the serialise template's vector
		// specialisation. Calling ::Serialize directly (rather than
		// hasher << vKey) leverages the existing
		//   template void Serialize<CHashWriter, unsigned char,
		//                            std::allocator<unsigned char>>(...)
		// instantiation in serialize/vector.cpp:199. Using
		// CHashWriter::operator<< would require a NEW instantiation in
		// chashwriter.cpp's explicit list, which we avoid to keep the
		// rebuild work isolated.
		std::vector<unsigned char> vKey(ssKey.begin(), ssKey.end());
		std::vector<unsigned char> vValue(ssValue.begin(), ssValue.end());
		::Serialize(hasher, vKey,   hasher.nType, hasher.nVersion);
		::Serialize(hasher, vValue, hasher.nType, hasher.nVersion);

		// Write hex line.
		out << HexStr(vKey.begin(), vKey.end()) << " "
		    << HexStr(vValue.begin(), vValue.end()) << "\n";

		++nRecords;

		// Bail if write failed (disk full, etc.).
		if (!out.good())
		{
			pcursor->close();
			out.close();
			boost::filesystem::remove(dumpfilePath);
			strError = strprintf("Write error on dumpfile at record %llu; "
			                     "partial dumpfile removed",
			                     (unsigned long long)nRecords);
			return false;
		}

		// Splash progress every 10000 records. The dump phase doesn't
		// know the total record count up front (that's the property
		// we're discovering by walking the cursor), so we show running
		// count only.
		if ((nRecords % 10000) == 0)
		{
			uiInterface.InitMessage(strprintf("Dumping records... %llu",
			                                  (unsigned long long)nRecords));
		}
	}

	pcursor->close();

	// Footer: checksum + record count + sentinel.
	uint256 checksum = hasher.GetHash();
	out << "\n";
	out << "# checksum dsha256=" << checksum.ToString()
	    << " records=" << nRecords << "\n";
	out << "# End of dump\n";

	if (!out.good())
	{
		out.close();
		boost::filesystem::remove(dumpfilePath);
		strError = "Write error on dumpfile footer; partial dumpfile removed";
		return false;
	}

	out.close();
	LogPrintf("DumpAllRecords: wrote %llu records to %s (dsha256 %s)\n",
	          (unsigned long long)nRecords,
	          dumpfilePath.string(),
	          checksum.ToString());
	return true;
}
// ===========================================================================
// CreateFromDump
// ===========================================================================
//
// Reads a v1 dumpfile, validates its checksum and record count, and writes
// every record into a fresh BDB file inside the given env. Refuses to
// overwrite an existing destination.
//
// Algorithm:
//   1. Open dumpfile.
//   2. Verify header line is exactly "# DigitalNote wallet rebuild ..."
//      with "# * Format: bdb-raw-v1" on a later line.
//   3. Read every non-comment, non-blank line as "<hex_key> <hex_value>".
//      Strict-validate each hex string with IsHex; bail on any malformed
//      line. Decode to bytes, hash via the same length-prefixed scheme as
//      DumpAllRecords, and accumulate into an in-memory vector.
//   4. Read the "# checksum dsha256=<hex> records=<N>" footer; parse out
//      both fields. Read the "# End of dump" sentinel. Reject if either
//      is missing or malformed.
//   5. Validate computed checksum == footer checksum, computed count ==
//      footer count. If either fails, abort: NO destination state has
//      been created yet.
//   6. Open a fresh BDB at newWalletFilename (DB_CREATE; refuses to
//      overwrite via boost::filesystem::exists pre-check).
//   7. In a single transaction, put every record. Allow overwrite (no
//      DB_NOOVERWRITE flag) -- cursor-ordered records have no collisions
//      by definition, but accept overwrites defensively against any
//      future dump-format change.
//   8. Commit, close, return success.
//
// On any failure during step 6 onwards: close BDB, delete partial
// destination file, return false.

namespace {

// Strip a trailing '\r' (handles CRLF dump files written on Windows even
// though we open in binary mode -- the dump WAS written by us, but a user
// might transport it through a tool that adds CRLF).
void StripCarriageReturn(std::string& line)
{
	if (!line.empty() && line.back() == '\r')
	{
		line.pop_back();
	}
}

// Parse "key=value" from a "# checksum dsha256=<hex> records=<N>" footer
// line. Tolerant of extra whitespace. Returns false on malformed input.
bool ParseFooterField(const std::string& field,
                      std::string& nameOut,
                      std::string& valueOut)
{
	size_t eq = field.find('=');
	if (eq == std::string::npos || eq == 0 || eq == field.size() - 1)
	{
		return false;
	}
	nameOut = field.substr(0, eq);
	valueOut = field.substr(eq + 1);
	return true;
}

}  // namespace

bool CreateFromDump(CDBEnv& dbenv,
                    const boost::filesystem::path& dumpfilePath,
                    const std::string& newWalletFilename,
                    std::string& strError)
{
	strError.clear();

	// Pre-flight: refuse to overwrite an existing destination. The caller
	// is responsible for choosing a fresh path; we just enforce.
	boost::filesystem::path newPath = GetDataDir() / newWalletFilename;
	if (boost::filesystem::exists(newPath))
	{
		strError = strprintf("Refusing to overwrite existing destination: %s",
		                     newPath.string());
		return false;
	}

	// Open the dumpfile for reading. Binary mode so we control line-ending
	// handling explicitly (StripCarriageReturn handles CRLF if present).
	boost::filesystem::ifstream in(dumpfilePath,
	                               std::ios::in | std::ios::binary);
	if (!in.is_open())
	{
		strError = strprintf("Cannot open dumpfile for reading: %s",
		                     dumpfilePath.string());
		return false;
	}

	// Step 1-2: read records into memory, hashing as we go. Defer all
	// destination-side work until after validation passes.
	struct RawRecord {
		std::vector<unsigned char> key;
		std::vector<unsigned char> value;
	};
	std::vector<RawRecord> records;

	CHashWriter hasher(SER_DISK, CLIENT_VERSION);
	bool fSawFormatLine = false;
	bool fSawHeader = false;
	std::string footerChecksum;
	uint64_t footerCount = 0;
	bool fSawFooterChecksum = false;
	bool fSawFooterCount = false;
	bool fSawEndOfDump = false;
	uint64_t lineNo = 0;

	std::string line;
	while (std::getline(in, line))
	{
		++lineNo;
		StripCarriageReturn(line);

		// Header detection: the very first non-empty line must start with
		// "# DigitalNote wallet rebuild dump".
		if (!fSawHeader)
		{
			if (line.empty()) continue;
			if (line.find("# DigitalNote wallet rebuild dump") != 0)
			{
				strError = strprintf(
					"Dumpfile header missing or malformed at line %llu: "
					"expected \"# DigitalNote wallet rebuild dump ...\"",
					(unsigned long long)lineNo);
				return false;
			}
			fSawHeader = true;
			continue;
		}

		// Sentinel: "# End of dump" must be the last non-blank line.
		// We continue reading after seeing it to ensure nothing follows
		// (defensive against truncation+append attacks on the dumpfile).
		if (line == "# End of dump")
		{
			fSawEndOfDump = true;
			continue;
		}

		if (fSawEndOfDump && !line.empty())
		{
			strError = strprintf(
				"Unexpected content after \"# End of dump\" at line %llu",
				(unsigned long long)lineNo);
			return false;
		}

		// Format-version assertion: "# * Format: bdb-raw-v1".
		if (line.find("# * Format:") == 0)
		{
			if (line.find("bdb-raw-v1") == std::string::npos)
			{
				strError = strprintf(
					"Unsupported dump format at line %llu: \"%s\" "
					"(this version supports bdb-raw-v1)",
					(unsigned long long)lineNo, line);
				return false;
			}
			fSawFormatLine = true;
			continue;
		}

		// Footer: "# checksum dsha256=<hex> records=<N>".
		if (line.find("# checksum") == 0)
		{
			std::istringstream iss(line);
			std::string token;
			iss >> token;  // "#"
			iss >> token;  // "checksum"
			while (iss >> token)
			{
				std::string name, value;
				if (!ParseFooterField(token, name, value))
				{
					strError = strprintf(
						"Malformed footer field at line %llu: \"%s\"",
						(unsigned long long)lineNo, token);
					return false;
				}
				if (name == "dsha256")
				{
					footerChecksum = value;
					fSawFooterChecksum = true;
				}
				else if (name == "records")
				{
					try
					{
						footerCount = static_cast<uint64_t>(
							std::stoull(value));
					}
					catch (const std::exception&)
					{
						strError = strprintf(
							"Malformed records count at line %llu: \"%s\"",
							(unsigned long long)lineNo, value);
						return false;
					}
					fSawFooterCount = true;
				}
				// Unknown fields are ignored for forward-compat, but we
				// still validate they're well-formed name=value.
			}
			continue;
		}

		// Other comment lines: skip silently.
		if (!line.empty() && line[0] == '#')
		{
			continue;
		}

		// Blank lines: skip silently.
		if (line.empty())
		{
			continue;
		}

		// Data line: "<hex_key> <hex_value>".
		size_t sp = line.find(' ');
		if (sp == std::string::npos || sp == 0 || sp == line.size() - 1)
		{
			strError = strprintf(
				"Malformed data line %llu (expected '<hex_key> <hex_value>'): \"%s\"",
				(unsigned long long)lineNo, line);
			return false;
		}
		std::string hexKey = line.substr(0, sp);
		std::string hexValue = line.substr(sp + 1);

		if (!IsHex(hexKey) || !IsHex(hexValue))
		{
			strError = strprintf(
				"Non-hex data on line %llu (key or value contains "
				"non-hex characters)", (unsigned long long)lineNo);
			return false;
		}
		if ((hexKey.size() & 1) || (hexValue.size() & 1))
		{
			// IsHex requires even length anyway, but assert defensively.
			strError = strprintf(
				"Odd-length hex on line %llu", (unsigned long long)lineNo);
			return false;
		}

		RawRecord rec;
		rec.key = ParseHex(hexKey);
		rec.value = ParseHex(hexValue);

		// Hash exactly as DumpAllRecords did: length-prefixed via the
		// vector serialise template.
		::Serialize(hasher, rec.key,   hasher.nType, hasher.nVersion);
		::Serialize(hasher, rec.value, hasher.nType, hasher.nVersion);

		records.push_back(std::move(rec));
	}

	in.close();

	// Step 4: ensure all required structural elements were seen.
	if (!fSawFormatLine)
	{
		strError = "Dumpfile is missing the \"# * Format: bdb-raw-v1\" line";
		return false;
	}
	if (!fSawFooterChecksum || !fSawFooterCount)
	{
		strError = "Dumpfile is missing the \"# checksum\" footer";
		return false;
	}
	if (!fSawEndOfDump)
	{
		strError = "Dumpfile is missing the \"# End of dump\" sentinel "
		           "(file may be truncated)";
		return false;
	}

	// Step 5: checksum and count must match.
	uint256 computedChecksum = hasher.GetHash();
	if (computedChecksum.ToString() != footerChecksum)
	{
		strError = strprintf(
			"Checksum mismatch: computed dsha256=%s, footer dsha256=%s",
			computedChecksum.ToString(), footerChecksum);
		return false;
	}
	if (static_cast<uint64_t>(records.size()) != footerCount)
	{
		strError = strprintf(
			"Record count mismatch: read %llu records, footer says %llu",
			(unsigned long long)records.size(),
			(unsigned long long)footerCount);
		return false;
	}

	LogPrintf("CreateFromDump: dump validated, %llu records, dsha256 %s\n",
	          (unsigned long long)records.size(),
	          computedChecksum.ToString());
	uiInterface.InitMessage(strprintf(
		"Validated dump (%llu records). Creating new wallet...",
		(unsigned long long)records.size()));

	// Step 6: open destination BDB. Pattern follows CWalletDB::Recover --
	// Db lifecycle managed manually here (not via CDB) because we want
	// the env's RAII to NOT register this file in mapDb/mapFileUseCount;
	// we'll be renaming it before any CDB ever opens it.
	bool fSuccess = true;
	Db* pdbDest = new Db(&dbenv.dbenv, 0);
	int ret = pdbDest->open(NULL,                    // Txn
	                        newWalletFilename.c_str(),
	                        "main",                  // Logical db name
	                        DB_BTREE,
	                        DB_CREATE,
	                        0);
	if (ret != 0)
	{
		delete pdbDest;
		strError = strprintf("Cannot create destination BDB %s (ret=%d)",
		                     newWalletFilename, ret);
		return false;
	}

	// Step 7: put every record under one transaction.
	DbTxn* ptxn = dbenv.TxnBegin();
	if (ptxn == NULL)
	{
		pdbDest->close(0);
		delete pdbDest;
		dbenv.RemoveDb(newWalletFilename);  // unlinks the partial DB
		strError = "txn_begin failed";
		return false;
	}

	// Periodic commit to bound the BDB dirty-page cache. Wrapping all
	// records in a single transaction works fine on small wallets but
	// fails with ENOMEM (ret=12) on large ones -- BDB's cache fills up
	// with dirty pages waiting on commit. We commit every kCommitBatchSize
	// records and start a fresh transaction so cache pressure stays bounded.
	//
	// Mid-loop commit failures are treated identically to per-record put
	// failures: abort, close, RemoveDb, return error. Partial state from
	// earlier successful commits is wiped by RemoveDb. VerifyNewWallet's
	// cursor-walk count check (run by the orchestrator after we return)
	// catches the unlikely all-puts-succeed-but-record-count-wrong case.
	const size_t kCommitBatchSize = 10000;
	size_t batchInBatch = 0;

	for (size_t i = 0; i < records.size(); ++i)
	{
		Dbt datKey(&records[i].key[0], records[i].key.size());
		Dbt datValue(&records[i].value[0], records[i].value.size());
		// 0 = no DB_NOOVERWRITE: the cursor walk produced a unique-key
		// stream by definition, but if for any reason a duplicate slips
		// through we'd rather take the later value than silently drop.
		int ret2 = pdbDest->put(ptxn, &datKey, &datValue, 0);
		if (ret2 != 0)
		{
			ptxn->abort();
			pdbDest->close(0);
			delete pdbDest;
			dbenv.RemoveDb(newWalletFilename);
			strError = strprintf(
				"Per-record write failed at record %llu (ret=%d); "
				"partial destination removed",
				(unsigned long long)i, ret2);
			return false;
		}
		++batchInBatch;

		// Commit the batch and start a fresh txn at every boundary.
		// Skip on the very last record -- the post-loop commit handles it.
		if (batchInBatch >= kCommitBatchSize && i + 1 < records.size())
		{
			int rc = ptxn->commit(0);
			if (rc != 0)
			{
				pdbDest->close(0);
				delete pdbDest;
				dbenv.RemoveDb(newWalletFilename);
				strError = strprintf(
					"Mid-loop txn commit failed at record %llu (ret=%d); "
					"partial destination removed",
					(unsigned long long)i, rc);
				return false;
			}
			ptxn = dbenv.TxnBegin();
			if (ptxn == NULL)
			{
				pdbDest->close(0);
				delete pdbDest;
				dbenv.RemoveDb(newWalletFilename);
				strError = strprintf(
					"Mid-loop TxnBegin failed at record %llu; "
					"partial destination removed",
					(unsigned long long)i);
				return false;
			}
			batchInBatch = 0;

			// Periodic progress log so large rebuilds aren't silent.
			LogPrintf("CreateFromDump: %llu / %llu records written\n",
			          (unsigned long long)(i + 1),
			          (unsigned long long)records.size());
			uiInterface.InitMessage(strprintf("Writing records... %llu / %llu",
			                                  (unsigned long long)(i + 1),
			                                  (unsigned long long)records.size()));
		}
	}

	ret = ptxn->commit(0);
	if (ret != 0)
	{
		pdbDest->close(0);
		delete pdbDest;
		dbenv.RemoveDb(newWalletFilename);
		strError = strprintf("Final txn commit failed (ret=%d)", ret);
		return false;
	}

	pdbDest->close(0);
	delete pdbDest;

	// Force a checkpoint + lsn_reset so the new file is fully detached
	// from the env's log sequence and can be moved without disturbing
	// anything that opens it next.
	dbenv.dbenv.txn_checkpoint(0, 0, 0);
	dbenv.dbenv.lsn_reset(newWalletFilename.c_str(), 0);

	LogPrintf("CreateFromDump: wrote %llu records to %s\n",
	          (unsigned long long)records.size(),
	          newPath.string());
	(void)fSuccess;
	return true;
}

// ===========================================================================
// Marker-file helpers
// ===========================================================================
//
// The rebuild orchestrator runs in init.cpp's AppInit2 BEFORE LoadWallet --
// at that point there is no GUI yet, so we cannot show a dialog directly.
// Instead we leave a marker file alongside wallet.dat that the GUI reads
// and consumes on first paint after launch.
//
// Pending flag    .rebuildwallet-pending    written by GUI at logout, read
//                                           by handler at next launch
// Result marker   .rebuildwallet-result     written by handler at end of
//                                           rebuild attempt, read+deleted
//                                           by GUI on first paint
//
// These are intentionally hidden (leading dot) and live in datadir, NOT
// in QSettings -- they describe a property of THIS wallet.dat, not a
// preference of the user. See the discussion in the design doc for the
// full rationale.

namespace {

const char* kPendingFlagName = ".rebuildwallet-pending";
const char* kResultMarkerName = ".rebuildwallet-result";

const char* RebuildResultStateToToken(RebuildResultState s)
{
	switch (s)
	{
	case REBUILD_RESULT_SUCCESS:               return "success";
	case REBUILD_RESULT_RECOVERED_FROM_CRASH:  return "recovered_from_crash";
	case REBUILD_RESULT_FAILED_PRESWAP:        return "failed_preswap";
	case REBUILD_RESULT_FAILED_FILESYSTEM:     return "failed_filesystem";
	case REBUILD_RESULT_NONE:                  return "none";
	}
	return "none";
}

RebuildResultState RebuildResultTokenToState(const std::string& token)
{
	if (token == "success")               return REBUILD_RESULT_SUCCESS;
	if (token == "recovered_from_crash")  return REBUILD_RESULT_RECOVERED_FROM_CRASH;
	if (token == "failed_preswap")        return REBUILD_RESULT_FAILED_PRESWAP;
	if (token == "failed_filesystem")     return REBUILD_RESULT_FAILED_FILESYSTEM;
	return REBUILD_RESULT_NONE;
}

}  // namespace

bool RebuildPendingFlagWrite()
{
	boost::filesystem::path p = GetDataDir() / kPendingFlagName;
	boost::filesystem::ofstream out(p, std::ios::out | std::ios::trunc);
	if (!out.is_open()) return false;
	// Empty file -- presence is the signal.
	out.close();
	return true;
}

bool RebuildPendingFlagExists()
{
	return boost::filesystem::exists(GetDataDir() / kPendingFlagName);
}

bool RebuildPendingFlagRemove()
{
	boost::filesystem::path p = GetDataDir() / kPendingFlagName;
	if (!boost::filesystem::exists(p)) return true;
	boost::system::error_code ec;
	boost::filesystem::remove(p, ec);
	return !ec;
}

bool RebuildResultWrite(RebuildResultState state, const std::string& reason)
{
	boost::filesystem::path p = GetDataDir() / kResultMarkerName;
	boost::filesystem::ofstream out(p, std::ios::out | std::ios::trunc);
	if (!out.is_open()) return false;
	out << RebuildResultStateToToken(state) << "\n";
	if (!reason.empty())
	{
		out << reason << "\n";
	}
	out.close();
	return true;
}

RebuildResultState RebuildResultRead(std::string& reason)
{
	reason.clear();
	boost::filesystem::path p = GetDataDir() / kResultMarkerName;
	if (!boost::filesystem::exists(p))
	{
		return REBUILD_RESULT_NONE;
	}
	boost::filesystem::ifstream in(p, std::ios::in);
	if (!in.is_open())
	{
		return REBUILD_RESULT_NONE;
	}
	std::string token;
	if (!std::getline(in, token))
	{
		return REBUILD_RESULT_NONE;
	}
	StripCarriageReturn(token);
	std::string secondLine;
	if (std::getline(in, secondLine))
	{
		StripCarriageReturn(secondLine);
		reason = secondLine;
	}
	return RebuildResultTokenToState(token);
}

bool RebuildResultRemove()
{
	boost::filesystem::path p = GetDataDir() / kResultMarkerName;
	if (!boost::filesystem::exists(p)) return true;
	boost::system::error_code ec;
	boost::filesystem::remove(p, ec);
	return !ec;
}

// ===========================================================================
// RebuildWallet -- the orchestrator
// ===========================================================================

namespace {

// Verify the freshly-written destination by walking it via cursor and
// counting records. Anything other than (records == expected) is a fail.
bool VerifyNewWallet(CDBEnv& dbenv,
                     const std::string& newWalletFilename,
                     uint64_t expectedRecords,
                     std::string& strError)
{
	Db* pdb = new Db(&dbenv.dbenv, 0);
	int ret = pdb->open(NULL,
	                    newWalletFilename.c_str(),
	                    "main",
	                    DB_BTREE,
	                    DB_THREAD,  // read-only-ish, we don't pass DB_CREATE
	                    0);
	if (ret != 0)
	{
		delete pdb;
		strError = strprintf("Verify: cannot open %s (ret=%d)",
		                     newWalletFilename, ret);
		return false;
	}
	Dbc* pcursor = NULL;
	ret = pdb->cursor(NULL, &pcursor, 0);
	if (ret != 0 || pcursor == NULL)
	{
		pdb->close(0);
		delete pdb;
		strError = strprintf("Verify: cannot open cursor on %s (ret=%d)",
		                     newWalletFilename, ret);
		return false;
	}

	uint64_t count = 0;
	uiInterface.InitMessage(strprintf("Verifying... 0 / %llu",
	                                  (unsigned long long)expectedRecords));
	while (true)
	{
		Dbt datKey, datValue;
		int rc = pcursor->get(&datKey, &datValue, DB_NEXT);
		if (rc == DB_NOTFOUND) break;
		if (rc != 0)
		{
			pcursor->close();
			pdb->close(0);
			delete pdb;
			strError = strprintf("Verify: cursor error on %s (rc=%d) "
			                     "after %llu records",
			                     newWalletFilename, rc,
			                     (unsigned long long)count);
			return false;
		}
		++count;
		if ((count % 10000) == 0)
		{
			uiInterface.InitMessage(strprintf(
				"Verifying... %llu / %llu",
				(unsigned long long)count,
				(unsigned long long)expectedRecords));
		}
	}

	pcursor->close();
	pdb->close(0);
	delete pdb;

	if (count != expectedRecords)
	{
		strError = strprintf("Verify: record count mismatch: walked %llu, "
		                     "expected %llu",
		                     (unsigned long long)count,
		                     (unsigned long long)expectedRecords);
		return false;
	}

	LogPrintf("VerifyNewWallet: %llu records confirmed in %s\n",
	          (unsigned long long)count, newWalletFilename);
	return true;
}

// Wrapper around BDB's dbenv.dbrename that hides the lifecycle and logs
// each attempt. We use dbrename rather than boost::filesystem::rename
// so the env's internal mapping (mapDb, log positions) stays consistent
// with what's on disk. dbrename with DB_AUTO_COMMIT flushes any pending
// log entries before doing the OS-level rename.
//
// On next launch (process restart), the env starts fresh and only knows
// about whatever files exist under the env path; the crash-recovery
// helper at the top of RebuildWallet uses plain filesystem operations
// because there's no live env state to keep in sync.
bool DoDbRename(CDBEnv& dbenv,
                const std::string& fromName,
                const std::string& toName,
                std::string& strError)
{
	int rc = dbenv.dbenv.dbrename(NULL, fromName.c_str(), NULL,
	                              toName.c_str(), DB_AUTO_COMMIT);
	if (rc != 0)
	{
		strError = strprintf("dbrename %s -> %s failed: BDB error %d",
		                     fromName, toName, rc);
		return false;
	}
	LogPrintf("RebuildWallet: dbrename %s -> %s\n", fromName, toName);
	return true;
}

// Same convention BackupWallet uses: portable filesystem rename. Used
// only by the crash-recovery path where the env doesn't yet know about
// the files. Logs each attempt.
bool DoRename(const boost::filesystem::path& from,
              const boost::filesystem::path& to,
              std::string& strError)
{
	boost::system::error_code ec;
	boost::filesystem::rename(from, to, ec);
	if (ec)
	{
		strError = strprintf("rename %s -> %s failed: %s",
		                     from.string(), to.string(), ec.message());
		return false;
	}
	LogPrintf("RebuildWallet: renamed %s -> %s\n",
	          from.string(), to.string());
	return true;
}

}  // namespace

bool RebuildWallet(CDBEnv& dbenv,
                   const std::string& walletFilename,
                   std::string& strError)
{
	strError.clear();

	const boost::filesystem::path datadir = GetDataDir();
	const boost::filesystem::path pathLive = datadir / walletFilename;
	const boost::filesystem::path pathDump = datadir / (walletFilename + ".dump");
	const boost::filesystem::path pathNew  = datadir / (walletFilename + ".new");
	const boost::filesystem::path pathBak  = datadir / (walletFilename + ".bak");

	const std::string newFilename = walletFilename + ".new";

	// -----------------------------------------------------------------
	// Crash-recovery: if a prior rebuild was interrupted between the two
	// renames (wallet.dat -> .bak, then .new -> wallet.dat), the user's
	// state on disk is: no wallet.dat, but .bak and .new both exist. The
	// recovery is mechanical and unambiguous: complete the second rename.
	// We do this BEFORE the pre-flight checks because pre-flight would
	// otherwise refuse on the present .bak.
	// -----------------------------------------------------------------
	if (!boost::filesystem::exists(pathLive)
	    && boost::filesystem::exists(pathBak)
	    && boost::filesystem::exists(pathNew))
	{
		LogPrintf("RebuildWallet: detected interrupted rebuild "
		          "(no %s, but %s and %s present); completing the swap.\n",
		          walletFilename, pathBak.string(), pathNew.string());

		std::string renameErr;
		if (!DoRename(pathNew, pathLive, renameErr))
		{
			strError = strprintf(
				"Recovery from interrupted rebuild failed: %s. "
				"Manually rename %s -> %s and start the wallet.",
				renameErr, pathNew.string(), pathLive.string());
			RebuildResultWrite(REBUILD_RESULT_FAILED_FILESYSTEM, strError);
			return false;
		}
		// Clean up any stale dump from the same interrupted run.
		boost::system::error_code ec;
		boost::filesystem::remove(pathDump, ec);

		RebuildResultWrite(REBUILD_RESULT_RECOVERED_FROM_CRASH,
		                   "A prior wallet rebuild was interrupted. The "
		                   "rebuild has been completed and your previous "
		                   "wallet is preserved as " + pathBak.filename().string());
		LogPrintf("RebuildWallet: interrupted rebuild completed successfully.\n");
		return true;
	}

	// -----------------------------------------------------------------
	// Pre-flight checks. ALL are hard refusals; the user explicitly
	// requested a rebuild and we want to avoid clobbering anything
	// unexpected. If anything looks off, leave the original wallet
	// alone and tell the user.
	// -----------------------------------------------------------------
	if (!boost::filesystem::exists(pathLive))
	{
		strError = strprintf("Source wallet %s does not exist",
		                     pathLive.string());
		RebuildResultWrite(REBUILD_RESULT_FAILED_FILESYSTEM, strError);
		return false;
	}
	if (boost::filesystem::exists(pathBak))
	{
		strError = strprintf(
			"%s already exists from a previous rebuild. Please move or "
			"remove it before rebuilding again.", pathBak.string());
		RebuildResultWrite(REBUILD_RESULT_FAILED_FILESYSTEM, strError);
		return false;
	}
	if (boost::filesystem::exists(pathNew))
	{
		strError = strprintf(
			"Stale %s exists from a previous failed run. Please remove "
			"it before rebuilding.", pathNew.string());
		RebuildResultWrite(REBUILD_RESULT_FAILED_FILESYSTEM, strError);
		return false;
	}
	if (boost::filesystem::exists(pathDump))
	{
		strError = strprintf(
			"Stale %s exists from a previous failed run. Please remove "
			"it before rebuilding.", pathDump.string());
		RebuildResultWrite(REBUILD_RESULT_FAILED_FILESYSTEM, strError);
		return false;
	}

	// Disk-space pre-flight. Need ~2x source size: one copy as the dump
	// (hex-encoded, ~2x source bytes) AND one copy as the new wallet
	// (similar size to source) -- worst case all three coexist briefly
	// before the dump is deleted post-rename. We're conservative.
	boost::system::error_code ec;
	uintmax_t srcSize = boost::filesystem::file_size(pathLive, ec);
	if (ec)
	{
		strError = strprintf("Cannot stat %s: %s", pathLive.string(),
		                     ec.message());
		RebuildResultWrite(REBUILD_RESULT_FAILED_FILESYSTEM, strError);
		return false;
	}
	boost::filesystem::space_info space =
		boost::filesystem::space(datadir, ec);
	if (!ec)
	{
		uintmax_t need = static_cast<uintmax_t>(srcSize) * 2;
		if (space.available < need)
		{
			strError = strprintf(
				"Insufficient free disk space: need ~%llu bytes "
				"(2x wallet size), have %llu free",
				(unsigned long long)need,
				(unsigned long long)space.available);
			RebuildResultWrite(REBUILD_RESULT_FAILED_FILESYSTEM, strError);
			return false;
		}
	}
	// If space() failed, log it but proceed -- not all filesystems support
	// space queries, and the user explicitly asked to rebuild.

	// -----------------------------------------------------------------
	// Phase 1: dump.
	// -----------------------------------------------------------------
	LogPrintf("RebuildWallet: phase 1 (dump) starting.\n");
	std::string dumpErr;
	if (!DumpAllRecords(dbenv, walletFilename, pathDump, dumpErr))
	{
		strError = strprintf("Dump phase failed: %s", dumpErr);
		// DumpAllRecords cleans up its own partial output.
		RebuildResultWrite(REBUILD_RESULT_FAILED_PRESWAP, strError);
		return false;
	}

	// Close any open handle to the source wallet within this process so
	// the file is fully released. The init.cpp call site has not opened
	// the wallet yet, but Verify() is about to be called and will need
	// the env in a clean state. Flushing here is cheap insurance.
	dbenv.Flush(false);

	// -----------------------------------------------------------------
	// Phase 2: create from dump.
	// -----------------------------------------------------------------
	LogPrintf("RebuildWallet: phase 2 (create from dump) starting.\n");
	std::string createErr;
	if (!CreateFromDump(dbenv, pathDump, newFilename, createErr))
	{
		strError = strprintf("Create phase failed: %s", createErr);
		// CreateFromDump cleans up its own partial output, but the dump
		// is still on disk -- remove it so the user's datadir is tidy.
		boost::system::error_code ec2;
		boost::filesystem::remove(pathDump, ec2);
		RebuildResultWrite(REBUILD_RESULT_FAILED_PRESWAP, strError);
		return false;
	}

	// -----------------------------------------------------------------
	// Phase 3: verify new wallet by counting records.
	// -----------------------------------------------------------------
	LogPrintf("RebuildWallet: phase 3 (verify) starting.\n");

	// We need the expected record count. Pull it from the dump footer.
	uint64_t expectedRecords = 0;
	{
		boost::filesystem::ifstream in(pathDump, std::ios::in);
		if (!in.is_open())
		{
			strError = "Verify: cannot reopen dump for record count read";
			RebuildResultWrite(REBUILD_RESULT_FAILED_PRESWAP, strError);
			boost::system::error_code ec3;
			boost::filesystem::remove(pathDump, ec3);
			boost::filesystem::remove(pathNew, ec3);
			return false;
		}
		std::string ln;
		while (std::getline(in, ln))
		{
			StripCarriageReturn(ln);
			if (ln.find("# checksum") != 0) continue;
			size_t pos = ln.find("records=");
			if (pos == std::string::npos) break;
			try
			{
				expectedRecords = static_cast<uint64_t>(
					std::stoull(ln.substr(pos + 8)));
			}
			catch (const std::exception&) {}
			break;
		}
	}

	std::string verifyErr;
	if (!VerifyNewWallet(dbenv, newFilename, expectedRecords, verifyErr))
	{
		strError = strprintf("Verify phase failed: %s", verifyErr);
		boost::system::error_code ec4;
		boost::filesystem::remove(pathDump, ec4);
		boost::filesystem::remove(pathNew, ec4);
		RebuildResultWrite(REBUILD_RESULT_FAILED_PRESWAP, strError);
		return false;
	}
	dbenv.Flush(false);

	// -----------------------------------------------------------------
	// Phase 4: file swap. Atomic renames in this exact order.
	// -----------------------------------------------------------------
	LogPrintf("RebuildWallet: phase 4 (file swap) starting.\n");
	uiInterface.InitMessage("Swapping files...");

	std::string renameErr;
	if (!DoDbRename(dbenv, walletFilename, walletFilename + ".bak", renameErr))
	{
		strError = strprintf("Swap phase failed (first rename): %s. "
		                     "Original wallet untouched.", renameErr);
		boost::system::error_code ec5;
		boost::filesystem::remove(pathDump, ec5);
		boost::filesystem::remove(pathNew, ec5);
		RebuildResultWrite(REBUILD_RESULT_FAILED_FILESYSTEM, strError);
		return false;
	}

	// DANGER ZONE: between the two renames, no file named wallet.dat
	// exists. On a same-filesystem rename this is microseconds. If we
	// crash here, the next launch will detect the state at the top of
	// this function and complete the swap.
	if (!DoDbRename(dbenv, newFilename, walletFilename, renameErr))
	{
		strError = strprintf("Swap phase failed (second rename): %s. "
		                     "Original wallet preserved as %s; new wallet "
		                     "preserved as %s. Restart will auto-recover.",
		                     renameErr, pathBak.string(), pathNew.string());
		// DO NOT delete .new or .bak here -- the recovery path on next
		// launch needs both. Just the dump can go.
		boost::system::error_code ec6;
		boost::filesystem::remove(pathDump, ec6);
		RebuildResultWrite(REBUILD_RESULT_FAILED_FILESYSTEM, strError);
		return false;
	}

	// -----------------------------------------------------------------
	// Phase 5: cleanup.
	// -----------------------------------------------------------------
	{
		boost::system::error_code ec7;
		boost::filesystem::remove(pathDump, ec7);
		// Per Q1 default: delete the dump on success too. Privacy wins
		// over forensic recovery; the .bak still exists if the user
		// needs to roll back.
	}

	uiInterface.InitMessage("Rebuild complete, loading wallet...");

	std::string okMsg = strprintf(
		"Wallet rebuilt successfully. Your previous wallet has been "
		"preserved as %s.", pathBak.filename().string());
	RebuildResultWrite(REBUILD_RESULT_SUCCESS, okMsg);
	LogPrintf("RebuildWallet: %s\n", okMsg);
	return true;
}
