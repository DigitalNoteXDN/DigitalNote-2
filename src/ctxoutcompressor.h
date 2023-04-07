#ifndef CTXOUTCOMPRESSOR_H
#define CTXOUTCOMPRESSOR_H

class CTxOut;

/** wrapper for CTxOut that provides a more compact serialization */
class CTxOutCompressor
{
private:
	CTxOut& txout;

public:
	CTxOutCompressor(CTxOut& txoutIn);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CTXOUTCOMPRESSOR_H
