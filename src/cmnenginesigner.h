#ifndef CMNENGINESIGNER_H
#define CMNENGINESIGNER_H

#include <string>
#include <vector>

class CTxIn;
class CPubKey;
class CKey;

/** Helper object for signing and checking signatures
 */
class CMNengineSigner
{
public:
	/// Is the inputs associated with this public key? (and there is 10000 XDN - checking if valid masternode)
	bool IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey);
	/// Set the private/public key values, returns true if successful
	bool SetKey(const std::string &strSecret, std::string& errorMessage, CKey& key, CPubKey& pubkey);
	/// Sign the message, returns true if successful
	bool SignMessage(const std::string &strMessage, std::string& errorMessage, std::vector<unsigned char>& vchSig, CKey key);
	/// Verify the message, returns true if succcessful
	bool VerifyMessage(CPubKey pubkey, std::vector<unsigned char>& vchSig, const std::string &strMessage, std::string& errorMessage);
};

#endif // CMNENGINESIGNER_H
