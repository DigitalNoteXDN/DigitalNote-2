#ifndef CADDRESS_H
#define CADDRESS_H

#include "protocol.h"
#include "net/cservice.h"

/** A CService with information about it as peer */
class CAddress : public CService
{
// TODO: make private (improves encapsulation)
public:
	uint64_t nServices;
	unsigned int nTime;		// disk and network only
	int64_t nLastTry;		// memory only
	
	CAddress();
	explicit CAddress(CService ipIn, uint64_t nServicesIn=NODE_NETWORK);

	void Init();

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CADDRESS_H
