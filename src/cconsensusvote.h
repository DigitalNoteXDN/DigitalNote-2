#ifndef CCONSENSUSVOTE_H
#define CCONSENSUSVOTE_H

#include <vector>

#include "ctxin.h"
#include "uint/uint256.h"

class CConsensusVote
{
public:
	CTxIn vinMasternode;
	uint256 txHash;
	int nBlockHeight;
	std::vector<unsigned char> vchMasterNodeSignature;

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	uint256 GetHash() const;
	bool SignatureValid();
	bool Sign();
};

#endif // CCONSENSUSVOTE_H
