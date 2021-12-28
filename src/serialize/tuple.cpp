
#include <boost/core/ref.hpp>
#include <boost/tuple/detail/tuple_basic.hpp>

#include "write.h"
#include "string.h"
#include "cdatastream.h"

#include "tuple.h"

//
// 3 tuple
//
template<typename T0, typename T1, typename T2>
unsigned int GetSerializeSize(const boost::tuple<T0, T1, T2>& item, int nType, int nVersion)
{
	unsigned int nSize = 0;

	nSize += GetSerializeSize(boost::get<0>(item), nType, nVersion);
	nSize += GetSerializeSize(boost::get<1>(item), nType, nVersion);
	nSize += GetSerializeSize(boost::get<2>(item), nType, nVersion);

	return nSize;
}

template<typename Stream, typename T0, typename T1, typename T2>
void Serialize(Stream& os, const boost::tuple<T0, T1, T2>& item, int nType, int nVersion)
{
	Serialize(os, boost::get<0>(item), nType, nVersion);
	Serialize(os, boost::get<1>(item), nType, nVersion);
	Serialize(os, boost::get<2>(item), nType, nVersion);
}

template<typename Stream, typename T0, typename T1, typename T2>
void Unserialize(Stream& is, boost::tuple<T0, T1, T2>& item, int nType, int nVersion)
{
	Unserialize(is, boost::get<0>(item), nType, nVersion);
	Unserialize(is, boost::get<1>(item), nType, nVersion);
	Unserialize(is, boost::get<2>(item), nType, nVersion);
}

//
// 4 tuple
//
template<typename T0, typename T1, typename T2, typename T3>
unsigned int GetSerializeSize(const boost::tuple<T0, T1, T2, T3>& item, int nType, int nVersion)
{
	unsigned int nSize = 0;

	nSize += GetSerializeSize(boost::get<0>(item), nType, nVersion);
	nSize += GetSerializeSize(boost::get<1>(item), nType, nVersion);
	nSize += GetSerializeSize(boost::get<2>(item), nType, nVersion);
	nSize += GetSerializeSize(boost::get<3>(item), nType, nVersion);

	return nSize;
}

template<typename Stream, typename T0, typename T1, typename T2, typename T3>
void Serialize(Stream& os, const boost::tuple<T0, T1, T2, T3>& item, int nType, int nVersion)
{
	Serialize(os, boost::get<0>(item), nType, nVersion);
	Serialize(os, boost::get<1>(item), nType, nVersion);
	Serialize(os, boost::get<2>(item), nType, nVersion);
	Serialize(os, boost::get<3>(item), nType, nVersion);
}

template<typename Stream, typename T0, typename T1, typename T2, typename T3>
void Unserialize(Stream& is, boost::tuple<T0, T1, T2, T3>& item, int nType, int nVersion)
{
	Unserialize(is, boost::get<0>(item), nType, nVersion);
	Unserialize(is, boost::get<1>(item), nType, nVersion);
	Unserialize(is, boost::get<2>(item), nType, nVersion);
	Unserialize(is, boost::get<3>(item), nType, nVersion);
}



//
// Generate Templates
//

// Serialize
template void Serialize<CDataStream, std::string, std::string, unsigned long>(CDataStream&,
	boost::tuples::tuple<std::string, std::string, unsigned long> const&, int, int);
template void Serialize<CDataStream, std::string, std::string, unsigned long long>(CDataStream&,
	boost::tuples::tuple<std::string, std::string, unsigned long long> const&, int, int);