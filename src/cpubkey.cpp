#include <cstring>
#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include "serialize.h"
#include "ckeyid.h"
#include "hash.h"
#include "uint/uint256.h"
#include "crypto/bmw/bmw512.h"
#include "secp256k1_context_verify.h"
#include "cdatastream.h"

#include "cpubkey.h"

/** This function is taken from the libsecp256k1 distribution and implements
 *  DER parsing for ECDSA signatures, while supporting an arbitrary subset of
 *  format violations.
 *
 *  Supported violations include negative integers, excessive padding, garbage
 *  at the end, and overly long length descriptors. This is safe to use in
 *  DigitalNote because since the activation of BIP66, signatures are verified to be
 *  strict DER before being passed to this module, and we know it supports all
 *  violations present in the blockchain before that point.
 */
static int ecdsa_signature_parse_der_lax(const secp256k1_context* ctx, secp256k1_ecdsa_signature* sig,
		const unsigned char *input, size_t inputlen)
{
	size_t rpos, rlen, spos, slen;
	size_t pos = 0;
	size_t lenbyte;
	unsigned char tmpsig[64] = {0};
	int overflow = 0;

	/* Hack to initialize sig with a correctly-parsed but invalid signature. */
	secp256k1_ecdsa_signature_parse_compact(ctx, sig, tmpsig);

	/* Sequence tag byte */
	if (pos == inputlen || input[pos] != 0x30)
	{
		return 0;
	}

	pos++;

	/* Sequence length bytes */
	if (pos == inputlen)
	{
		return 0;
	}

	lenbyte = input[pos++];

	if (lenbyte & 0x80)
	{
		lenbyte -= 0x80;
		
		if (pos + lenbyte > inputlen)
		{
			return 0;
		}
		
		pos += lenbyte;
	}

	/* Integer tag byte for R */
	if (pos == inputlen || input[pos] != 0x02)
	{
		return 0;
	}

	pos++;

	/* Integer length for R */
	if (pos == inputlen)
	{
		return 0;
	}

	lenbyte = input[pos++];

	if (lenbyte & 0x80)
	{
		lenbyte -= 0x80;
		if (pos + lenbyte > inputlen)
		{
			return 0;
		}
		
		while (lenbyte > 0 && input[pos] == 0)
		{
			pos++;
			lenbyte--;
		}
		
		if (lenbyte >= sizeof(size_t))
		{
			return 0;
		}
		
		rlen = 0;
		
		while (lenbyte > 0)
		{
			rlen = (rlen << 8) + input[pos];
			pos++;
			lenbyte--;
		}
	}
	else
	{
		rlen = lenbyte;
	}

	if (rlen > inputlen - pos)
	{
		return 0;
	}

	rpos = pos;
	pos += rlen;

	/* Integer tag byte for S */
	if (pos == inputlen || input[pos] != 0x02)
	{
		return 0;
	}

	pos++;

	/* Integer length for S */
	if (pos == inputlen)
	{
		return 0;
	}

	lenbyte = input[pos++];

	if (lenbyte & 0x80)
	{
		lenbyte -= 0x80;
		if (pos + lenbyte > inputlen)
		{
			return 0;
		}
		
		while (lenbyte > 0 && input[pos] == 0)
		{
			pos++;
			lenbyte--;
		}
		
		if (lenbyte >= sizeof(size_t))
		{
			return 0;
		}
		
		slen = 0;
		
		while (lenbyte > 0)
		{
			slen = (slen << 8) + input[pos];
			pos++;
			lenbyte--;
		}
	}
	else
	{
		slen = lenbyte;
	}

	if (slen > inputlen - pos)
	{
		return 0;
	}

	spos = pos;
	pos += slen;

	/* Ignore leading zeroes in R */
	while (rlen > 0 && input[rpos] == 0)
	{
		rlen--;
		rpos++;
	}

	/* Copy R value */
	if (rlen > 32)
	{
		overflow = 1;
	}
	else
	{
		memcpy(tmpsig + 32 - rlen, input + rpos, rlen);
	}

	/* Ignore leading zeroes in S */
	while (slen > 0 && input[spos] == 0)
	{
		slen--;
		spos++;
	}

	/* Copy S value */
	if (slen > 32)
	{
		overflow = 1;
	}
	else
	{
		memcpy(tmpsig + 64 - slen, input + spos, slen);
	}

	if (!overflow)
	{
		overflow = !secp256k1_ecdsa_signature_parse_compact(ctx, sig, tmpsig);
	}

	if (overflow)
	{
		/* Overwrite the result again with a correctly-parsed but invalid
		   signature if parsing failed. */
		memset(tmpsig, 0, 64);
		
		secp256k1_ecdsa_signature_parse_compact(ctx, sig, tmpsig);
	}

	return 1;
}

// Comparator implementation.
bool operator==(const CPubKey &a, const CPubKey &b)
{
	return a.vch[0] == b.vch[0] &&
		   memcmp(a.vch, b.vch, a.size()) == 0;
}

bool operator!=(const CPubKey &a, const CPubKey &b)
{
	return !(a == b);
}

bool operator<(const CPubKey &a, const CPubKey &b)
{
	return a.vch[0] < b.vch[0] ||
		   (a.vch[0] == b.vch[0] && memcmp(a.vch, b.vch, a.size()) < 0);
}

// Construct an invalid public key.
CPubKey::CPubKey()
{
	Invalidate();
}

// Construct a public key using begin/end iterators to byte data.
#if !defined(__clang__)
	template<typename T>
	CPubKey::CPubKey(const T pbegin, const T pend)
	{
		Set(pbegin, pend);
	}

	template CPubKey::CPubKey<unsigned char*>(unsigned char*, unsigned char*);
#endif // !defined(__clang__)

// Construct a public key from a byte vector.
CPubKey::CPubKey(const std::vector<unsigned char> &vch)
{
	Set(vch.begin(), vch.end());
}

// Initialize a public key using begin/end iterators to byte data.
template<typename T>
void CPubKey::Set(const T pbegin, const T pend)
{
	int len = pend == pbegin ? 0 : GetLen(pbegin[0]);

	if (len && len == (pend-pbegin))
	{
		memcpy(vch, (unsigned char*)&pbegin[0], len);
	}
	else
	{
		Invalidate();
	}
}

template void CPubKey::Set<unsigned char*>(unsigned char*, unsigned char*);
template void CPubKey::Set<unsigned char const*>(unsigned char const*, unsigned char const*);

// Simple read-only vector-like interface to the pubkey data.
unsigned int CPubKey::size() const
{
	return GetLen(vch[0]);
}

const unsigned char* CPubKey::begin() const
{
	return vch;
}

const unsigned char* CPubKey::end() const
{
	return vch+size();
}

const unsigned char& CPubKey::operator[](unsigned int pos) const
{
	return vch[pos];
}

// Implement serialization, as if this was a byte vector.
unsigned int CPubKey::GetSerializeSize(int nType, int nVersion) const
{
	return size() + 1;
}

template<typename Stream>
void CPubKey::Serialize(Stream &s, int nType, int nVersion) const
{
	unsigned int len = size();
	
	::WriteCompactSize(s, len);
	
	s.write((char*)vch, len);
}

template void CPubKey::Serialize<CDataStream>(CDataStream&, int, int) const;

template<typename Stream>
void CPubKey::Unserialize(Stream &s, int nType, int nVersion)
{
	unsigned int len = ::ReadCompactSize(s);
	
	if (len <= 65)
	{
		s.read((char*)vch, len);
	}
	else
	{
		// invalid pubkey, skip available data
		char dummy;
		
		while (len--)
		{
			s.read(&dummy, 1);
		}
		
		Invalidate();
	}
}

template void CPubKey::Unserialize<CDataStream>(CDataStream&, int, int);

// Get the KeyID of this public key (hash of its serialization)
CKeyID CPubKey::GetID() const
{
	return CKeyID(Hash160(vch, vch+size()));
}

// Get the 256-bit hash of this public key.
uint256 CPubKey::GetHash() const
{
	return Hash_bmw512(vch, vch+size());
}

// Check syntactic correctness.
//
// Note that this is consensus critical as CheckSig() calls it!
bool CPubKey::IsValid() const
{
	return size() > 0;
}

bool CPubKey::IsFullyValid() const
{
	if (!IsValid())
	{
		return false;
	}

	secp256k1_pubkey pubkey;

	return secp256k1_ec_pubkey_parse(secp256k1_context_verify, &pubkey, &(*this)[0], size());
}

// Check whether this is a compressed public key.
bool CPubKey::IsCompressed() const
{
	return size() == 33;
}

bool CPubKey::Verify(const uint256 &hash, const std::vector<unsigned char>& vchSig) const
{
	if (!IsValid())
	{
		return false;
	}

	secp256k1_pubkey pubkey;
	secp256k1_ecdsa_signature sig;

	if (!secp256k1_ec_pubkey_parse(secp256k1_context_verify, &pubkey, &(*this)[0], size()))
	{
		return false;
	}

	if (vchSig.size() == 0)
	{
		return false;
	}

	if (!ecdsa_signature_parse_der_lax(secp256k1_context_verify, &sig, &vchSig[0], vchSig.size()))
	{
		return false;
	}

	/* libsecp256k1's ECDSA verification requires lower-S signatures, which have
	 * not historically been enforced in DigitalNote, so normalize them first. */
	secp256k1_ecdsa_signature_normalize(secp256k1_context_verify, &sig, &sig);

	return secp256k1_ecdsa_verify(secp256k1_context_verify, &sig, hash.begin(), &pubkey);
}

/* static */
bool CPubKey::CheckLowS(const std::vector<unsigned char>& vchSig)
{
	secp256k1_ecdsa_signature sig;

	if (!ecdsa_signature_parse_der_lax(secp256k1_context_verify, &sig, &vchSig[0], vchSig.size()))
	{
		return false;
	}

	return (!secp256k1_ecdsa_signature_normalize(secp256k1_context_verify, NULL, &sig));
}

bool CPubKey::RecoverCompact(const uint256 &hash, const std::vector<unsigned char>& vchSig)
{
	if (vchSig.size() != 65)
	{
		return false;
	}

	int recid = (vchSig[0] - 27) & 3;
	bool fComp = ((vchSig[0] - 27) & 4) != 0;
	secp256k1_pubkey pubkey;
	secp256k1_ecdsa_recoverable_signature sig;

	if (!secp256k1_ecdsa_recoverable_signature_parse_compact(secp256k1_context_verify, &sig, &vchSig[1], recid))
	{
		return false;
	}

	if (!secp256k1_ecdsa_recover(secp256k1_context_verify, &pubkey, &sig, hash.begin()))
	{
		return false;
	}

	unsigned char pub[65];
	size_t publen = 65;

	secp256k1_ec_pubkey_serialize(
		secp256k1_context_verify,
		pub,
		&publen,
		&pubkey,
		fComp ? SECP256K1_EC_COMPRESSED : SECP256K1_EC_UNCOMPRESSED
	);
	
	Set(pub, pub + publen);

	return true;
}

bool CPubKey::Decompress()
{
    if (!IsValid())
	{
        return false;
	}
	
    secp256k1_pubkey pubkey;
    
	if (!secp256k1_ec_pubkey_parse(secp256k1_context_verify, &pubkey, &(*this)[0], size()))
	{
        return false;
    }
	
    unsigned char pub[65];
    size_t publen = 65;
    
	secp256k1_ec_pubkey_serialize(secp256k1_context_verify, pub, &publen, &pubkey, SECP256K1_EC_UNCOMPRESSED);
    
	Set(pub, pub + publen);
	
    return true;
}

bool CPubKey::Derive(CPubKey& pubkeyChild, unsigned char ccChild[32], unsigned int nChild, const unsigned char cc[32]) const
{
    assert(IsValid());
    assert((nChild >> 31) == 0);
    assert(begin() + 33 == end());
    
	unsigned char out[64];
    BIP32Hash(cc, nChild, *begin(), begin()+1, out);
    
	memcpy(ccChild, out+32, 32);
    
	secp256k1_pubkey pubkey;
	
    if (!secp256k1_ec_pubkey_parse(secp256k1_context_verify, &pubkey, &(*this)[0], size()))
	{
        return false;
    }
	
    if (!secp256k1_ec_pubkey_tweak_add(secp256k1_context_verify, &pubkey, out))
	{
        return false;
    }
	
    unsigned char pub[33];
    size_t publen = 33;
    
	secp256k1_ec_pubkey_serialize(secp256k1_context_verify, pub, &publen, &pubkey, SECP256K1_EC_COMPRESSED);
    pubkeyChild.Set(pub, pub + publen);
    
	return true;
}

// Raw for stealth address
std::vector<unsigned char> CPubKey::Raw() const
{
	std::vector<unsigned char> r;

	r.insert(r.end(), vch, vch+size());

	return r;
}

/*
	Private
*/
// Compute the length of a pubkey with a given first byte.
unsigned int CPubKey::GetLen(unsigned char chHeader)
{
	if (chHeader == 2 || chHeader == 3)
	{
		return 33;
	}

	if (chHeader == 4 || chHeader == 6 || chHeader == 7)
	{
		return 65;
	}

	return 0;
}

// Set this key data to be invalid
void CPubKey::Invalidate()
{
	vch[0] = 0xFF;
}

