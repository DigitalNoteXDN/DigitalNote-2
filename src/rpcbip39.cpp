#include <bip39.h>
#include <bip39/mnemonic.h>
#include <bip39/seed.h>

#include "util.h"
#include "json/json_spirit_value.h"
#include "enums/rpcerrorcode.h"
#include "rpcprotocol.h"
#include "ckey.h"
#include "cdigitalnotesecret.h"

json_spirit::Value bip39_new_mnemonic(const json_spirit::Array& params, bool fHelp)
{
	BIP39::Mnemonic mnemonic;
	BIP39::Entropy entropy;
	BIP39::CheckSum checksum;
	BIP39::Seed seed;
	CKey vchSecret;
	
	json_spirit::Object results;
	std::string lang_code = "EN";
	std::string mnemonic_str;
	
	if (fHelp || params.size() > 1)
	{
		throw std::runtime_error(
			"bip39_new_mnemonic [lang_code=EN]\n"
			"Generates a new BIP39 mnemonic."
		);
	}
	
	if (params.size() == 1)
	{
		lang_code = params[0].get_str();
	}
	
	// Load the english words database
	if(!mnemonic.LoadLanguage(lang_code))
	{
		return JSONRPCError(RPC_TYPE_ERROR, "Failed to load language.");
	}
	
	// Generate random entropy
	if(!entropy.genRandom())
	{
		return JSONRPCError(RPC_TYPE_ERROR, "Failed to generate new random entropy.");
	}
	
	// Generate checksum
	if(!entropy.genCheckSum(checksum))
	{
		return JSONRPCError(RPC_TYPE_ERROR, "Failed to generate checksum.");
	}
	
	// Generate mnemonic with entropy and checksum
	if(!mnemonic.Set(entropy, checksum))
	{
		return JSONRPCError(RPC_TYPE_ERROR, "Failed to generate mnemonic with entropy and checksum.");
	}
	
	// Get Seed
	seed = mnemonic.GetSeed();
	
	// Set seed inside key
	vchSecret.Set(seed.begin(), seed.end(), false);
	
	if(!vchSecret.IsValid())
	{
		return JSONRPCError(RPC_TYPE_ERROR, "Failed to generate private key with seed.");
	}
	
	mnemonic_str = mnemonic.GetStr();
	
	// results
	results.push_back(json_spirit::Pair("checksum", checksum.GetStr()));
	results.push_back(json_spirit::Pair("entropy", entropy.GetStr()));
	results.push_back(json_spirit::Pair("mnemonic", EncodeBase64(mnemonic_str)));
	results.push_back(json_spirit::Pair("seed", seed.GetStr()));
	results.push_back(json_spirit::Pair("private_key", CDigitalNoteSecret(vchSecret).ToString()));
	
	return results;
}

json_spirit::Value bip39_get_privkey(const json_spirit::Array& params, bool fHelp)
{
	BIP39::Mnemonic mnemonic;
	BIP39::Seed seed;
	CKey vchSecret;
	
	json_spirit::Object results;
	std::string mnemonic_str;
	std::string lang_code = "EN";

	if (fHelp || params.size() < 1 || params.size() > 2)
	{
		throw std::runtime_error(
			"bip39_get_privkey [mnemonic] [lang_code=EN]\n"
			"Generates private key with mnemonic words."
		);
	}
	
	mnemonic_str = params[0].get_str();
	
	if (params.size() == 2)
	{
		lang_code = params[1].get_str();
	}
	
	// Load the english words database
	if(!mnemonic.LoadLanguage(lang_code))
	{
		return JSONRPCError(RPC_TYPE_ERROR, "Failed to load language.");
	}
	
	// Generate mnemonic with mnemonic string
	if(!mnemonic.Set(mnemonic_str))
	{
		return JSONRPCError(RPC_TYPE_ERROR, "Failed to generate mnemonic with mnemonic string.");
	}
	
	// Get Seed
	seed = mnemonic.GetSeed();
	
	// Set seed inside key
	vchSecret.Set(seed.begin(), seed.end(), false);
	
	if(!vchSecret.IsValid())
	{
		return JSONRPCError(RPC_TYPE_ERROR, "Failed to generate private key with seed.");
	}
	
	// results
	results.push_back(json_spirit::Pair("checksum", mnemonic.GetCheckSum().GetStr()));
	results.push_back(json_spirit::Pair("entropy", mnemonic.GetEntropy().GetStr()));
	results.push_back(json_spirit::Pair("mnemonic", EncodeBase64(mnemonic_str)));
	results.push_back(json_spirit::Pair("seed", seed.GetStr()));
	results.push_back(json_spirit::Pair("private_key", CDigitalNoteSecret(vchSecret).ToString()));
	
	return results;
}

