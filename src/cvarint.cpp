#include "serialize.h"
#include "cdatastream.h"
#include "cautofile.h"
#include "chashwriter.h"

#include "cvarint.h"

// Variable-length integers: bytes are a MSB base-128 encoding of the number.
// The high bit in each byte signifies whether another digit follows. To make
// the encoding is one-to-one, one is subtracted from all but the last digit.
// Thus, the byte sequence a[] with length len, where all but the last byte
// has bit 128 set, encodes the number:
//
//   (a[len-1] & 0x7F) + sum(i=1..len-1, 128^i*((a[len-i-1] & 0x7F)+1))
//
// Properties:
// * Very small (0-127: 1 byte, 128-16511: 2 bytes, 16512-2113663: 3 bytes)
// * Every integer has exactly one encoding
// * Encoding does not depend on size of original integer type
// * No redundancy: every (infinite) byte sequence corresponds to a list
//   of encoded integers.
//
// 0:         [0x00]  256:        [0x81 0x00]
// 1:         [0x01]  16383:      [0xFE 0x7F]
// 127:       [0x7F]  16384:      [0xFF 0x00]
// 128:  [0x80 0x00]  16511: [0x80 0xFF 0x7F]
// 255:  [0x80 0x7F]  65535: [0x82 0xFD 0x7F]
// 2^32:           [0x8E 0xFE 0xFE 0xFF 0x00]

template<typename I>
inline unsigned int GetSizeOfVarInt(I n)
{
    int nRet = 0;
	
    while(true)
	{
        nRet++;
        
		if (n <= 0x7F)
		{
            break;
		}
		
        n = (n >> 7) - 1;
    }
	
    return nRet;
}

template<typename Stream, typename I>
void WriteVarInt(Stream& os, I n)
{
	unsigned char tmp[(sizeof(n)*8+6)/7];
	int len=0;

	while(true)
	{
		tmp[len] = (n & 0x7F) | (len ? 0x80 : 0x00);
		
		if (n <= 0x7F)
		{
			break;
		}
		
		n = (n >> 7) - 1;
		len++;
	}

	do
	{
		WRITEDATA(os, tmp[len]);
	} while(len--);
}

template<typename Stream, typename I>
I ReadVarInt(Stream& is)
{
	I n = 0;

	while(true)
	{
		unsigned char chData;
		
		READDATA(is, chData);
		
		n = (n << 7) | (chData & 0x7F);
		
		if (chData & 0x80)
		{
			n++;
		}
		else
		{
			return n;
		}
	}
}

template<typename I>
CVarInt<I>::CVarInt(I& nIn) : n(nIn)
{
	
}

template<typename I>
unsigned int CVarInt<I>::GetSerializeSize(int, int) const
{
	return GetSizeOfVarInt<I>(n);
}

template<typename I>
template<typename Stream>
void CVarInt<I>::Serialize(Stream &s, int, int) const
{
	WriteVarInt<Stream,I>(s, n);
}

template<typename I>
template<typename Stream>
void CVarInt<I>::Unserialize(Stream& s, int, int)
{
	n = ReadVarInt<Stream,I>(s);
}

template<typename I>
CVarInt<I> WrapVarInt(I& n)
{
	return CVarInt<I>(n);
}

//
// Template generation
//

// CVarInt<int>
template class CVarInt<int>;
template void CVarInt<int>::Serialize<CDataStream>(CDataStream&, int, int) const;
template void CVarInt<int>::Unserialize<CDataStream>(CDataStream&, int, int);

// CVarInt<unsigned int>
template class CVarInt<unsigned int>;
template void CVarInt<unsigned int>::Serialize<CDataStream>(CDataStream&, int, int) const;
template void CVarInt<unsigned int>::Serialize<CHashWriter>(CHashWriter&, int, int) const;
template void CVarInt<unsigned int>::Serialize<CAutoFile>(CAutoFile&, int, int) const;
template void CVarInt<unsigned int>::Unserialize<CDataStream>(CDataStream&, int, int);
template void CVarInt<unsigned int>::Unserialize<CAutoFile>(CAutoFile&, int, int);


// CVarInt<long>
template class CVarInt<long>;
template void CVarInt<long>::Serialize<CDataStream>(CDataStream&, int, int) const;
template void CVarInt<long>::Serialize<CAutoFile>(CAutoFile&, int, int) const;
template void CVarInt<long>::Serialize<CHashWriter>(CHashWriter&, int, int) const;
template void CVarInt<long>::Unserialize<CDataStream>(CDataStream&, int, int);
template void CVarInt<long>::Unserialize<CAutoFile>(CAutoFile&, int, int);

// WrapVarInt
template CVarInt<int> WrapVarInt<int>(int&);
template CVarInt<unsigned int> WrapVarInt<unsigned int>(unsigned int&);
template CVarInt<long> WrapVarInt<long>(long&);