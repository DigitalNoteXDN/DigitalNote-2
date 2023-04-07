#include "base.h"
#include "read.h"
#include "write.h"
#include "cdatastream.h"
#include "chashwriter.h"
#include "cautofile.h"

#include "string.h"

template<typename C>
unsigned int GetSerializeSize(const std::basic_string<C>& str, int, int)
{
	return GetSizeOfCompactSize(str.size()) + str.size() * sizeof(str[0]);
}

template<typename Stream, typename C>
void Serialize(Stream& os, const std::basic_string<C>& str, int, int)
{
	WriteCompactSize(os, str.size());

	if (!str.empty())
	{
		os.write((char*)&str[0], str.size() * sizeof(str[0]));
	}
}

template<typename Stream, typename C>
void Unserialize(Stream& is, std::basic_string<C>& str, int, int)
{
	unsigned int nSize = ReadCompactSize(is);

	str.resize(nSize);

	if (nSize != 0)
	{
		is.read((char*)&str[0], nSize * sizeof(str[0]));
	}
}



//
// Generate Templates
//

// GetSerializeSize
template unsigned int GetSerializeSize<char>(std::basic_string<char> const&, int, int);

// Serialize
template void Serialize<CAutoFile, char>(CAutoFile&, std::basic_string<char> const&, int, int);
template void Serialize<CDataStream, char>(CDataStream&, std::basic_string<char> const&, int, int);
template void Serialize<CHashWriter, char>(CHashWriter&, std::basic_string<char> const&, int, int);

// Unserialize
template void Unserialize<CDataStream, char>(CDataStream&, std::basic_string<char>&, int, int);
template void Unserialize<CAutoFile, char>(CAutoFile&, std::basic_string<char>&, int, int);

