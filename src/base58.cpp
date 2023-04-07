#include <cstring>
#include <algorithm>
#include <cassert>

#include "crypto/bmw/bmw512.h"
#include "cbignum_error.h"
#include "cbignum_ctx.h"
#include "cbignum.h"
#include "base58.h"

/* All alphanumeric characters except for "0", "I", "O", and "l" */
static const char* pszBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

bool DecodeBase58(const char* psz, std::vector<unsigned char>& vchRet)
{
	CBigNum_CTX pctx;
	vchRet.clear();
	CBigNum bn58 = 58;
	CBigNum bn = 0;
	CBigNum bnChar;

	// Skip leading spaces.
	while (*psz && isspace(*psz))
	{
		psz++;
	}

	// Skip and count leading '1's.
	int zeroes = 0;
	while (*psz == '1')
	{
		zeroes++;
		psz++;
	}

	// Convert big endian string to bignum
	for (const char* p = psz; *p; p++)
	{
		const char* p1 = strchr(pszBase58, *p);
		
		if (p1 == NULL)
		{
			while (isspace(*p))
			{
				p++;
			}
			
			if (*p != '\0')
			{
				return false;
			}
			
			break;
		}
		
		bnChar.setulong(p1 - pszBase58);
		
		if (!BN_mul(bn.to_bignum(), bn.to_bignum(), bn58.to_bignum(), pctx))
		{
			throw CBigNum_Error("DecodeBase58 : BN_mul failed");
		}
		
		bn += bnChar;
	}

	 // Get bignum as little endian data
	std::vector<unsigned char> vchTmp = bn.getvch();

	// Trim off sign byte if present
	if (vchTmp.size() >= 2 && vchTmp.end()[-1] == 0 && vchTmp.end()[-2] >= 0x80)
	{
		vchTmp.erase(vchTmp.end()-1);
	}

	// Restore leading zeros
	int nLeadingZeros = 0;
	for (const char* p = psz; *p == pszBase58[0]; p++)
	{
		nLeadingZeros++;
	}

	vchRet.assign(nLeadingZeros + vchTmp.size(), 0);

	// Convert little endian data to big endian
	reverse_copy(vchTmp.begin(), vchTmp.end(), vchRet.end() - vchTmp.size());

	return true;
}

std::string EncodeBase58(const unsigned char* pbegin, const unsigned char* pend)
{
	// Skip & count leading zeroes.
	int zeroes = 0;

	while (pbegin != pend && *pbegin == 0)
	{
		pbegin++;
		zeroes++;
	}

	// Allocate enough space in big-endian base58 representation.
	std::vector<unsigned char> b58((pend - pbegin) * 138 / 100 + 1); // log(256) / log(58), rounded up.

	// Process the bytes.
	while (pbegin != pend)
	{
		int carry = *pbegin;
		
		// Apply "b58 = b58 * 256 + ch".
		for (std::vector<unsigned char>::reverse_iterator it = b58.rbegin(); it != b58.rend(); it++)
		{
			carry += 256 * (*it);
			*it = carry % 58;
			carry /= 58;
		}
		
		assert(carry == 0);
		
		pbegin++;
	}

	// Skip leading zeroes in base58 result.
	std::vector<unsigned char>::iterator it = b58.begin();

	while (it != b58.end() && *it == 0)
	{
		it++;
	}

	// Translate the result into a string.
	std::string str;

	str.reserve(zeroes + (b58.end() - it));
	str.assign(zeroes, '1');

	while (it != b58.end())
	{
		str += pszBase58[*(it++)];
	}

	return str;
}

std::string EncodeBase58(const std::vector<unsigned char>& vch)
{
	return EncodeBase58(&vch[0], &vch[0] + vch.size());
}

bool DecodeBase58(const std::string& str, std::vector<unsigned char>& vchRet)
{
	return DecodeBase58(str.c_str(), vchRet);
}

std::string EncodeBase58Check(const std::vector<unsigned char>& vchIn)
{
	// add 4-byte hash check to the end
	std::vector<unsigned char> vch(vchIn);
	uint256 hash = Hash_bmw512(vch.begin(), vch.end());

	vch.insert(vch.end(), (unsigned char*)&hash, (unsigned char*)&hash + 4);

	return EncodeBase58(vch);
}

bool DecodeBase58Check(const char* psz, std::vector<unsigned char>& vchRet)
{
	if (!DecodeBase58(psz, vchRet) || (vchRet.size() < 4))
	{
		vchRet.clear();
		
		return false;
	}

	// re-calculate the checksum, insure it matches the included 4-byte checksum
	uint256 hash = Hash_bmw512(vchRet.begin(), vchRet.end()-4);

	if (memcmp(&hash, &vchRet.end()[-4], 4) != 0)
	{
		vchRet.clear();
		
		return false;
	}

	vchRet.resize(vchRet.size()-4);

	return true;
}

bool DecodeBase58Check(const std::string& str, std::vector<unsigned char>& vchRet)
{
	return DecodeBase58Check(str.c_str(), vchRet);
}

