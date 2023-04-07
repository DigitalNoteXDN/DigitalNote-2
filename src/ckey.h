#ifndef CKEY_H
#define CKEY_H

#include "types/cprivkey.h"

class CKey;
class CPubKey;
class uint256;

bool operator==(const CKey &a, const CKey &b);

/** An encapsulated private key. */
class CKey
{
private:
	// Whether this private key is valid. We check for correctness when modifying the key
	// data, so fValid should always correspond to the actual state.
	bool fValid;

	// Whether the public key corresponding to this private key is (to be) compressed.
	bool fCompressed;

	// The actual byte data
	unsigned char vch[32];

	// Check whether the 32-byte array pointed to be vch is valid keydata.
	bool static Check(const unsigned char *vch);

public:
	CKey();
	CKey(const CKey &secret);
	~CKey();

	CKey& operator=(const CKey&) = default;

	friend bool operator==(const CKey &a, const CKey &b);

	// Initialize using begin and end iterators to byte data.
	template<typename T>
	void Set(const T pbegin, const T pend, bool fCompressedIn);

	// Simple read-only vector-like interface.
	unsigned int size() const;
	const unsigned char *begin() const;
	const unsigned char *end() const;

	// Check whether this private key is valid.
	bool IsValid() const;

	// Check whether the public key corresponding to this private key is (to be) compressed.
	bool IsCompressed() const;

	// Initialize from a CPrivKey (serialized OpenSSL private key data).
	bool SetPrivKey(const CPrivKey &vchPrivKey, bool fCompressed);

	// Generate a new private key using a cryptographic PRNG.
	void MakeNewKey(bool fCompressed);

	// Convert the private key to a CPrivKey (serialized OpenSSL private key data).
	// This is expensive.
	CPrivKey GetPrivKey() const;

	// Compute the public key from a private key.
	// This is expensive.
	CPubKey GetPubKey() const;

	/**
	 * Create a DER-serialized signature.
	 * The test_case parameter tweaks the deterministic nonce.
	 */
	bool Sign(const uint256& hash, std::vector<unsigned char>& vchSig, uint32_t test_case = 0) const;

	// Create a compact signature (65 bytes), which allows reconstructing the used public key.
	// The format is one header byte, followed by two times 32 bytes for the serialized r and s values.
	// The header byte: 0x1B = first key with even y, 0x1C = first key with odd y,
	//                  0x1D = second key with even y, 0x1E = second key with odd y,
	//                  add 0x04 for compressed keys.
	bool SignCompact(const uint256 &hash, std::vector<unsigned char>& vchSig) const;

	// Derive BIP32 child key.
	bool Derive(CKey& keyChild, unsigned char ccChild[32], unsigned int nChild, const unsigned char cc[32]) const;

	/**
	 * Verify thoroughly whether a private key and a public key match.
	 * This is done using a different mechanism than just regenerating it.
	 */
	bool VerifyPubKey(const CPubKey& vchPubKey) const;

	// Load private key and check that public key matches.
	bool Load(CPrivKey &privkey, CPubKey &vchPubKey, bool fSkipCheck);

	// Note: Not been used or declaired?!
	// Check whether an element of a signature (r or s) is valid.
	//static bool CheckSignatureElement(const unsigned char *vch, int len, bool half);
};

/** Initialize the elliptic curve support. May not be called twice without calling ECC_Stop first. */
void ECC_Start(void);

/** Deinitialize the elliptic curve support. No-op if ECC_Start wasn't called first. */
void ECC_Stop(void);

/** Check that required EC support is available at runtime. */
bool ECC_InitSanityCheck(void);

#endif // CKEY_H
