#include "compat.h"

#include <boost/assign/list_of.hpp>

#include "json/json_spirit_utils.h"

#include "enums/rpcerrorcode.h"
#include "rpcserver.h"
#include "txdb-leveldb.h"
#include "init.h"
#include "ctransaction.h"
#include "script.h"
#include "net.h"
#include "main_extern.h"
#include "ckey.h"
#include "ctxout.h"
#include "ctxin.h"
#include "cdigitalnotesecret.h"
#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "util.h"
#include "cblockindex.h"
#include "enums/serialize_type.h"
#include "ctxindex.h"
#include "version.h"
#include "rpcprotocol.h"

#ifdef ENABLE_WALLET
#include "coutput.h"
#include "cwallet.h"
#include "cwallettx.h"
#endif // ENABLE_WALLET

#include "rpcrawtransaction.h"

void ScriptPubKeyToJSON(const CScript& scriptPubKey, json_spirit::Object& out, bool fIncludeHex)
{
	txnouttype type;
	std::vector<CTxDestination> addresses;
	int nRequired;

	out.push_back(json_spirit::Pair("asm", scriptPubKey.ToString()));

	if (fIncludeHex)
	{
		out.push_back(json_spirit::Pair("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end())));
	}

	if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired))
	{
		out.push_back(json_spirit::Pair("type", GetTxnOutputType(type)));
		
		return;
	}

	out.push_back(json_spirit::Pair("reqSigs", nRequired));
	out.push_back(json_spirit::Pair("type", GetTxnOutputType(type)));

	json_spirit::Array a;
	for(const CTxDestination& addr : addresses)
	{
		a.push_back(CDigitalNoteAddress(addr).ToString());
	}

	out.push_back(json_spirit::Pair("addresses", a));
}

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry)
{
	entry.push_back(json_spirit::Pair("txid", tx.GetHash().GetHex()));
	entry.push_back(json_spirit::Pair("version", tx.nVersion));
	entry.push_back(json_spirit::Pair("time", (int64_t)tx.nTime));
	entry.push_back(json_spirit::Pair("locktime", (int64_t)tx.nLockTime));

	json_spirit::Array vin;

	for(const CTxIn& txin : tx.vin)
	{
		json_spirit::Object in;
		
		if (tx.IsCoinBase())
		{
			in.push_back(json_spirit::Pair("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
		}
		else
		{
			in.push_back(json_spirit::Pair("txid", txin.prevout.hash.GetHex()));
			in.push_back(json_spirit::Pair("vout", (int64_t)txin.prevout.n));
			
			json_spirit::Object o;
			
			o.push_back(json_spirit::Pair("asm", txin.scriptSig.ToString()));
			o.push_back(json_spirit::Pair("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
			
			in.push_back(json_spirit::Pair("scriptSig", o));
		}
		
		in.push_back(json_spirit::Pair("sequence", (int64_t)txin.nSequence));
		vin.push_back(in);
	}

	entry.push_back(json_spirit::Pair("vin", vin));
	json_spirit::Array vout;

	for (unsigned int i = 0; i < tx.vout.size(); i++)
	{
		const CTxOut& txout = tx.vout[i];
		json_spirit::Object out;
		
		out.push_back(json_spirit::Pair("value", ValueFromAmount(txout.nValue)));
		out.push_back(json_spirit::Pair("n", (int64_t)i));
		
		json_spirit::Object o;
		
		ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
		
		out.push_back(json_spirit::Pair("scriptPubKey", o));
		vout.push_back(out);
	}

	entry.push_back(json_spirit::Pair("vout", vout));

	if (hashBlock != 0)
	{
		entry.push_back(json_spirit::Pair("blockhash", hashBlock.GetHex()));
		std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
		
		if (mi != mapBlockIndex.end() && (*mi).second)
		{
			CBlockIndex* pindex = (*mi).second;
			
			if (pindex->IsInMainChain())
			{
				entry.push_back(json_spirit::Pair("confirmations", 1 + nBestHeight - pindex->nHeight));
				entry.push_back(json_spirit::Pair("blocktime", (int64_t)pindex->nTime));
			}
			else
			{
				entry.push_back(json_spirit::Pair("confirmations", 0));
			}
		}
	}
}

json_spirit::Value getrawtransaction(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() < 1 || params.size() > 2)
	{
		throw std::runtime_error(
			"getrawtransaction <txid> [verbose=0]\n"
			"If verbose=0, returns a string that is\n"
			"serialized, hex-encoded data for <txid>.\n"
			"If verbose is non-zero, returns an json_spirit::Object\n"
			"with information about <txid>."
		);
	}

	uint256 hash;
	hash.SetHex(params[0].get_str());

	bool fVerbose = false;
	if (params.size() > 1)
	{
		fVerbose = (params[1].get_int() != 0);
	}

	CTransaction tx;
	uint256 hashBlock = 0;
	if (!GetTransaction(hash, tx, hashBlock))
	{
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");
	}

	CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);

	ssTx << tx;

	std::string strHex = HexStr(ssTx.begin(), ssTx.end());

	if (!fVerbose)
	{
		return strHex;
	}

	json_spirit::Object result;

	result.push_back(json_spirit::Pair("hex", strHex));

	TxToJSON(tx, hashBlock, result);

	return result;
}

#ifdef ENABLE_WALLET
json_spirit::Value listunspent(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() > 3)
	{
		throw std::runtime_error(
			"listunspent [minconf=1] [maxconf=9999999]  [\"address\",...]\n"
			"Returns array of unspent transaction outputs\n"
			"with between minconf and maxconf (inclusive) confirmations.\n"
			"Optionally filtered to only include txouts paid to specified addresses.\n"
			"Results are an array of json_spirit::Objects, each of which has:\n"
			"{txid, vout, scriptPubKey, amount, confirmations}"
		);
	}

	RPCTypeCheck(params, boost::assign::list_of(json_spirit::int_type)(json_spirit::int_type)(json_spirit::array_type));

	int nMinDepth = 1;
	if (params.size() > 0)
	{
		nMinDepth = params[0].get_int();
	}

	int nMaxDepth = 9999999;
	if (params.size() > 1)
	{
		nMaxDepth = params[1].get_int();
	}

	std::set<CDigitalNoteAddress> setAddress;

	if (params.size() > 2)
	{
		json_spirit::Array inputs = params[2].get_array();
		
		for(json_spirit::Value& input : inputs)
		{
			CDigitalNoteAddress address(input.get_str());
			
			if (!address.IsValid())
			{
				throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid DigitalNote address: ")+input.get_str());
			}
			
			if (setAddress.count(address))
			{
				throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ")+input.get_str());
			}
			
			setAddress.insert(address);
		}
	}

	json_spirit::Array results;
	std::vector<COutput> vecOutputs;

	assert(pwalletMain != NULL);

	pwalletMain->AvailableCoins(vecOutputs, false);

	for(const COutput& out : vecOutputs)
	{
		if (out.nDepth < nMinDepth || out.nDepth > nMaxDepth)
		{
			continue;
		}
		
		if(setAddress.size())
		{
			CTxDestination address;
			if(!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
			{
				continue;
			}
			
			if (!setAddress.count(address))
			{
				continue;
			}
		}

		int64_t nValue = out.tx->vout[out.i].nValue;
		const CScript& pk = out.tx->vout[out.i].scriptPubKey;
		json_spirit::Object entry;
		
		entry.push_back(json_spirit::Pair("txid", out.tx->GetHash().GetHex()));
		entry.push_back(json_spirit::Pair("vout", out.i));
		
		CTxDestination address;
		if (ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
		{
			entry.push_back(json_spirit::Pair("address", CDigitalNoteAddress(address).ToString()));
			
			if (pwalletMain->mapAddressBook.count(address))
			{
				entry.push_back(json_spirit::Pair("account", pwalletMain->mapAddressBook[address]));
			}
		}
		
		entry.push_back(json_spirit::Pair("scriptPubKey", HexStr(pk.begin(), pk.end())));
		
		if (pk.IsPayToScriptHash())
		{
			CTxDestination address;
			
			if (ExtractDestination(pk, address))
			{
				const CScriptID& hash = boost::get<CScriptID>(address);
				CScript redeemScript;
				
				if (pwalletMain->GetCScript(hash, redeemScript))
				{
					entry.push_back(json_spirit::Pair("redeemScript", HexStr(redeemScript.begin(), redeemScript.end())));
				}
			}
		}
		
		entry.push_back(json_spirit::Pair("amount",ValueFromAmount(nValue)));
		entry.push_back(json_spirit::Pair("confirmations",out.nDepth));
		entry.push_back(json_spirit::Pair("spendable", out.fSpendable));
		
		results.push_back(entry);
	}

	return results;
}
#endif // ENABLE_WALLET

json_spirit::Value createrawtransaction(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 2)
	{
		throw std::runtime_error(
			"createrawtransaction [{\"txid\":txid,\"vout\":n},...] {address:amount,...}\n"
			"Create a transaction spending given inputs\n"
			"(array of objects containing transaction id and output number),\n"
			"sending to given address(es).\n"
			"Returns hex-encoded raw transaction.\n"
			"Note that the transaction's inputs are not signed, and\n"
			"it is not stored in the wallet or transmitted to the network."
		);
	}

	RPCTypeCheck(params, boost::assign::list_of(json_spirit::array_type)(json_spirit::obj_type));

	json_spirit::Array inputs = params[0].get_array();
	json_spirit::Object sendTo = params[1].get_obj();

	CTransaction rawTx;

	for(json_spirit::Value& input : inputs)
	{
		const json_spirit::Object& o = input.get_obj();
		const json_spirit::Value& txid_v = find_value(o, "txid");
		
		if (txid_v.type() != json_spirit::str_type)
		{
			throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing txid key");
		}
		
		std::string txid = txid_v.get_str();
		if (!IsHex(txid))
		{
			throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");
		}
		
		const json_spirit::Value& vout_v = find_value(o, "vout");
		if (vout_v.type() != json_spirit::int_type)
		{
			throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
		}
		
		int nOutput = vout_v.get_int();
		if (nOutput < 0)
		{
			throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");
		}
		
		CTxIn in(COutPoint(uint256(txid), nOutput));
		rawTx.vin.push_back(in);
	}

	std::set<CDigitalNoteAddress> setAddress;

	for(const json_spirit::Pair& s : sendTo)
	{
		CDigitalNoteAddress address(s.name_);
		if (!address.IsValid())
		{
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid DigitalNote address: ")+s.name_);
		}
		
		if (setAddress.count(address))
		{
			throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ")+s.name_);
		}
		
		setAddress.insert(address);

		CScript scriptPubKey;
		scriptPubKey.SetDestination(address.Get());
		CAmount nAmount = AmountFromValue(s.value_);

		CTxOut out(nAmount, scriptPubKey);
		rawTx.vout.push_back(out);
	}

	CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

	ss << rawTx;

	return HexStr(ss.begin(), ss.end());
}

json_spirit::Value decoderawtransaction(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 1)
	{
		throw std::runtime_error(
			"decoderawtransaction <hex string>\n"
			"Return a JSON object representing the serialized, hex-encoded transaction."
		);
	}

	RPCTypeCheck(params, boost::assign::list_of(json_spirit::str_type));

	std::vector<unsigned char> txData(ParseHex(params[0].get_str()));
	CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
	CTransaction tx;

	try
	{
		ssData >> tx;
	}
	catch (std::exception &e)
	{
		throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
	}

	json_spirit::Object result;
	TxToJSON(tx, 0, result);

	return result;
}

json_spirit::Value decodescript(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 1)
	{
		throw std::runtime_error(
			"decodescript <hex string>\n"
			"Decode a hex-encoded script."
		);
	}

	RPCTypeCheck(params, boost::assign::list_of(json_spirit::str_type));

	json_spirit::Object r;
	CScript script;
	if (params[0].get_str().size() > 0)
	{
		std::vector<unsigned char> scriptData(ParseHexV(params[0], "argument"));
		
		script = CScript(scriptData.begin(), scriptData.end());
	}
	else
	{
		// Empty scripts are valid
	}

	ScriptPubKeyToJSON(script, r, false);

	r.push_back(json_spirit::Pair("p2sh", CDigitalNoteAddress(script.GetID()).ToString()));

	return r;
}

json_spirit::Value signrawtransaction(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() < 1 || params.size() > 4)
	{
		throw std::runtime_error(
			"signrawtransaction <hex string> [{\"txid\":txid,\"vout\":n,\"scriptPubKey\":hex,\"redeemScript\":hex},...] [<privatekey1>,...] [sighashtype=\"ALL\"]\n"
			"Sign inputs for raw transaction (serialized, hex-encoded).\n"
			"Second optional argument (may be null) is an array of previous transaction outputs that\n"
			"this transaction depends on but may not yet be in the blockchain.\n"
			"Third optional argument (may be null) is an array of base58-encoded private\n"
			"keys that, if given, will be the only keys used to sign the transaction.\n"
			"Fourth optional argument is a string that is one of six values; ALL, NONE, SINGLE or\n"
			"ALL|ANYONECANPAY, NONE|ANYONECANPAY, SINGLE|ANYONECANPAY.\n"
			"Returns json object with keys:\n"
			"  hex : raw transaction with signature(s) (hex-encoded string)\n"
			"  complete : 1 if transaction has a complete set of signature (0 if not)"
#ifdef ENABLE_WALLET
			+ HelpRequiringPassphrase()
#endif // ENABLE_WALLET
		);
	}

	RPCTypeCheck(params, boost::assign::list_of(json_spirit::str_type)(json_spirit::array_type)(json_spirit::array_type)(json_spirit::str_type), true);

	std::vector<unsigned char> txData(ParseHex(params[0].get_str()));
	CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
	std::vector<CTransaction> txVariants;

	while (!ssData.empty())
	{
		try {
			CTransaction tx;
			
			ssData >> tx;
			
			txVariants.push_back(tx);
		}
		catch (std::exception &e)
		{
			throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
		}
	}

	if (txVariants.empty())
	{
		throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");
	}

	// mergedTx will end up with all the signatures; it
	// starts as a clone of the rawtx:
	CTransaction mergedTx(txVariants[0]);
	bool fComplete = true;

	// Fetch previous transactions (inputs):
	std::map<COutPoint, CScript> mapPrevOut;

	for(unsigned int i = 0; i < mergedTx.vin.size(); i++)
	{
		CTransaction tempTx;
		MapPrevTx mapPrevTx;
		CTxDB txdb("r");
		std::map<uint256, CTxIndex> unused;
		bool fInvalid;

		// FetchInputs aborts on failure, so we go one at a time.
		tempTx.vin.push_back(mergedTx.vin[i]);
		tempTx.FetchInputs(txdb, unused, false, false, mapPrevTx, fInvalid);
		
		// Copy results into mapPrevOut:
		for(const CTxIn& txin : tempTx.vin)
		{
			const uint256& prevHash = txin.prevout.hash;
			
			if (mapPrevTx.count(prevHash) && mapPrevTx[prevHash].second.vout.size()>txin.prevout.n)
			{
				mapPrevOut[txin.prevout] = mapPrevTx[prevHash].second.vout[txin.prevout.n].scriptPubKey;
			}
		}
	}

	bool fGivenKeys = false;
	CBasicKeyStore tempKeystore;

	if (params.size() > 2 && params[2].type() != json_spirit::null_type)
	{
		fGivenKeys = true;
		json_spirit::Array keys = params[2].get_array();
		
		for(json_spirit::Value k : keys)
		{
			CDigitalNoteSecret vchSecret;
			bool fGood = vchSecret.SetString(k.get_str());
			
			if (!fGood)
			{
				throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
			}
			
			CKey key = vchSecret.GetKey();
			tempKeystore.AddKey(key);
		}
	}
#ifdef ENABLE_WALLET
	else
	{
		EnsureWalletIsUnlocked();
	}
#endif // ENABLE_WALLET

	// Add previous txouts given in the RPC call:
	if (params.size() > 1 && params[1].type() != json_spirit::null_type)
	{
		json_spirit::Array prevTxs = params[1].get_array();
		
		for(json_spirit::Value& p : prevTxs)
		{
			if (p.type() != json_spirit::obj_type)
			{
				throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");
			}
			
			json_spirit::Object prevOut = p.get_obj();

			RPCTypeCheck(prevOut, boost::assign::map_list_of("txid", json_spirit::str_type)("vout", json_spirit::int_type)("scriptPubKey", json_spirit::str_type));

			std::string txidHex = find_value(prevOut, "txid").get_str();
			if (!IsHex(txidHex))
			{
				throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "txid must be hexadecimal");
			}
			
			uint256 txid;
			txid.SetHex(txidHex);

			int nOut = find_value(prevOut, "vout").get_int();
			if (nOut < 0)
			{
				throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");
			}
			
			std::string pkHex = find_value(prevOut, "scriptPubKey").get_str();
			if (!IsHex(pkHex))
			{
				throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "scriptPubKey must be hexadecimal");
			}
			
			std::vector<unsigned char> pkData(ParseHex(pkHex));
			CScript scriptPubKey(pkData.begin(), pkData.end());

			COutPoint outpoint(txid, nOut);
			if (mapPrevOut.count(outpoint))
			{
				// Complain if scriptPubKey doesn't match
				if (mapPrevOut[outpoint] != scriptPubKey)
				{
					std::string err("Previous output scriptPubKey mismatch:\n");
					
					err = err + mapPrevOut[outpoint].ToString() + "\nvs:\n" + scriptPubKey.ToString();
					
					throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
				}
			}
			else
			{
				mapPrevOut[outpoint] = scriptPubKey;
			}
			
			// if redeemScript given and not using the local wallet (private keys
			// given), add redeemScript to the tempKeystore so it can be signed:
			if (fGivenKeys && scriptPubKey.IsPayToScriptHash())
			{
				RPCTypeCheck(prevOut, boost::assign::map_list_of("txid", json_spirit::str_type)("vout", json_spirit::int_type)("scriptPubKey", json_spirit::str_type)("redeemScript",json_spirit::str_type));
				json_spirit::Value v = find_value(prevOut, "redeemScript");
				
				if (!(v == json_spirit::Value::null))
				{
					std::vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
					CScript redeemScript(rsData.begin(), rsData.end());
					
					tempKeystore.AddCScript(redeemScript);
				}
			}
		}
	}

#ifdef ENABLE_WALLET
	const CKeyStore& keystore = ((fGivenKeys || !pwalletMain) ? tempKeystore : *pwalletMain);
#else // ENABLE_WALLET
	const CKeyStore& keystore = tempKeystore;
#endif // ENABLE_WALLET

	int nHashType = SIGHASH_ALL;
	
	if (params.size() > 3 && params[3].type() != json_spirit::null_type)
	{
		static std::map<std::string, int> mapSigHashValues =
			boost::assign::map_list_of(std::string("ALL"), int(SIGHASH_ALL))
			(std::string("ALL|ANYONECANPAY"), int(SIGHASH_ALL|SIGHASH_ANYONECANPAY))
			(std::string("NONE"), int(SIGHASH_NONE))
			(std::string("NONE|ANYONECANPAY"), int(SIGHASH_NONE|SIGHASH_ANYONECANPAY))
			(std::string("SINGLE"), int(SIGHASH_SINGLE))
			(std::string("SINGLE|ANYONECANPAY"), int(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY));
		
		std::string strHashType = params[3].get_str();
		
		if (mapSigHashValues.count(strHashType))
		{
			nHashType = mapSigHashValues[strHashType];
		}
		else
		{
			throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
		}
	}

	bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

	// Sign what we can:
	for (unsigned int i = 0; i < mergedTx.vin.size(); i++)
	{
		CTxIn& txin = mergedTx.vin[i];
		
		if (mapPrevOut.count(txin.prevout) == 0)
		{
			fComplete = false;
			
			continue;
		}
		
		const CScript& prevPubKey = mapPrevOut[txin.prevout];

		txin.scriptSig.clear();
		
		// Only sign SIGHASH_SINGLE if there's a corresponding output:
		if (!fHashSingle || (i < mergedTx.vout.size()))
		{
			SignSignature(keystore, prevPubKey, mergedTx, i, nHashType);
		}
		
		// ... and merge in other signatures:
		for(const CTransaction& txv : txVariants)
		{
			txin.scriptSig = CombineSignatures(prevPubKey, mergedTx, i, txin.scriptSig, txv.vin[i].scriptSig);
		}
		
		if (!VerifyScript(txin.scriptSig, prevPubKey, mergedTx, i, STANDARD_SCRIPT_VERIFY_FLAGS, 0))
		{
			fComplete = false;
		}
	}

	json_spirit::Object result;
	CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);

	ssTx << mergedTx;

	result.push_back(json_spirit::Pair("hex", HexStr(ssTx.begin(), ssTx.end())));
	result.push_back(json_spirit::Pair("complete", fComplete));

	return result;
}

json_spirit::Value sendrawtransaction(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() < 1 || params.size() > 1)
	{
		throw std::runtime_error(
			"sendrawtransaction <hex string>\n"
			"Submits raw transaction (serialized, hex-encoded) to local node and network."
		);
	}

	RPCTypeCheck(params, boost::assign::list_of(json_spirit::str_type));

	// parse hex string from parameter
	std::vector<unsigned char> txData(ParseHex(params[0].get_str()));
	CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
	CTransaction tx;

	// deserialize binary data stream
	try
	{
		ssData >> tx;
	}
	catch (std::exception &e)
	{
		throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
	}

	uint256 hashTx = tx.GetHash();

	// See if the transaction is already in a block
	// or in the memory pool:
	CTransaction existingTx;
	uint256 hashBlock = 0;

	if (GetTransaction(hashTx, existingTx, hashBlock))
	{
		if (hashBlock != 0)
		{
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("transaction already in block ")+hashBlock.GetHex());
		}
		
		// Not in block, but already in the memory pool; will drop
		// through to re-relay it.
	}
	else
	{
		// push to local node
		if (!AcceptToMemoryPool(mempool, tx, true, NULL, false, false, true))
		{
			throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX rejected");
		}
	}

	RelayTransaction(tx, hashTx);

	return hashTx.GetHex();
}

json_spirit::Value searchrawtransactions(const json_spirit::Array &params, bool fHelp)
{
	if (fHelp || params.size() < 1 || params.size() > 4)
	{
		throw std::runtime_error(
			"searchrawtransactions <address> [verbose=1] [skip=0] [count=100]\n"
		);
	}

	CDigitalNoteAddress address(params[0].get_str());
	if (!address.IsValid())
	{
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DigitalNote address");
	}

	CTxDestination dest = address.Get();

	std::vector<uint256> vtxhash;
	if (!FindTransactionsByDestination(dest, vtxhash))
	{
		throw JSONRPCError(RPC_DATABASE_ERROR, "Cannot search for address");
	}

	int nSkip = 0;
	int nCount = 100;
	bool fVerbose = true;

	if (params.size() > 1)
	{
		fVerbose = (params[1].get_int() != 0);
	}

	if (params.size() > 2)
	{
		nSkip = params[2].get_int();
	}

	if (params.size() > 3)
	{
		nCount = params[3].get_int();
	}

	if (nSkip < 0)
	{
		nSkip += vtxhash.size();
	}

	if (nSkip < 0)
	{
		nSkip = 0;
	}

	if (nCount < 0)
	{
		nCount = 0;
	}

	std::vector<uint256>::const_iterator it = vtxhash.begin();
	while (it != vtxhash.end() && nSkip--)
	{
		it++;
	}

	json_spirit::Array result;
	while (it != vtxhash.end() && nCount--)
	{
		CTransaction tx;
		uint256 hashBlock;
		
		if (!GetTransaction(*it, tx, hashBlock))
		{
		   // throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Cannot read transaction from disk");
		   json_spirit::Object obj;
		   obj.push_back(json_spirit::Pair("ERROR", "Cannot read transaction from disk"));
		   
		   result.push_back(obj);
		}
		else
		{
			CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
			
			ssTx << tx;
			
			std::string strHex = HexStr(ssTx.begin(), ssTx.end());
			if (fVerbose)
			{
				json_spirit::Object object;
				TxToJSON(tx, hashBlock, object);
				
				object.push_back(json_spirit::Pair("hex", strHex));
				result.push_back(object);
			}
			else
			{
				result.push_back(strHex);
			}
		}
		
		it++;
	}

	return result;
}

