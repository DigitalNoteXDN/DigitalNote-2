#include "util.h"
#include "hash.h"
#include "serialize.h"
#include "cdatastream.h"

#include "cmasternodepaymentwinner.h"

CMasternodePaymentWinner::CMasternodePaymentWinner()
{
	nBlockHeight = 0;
	score = 0;
	vin = CTxIn();
	payee = CScript();
}

uint256 CMasternodePaymentWinner::GetHash()
{
	uint256 n2 = Hash(BEGIN(nBlockHeight), END(nBlockHeight));
	uint256 n3 = vin.prevout.hash > n2 ? (vin.prevout.hash - n2) : (n2 - vin.prevout.hash);

	return n3;
}

unsigned int CMasternodePaymentWinner::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;

	s.nType = nType;
	s.nVersion = nVersion;
	
	READWRITE(nBlockHeight);
	READWRITE(payee);
	READWRITE(vin);
	READWRITE(score);
	READWRITE(vchSig);
	
	return nSerSize;
}

template<typename Stream>
void CMasternodePaymentWinner::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(nBlockHeight);
	READWRITE(payee);
	READWRITE(vin);
	READWRITE(score);
	READWRITE(vchSig);
}

template<typename Stream>
void CMasternodePaymentWinner::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(nBlockHeight);
	READWRITE(payee);
	READWRITE(vin);
	READWRITE(score);
	READWRITE(vchSig);
}

template void CMasternodePaymentWinner::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CMasternodePaymentWinner::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

