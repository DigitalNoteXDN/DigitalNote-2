#ifndef CVARINT_H
#define CVARINT_H

#define VARINT(obj)		REF(WrapVarInt(REF(obj)))

template<typename I>
class CVarInt
{
protected:
	I &n;

public:
	CVarInt(I& nIn);

	unsigned int GetSerializeSize(int, int) const;
	
	template<typename Stream>
	void Serialize(Stream &s, int, int) const;

	template<typename Stream>
	void Unserialize(Stream& s, int, int);
};

template<typename I>
CVarInt<I> WrapVarInt(I& n);

#endif // CVARINT_H
