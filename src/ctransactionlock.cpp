#include "compat.h"

#include "util.h"
#include "cconsensusvote.h"
#include "cmasternodeman.h"
#include "masternode_extern.h"
#include "blockparams.h"
#include "net/cnetaddr.h"
#include "version.h"

#include "ctransactionlock.h"

bool CTransactionLock::SignaturesValid()
{
	for(CConsensusVote vote : vecConsensusVotes)
	{
		int n = mnodeman.GetMasternodeRank(vote.vinMasternode, vote.nBlockHeight, MIN_INSTANTX_PROTO_VERSION);

		if(n == -1)
		{
			LogPrintf("CTransactionLock::SignaturesValid() - Unknown Masternode\n");
			
			return false;
		}

		if(n > INSTANTX_SIGNATURES_TOTAL)
		{
			LogPrintf("CTransactionLock::SignaturesValid() - Masternode not in the top %s\n", INSTANTX_SIGNATURES_TOTAL);
			
			return false;
		}

		if(!vote.SignatureValid())
		{
			LogPrintf("CTransactionLock::SignaturesValid() - Signature not valid\n");
			
			return false;
		}
	}

	return true;
}

void CTransactionLock::AddSignature(CConsensusVote& cv)
{
	vecConsensusVotes.push_back(cv);
}

int CTransactionLock::CountSignatures()
{
	/*
		Only count signatures where the BlockHeight matches the transaction's blockheight.
		The votes have no proof it's the correct blockheight
	*/
	if(nBlockHeight == 0)
	{
		return -1;
	}

	int n = 0;
	for(CConsensusVote v : vecConsensusVotes)
	{
		if(v.nBlockHeight == nBlockHeight)
		{
			n++;
		}
	}

	return n;
}

uint256 CTransactionLock::GetHash()
{
	return txHash;
}

