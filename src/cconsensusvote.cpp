#include "compat.h"

#include <boost/lexical_cast.hpp>

#include "util.h"
#include "serialize.h"
#include "ckey.h"
#include "cscriptid.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "masternodeman.h"
#include "masternode_extern.h"
#include "cmnenginesigner.h"
#include "mnengine_extern.h"
#include "cdatastream.h"

#include "cconsensusvote.h"

unsigned int CConsensusVote::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;

	s.nType = nType;
	s.nVersion = nVersion;
	
	READWRITE(txHash);
	READWRITE(vinMasternode);
	READWRITE(vchMasterNodeSignature);
	READWRITE(nBlockHeight);
	
	return nSerSize;
}

template<typename Stream>
void CConsensusVote::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(txHash);
	READWRITE(vinMasternode);
	READWRITE(vchMasterNodeSignature);
	READWRITE(nBlockHeight);
}

template<typename Stream>
void CConsensusVote::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(txHash);
	READWRITE(vinMasternode);
	READWRITE(vchMasterNodeSignature);
	READWRITE(nBlockHeight);
}

template void CConsensusVote::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CConsensusVote::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

uint256 CConsensusVote::GetHash() const
{
	return vinMasternode.prevout.hash + vinMasternode.prevout.n + txHash;
}

bool CConsensusVote::SignatureValid()
{
	std::string errorMessage;
	std::string strMessage = txHash.ToString().c_str() + boost::lexical_cast<std::string>(nBlockHeight);
	//LogPrintf("verify strMessage %s \n", strMessage.c_str());

	CMasternode* pmn = mnodeman.Find(vinMasternode);

	if(pmn == NULL)
	{
		LogPrintf("InstantX::CConsensusVote::SignatureValid() - Unknown Masternode\n");
		
		return false;
	}

	if(!mnEngineSigner.VerifyMessage(pmn->pubkey2, vchMasterNodeSignature, strMessage, errorMessage))
	{
		LogPrintf("InstantX::CConsensusVote::SignatureValid() - Verify message failed\n");
		
		return false;
	}

	return true;
}

bool CConsensusVote::Sign()
{
	std::string errorMessage;

	CKey key2;
	CPubKey pubkey2;
	std::string strMessage = txHash.ToString().c_str() + boost::lexical_cast<std::string>(nBlockHeight);
	//LogPrintf("signing strMessage %s \n", strMessage.c_str());
	//LogPrintf("signing privkey %s \n", strMasterNodePrivKey.c_str());

	if(!mnEngineSigner.SetKey(strMasterNodePrivKey, errorMessage, key2, pubkey2))
	{
		LogPrintf("CConsensusVote::Sign() - ERROR: Invalid masternodeprivkey: '%s'\n", errorMessage.c_str());
		
		return false;
	}

	if(!mnEngineSigner.SignMessage(strMessage, errorMessage, vchMasterNodeSignature, key2))
	{
		LogPrintf("CConsensusVote::Sign() - Sign message failed");
		
		return false;
	}

	if(!mnEngineSigner.VerifyMessage(pubkey2, vchMasterNodeSignature, strMessage, errorMessage))
	{
		LogPrintf("CConsensusVote::Sign() - Verify message failed");
		
		return false;
	}

	return true;
}

