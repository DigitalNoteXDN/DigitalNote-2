#ifndef SERIALIZE_H
#define SERIALIZE_H


#define READDATA(s, obj)	s.read((char*)&(obj), sizeof(obj))
#define READWRITE(obj)		(nSerSize += ::SerReadWrite(s, (obj), nType, nVersion, ser_action))
#define READWRITES(obj)		(::SerReadWrite(s, (obj), nType, nVersion, ser_action))
#define WRITEDATA(s, obj)	s.write((char*)&(obj), sizeof(obj))

#include "serialize/base.h"
#include "serialize/action.h"
#include "serialize/read.h"
#include "serialize/write.h"
#include "serialize/string.h"
#include "serialize/vector.h"
#include "serialize/pair.h"
#include "serialize/tuple.h"
#include "serialize/map.h"
#include "serialize/set.h"

template<typename Stream, typename T>
inline unsigned int SerReadWrite(Stream& s, const T& obj, int nType, int nVersion, CSerActionGetSerializeSize ser_action)
{
	return ::GetSerializeSize(obj, nType, nVersion);
}

template<typename Stream, typename T>
inline unsigned int SerReadWrite(Stream& s, const T& obj, int nType, int nVersion, CSerActionSerialize ser_action)
{
	::Serialize(s, obj, nType, nVersion);

	return 0;
}

template<typename Stream, typename T>
inline unsigned int SerReadWrite(Stream& s, T& obj, int nType, int nVersion, CSerActionUnserialize ser_action)
{
	::Unserialize(s, obj, nType, nVersion);

	return 0;
}

struct ser_streamplaceholder
{
	int nType;
	int nVersion;
};


#endif // SERIALIZE_H
