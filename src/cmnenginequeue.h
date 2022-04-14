#ifndef CMNENGINEQUEUE_H
#define CMNENGINEQUEUE_H

#include <vector>

#include "ctxin.h"

class CService;

/**
 * A currently inprogress MNengine merge information
 */
class CMNengineQueue
{
public:
	CTxIn vin;
	int64_t time;
	bool ready; //ready for submit
	std::vector<unsigned char> vchSig;

	CMNengineQueue();

	bool GetAddress(CService &addr);
	bool GetProtocolVersion(int &protocolVersion);
	bool Sign();
	bool Relay();
	bool IsExpired();
	bool CheckSignature();

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CMNENGINEQUEUE_H
