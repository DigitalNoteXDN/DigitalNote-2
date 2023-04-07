#ifndef CSCRIPTCOMPRESSOR_H
#define CSCRIPTCOMPRESSOR_H

#include <vector>

class CScript;
class CKeyID;
class CScriptID;
class CPubKey;

/** Compact serializer for scripts.
 *
 *  It detects common cases and encodes them much more efficiently.
 *  3 special cases are defined:
 *  * Pay to pubkey hash (encoded as 21 bytes)
 *  * Pay to script hash (encoded as 21 bytes)
 *  * Pay to pubkey starting with 0x02, 0x03 or 0x04 (encoded as 33 bytes)
 *
 *  Other scripts up to 121 bytes require 1 byte + script length. Above
 *  that, scripts up to 16505 bytes require 2 bytes + script length.
 */
class CScriptCompressor
{
private:
	// make this static for now (there are only 6 special scripts defined)
	// this can potentially be extended together with a new nVersion for
	// transactions, in which case this value becomes dependent on nVersion
	// and nHeight of the enclosing transaction.
	static const unsigned int nSpecialScripts = 6;
	CScript &script;

protected:
	// These check for scripts for which a special case with a shorter encoding is defined.
	// They are implemented separately from the CScript test, as these test for exact byte
	// sequence correspondences, and are more strict. For example, IsToPubKey also verifies
	// whether the public key is valid (as invalid ones cannot be represented in compressed
	// form).
	bool IsToKeyID(CKeyID &hash) const;
	bool IsToScriptID(CScriptID &hash) const;
	bool IsToPubKey(CPubKey &pubkey) const;

	bool Compress(std::vector<unsigned char> &out) const;
	unsigned int GetSpecialSize(unsigned int nSize) const;
	bool Decompress(unsigned int nSize, const std::vector<unsigned char> &out);

public:
	CScriptCompressor(CScript &scriptIn);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream &s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream &s, int nType, int nVersion);
};

#endif // CSCRIPTCOMPRESSOR_H
