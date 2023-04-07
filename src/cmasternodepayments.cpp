#include "compat.h"

#include <boost/lexical_cast.hpp>

#include "spork.h"
#include "cpubkey.h"
#include "util.h"
#include "ckey.h"
#include "hash.h"
#include "main_extern.h"
#include "cblockindex.h"
#include "cinv.h"
#include "net.h"
#include "thread.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "cmasternodepaymentwinner.h"
#include "cactivemasternode.h"
#include "masternode.h"
#include "masternode_extern.h"
#include "cmnenginesigner.h"
#include "mnengine_extern.h"
#include "script.h"
#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "util/backwards.h"

#include "cmasternodepayments.h"

CMasternodePayments::CMasternodePayments()
{
	strMainPubKey = "";
	enabled = false;
}

int CMasternodePayments::GetMinMasternodePaymentsProto()
{
	return IsSporkActive(SPORK_10_MASTERNODE_PAY_UPDATED_NODES)
			? MIN_MASTERNODE_PAYMENT_PROTO_VERSION_2
			: MIN_MASTERNODE_PAYMENT_PROTO_VERSION_1;
}

bool CMasternodePayments::CheckSignature(CMasternodePaymentWinner& winner)
{
	//note: need to investigate why this is failing
	std::string strMessage = winner.vin.ToString().c_str() + boost::lexical_cast<std::string>(winner.nBlockHeight) + winner.payee.ToString();
	std::string strPubKey = strMainPubKey ;
	CPubKey pubkey(ParseHex(strPubKey));

	std::string errorMessage = "";

	if(!mnEngineSigner.VerifyMessage(pubkey, winner.vchSig, strMessage, errorMessage))
	{
		return false;
	}

	return true;
}

bool CMasternodePayments::Sign(CMasternodePaymentWinner& winner)
{
	std::string strMessage = winner.vin.ToString().c_str() + boost::lexical_cast<std::string>(winner.nBlockHeight) + winner.payee.ToString();

	CKey key2;
	CPubKey pubkey2;
	std::string errorMessage = "";

	if(!mnEngineSigner.SetKey(strMasterPrivKey, errorMessage, key2, pubkey2))
	{
		LogPrintf("CMasternodePayments::Sign - ERROR: Invalid Masternodeprivkey: '%s'\n", errorMessage.c_str());
		LogPrintf("CMasternodePayments::Sign - FORCE BYPASS - SetKey checks!!!\n");
		
		return false;
	}

	if(!mnEngineSigner.SignMessage(strMessage, errorMessage, winner.vchSig, key2))
	{
		LogPrintf("CMasternodePayments::Sign - Sign message failed\n");
		LogPrintf("CMasternodePayments::Sign - FORCE BYPASS - Sign message checks!!!\n");
		
		return false;
	}

	if(!mnEngineSigner.VerifyMessage(pubkey2, winner.vchSig, strMessage, errorMessage))
	{
		LogPrintf("CMasternodePayments::Sign - Verify message failed\n");
		LogPrintf("CMasternodePayments::Sign - FORCE BYPASS - Verify message checks!!!\n");
		
		return false;
	}

	return true;
}

uint64_t CMasternodePayments::CalculateScore(uint256 blockHash, CTxIn& vin)
{
	uint256 n1 = blockHash;
	uint256 n2 = Hash(BEGIN(n1), END(n1));
	uint256 n3 = Hash(BEGIN(vin.prevout.hash), END(vin.prevout.hash));
	uint256 n4 = n3 > n2 ? (n3 - n2) : (n2 - n3);

	//printf(" -- CMasternodePayments CalculateScore() n2 = %d \n", n2.Get64());
	//printf(" -- CMasternodePayments CalculateScore() n3 = %d \n", n3.Get64());
	//printf(" -- CMasternodePayments CalculateScore() n4 = %d \n", n4.Get64());

	return n4.Get64();
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee, CTxIn& vin)
{
	for(CMasternodePaymentWinner& winner : vWinning)
	{
		if(winner.nBlockHeight == nBlockHeight)
		{
			payee = winner.payee;
			vin = winner.vin;
			
			return true;
		}
	}

	return false;
}

bool CMasternodePayments::GetWinningMasternode(int nBlockHeight, CTxIn& vinOut)
{
	/*
	// Try to get frist masternode in our list
	CMasternode* winningNode = mnodeman.GetCurrentMasterNode(1);

	// If initial sync or we can't find a masternode in our list
	if(IsInitialBlockDownload() || !winningNode || !ProcessBlock(nBlockHeight))
	{
		// Return false (for sanity, we have no masternode to pay)
		return false;
	}

	// Set masternode winner to pay
	for(CMasternodePaymentWinner& winner : vWinning)
	{
		payee = winner.payee;
		vin = winner.vin;
	}

	// Return true if previous checks pass
	return true;
	*/

	for(CMasternodePaymentWinner& winner : vWinning)
	{
		if(winner.nBlockHeight == nBlockHeight)
		{
			vinOut = winner.vin;
			
			return true;
		}
	}

	return false;
}

bool CMasternodePayments::AddWinningMasternode(CMasternodePaymentWinner& winnerIn)
{
	uint256 blockHash = 0;

	if(!GetBlockHash(blockHash, winnerIn.nBlockHeight-576))
	{
		return false;
	}

	winnerIn.score = CalculateScore(blockHash, winnerIn.vin);

	bool foundBlock = false;

	for(CMasternodePaymentWinner& winner : vWinning)
	{
		if(winner.nBlockHeight == winnerIn.nBlockHeight)
		{
			foundBlock = true;
			
			if(winner.score < winnerIn.score)
			{
				winner.score = winnerIn.score;
				winner.vin = winnerIn.vin;
				winner.payee = winnerIn.payee;
				winner.vchSig = winnerIn.vchSig;

				mapSeenMasternodeVotes.insert(std::make_pair(winnerIn.GetHash(), winnerIn));

				return true;
			}
		}
	}

	// if it's not in the vector
	if(!foundBlock)
	{
		vWinning.push_back(winnerIn);
		mapSeenMasternodeVotes.insert(std::make_pair(winnerIn.GetHash(), winnerIn));

		return true;
	}

	return false;
}

void CMasternodePayments::CleanPaymentList()
{
	LOCK(cs_masternodepayments);

	if(pindexBest == NULL)
	{
		return;
	}

	int nLimit = std::max(((int)mnodeman.size())*((int)1.25), 1000);

	for(std::vector<CMasternodePaymentWinner>::iterator it = vWinning.begin(); it < vWinning.end(); it++)
	{
		if(pindexBest->nHeight - (*it).nBlockHeight > nLimit)
		{
			if(fDebug)
			{
				LogPrintf("CMasternodePayments::CleanPaymentList - Removing old Masternode payment - block %d\n", (*it).nBlockHeight);
			}
			
			vWinning.erase(it);
			
			break;
		}
	}
}

bool CMasternodePayments::ProcessBlock(int nBlockHeight)
{
	LOCK(cs_masternodepayments);

	if(nBlockHeight <= nLastBlockHeight)
	{
		return false;
	}

	if(!enabled)
	{
		return false;
	}

	CMasternodePaymentWinner newWinner;
	int nMinimumAge = mnodeman.CountEnabled();
	CScript payeeSource;

	uint256 hash;
	
	if(!GetBlockHash(hash, nBlockHeight-10))
	{
		return false;
	}

	unsigned int nHash;

	memcpy(&nHash, &hash, 2);

	LogPrintf(" ProcessBlock Start nHeight %d - vin %s. \n", nBlockHeight, activeMasternode.vin.ToString().c_str());

	std::vector<CTxIn> vecLastPayments;

	for(CMasternodePaymentWinner& winner : backwards<std::vector<CMasternodePaymentWinner>>(vWinning))
	{
		//if we already have the same vin - we have one full payment cycle, break
		if(vecLastPayments.size() > (unsigned int)nMinimumAge)
		{
			break;
		}

		vecLastPayments.push_back(winner.vin);
	}

	// pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
	CMasternode* pmn = mnodeman.FindOldestNotInVec(vecLastPayments, nMinimumAge);
	
	if(pmn != NULL)
	{
		LogPrintf(" Found by FindOldestNotInVec \n");

		newWinner.score = 0;
		newWinner.nBlockHeight = nBlockHeight;
		newWinner.vin = pmn->vin;
		
		if(pmn->donationPercentage > 0 && (nHash % 100) <= (unsigned int)pmn->donationPercentage)
		{
			newWinner.payee = pmn->donationAddress;
		}
		else
		{
			newWinner.payee = GetScriptForDestination(pmn->pubkey.GetID());
		}
		
		payeeSource = GetScriptForDestination(pmn->pubkey.GetID());
	}

	//if we can't find new MN to get paid, pick first active MN counting back from the end of vecLastPayments list
	if(newWinner.nBlockHeight == 0 && nMinimumAge > 0)
	{
		LogPrintf(" Find by reverse \n");
		
		for(CTxIn& vinLP : backwards<std::vector<CTxIn>>(vecLastPayments))
		{
			CMasternode* pmn = mnodeman.Find(vinLP);
			if(pmn != NULL)
			{
				pmn->Check();
				
				if(!pmn->IsEnabled())
				{
					continue;
				}
				
				newWinner.score = 0;
				newWinner.nBlockHeight = nBlockHeight;
				newWinner.vin = pmn->vin;

				if(pmn->donationPercentage > 0 && (nHash % 100) <= (unsigned int)pmn->donationPercentage)
				{
					newWinner.payee = pmn->donationAddress;
				}
				else
				{
					newWinner.payee = GetScriptForDestination(pmn->pubkey.GetID());
				}

				payeeSource = GetScriptForDestination(pmn->pubkey.GetID());

				break; // we found active MN
			}
		}
	}

	if(newWinner.nBlockHeight == 0)
	{
		return false;
	}

	CTxDestination address1;
	ExtractDestination(newWinner.payee, address1);
	CDigitalNoteAddress address2(address1);

	CTxDestination address3;
	ExtractDestination(payeeSource, address3);
	CDigitalNoteAddress address4(address3);

	LogPrintf("Winner payee %s nHeight %d vin source %s. \n", address2.ToString().c_str(), newWinner.nBlockHeight, address4.ToString().c_str());

	if(Sign(newWinner))
	{
		if(AddWinningMasternode(newWinner))
		{
			Relay(newWinner);

			nLastBlockHeight = nBlockHeight;
			
			return true;
		}
	}

	return false;
}

void CMasternodePayments::Relay(CMasternodePaymentWinner& winner)
{
	CInv inv(MSG_MASTERNODE_WINNER, winner.GetHash());
	std::vector<CInv> vInv;
	
	vInv.push_back(inv);

	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		pnode->PushMessage("inv", vInv);
	}
}

void CMasternodePayments::Sync(CNode* node)
{
	LOCK(cs_masternodepayments);

	for(CMasternodePaymentWinner& winner : vWinning)
	{
		if(winner.nBlockHeight >= pindexBest->nHeight-10 && winner.nBlockHeight <= pindexBest->nHeight + 20)
		{
			node->PushMessage("mnw", winner);
		}
	}
}

bool CMasternodePayments::SetPrivKey(const std::string &strPrivKey)
{
	CMasternodePaymentWinner winner;

	// Test signing successful, proceed
	strMasterPrivKey = strPrivKey;

	Sign(winner);

	if(CheckSignature(winner))
	{
		LogPrintf("CMasternodePayments::SetPrivKey - Successfully initialized as Masternode payments master\n");
		
		enabled = true;
		
		return true;
	}
	else
	{
		return false;
	}
}

