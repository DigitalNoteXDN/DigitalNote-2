#include <iostream>
#include <fstream>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/variant/get.hpp>
#include <boost/algorithm/string.hpp>

#include "enums/rpcerrorcode.h"
#include "init.h" // for pwalletMain
#include "rpcserver.h"
#include "cwallet.h"
#include "cwallettx.h"
#include "walletdb.h"
#include "cblockindex.h"
#include "wallet.h"
#include "main_extern.h"
#include "thread.h"
#include "ckey.h"
#include "cscript.h"
#include "util.h"
#include "script.h"
#include "cdigitalnotesecret.h"
#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "ui_translate.h"
#include "ckeymetadata.h"
#include "version.h"
#include "rpcprotocol.h"
#include "db.h"
#include "walletrebuild.h"
#include <boost/filesystem/path.hpp>

void EnsureWalletIsUnlocked();

// Extended DecodeDumpTime implementation, see this page for details:
// http://stackoverflow.com/questions/3786201/parsing-of-date-time-from-string-boost
const std::locale formats[] = {
	std::locale(std::locale::classic(),new boost::posix_time::time_input_facet("%Y-%m-%dT%H:%M:%SZ")),
	std::locale(std::locale::classic(),new boost::posix_time::time_input_facet("%Y-%m-%d %H:%M:%S")),
	std::locale(std::locale::classic(),new boost::posix_time::time_input_facet("%Y/%m/%d %H:%M:%S")),
	std::locale(std::locale::classic(),new boost::posix_time::time_input_facet("%d.%m.%Y %H:%M:%S")),
	std::locale(std::locale::classic(),new boost::posix_time::time_input_facet("%Y-%m-%d"))
};

const size_t formats_n = sizeof(formats) / sizeof(formats[0]);

std::time_t pt_to_time_t(const boost::posix_time::ptime& pt)
{
	boost::posix_time::ptime timet_start(boost::gregorian::date(1970,1,1));
	boost::posix_time::time_duration diff = pt - timet_start;

	return diff.ticks() / boost::posix_time::time_duration::rep_type::ticks_per_second;
}

int64_t DecodeDumpTime(const std::string& s)
{
	boost::posix_time::ptime pt;

	for(size_t i = 0; i< formats_n; ++i)
	{
		std::istringstream is(s);
		
		is.imbue(formats[i]);
		is >> pt;
		
		if(pt != boost::posix_time::ptime())
		{
			break;
		}
	}

	return pt_to_time_t(pt);
}

std::string static EncodeDumpTime(int64_t nTime)
{
	return DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", nTime);
}

std::string static EncodeDumpString(const std::string &str)
{
	std::stringstream ret;

	for(unsigned char c : str)
	{
		if (c <= 32 || c >= 128 || c == '%')
		{
			ret << '%' << HexStr(&c, &c + 1);
		}
		else
		{
			ret << c;
		}
	}

	return ret.str();
}

std::string DecodeDumpString(const std::string &str)
{
	std::stringstream ret;

	for (unsigned int pos = 0; pos < str.length(); pos++)
	{
		unsigned char c = str[pos];
		
		if (c == '%' && pos+2 < str.length())
		{
			c = (((str[pos+1]>>6)*9+((str[pos+1]-'0')&15)) << 4) |
				((str[pos+2]>>6)*9+((str[pos+2]-'0')&15));
			pos += 2;
		}
		
		ret << c;
	}

	return ret.str();
}

class CTxDump
{
public:
	CBlockIndex *pindex;
	int64_t nValue;
	bool fSpent;
	CWalletTx* ptx;
	int nOut;

	CTxDump(CWalletTx* ptx = NULL, int nOut = -1)
	{
		this->pindex = NULL;
		this->nValue = 0;
		this->fSpent = false;
		this->ptx = ptx;
		this->nOut = nOut;
	}
};

json_spirit::Value importprivkey(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() < 1 || params.size() > 3)
	{
		throw std::runtime_error(
			"importprivkey <DigitalNoteprivkey> [label] [rescan=true]\n"
			"Adds a private key (as returned by dumpprivkey) to your wallet."
		);
	}

	std::string strSecret = params[0].get_str();
	std::string strLabel = "";
	if (params.size() > 1)
	{
		strLabel = params[1].get_str();
	}

	// Whether to perform rescan after import
	bool fRescan = true;
	if (params.size() > 2)
	{
		fRescan = params[2].get_bool();
	}

	CDigitalNoteSecret vchSecret;
	bool fGood = vchSecret.SetString(strSecret);

	if (!fGood)
	{
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
	}

	if (fWalletUnlockStakingOnly)
	{
		throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is unlocked for staking only.");
	}

	if (!pwalletMain->ImportPrivateKey(vchSecret, strLabel, fRescan))
	{
		throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");
	}

	CKey key = vchSecret.GetKey();
	CPubKey pubkey = key.GetPubKey();
	assert(key.VerifyPubKey(pubkey));

	CKeyID vchAddress = pubkey.GetID();

	LOCK2(cs_main, pwalletMain->cs_wallet);

	pwalletMain->MarkDirty();
	pwalletMain->SetAddressBookName(vchAddress, strLabel);

	// Don't throw error in case a key is already there
	if (pwalletMain->HaveKey(vchAddress))
	{
		return json_spirit::Value::null;
	}

	pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

	if (!pwalletMain->AddKeyPubKey(key, pubkey))
	{
		throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");
	}

	// whenever a key is imported, we need to scan the whole chain
	pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'

	if (fRescan)
	{
		pwalletMain->ScanForWalletTransactions(pindexGenesisBlock, true);
		pwalletMain->ReacceptWalletTransactions();
	}

	return json_spirit::Value::null;
}

json_spirit::Value importaddress(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() < 1 || params.size() > 3)
	{
		throw std::runtime_error(
			"importaddress \"address\" ( \"label\" rescan )\n"
			"\nAdds an address or script (in hex) that can be watched as if it were in your wallet but cannot be used to spend.\n"
			"\nArguments:\n"
			"1. \"address\"          (string, required) The address\n"
			"2. \"label\"            (string, optional, default=\"\") An optional label\n"
			"3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"
			"\nNote: This call can take minutes to complete if rescan is true.\n"
			"\nExamples:\n"
			"\nImport an address with rescan\n"
			+ HelpExampleCli("importaddress", "\"myaddress\"") +
			"\nImport using a label without rescan\n"
			+ HelpExampleCli("importaddress", "\"myaddress\" \"testing\" false") +
			"\nAs a JSON-RPC call\n"
			+ HelpExampleRpc("importaddress", "\"myaddress\", \"testing\", false")
		);
	}

	CScript script;

	CDigitalNoteAddress address(params[0].get_str());
	if (address.IsValid())
	{
		script = GetScriptForDestination(address.Get());
	}
	else if (IsHex(params[0].get_str()))
	{
		std::vector<unsigned char> data(ParseHex(params[0].get_str()));
		script = CScript(data.begin(), data.end());
	}
	else
	{
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DigitalNote address or script");
	}

	std::string strLabel = "";
	if (params.size() > 1)
	{
		strLabel = params[1].get_str();
	}

	// Whether to perform rescan after import
	bool fRescan = true;
	if (params.size() > 2)
	{
		fRescan = params[2].get_bool();
	}

	if (::IsMine(*pwalletMain, script) == ISMINE_SPENDABLE)
	{
		throw JSONRPCError(RPC_WALLET_ERROR, "The wallet already contains the private key for this address or script");
	}

	// add to address book or update label
	if (address.IsValid())
	{
		pwalletMain->SetAddressBookName(address.Get(), strLabel);
	}

	// Don't throw error in case an address is already there
	if (pwalletMain->HaveWatchOnly(script))
	{
		return json_spirit::Value::null;
	}

	pwalletMain->MarkDirty();

	if (!pwalletMain->AddWatchOnly(script))
	{
		throw JSONRPCError(RPC_WALLET_ERROR, "Error adding address to wallet");
	}

	if (fRescan)
	{
		pwalletMain->ScanForWalletTransactions(pindexGenesisBlock, true);
		pwalletMain->ReacceptWalletTransactions();
	}

	return json_spirit::Value::null;
}

json_spirit::Value removeaddress(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 1)
	{
		throw std::runtime_error(
			"removeaddress \"address\"\n"
			"\nStops the wallet from tracking a watch-only address.\n"
			"This does not affect any funds (watch-only is observe-only); it simply\n"
			"removes the address from the wallet's tracking set.\n"
			"\nArguments:\n"
			"1. \"address\"          (string, required) The address or script (in hex) to stop watching\n"
			"\nResult:\n"
			"true|false             (boolean) Whether the address was removed\n"
			"\nExamples:\n"
			"\nStop watching an address\n"
			+ HelpExampleCli("removeaddress", "\"myaddress\"") +
			"\nAs a JSON-RPC call\n"
			+ HelpExampleRpc("removeaddress", "\"myaddress\"")
		);
	}

	CScript script;

	CDigitalNoteAddress address(params[0].get_str());
	if (address.IsValid())
	{
		script = GetScriptForDestination(address.Get());
	}
	else if (IsHex(params[0].get_str()))
	{
		std::vector<unsigned char> data(ParseHex(params[0].get_str()));
		script = CScript(data.begin(), data.end());
	}
	else
	{
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DigitalNote address or script");
	}

	LOCK2(cs_main, pwalletMain->cs_wallet);

	if (!pwalletMain->HaveWatchOnly(script))
	{
		throw JSONRPCError(RPC_WALLET_ERROR, "Address is not currently watched");
	}

	if (!pwalletMain->RemoveWatchOnly(script))
	{
		throw JSONRPCError(RPC_WALLET_ERROR, "Error removing watch-only address from wallet");
	}

	pwalletMain->MarkDirty();

	return true;
}

json_spirit::Value importwallet(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 1)
	{
		throw std::runtime_error(
			"importwallet <filename>\n"
			"Imports keys from a wallet dump file (see dumpwallet)."
		);
	}

	EnsureWalletIsUnlocked();

	std::ifstream file;
	file.open(params[0].get_str().c_str());
	if (!file.is_open())
	{
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");
	}

	int64_t nTimeBegin = pindexBest->nTime;
	int64_t nFilesize = std::max((int64_t)1, (int64_t)file.tellg());
	bool fGood = true;

	pwalletMain->ShowProgress(ui_translate("Importing..."), 0); // show progress dialog in GUI

	while (file.good())
	{
		pwalletMain->ShowProgress("", std::max(1, std::min(99, (int)(((double)file.tellg() / (double)nFilesize) * 100))));
		
		std::string line;
		std::getline(file, line);
		if (line.empty() || line[0] == '#')
		{
			continue;
		}
		
		std::vector<std::string> vstr;
		boost::split(vstr, line, boost::is_any_of(" "));
		if (vstr.size() < 2)
		{
			continue;
		}
		
		CDigitalNoteSecret vchSecret;
		if (!vchSecret.SetString(vstr[0]))
		{
			continue;
		}
		
		CKey key = vchSecret.GetKey();
		CPubKey pubkey = key.GetPubKey();
		
		assert(key.VerifyPubKey(pubkey));
		
		CKeyID keyid = pubkey.GetID();
		if (pwalletMain->HaveKey(keyid))
		{
			LogPrintf("Skipping import of %s (key already present)\n", CDigitalNoteAddress(keyid).ToString());
			
			continue;
		}
		
		int64_t nTime = DecodeDumpTime(vstr[1]);
		std::string strLabel;
		bool fLabel = true;
		
		for (unsigned int nStr = 2; nStr < vstr.size(); nStr++)
		{
			if (boost::algorithm::starts_with(vstr[nStr], "#"))
			{
				break;
			}
			
			if (vstr[nStr] == "change=1")
			{
				fLabel = false;
			}
			
			if (vstr[nStr] == "reserve=1")
			{
				fLabel = false;
			}
			
			if (boost::algorithm::starts_with(vstr[nStr], "label="))
			{
				strLabel = DecodeDumpString(vstr[nStr].substr(6));
				fLabel = true;
			}
		}
		
		LogPrintf("Importing %s...\n", CDigitalNoteAddress(keyid).ToString());
		
		if (!pwalletMain->AddKey(key))
		{
			fGood = false;
			
			continue;
		}
		
		pwalletMain->mapKeyMetadata[keyid].nCreateTime = nTime;
		
		if (fLabel)
		{
			pwalletMain->SetAddressBookName(keyid, strLabel);
		}
		
		nTimeBegin = std::min(nTimeBegin, nTime);
	}

	file.close();
	pwalletMain->ShowProgress("", 100); // hide progress dialog in GUI

	CBlockIndex *pindex = pindexBest;
	while (pindex && pindex->pprev && pindex->nTime > nTimeBegin - 7200)
	{
		pindex = pindex->pprev;
	}

	if (!pwalletMain->nTimeFirstKey || nTimeBegin < pwalletMain->nTimeFirstKey)
	{
		pwalletMain->nTimeFirstKey = nTimeBegin;
	}

	LogPrintf("Rescanning last %i blocks\n", pindexBest->nHeight - pindex->nHeight + 1);

	pwalletMain->ScanForWalletTransactions(pindex);
	pwalletMain->ReacceptWalletTransactions();
	pwalletMain->MarkDirty();

	if (!fGood)
	{
		throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys to wallet");
	}

	return json_spirit::Value::null;
}

json_spirit::Value dumpprivkey(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 1)
	{
		throw std::runtime_error(
			"dumpprivkey <DigitalNote>\n"
			"Reveals the private key corresponding to <DigitalNote>."
		);
	}

	EnsureWalletIsUnlocked();

	std::string strAddress = params[0].get_str();
	CDigitalNoteAddress address;
	if (!address.SetString(strAddress))
	{
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DigitalNote address");
	}

	if (fWalletUnlockStakingOnly)
	{
		throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is unlocked for staking only.");
	}

	CKeyID keyID;
	if (!address.GetKeyID(keyID))
	{
		throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
	}

	CKey vchSecret;
	if (!pwalletMain->GetKey(keyID, vchSecret))
	{
		throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
	}

	return CDigitalNoteSecret(vchSecret).ToString();
}

json_spirit::Value dumpwallet(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 1)
	{
		throw std::runtime_error(
			"dumpwallet <filename>\n"
			"Dumps all wallet keys in a human-readable format."
		);
	}

	EnsureWalletIsUnlocked();

	std::ofstream file;
	file.open(params[0].get_str().c_str());
	if (!file.is_open())
	{
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");
	}

	std::map<CKeyID, int64_t> mapKeyBirth;
	std::set<CKeyID> setKeyPool;

	pwalletMain->GetKeyBirthTimes(mapKeyBirth);
	pwalletMain->GetAllReserveKeys(setKeyPool);

	// sort time/key pairs
	std::vector<std::pair<int64_t, CKeyID> > vKeyBirth;
	for (std::map<CKeyID, int64_t>::const_iterator it = mapKeyBirth.begin(); it != mapKeyBirth.end(); it++)
	{
		vKeyBirth.push_back(std::make_pair(it->second, it->first));
	}

	mapKeyBirth.clear();
	std::sort(vKeyBirth.begin(), vKeyBirth.end());

	// produce output
	file << strprintf("# Wallet dump created by DigitalNote %s (%s)\n", CLIENT_BUILD, CLIENT_DATE);
	file << strprintf("# * Created on %s\n", EncodeDumpTime(GetTime()));
	file << strprintf("# * Best block at time of backup was %i (%s),\n", nBestHeight, hashBestChain.ToString());
	file << strprintf("#   mined on %s\n", EncodeDumpTime(pindexBest->nTime));
	file << "\n";

	for (std::vector<std::pair<int64_t, CKeyID> >::const_iterator it = vKeyBirth.begin(); it != vKeyBirth.end(); it++)
	{
		const CKeyID &keyid = it->second;
		std::string strTime = EncodeDumpTime(it->first);
		std::string strAddr = CDigitalNoteAddress(keyid).ToString();

		CKey key;
		if (pwalletMain->GetKey(keyid, key))
		{
			if (pwalletMain->mapAddressBook.count(keyid))
			{
				file << strprintf(
					"%s %s label=%s # addr=%s\n",
					CDigitalNoteSecret(key).ToString(),
					strTime,
					EncodeDumpString(pwalletMain->mapAddressBook[keyid]),
					strAddr
				);
			}
			else if (setKeyPool.count(keyid))
			{
				file << strprintf("%s %s reserve=1 # addr=%s\n", CDigitalNoteSecret(key).ToString(), strTime, strAddr);
			}
			else
			{
				file << strprintf("%s %s change=1 # addr=%s\n", CDigitalNoteSecret(key).ToString(), strTime, strAddr);
			}
		}
	}
	
	file << "\n";
	file << "# End of dump\n";
	file.close();

	return json_spirit::Value::null;
}


json_spirit::Value dumprawwallet(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 1)
	{
		throw std::runtime_error(
			"dumprawwallet \"filename\"\n"
			"\n"
			"Hidden RPC. Dumps every BDB record in the wallet (in cursor order)\n"
			"to <filename> in the rebuild-dump v1 format. Unlike dumpwallet,\n"
			"this preserves ALL record types -- watch-only addresses, A4 coin\n"
			"locks, stealth addresses, multisig redeem scripts, the BIP39\n"
			"mnemonic master key, address book entries, transaction history --\n"
			"so the dumpfile can be used to reconstruct an exact-equivalent\n"
			"wallet.dat (modulo BDB free-page bloat) via createfromdumpfile or\n"
			"the -rebuildwallet startup path.\n"
			"\n"
			"Does not require the wallet to be unlocked: the dumpfile contains\n"
			"encrypted-key records as-is, never plaintext private keys.\n"
			"\n"
			"WARNING: while this RPC runs, BDB writes from the live wallet may\n"
			"interleave with cursor reads, producing an inconsistent snapshot.\n"
			"For a guaranteed-consistent dump use the -rebuildwallet startup\n"
			"path, which runs with the wallet closed.\n"
			"\n"
			"Note: on Windows, always quote paths to avoid backslash mangling:\n"
			"  dumprawwallet \"C:\\temp\\test.dump\"        (correct)\n"
			"  dumprawwallet C:\\temp\\test.dump          (avoid; relies on\n"
			"                                              the GUI console's\n"
			"                                              escape parsing)"
		);
	}

	boost::filesystem::path dumpfilePath(params[0].get_str());
	std::string strError;
	if (!DumpAllRecords(bitdb, "wallet.dat", dumpfilePath, strError))
	{
		throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}

	return json_spirit::Value::null;
}


json_spirit::Value createfromdumpfile(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 2)
	{
		throw std::runtime_error(
			"createfromdumpfile \"dumpfile\" \"new-wallet-filename\"\n"
			"\n"
			"Hidden RPC. Reads a v1 dumpfile (as produced by dumprawwallet\n"
			"or by the -rebuildwallet startup path) and writes a fresh BDB\n"
			"wallet at <new-wallet-filename> within the data directory.\n"
			"Refuses to overwrite an existing destination.\n"
			"\n"
			"Validates the dump's double-SHA256 checksum and record count\n"
			"BEFORE creating any destination state. A malformed or tampered\n"
			"dumpfile produces no partial output.\n"
			"\n"
			"This is the inverse of dumprawwallet. Together they implement\n"
			"the same dump-and-restore mechanism that the -rebuildwallet\n"
			"orchestrator uses internally; this RPC is exposed for testing\n"
			"and for advanced manual recovery workflows.\n"
			"\n"
			"Note: <new-wallet-filename> is a filename relative to the data\n"
			"directory, NOT an absolute path. The new file is created via\n"
			"the same BDB environment as the live wallet.\n"
			"\n"
			"Note: on Windows, always quote the dumpfile path to avoid\n"
			"backslash mangling:\n"
			"  createfromdumpfile \"C:\\temp\\test.dump\" roundtrip.dat   (correct)\n"
			"  createfromdumpfile C:\\temp\\test.dump roundtrip.dat     (avoid)"
		);
	}

	boost::filesystem::path dumpfilePath(params[0].get_str());
	std::string newFilename = params[1].get_str();
	std::string strError;
	if (!CreateFromDump(bitdb, dumpfilePath, newFilename, strError))
	{
		throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}

	return json_spirit::Value::null;
}
