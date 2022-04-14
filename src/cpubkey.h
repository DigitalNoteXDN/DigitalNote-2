#ifndef CPUBKEY_H
#define CPUBKEY_H

#include <vector>

class CPubKey;
class CKeyID;
class uint256;

bool operator==(const CPubKey &a, const CPubKey &b);
bool operator!=(const CPubKey &a, const CPubKey &b);
bool operator<(const CPubKey &a, const CPubKey &b);
	
/** An encapsulated public key. */
class CPubKey
{
private:
	// Just store the serialized data.
	// Its length can very cheaply be computed from the first byte.
	unsigned char vch[65];

	// Compute the length of a pubkey with a given first byte.
	static unsigned int GetLen(unsigned char chHeader);

	// Set this key data to be invalid
	void Invalidate();

public:
	// Construct an invalid public key.
	CPubKey();

	// Construct a public key using begin/end iterators to byte data.
	#if defined(__clang__)
		template<typename T>
		CPubKey(const T pbegin, const T pend)
		{
			Set(pbegin, pend);
		}	
	#else // defined(__clang__)
		template<typename T>
		CPubKey(const T pbegin, const T pend);
	#endif // defined(__clang__)

	// Construct a public key from a byte vector.
	CPubKey(const std::vector<unsigned char> &vch);

	// Initialize a public key using begin/end iterators to byte data.
	template<typename T>
	void Set(const T pbegin, const T pend);

	// Simple read-only vector-like interface to the pubkey data.
	unsigned int size() const;
	const unsigned char *begin() const;
	const unsigned char *end() const;
	const unsigned char &operator[](unsigned int pos) const;

	// Comparator implementation.
	friend bool operator==(const CPubKey &a, const CPubKey &b);
	friend bool operator!=(const CPubKey &a, const CPubKey &b);
	friend bool operator<(const CPubKey &a, const CPubKey &b);

	// Implement serialization, as if this was a byte vector.
	unsigned int GetSerializeSize(int nType, int nVersion) const;

	template<typename Stream>
	void Serialize(Stream &s, int nType, int nVersion) const;

	template<typename Stream>
	void Unserialize(Stream &s, int nType, int nVersion);

	// Get the KeyID of this public key (hash of its serialization)
	CKeyID GetID() const;

	// Get the 256-bit hash of this public key.
	uint256 GetHash() const;

	// Check syntactic correctness.
	//
	// Note that this is consensus critical as CheckSig() calls it!
	bool IsValid() const;

	// fully validate whether this is a valid public key (more expensive than IsValid())
	bool IsFullyValid() const;

	// Check whether this is a compressed public key.
	bool IsCompressed() const;

	// Verify a DER signature (~72 bytes).
	// If this public key is not fully valid, the return value will be false.
	bool Verify(const uint256 &hash, const std::vector<unsigned char>& vchSig) const;

	/**
	 * Check whether a signature is normalized (lower-S).
	 */
	static bool CheckLowS(const std::vector<unsigned char>& vchSig);

	// Recover a public key from a compact signature.
	bool RecoverCompact(const uint256 &hash, const std::vector<unsigned char>& vchSig);

	// Turn this public key into an uncompressed public key.
	bool Decompress();

	// Derive BIP32 child pubkey.
	bool Derive(CPubKey& pubkeyChild, unsigned char ccChild[32], unsigned int nChild, const unsigned char cc[32]) const;

	// Raw for stealth address
	std::vector<unsigned char> Raw() const;
};

#endif // CPUBKEY_H
