#include <limits>

#include "cscript.h"
#include "cvarint.h"

#include "serialize/base.h"

// Used to bypass the rule against non-const reference to temporary
// where it makes sense with wrappers such as CFlatData or CTxDB
template<typename T>
T& REF(const T& val)
{
    return const_cast<T&>(val);
}

/**
 * Used to acquire a non-const pointer "this" to generate bodies
 * of const serialization operations from a template
 */
template<typename T>
T* NCONST_PTR(const T* val)
{
    return const_cast<T*>(val);
}

/**
 * Get begin pointer of vector (non-const version).
 * @note These functions avoid the undefined case of indexing into an empty
 * vector, as well as that of indexing after the end of the vector.
 */
template <class T, class TAl>
T* begin_ptr(std::vector<T, TAl>& v)
{
	return v.empty() ? NULL : &v[0];
}

/** Get begin pointer of vector (const version) */
template <class T, class TAl>
const T* begin_ptr(const std::vector<T, TAl>& v)
{
	return v.empty() ? NULL : &v[0];
}

/** Get end pointer of vector (non-const version) */
template <class T, class TAl>
T* end_ptr(std::vector<T, TAl>& v)
{
	return v.empty() ? NULL : (&v[0] + v.size());
}

/** Get end pointer of vector (const version) */
template <class T, class TAl>
const T* end_ptr(const std::vector<T, TAl>& v)
{
	return v.empty() ? NULL : (&v[0] + v.size());
}

//
// Compact size
//  size <  253        -- 1 byte
//  size <= USHRT_MAX  -- 3 bytes  (253 + 2 bytes)
//  size <= UINT_MAX   -- 5 bytes  (254 + 4 bytes)
//  size >  UINT_MAX   -- 9 bytes  (255 + 8 bytes)
//
unsigned int GetSizeOfCompactSize(uint64_t nSize)
{
	if (nSize < 253)
	{
		return sizeof(unsigned char);
	}
	else if (nSize <= std::numeric_limits<unsigned short>::max())
	{
		return sizeof(unsigned char) + sizeof(unsigned short);
	}
	else if (nSize <= std::numeric_limits<unsigned int>::max())
	{
		return sizeof(unsigned char) + sizeof(unsigned int);
	}
	else
	{
		return sizeof(unsigned char) + sizeof(uint64_t);
	}
}

//
// Forward declaire
//
class CFlatData;
class CSporkMessage;

//
// Generate template 
//

// REF
template int& REF<int>(int const&);
template unsigned int& REF<unsigned int>(unsigned int const&);
template long& REF<long>(long const&);
template long long& REF<long long>(long long const&);
template CScript& REF<CScript>(CScript const&);
template CFlatData& REF<CFlatData>(CFlatData const&);
template CVarInt<int>& REF<CVarInt<int>>(CVarInt<int> const&);
template CVarInt<long>& REF<CVarInt<long>>(CVarInt<long> const&);
template CVarInt<long long>& REF<CVarInt<long long>>(CVarInt<long long> const&);
template CVarInt<unsigned int>& REF<CVarInt<unsigned int>>(CVarInt<unsigned int> const&);

// NCONST_PTR
template CSporkMessage* NCONST_PTR<CSporkMessage>(CSporkMessage const*);

// begin_ptr
template unsigned char* begin_ptr<unsigned char, std::allocator<unsigned char>>(std::vector<unsigned char, std::allocator<unsigned char>>&);