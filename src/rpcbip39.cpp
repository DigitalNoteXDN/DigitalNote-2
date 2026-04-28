#include <bip39.h>
#include <bip39/mnemonic.h>
#include <bip39/seed.h>
#include <bip39/bip39_passphrase.h>

#include "util.h"
#include "json/json_spirit_value.h"
#include "enums/rpcerrorcode.h"
#include "rpcprotocol.h"
#include "rpcserver.h"
#include "ckey.h"
#include "cdigitalnotesecret.h"
#include "cwallet.h"
#include "init.h"
#include <openssl/crypto.h>

json_spirit::Value getrecoveryphrase(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 1)
	{
		throw std::runtime_error(
			"getrecoveryphrase \"passphrase\"\n"
			"\n"
			"Returns the 24-word BIP39 recovery phrase for this wallet.\n"
			"The wallet must be unlocked before calling this command.\n"
			"The phrase is derived deterministically from your wallet password.\n"
			"The same password always produces the same phrase.\n"
			"\n"
			"IMPORTANT: Keep this phrase secret. Anyone with this phrase and\n"
			"           your wallet.dat can access your funds.\n"
			"\n"
			"NOTE: For wallets encrypted with an older version of DigitalNote,\n"
			"      use the GUI first: Settings -> Recovery Phrase\n"
			"      to complete the one-time upgrade.\n"
			"\n"
			"Arguments:\n"
			"  passphrase  (string, required) Your wallet encryption password\n"
			"\n"
			"Result:\n"
			"  {\n"
			"    \"phrase\" : \"word1 word2 ... word24\"\n"
			"  }\n"
			"\n"
			"Examples:\n"
			+ HelpExampleCli("getrecoveryphrase", "\"my wallet password\"")
			+ HelpExampleRpc("getrecoveryphrase", "\"my wallet password\"")
		);
	}

	if (!pwalletMain)
		throw JSONRPCError(RPC_WALLET_ERROR, "Wallet not loaded.");

	if (!pwalletMain->IsCrypted())
		throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE,
			"Error: Wallet is not encrypted. Encrypt your wallet first.");

	// Wallet must be unlocked - we do not unlock it here
	if (pwalletMain->IsLocked())
		throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED,
			"Error: Wallet is locked. "
			"Please unlock with walletpassphrase before calling getrecoveryphrase.");

	// Validate passphrase
	SecureString strPassphrase;
	strPassphrase.reserve(100);
	strPassphrase = params[0].get_str().c_str();

	if (strPassphrase.empty())
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Passphrase cannot be empty.");

	// Verify the password is correct
	if (!pwalletMain->VerifyPassphrase(strPassphrase))
		throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT,
			"Error: The wallet passphrase entered was incorrect.");

	// Wallet must have gone through the recovery phrase upgrade
	// (Settings -> Recovery Phrase in GUI performs this one-time step)
	if (!pwalletMain->HasRecoveryPhraseFlag())
		throw JSONRPCError(RPC_WALLET_ERROR,
			"Error: This wallet has not yet been upgraded for recovery phrase support. "
			"Use the GUI: Settings -> Recovery Phrase to complete the one-time upgrade. "
			"Note: Multiple recovery phrases are not supported - one phrase per wallet.");

	// Derive the recovery mnemonic from the passphrase
	SecureString mnemonic;
	BIP39Passphrase::Result res = BIP39Passphrase::mnemonicFromPassphrase(strPassphrase, mnemonic);
	OPENSSL_cleanse(const_cast<char*>(strPassphrase.data()), strPassphrase.size());

	if (res != BIP39Passphrase::Result::OK)
		throw JSONRPCError(RPC_WALLET_ERROR, "Failed to generate recovery phrase.");

	json_spirit::Object result;
	result.push_back(json_spirit::Pair("phrase",
		std::string(mnemonic.begin(), mnemonic.end())));
	OPENSSL_cleanse(const_cast<char*>(mnemonic.data()), mnemonic.size());

	return result;
}
