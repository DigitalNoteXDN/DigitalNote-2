#include "compat.h"

#include <boost/lexical_cast.hpp>

#include "util.h"
#include "cvalidationstate.h"
#include "mining.h"
#include "main_extern.h"
#include "cblockindex.h"
#include "main.h"
#include "serialize.h"
#include "hash.h"
#include "init.h"
#include "masternode.h"
#include "masternodeman.h"
#include "masternode_extern.h"
#include "ctxout.h"
#include "cmnenginepool.h"
#include "mnengine_extern.h"
#include "thread.h"
#include "cdatastream.h"

#include "cmasternode.h"

CMasternode::CMasternode()
{
	LOCK(cs);

	vin = CTxIn();
	addr = CService();
	pubkey = CPubKey();
	pubkey2 = CPubKey();
	sig = std::vector<unsigned char>();
	activeState = MASTERNODE_ENABLED;
	sigTime = GetAdjustedTime();
	lastDseep = 0;
	lastTimeSeen = 0;
	cacheInputAge = 0;
	cacheInputAgeBlock = 0;
	unitTest = false;
	allowFreeTx = true;
	protocolVersion = MIN_PEER_PROTO_VERSION;
	nLastDsq = 0;
	donationAddress = CScript();
	donationPercentage = 0;
	nVote = 0;
	lastVote = 0;
	nScanningErrorCount = 0;
	nLastScanningErrorBlockHeight = 0;
	//mark last paid as current for new entries
	nLastPaid = GetAdjustedTime();
	isPortOpen = true;
	isOldNode = true;
}

CMasternode::CMasternode(const CMasternode& other)
{
	LOCK(cs);

	vin = other.vin;
	addr = other.addr;
	pubkey = other.pubkey;
	pubkey2 = other.pubkey2;
	sig = other.sig;
	activeState = other.activeState;
	sigTime = other.sigTime;
	lastDseep = other.lastDseep;
	lastTimeSeen = other.lastTimeSeen;
	cacheInputAge = other.cacheInputAge;
	cacheInputAgeBlock = other.cacheInputAgeBlock;
	unitTest = other.unitTest;
	allowFreeTx = other.allowFreeTx;
	protocolVersion = other.protocolVersion;
	nLastDsq = other.nLastDsq;
	donationAddress = other.donationAddress;
	donationPercentage = other.donationPercentage;
	nVote = other.nVote;
	lastVote = other.lastVote;
	nScanningErrorCount = other.nScanningErrorCount;
	nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
	nLastPaid = other.nLastPaid;
	nLastPaid = GetAdjustedTime();
	isPortOpen = other.isPortOpen;
	isOldNode = other.isOldNode;
}

CMasternode::CMasternode(CService newAddr, CTxIn newVin, CPubKey newPubkey, std::vector<unsigned char> newSig, int64_t newSigTime, CPubKey newPubkey2,
		int protocolVersionIn, CScript newDonationAddress, int newDonationPercentage)
{
	LOCK(cs);

	vin = newVin;
	addr = newAddr;
	pubkey = newPubkey;
	pubkey2 = newPubkey2;
	sig = newSig;
	activeState = MASTERNODE_ENABLED;
	sigTime = newSigTime;
	lastDseep = 0;
	lastTimeSeen = 0;
	cacheInputAge = 0;
	cacheInputAgeBlock = 0;
	unitTest = false;
	allowFreeTx = true;
	protocolVersion = protocolVersionIn;
	nLastDsq = 0;
	donationAddress = newDonationAddress;
	donationPercentage = newDonationPercentage;
	nVote = 0;
	lastVote = 0;
	nScanningErrorCount = 0;
	nLastScanningErrorBlockHeight = 0;
	isPortOpen = true;
	isOldNode = true;
}

void CMasternode::swap(CMasternode& first, CMasternode& second) // nothrow
{
	// by swapping the members of two classes,
	// the two classes are effectively swapped
	std::swap(first.vin, second.vin);
	std::swap(first.addr, second.addr);
	std::swap(first.pubkey, second.pubkey);
	std::swap(first.pubkey2, second.pubkey2);
	std::swap(first.sig, second.sig);
	std::swap(first.activeState, second.activeState);
	std::swap(first.sigTime, second.sigTime);
	std::swap(first.lastDseep, second.lastDseep);
	std::swap(first.lastTimeSeen, second.lastTimeSeen);
	std::swap(first.cacheInputAge, second.cacheInputAge);
	std::swap(first.cacheInputAgeBlock, second.cacheInputAgeBlock);
	std::swap(first.unitTest, second.unitTest);
	std::swap(first.allowFreeTx, second.allowFreeTx);
	std::swap(first.protocolVersion, second.protocolVersion);
	std::swap(first.nLastDsq, second.nLastDsq);
	std::swap(first.donationAddress, second.donationAddress);
	std::swap(first.donationPercentage, second.donationPercentage);
	std::swap(first.nVote, second.nVote);
	std::swap(first.lastVote, second.lastVote);
	std::swap(first.nScanningErrorCount, second.nScanningErrorCount);
	std::swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
	std::swap(first.nLastPaid, second.nLastPaid);
	std::swap(first.isPortOpen, second.isPortOpen);
	std::swap(first.isOldNode, second.isOldNode);
}

CMasternode& CMasternode::operator=(CMasternode from)
{
	this->swap(*this, from);
	
	return *this;
}

bool operator==(const CMasternode& a, const CMasternode& b)
{
	return a.vin == b.vin;
}

bool operator!=(const CMasternode& a, const CMasternode& b)
{
	return !(a.vin == b.vin);
}

//
// Deterministically calculate a given "score" for a masternode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CMasternode::CalculateScore(int mod, int64_t nBlockHeight)
{
	if(pindexBest == NULL)
	{
		return 0;
	}

	uint256 hash = 0;
	uint256 aux = vin.prevout.hash + vin.prevout.n;

	if(!GetBlockHash(hash, nBlockHeight))
	{
		return 0;
	}

	uint256 hash2 = Hash(BEGIN(hash), END(hash));
	uint256 hash3 = Hash(BEGIN(hash), END(hash), BEGIN(aux), END(aux));

	uint256 r = (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);

	return r;
}

int64_t CMasternode::SecondsSincePayment()
{
	return (GetAdjustedTime() - nLastPaid);
}

void CMasternode::UpdateLastSeen(int64_t override)
{
	if(override == 0)
	{
		lastTimeSeen = GetAdjustedTime();
	}
	else
	{
		lastTimeSeen = override;
	}
}

void CMasternode::ChangePortStatus(bool status)
{
	isPortOpen = status;
}

void CMasternode::ChangeNodeStatus(bool status)
{
	isOldNode = status;
}

uint64_t CMasternode::SliceHash(uint256& hash, int slice)
{
	uint64_t n = 0;
	
	memcpy(&n, &hash+slice*64, 64);
	
	return n;
}

void CMasternode::Check()
{
	if(ShutdownRequested())
	{
		return;
	}

	//TODO: Random segfault with this line removed
	TRY_LOCK(cs_main, lockRecv);

	if(!lockRecv)
	{
		return;
	}

	//once spent, stop doing the checks
	if(activeState == MASTERNODE_VIN_SPENT)
	{
		return;
	}

	if(!UpdatedWithin(MASTERNODE_REMOVAL_SECONDS))
	{
		activeState = MASTERNODE_REMOVE;
		
		return;
	}

	if(!UpdatedWithin(MASTERNODE_EXPIRATION_SECONDS))
	{
		activeState = MASTERNODE_EXPIRED;
		
		return;
	}

	if(!unitTest)
	{
		CValidationState state;
		CTransaction tx = CTransaction();
		CTxOut vout = CTxOut(MNengine_POOL_MAX, mnEnginePool.collateralPubKey);
		
		tx.vin.push_back(vin);
		tx.vout.push_back(vout);

		if(!AcceptableInputs(mempool, tx, false, NULL))
		{
			activeState = MASTERNODE_VIN_SPENT;
			
			return;
		}
	}

	activeState = MASTERNODE_ENABLED; // OK
}

bool CMasternode::UpdatedWithin(int seconds)
{
	// LogPrintf("UpdatedWithin %d, %d --  %d \n", GetAdjustedTime() , lastTimeSeen, (GetAdjustedTime() - lastTimeSeen) < seconds);

	return (GetAdjustedTime() - lastTimeSeen) < seconds;
}

void CMasternode::Disable()
{
	lastTimeSeen = 0;
}

bool CMasternode::IsEnabled()
{
	return isPortOpen && activeState == MASTERNODE_ENABLED;
}

int CMasternode::GetMasternodeInputAge()
{
	if(pindexBest == NULL)
	{
		return 0;
	}
	
	if(cacheInputAge == 0)
	{
		cacheInputAge = GetInputAge(vin);
		cacheInputAgeBlock = pindexBest->nHeight;
	}

	return cacheInputAge+(pindexBest->nHeight-cacheInputAgeBlock);
}

std::string CMasternode::Status()
{
	std::string strStatus = "ACTIVE";

	if(activeState == CMasternode::MASTERNODE_ENABLED)
	{
		strStatus = "ENABLED";
	}
	
	if(activeState == CMasternode::MASTERNODE_EXPIRED)
	{
		strStatus = "EXPIRED";
	}
	
	if(activeState == CMasternode::MASTERNODE_VIN_SPENT)
	{
		strStatus = "VIN_SPENT";
	}
	
	if(activeState == CMasternode::MASTERNODE_REMOVE)
	{
		strStatus = "REMOVE";
	}
	
	if(activeState == CMasternode::MASTERNODE_POS_ERROR)
	{
		strStatus = "POS_ERROR";
	}
	
	return strStatus;
}

unsigned int CMasternode::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	const bool fGetSize = true;
	const bool fWrite = false;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	s.nType = nType;
	s.nVersion = nVersion;
	
	// serialized format:
	// * version byte (currently 0)
	// * all fields (?)
	{
		LOCK(cs);
		
		unsigned char nVersion = 0;
		READWRITE(nVersion);
		READWRITE(vin);
		READWRITE(addr);
		READWRITE(pubkey);
		READWRITE(pubkey2);
		READWRITE(sig);
		READWRITE(activeState);
		READWRITE(sigTime);
		READWRITE(lastDseep);
		READWRITE(lastTimeSeen);
		READWRITE(cacheInputAge);
		READWRITE(cacheInputAgeBlock);
		READWRITE(unitTest);
		READWRITE(allowFreeTx);
		READWRITE(protocolVersion);
		READWRITE(nLastDsq);
		READWRITE(donationAddress);
		READWRITE(donationPercentage);
		READWRITE(nVote);
		READWRITE(lastVote);
		READWRITE(nScanningErrorCount);
		READWRITE(nLastScanningErrorBlockHeight);
		READWRITE(nLastPaid);
		READWRITE(isPortOpen);
		READWRITE(isOldNode);
	}
	
	return nSerSize;
}

template<typename Stream>
void CMasternode::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
		// serialized format:
	// * version byte (currently 0)
	// * all fields (?)
	{
		LOCK(cs);
		
		unsigned char nVersion = 0;
		READWRITE(nVersion);
		READWRITE(vin);
		READWRITE(addr);
		READWRITE(pubkey);
		READWRITE(pubkey2);
		READWRITE(sig);
		READWRITE(activeState);
		READWRITE(sigTime);
		READWRITE(lastDseep);
		READWRITE(lastTimeSeen);
		READWRITE(cacheInputAge);
		READWRITE(cacheInputAgeBlock);
		READWRITE(unitTest);
		READWRITE(allowFreeTx);
		READWRITE(protocolVersion);
		READWRITE(nLastDsq);
		READWRITE(donationAddress);
		READWRITE(donationPercentage);
		READWRITE(nVote);
		READWRITE(lastVote);
		READWRITE(nScanningErrorCount);
		READWRITE(nLastScanningErrorBlockHeight);
		READWRITE(nLastPaid);
		READWRITE(isPortOpen);
		READWRITE(isOldNode);
	}
}

template<typename Stream>
void CMasternode::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
		// serialized format:
	// * version byte (currently 0)
	// * all fields (?)
	{
		LOCK(cs);
		
		unsigned char nVersion = 0;
		READWRITE(nVersion);
		READWRITE(vin);
		READWRITE(addr);
		READWRITE(pubkey);
		READWRITE(pubkey2);
		READWRITE(sig);
		READWRITE(activeState);
		READWRITE(sigTime);
		READWRITE(lastDseep);
		READWRITE(lastTimeSeen);
		READWRITE(cacheInputAge);
		READWRITE(cacheInputAgeBlock);
		READWRITE(unitTest);
		READWRITE(allowFreeTx);
		READWRITE(protocolVersion);
		READWRITE(nLastDsq);
		READWRITE(donationAddress);
		READWRITE(donationPercentage);
		READWRITE(nVote);
		READWRITE(lastVote);
		READWRITE(nScanningErrorCount);
		READWRITE(nLastScanningErrorBlockHeight);
		READWRITE(nLastPaid);
		READWRITE(isPortOpen);
		READWRITE(isOldNode);
	}
}

template void CMasternode::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CMasternode::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

