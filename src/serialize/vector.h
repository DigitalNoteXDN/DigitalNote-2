#ifndef SERIALIZE_VECTOR_H
#define SERIALIZE_VECTOR_H

#include <vector>
#include <boost/type_traits.hpp>

#include "read.h"

class CScript;

template<typename T, typename A>
unsigned int GetSerializeSize_impl(const std::vector<T, A>& v, int nType, int nVersion, const boost::true_type&);
template<typename T, typename A>
unsigned int GetSerializeSize_impl(const std::vector<T, A>& v, int nType, int nVersion, const boost::false_type&);
template<typename T, typename A>
unsigned int GetSerializeSize(const std::vector<T, A>& v, int nType, int nVersion);
template<typename Stream, typename T, typename A>
void Serialize_impl(Stream& os, const std::vector<T, A>& v, int nType, int nVersion, const boost::true_type&);
template<typename Stream, typename T, typename A>
void Serialize_impl(Stream& os, const std::vector<T, A>& v, int nType, int nVersion, const boost::false_type&);
template<typename Stream, typename T, typename A>
void Serialize(Stream& os, const std::vector<T, A>& v, int nType, int nVersion);
template<typename Stream, typename T, typename A>
void Unserialize_impl(Stream& is, std::vector<T, A>& v, int nType, int nVersion, const boost::true_type&);
template<typename Stream, typename T, typename A>
void Unserialize_impl(Stream& is, std::vector<T, A>& v, int nType, int nVersion, const boost::false_type&);
template<typename Stream, typename T, typename A>
void Unserialize(Stream& is, std::vector<T, A>& v, int nType, int nVersion);

// others derived from vector
extern unsigned int GetSerializeSize(const CScript& v, int nType, int nVersion);
template<typename Stream>
void Serialize(Stream& os, const CScript& v, int nType, int nVersion);
template<typename Stream>
void Unserialize(Stream& is, CScript& v, int nType, int nVersion);

#endif // SERIALIZE_VECTOR_H
