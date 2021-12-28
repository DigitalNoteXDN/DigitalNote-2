#include "compat.h"

#include <boost/thread.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <algorithm>
#include <random>

#include "chainparams.h"
#include "cchainparams.h"
#include "ccoincontrol.h"
#include "kernel.h"
#include "txdb-leveldb.h"
#include "blockparams.h"
#include "fork.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "cmasternodepayments.h"
#include "masternodeman.h"
#include "masternode_extern.h"
#include "coutput.h"
#include "cwallettx.h"
#include "mining.h"
#include "walletdb.h"
#include "caccountingentry.h"
#include "cblock.h"
#include "creservekey.h"
#include "ckeypool.h"
#include "wallet.h"
#include "script.h"
#include "enums/opcodetype.h"
#include "main_const.h"
#include "main_extern.h"
#include "ctxmempool.h"
#include "webwalletconnector.h"
#include "smsg.h"
#include "ckeymetadata.h"
#include "cstealthkeymetadata.h"
#include "comparevalueonly.h"
#include "ccrypter.h"
#include "cmasterkey.h"
#include "types/csecret.h"
#include "ckey.h"
#include "ctxout.h"
#include "hash.h"
#include "types/txitems.h"
#include "types/valtype.h"
#include "cbitcoinaddress.h"
#include "cdigitalnotesecret.h"
#include "cdigitalnoteaddress.h"
#include "thread.h"
#include "ui_interface.h"
#include "ui_translate.h"
#include "util.h"
#include "cblockindex.h"
#include "ctxindex.h"
#include "serialize.h"

#include "cwallet.h"

class CMasternode;

/**
	Private Functions
*/
// Select some coins without random shuffle or best subset approximation
bool CWallet::SelectCoinsForStaking(int64_t nTargetValue, unsigned int nSpendTime,
		setCoins_t& setCoinsRet, int64_t& nValueRet) const
{
	std::vector<COutput> vCoins;
	AvailableCoinsForStaking(vCoins, nSpendTime);

	setCoinsRet.clear();
	nValueRet = 0;

	for(COutput output : vCoins)
	{
		if(!output.fSpendable)
		{
			continue;
		}
		
		const CWalletTx *pcoin = output.tx;
		int i = output.i;

		// Stop if we've chosen enough inputs
		if (nValueRet >= nTargetValue)
		{
			break;
		}
		
		int64_t n = pcoin->vout[i].nValue;

		std::pair<int64_t,std::pair<const CWalletTx*,unsigned int> > coin = std::make_pair(n,std::make_pair(pcoin, i));

		if (n >= nTargetValue)
		{
			// If input value is greater or equal to target then simply insert
			//    it into the current subset and exit
			setCoinsRet.insert(coin.second);
			
			nValueRet += coin.first;
			
			break;
		}
		else if (n < nTargetValue + CENT)
		{
			setCoinsRet.insert(coin.second);
			
			nValueRet += coin.first;
		}
	}

	return true;
}

bool CWallet::SelectCoins(int64_t nTargetValue, unsigned int nSpendTime, setCoins_t& setCoinsRet,
		int64_t& nValueRet, const CCoinControl* coinControl, AvailableCoinsType coin_type, bool useIX) const
{
	std::vector<COutput> vCoins;
	AvailableCoins(vCoins, true, coinControl, coin_type, useIX);

	// coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
	if (coinControl && coinControl->HasSelected())
	{
		for(const COutput& out : vCoins)
		{
			if(!out.fSpendable)
			{
				continue;
			}
			
			nValueRet += out.tx->vout[out.i].nValue;
			setCoinsRet.insert(std::make_pair(out.tx, out.i));
		}
		
		return (nValueRet >= nTargetValue);
	}

	boost::function<bool (const CWallet*, int64_t, unsigned int, int, int, std::vector<COutput>,
			setCoins_t&, int64_t&)> f = &CWallet::SelectCoinsMinConf;

	return (f(this, nTargetValue, nSpendTime, 1, 10, vCoins, setCoinsRet, nValueRet) ||
			f(this, nTargetValue, nSpendTime, 1, 1, vCoins, setCoinsRet, nValueRet) ||
			f(this, nTargetValue, nSpendTime, 0, 1, vCoins, setCoinsRet, nValueRet));
}

void CWallet::AddToSpends(const COutPoint& outpoint, const uint256& wtxid)
{
	std::pair<mmTxSpends_t::iterator, mmTxSpends_t::iterator> range;

	mmTxSpends.insert(std::make_pair(outpoint, wtxid));

	range = mmTxSpends.equal_range(outpoint);

	SyncMetaData(range);
}

void CWallet::AddToSpends(const uint256& wtxid)
{
	assert(mapWallet.count(wtxid));
	
	CWalletTx& thisTx = mapWallet[wtxid];

	if (thisTx.IsCoinBase()) // Coinbases don't spend anything!
	{
		return;
	}

	for(const CTxIn& txin : thisTx.vin)
	{
		AddToSpends(txin.prevout, wtxid);
	}
}

void CWallet::SyncMetaData(std::pair<mmTxSpends_t::iterator, mmTxSpends_t::iterator> range)
{
	// We want all the wallet transactions in range to have the same metadata as
	// the oldest (smallest nOrderPos).
	// So: find smallest nOrderPos:

	int nMinOrderPos = std::numeric_limits<int>::max();
	const CWalletTx* copyFrom = NULL;

	for (mmTxSpends_t::iterator it = range.first; it != range.second; ++it)
	{
		const uint256& hash = it->second;
		int n = mapWallet[hash].nOrderPos;
		
		if (n < nMinOrderPos)
		{
			nMinOrderPos = n;
			copyFrom = &mapWallet[hash];
		}
	}

	// Now copy data from copyFrom to rest:
	for (mmTxSpends_t::iterator it = range.first; it != range.second; ++it)
	{
		const uint256& hash = it->second;
		CWalletTx* copyTo = &mapWallet[hash];
		
		if (copyFrom == copyTo)
		{
			continue;
		}
		
		copyTo->mapValue = copyFrom->mapValue;
		copyTo->vOrderForm = copyFrom->vOrderForm;
		// fTimeReceivedIsTxTime not copied on purpose
		// nTimeReceived not copied on purpose
		copyTo->nTimeSmart = copyFrom->nTimeSmart;
		copyTo->fFromMe = copyFrom->fFromMe;
		copyTo->strFromAccount = copyFrom->strFromAccount;
		// nOrderPos not copied on purpose
		// cached members not copied on purpose
	}
}

/**
	Public Functions
*/
CWallet::CWallet()
{
	SetNull();
}

CWallet::CWallet(std::string strWalletFileIn)
{
	SetNull();

	strWalletFile = strWalletFileIn;
	fFileBacked = true;
}

bool CWallet::HasCollateralInputs(bool fOnlyConfirmed) const
{
	std::vector<COutput> vCoins;
	
	AvailableCoins(vCoins, fOnlyConfirmed);

	int nFound = 0;
	for(const COutput& out : vCoins)
	{
		if(IsCollateralAmount(out.tx->vout[out.i].nValue))
		{
			nFound++;
		}
	}

	return nFound > 0;
}

bool CWallet::IsCollateralAmount(int64_t nInputAmount) const
{
	return  nInputAmount != 0 &&
			nInputAmount % MNengine_COLLATERAL == 0 &&
			nInputAmount < MNengine_COLLATERAL * 5 &&
			nInputAmount > MNengine_COLLATERAL;
}

int CWallet::CountInputsWithAmount(int64_t nInputAmount)
{
	int64_t nTotal = 0;

	{
		LOCK(cs_wallet);
		
		for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
		{
			const CWalletTx* pcoin = &(*it).second;
			if (pcoin->IsTrusted())
			{
				int nDepth = pcoin->GetDepthInMainChain(false);

				for (unsigned int i = 0; i < pcoin->vout.size(); i++)
				{
					bool mine = IsMine(pcoin->vout[i]);
					COutput out = COutput(pcoin, i, nDepth, mine);
					CTxIn vin = CTxIn(out.tx->GetHash(), out.i);

					if(out.tx->vout[out.i].nValue != nInputAmount)
					{
						continue;
					}
					
					if(pcoin->IsSpent(i) || !IsMine(pcoin->vout[i]))
					{
						continue;
					}
					
					nTotal++;
				}
			}
		}
	}

	return nTotal;
}

bool CWallet::SelectCoinsCollateral(std::vector<CTxIn>& setCoinsRet, int64_t& nValueRet) const
{
	std::vector<COutput> vCoins;

	//printf(" selecting coins for collateral\n");
	AvailableCoins(vCoins);

	//printf("found coins %d\n", (int)vCoins.size());

	setCoins_t setCoinsRet2;

	for(const COutput& out : vCoins)
	{
		// collateral inputs will always be a multiple of DARSEND_COLLATERAL, up to five
		if(IsCollateralAmount(out.tx->vout[out.i].nValue))
		{
			CTxIn vin = CTxIn(out.tx->GetHash(),out.i);

			vin.prevPubKey = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey
			nValueRet += out.tx->vout[out.i].nValue;
			setCoinsRet.push_back(vin);
			setCoinsRet2.insert(std::make_pair(out.tx, out.i));
			return true;
		}
	}

	return false;
}

bool CWallet::GetTransaction(const uint256 &hashTx, CWalletTx& wtx)
{
	{
		LOCK(cs_wallet);
		
		mapWallet_t::iterator mi = mapWallet.find(hashTx);
		
		if (mi != mapWallet.end())
		{
			wtx = (*mi).second;
			
			return true;
		}
	}

	return false;
}

bool CWallet::GetStakeWeightFromValue(const int64_t& nTime, const int64_t& nValue, uint64_t& nWeight)
{
	//This is a negative value when there is no weight. But set it to zero
	//so the user is not confused. Used in reporting in Coin Control.
	// Descisions based on this function should be used with care.
	int64_t nTimeWeight = GetWeight(nTime, (int64_t)GetTime());
	
	if (nTimeWeight < 0)
	{
			nTimeWeight=0;
	}
	
	CBigNum bnCoinDayWeight = CBigNum(nValue) * nTimeWeight / COIN / (24 * 60 * 60);
	
	nWeight = bnCoinDayWeight.getuint64();
	
	return true;
}

void CWallet::SetNull()
{
	nWalletVersion = FEATURE_BASE;
	nWalletMaxVersion = FEATURE_BASE;
	fFileBacked = false;
	nMasterKeyMaxID = 0;
	pwalletdbEncryption = NULL;
	nOrderPosNext = 0;
	nTimeFirstKey = 0;
	nLastFilteredHeight = 0;
	fWalletUnlockAnonymizeOnly = false;
}

const CWalletTx* CWallet::GetWalletTx(const uint256& hash) const
{
    LOCK(cs_wallet);
	
    mapWallet_t::const_iterator it = mapWallet.find(hash);
	
    if (it == mapWallet.end())
	{
        return NULL;
	}

    return &(it->second);
}

// check whether we are allowed to upgrade (or already support) to the named feature
bool CWallet::CanSupportFeature(enum WalletFeature wf) 
{
	AssertLockHeld(cs_wallet);
	
	return nWalletMaxVersion >= wf;
}

void CWallet::AvailableCoinsForStaking(std::vector<COutput>& vCoins, unsigned int nSpendTime) const
{
	vCoins.clear();

	{
		LOCK2(cs_main, cs_wallet);
		
		for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
		{
			const CWalletTx* pcoin = &(*it).second;

			int nDepth = pcoin->GetDepthInMainChain();
			if (nDepth < 1)
			{
				continue;
			}
			
			if (nDepth < nStakeMinConfirmations)
			{
				continue;
			}
			
			if (pcoin->GetBlocksToMaturity() > 0)
			{
				continue;
			}
			
			bool found = false;
			
			for (unsigned int i = 0; i < pcoin->vout.size(); i++)
			{
				if (pcoin->vout[i].nValue == MasternodeCollateral(pindexBest->nHeight)*COIN)
				{
					//LogPrintf("CWallet::AvailableCoinsForStaking - Found Masternode collateral.\n");
					
					found = true;
					
					break;
				}
				
				if (IsCollateralAmount(pcoin->vout[i].nValue))
				{
					//LogPrintf("CWallet::AvailableCoinsForStaking - Found Collateral amount.\n");
					
					found = true;
					
					break;
				}
			}

			if(found)
			{
				continue;
			}
			
			for (unsigned int i = 0; i < pcoin->vout.size(); i++)
			{
				isminetype mine = IsMine(pcoin->vout[i]);
				
				if (
					!(pcoin->IsSpent(i)) &&
					mine != ISMINE_NO &&
					pcoin->vout[i].nValue >= nMinimumInputValue
				)
				{
					vCoins.push_back(COutput(pcoin, i, nDepth, mine & ISMINE_SPENDABLE));
				}
			}
		}
	}
}

// populate vCoins with vector of available COutputs.
void CWallet::AvailableCoins(std::vector<COutput>& vCoins, bool fOnlyConfirmed, const CCoinControl *coinControl,
		AvailableCoinsType coin_type, bool useIX) const
{
	vCoins.clear();

	{
		LOCK2(cs_main, cs_wallet);
		
		for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
		{
			const CWalletTx* pcoin = &(*it).second;

			if (!IsFinalTx(*pcoin))
			{
				continue;
			}
			
			if (fOnlyConfirmed && !pcoin->IsTrusted())
			{
				continue;
			}
			
			if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
			{
				continue;
			}
		
			if(pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() > 0)
			{
				continue;
			}
			
			int nDepth = pcoin->GetDepthInMainChain(false);
			if (nDepth <= 0) // NOTE: coincontrol fix / ignore 0 confirm
			{
				continue;
			}
			
			// do not use IX for inputs that have less then 6 blockchain confirmations
			if (useIX && nDepth < 10)
			{
				continue;
			}
			
			for (unsigned int i = 0; i < pcoin->vout.size(); i++)
			{
				bool found = false;
				
				if(coin_type == ONLY_NOT10000IFMN)
				{
					found = !(fMasterNode && pcoin->vout[i].nValue == MasternodeCollateral(pindexBest->nHeight)*COIN);
				}
				else if (coin_type == ONLY_NONDENOMINATED_NOT10000IFMN)
				{
					if (IsCollateralAmount(pcoin->vout[i].nValue))
					{
						continue; // do not use collateral amounts
					}
					
					if(fMasterNode)
					{
						found = pcoin->vout[i].nValue != MasternodeCollateral(pindexBest->nHeight)*COIN; // do not use Hot MN funds
					}
				}
				else
				{
					found = true;
				}
				
				if(!found)
				{
					continue;
				}
				
				isminetype mine = IsMine(pcoin->vout[i]);
				
				if (
					!(pcoin->IsSpent(i)) &&
					mine != ISMINE_NO &&
					!IsLockedCoin((*it).first, i) &&
					pcoin->vout[i].nValue > 0 &&
					(
						!coinControl ||
						!coinControl->HasSelected() ||
						coinControl->IsSelected((*it).first, i)
					)
				)
				{
					vCoins.push_back(COutput(pcoin, i, nDepth, mine & ISMINE_SPENDABLE));
				}
			}
		}
	}
}

void CWallet::AvailableCoinsMN(std::vector<COutput>& vCoins, bool fOnlyConfirmed, const CCoinControl *coinControl,
		AvailableCoinsType coin_type, bool useIX) const
{
	vCoins.clear();

	{
		LOCK2(cs_main, cs_wallet);

		for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
		{
			const CWalletTx* pcoin = &(*it).second;

			if (!IsFinalTx(*pcoin))
			{
				continue;
			}

			if (fOnlyConfirmed && !pcoin->IsTrusted())
			{
				continue;
			}

			if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
			{
				continue;
			}

			if(pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() > 0)
			{
				continue;
			}
			
			int nDepth = pcoin->GetDepthInMainChain();
			if (nDepth <= 0) // NOTE: coincontrol fix / ignore 0 confirm
			{
				continue;
			}
			
			// do not use IX for inputs that have less then 6 blockchain confirmations
			if (useIX && nDepth < 10)
			{
				continue;
			}
			
			for (unsigned int i = 0; i < pcoin->vout.size(); i++)
			{
				bool found = false;

				if(coin_type == ONLY_NOT10000IFMN)
				{
					found = !(fMasterNode && pcoin->vout[i].nValue == MasternodeCollateral(pindexBest->nHeight)*COIN);
				}
				else if (coin_type == ONLY_NONDENOMINATED_NOT10000IFMN)
				{
					if (IsCollateralAmount(pcoin->vout[i].nValue))
					{
						continue; // do not use collateral amounts
					}
					
					if(fMasterNode)
					{
						found = pcoin->vout[i].nValue != MasternodeCollateral(pindexBest->nHeight)*COIN; // do not use Hot MN funds
					}
				}
				else
				{
					found = true;
				}
				
				if(!found)
				{
					continue;
				}
				
				isminetype mine = IsMine(pcoin->vout[i]);
				
				if (
					!(pcoin->IsSpent(i)) &&
					mine != ISMINE_NO &&
					!IsLockedCoin((*it).first, i) &&
					pcoin->vout[i].nValue > 0 &&
					(
						!coinControl ||
						!coinControl->HasSelected() ||
						coinControl->IsSelected((*it).first, i)
					)
				)
				{
					vCoins.push_back(COutput(pcoin, i, nDepth, (mine & ISMINE_SPENDABLE) != ISMINE_NO));
				}
			}
		}
	}
}

bool CWallet::SelectCoinsMinConf(int64_t nTargetValue, unsigned int nSpendTime, int nConfMine, int nConfTheirs,
		std::vector<COutput> vCoins, setCoins_t& setCoinsRet, int64_t& nValueRet) const
{
	setCoinsRet.clear();
	nValueRet = 0;

	// List of values less than target
	std::pair<int64_t, std::pair<const CWalletTx*,unsigned int> > coinLowestLarger;
	coinLowestLarger.first = std::numeric_limits<int64_t>::max();
	coinLowestLarger.second.first = NULL;
	std::vector<std::pair<int64_t, std::pair<const CWalletTx*,unsigned int> > > vValue;
	int64_t nTotalLower = 0;

	std::shuffle(vCoins.begin(), vCoins.end(), std::mt19937(std::random_device()()));

	for(const COutput &output : vCoins)
	{
		if (!output.fSpendable)
		{
			continue;
		}
		
		const CWalletTx *pcoin = output.tx;

		if (output.nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? nConfMine : nConfTheirs))
		{
			continue;
		}
		
		int i = output.i;

		// Follow the timestamp rules
		if (pcoin->nTime > nSpendTime)
		{
			continue;
		}
		
		int64_t n = pcoin->vout[i].nValue;

		std::pair<int64_t,std::pair<const CWalletTx*,unsigned int> > coin = std::make_pair(n,std::make_pair(pcoin, i));

		if (n == nTargetValue)
		{
			setCoinsRet.insert(coin.second);
			nValueRet += coin.first;
			
			return true;
		}
		else if (n < nTargetValue + CENT)
		{
			vValue.push_back(coin);
			nTotalLower += n;
		}
		else if (n < coinLowestLarger.first)
		{
			coinLowestLarger = coin;
		}
	}

	if (nTotalLower == nTargetValue)
	{
		for (unsigned int i = 0; i < vValue.size(); ++i)
		{
			setCoinsRet.insert(vValue[i].second);
			nValueRet += vValue[i].first;
		}
		
		return true;
	}

	if (nTotalLower < nTargetValue)
	{
		if (coinLowestLarger.second.first == NULL)
		{
			return false;
		}
		
		setCoinsRet.insert(coinLowestLarger.second);
		nValueRet += coinLowestLarger.first;
		
		return true;
	}

	// Solve subset sum by stochastic approximation
	sort(vValue.rbegin(), vValue.rend(), CompareValueOnly<std::pair<const CWalletTx*, unsigned int>>());
	std::vector<char> vfBest;
	int64_t nBest;

	ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest, 1000);

	if (nBest != nTargetValue && nTotalLower >= nTargetValue + CENT)
	{
		ApproximateBestSubset(vValue, nTotalLower, nTargetValue + CENT, vfBest, nBest, 1000);
	}

	// If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
	//                                   or the next bigger coin is closer), return the bigger coin
	if (
		coinLowestLarger.second.first &&
		(
			(
				nBest != nTargetValue &&
				nBest < nTargetValue + CENT
			)
			||
			coinLowestLarger.first <= nBest
		)
	)
	{
		setCoinsRet.insert(coinLowestLarger.second);
		nValueRet += coinLowestLarger.first;
	}
	else
	{
		for (unsigned int i = 0; i < vValue.size(); i++)
		{
			if (vfBest[i])
			{
				setCoinsRet.insert(vValue[i].second);
				nValueRet += vValue[i].first;
			}
		}

		LogPrint("selectcoins", "SelectCoins() best subset: ");
		
		for (unsigned int i = 0; i < vValue.size(); i++)
		{
			if (vfBest[i])
			{
				LogPrint("selectcoins", "%s ", FormatMoney(vValue[i].first));
			}
		}
		
		LogPrint("selectcoins", "total %s\n", FormatMoney(nBest));
	}

	return true;
}

// Outpoint is spent if any non-conflicted transaction
// spends it:
bool CWallet::IsSpent(const uint256& hash, unsigned int n) const
{
	const COutPoint outpoint(hash, n);
	mmTxSpendsRange_t range;
	range = mmTxSpends.equal_range(outpoint);

	for (mmTxSpends_t::const_iterator it = range.first; it != range.second; ++it)
	{
		const uint256& wtxid = it->second;
		mapWallet_t::const_iterator mit = mapWallet.find(wtxid);
		
		if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0)
		{
			return true; // Spent
		}
	}

	return false;
}

bool CWallet::IsLockedCoin(uint256 hash, unsigned int n) const
{
	AssertLockHeld(cs_wallet); // setLockedCoins

	COutPoint outpt(hash, n);

	return (setLockedCoins.count(outpt) > 0);
}

void CWallet::LockCoin(COutPoint& output)
{
	AssertLockHeld(cs_wallet); // setLockedCoins

	setLockedCoins.insert(output);
}

void CWallet::UnlockCoin(COutPoint& output)
{
	AssertLockHeld(cs_wallet); // setLockedCoins

	setLockedCoins.erase(output);
}

void CWallet::UnlockAllCoins()
{
	AssertLockHeld(cs_wallet); // setLockedCoins

	setLockedCoins.clear();
}

void CWallet::ListLockedCoins(std::vector<COutPoint>& vOutpts)
{
	AssertLockHeld(cs_wallet); // setLockedCoins

	for (std::set<COutPoint>::iterator it = setLockedCoins.begin(); it != setLockedCoins.end(); it++)
	{
		COutPoint outpt = (*it);
		
		vOutpts.push_back(outpt);
	}
}

int64_t CWallet::GetTotalValue(std::vector<CTxIn> vCoins)
{
	int64_t nTotalValue = 0;
	CWalletTx wtx;

	for(CTxIn i : vCoins)
	{
		if (mapWallet.count(i.prevout.hash))
		{
			CWalletTx& wtx = mapWallet[i.prevout.hash];
			
			if(i.prevout.n < wtx.vout.size())
			{
				nTotalValue += wtx.vout[i.prevout.n].nValue;
			}
		}
		else
		{
			LogPrintf("GetTotalValue -- Couldn't find transaction\n");
		}
	}

	return nTotalValue;
}

CPubKey CWallet::GenerateNewKey()
{
	AssertLockHeld(cs_wallet); // mapKeyMetadata
	
	bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

	CKey secret;
	secret.MakeNewKey(fCompressed);

	// Compressed public keys were introduced in version 0.6.0
	if (fCompressed)
	{
		SetMinVersion(FEATURE_COMPRPUBKEY);
	}

	CPubKey pubkey = secret.GetPubKey();

	assert(secret.VerifyPubKey(pubkey));

	// Create new metadata
	int64_t nCreationTime = GetTime();
	mapKeyMetadata[pubkey.GetID()] = CKeyMetadata(nCreationTime);

	if (!nTimeFirstKey || nCreationTime < nTimeFirstKey)
	{
		nTimeFirstKey = nCreationTime;
	}

	if (!AddKeyPubKey(secret, pubkey))
	{
		throw std::runtime_error("CWallet::GenerateNewKey() : AddKey failed");
	}

	return pubkey;
}

bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey &pubkey)
{
	AssertLockHeld(cs_wallet); // mapKeyMetadata

	if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey))
	{
		return false;
	}

		// check if we need to remove from watch-only
	CScript script;
	script = GetScriptForDestination(pubkey.GetID());

	if (HaveWatchOnly(script))
	{
		RemoveWatchOnly(script);
	}

	if (!fFileBacked)
	{
		return true;
	}

	if (!IsCrypted())
	{
		return CWalletDB(strWalletFile).WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);
	}

	return true;
}

bool CWallet::LoadKey(const CKey& key, const CPubKey &pubkey)
{
	return CCryptoKeyStore::AddKeyPubKey(key, pubkey);
}

bool CWallet::LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &meta)
{
	AssertLockHeld(cs_wallet); // mapKeyMetadata

	if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
	{
		nTimeFirstKey = meta.nCreateTime;
	}

	mapKeyMetadata[pubkey.GetID()] = meta;

	return true;
}

bool CWallet::LoadMinVersion(int nVersion)
{
	AssertLockHeld(cs_wallet);

	nWalletVersion = nVersion;
	nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion);

	return true;
}

bool CWallet::AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
	if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret))
	{
		return false;
	}

	if (!fFileBacked)
	{
		return true;
	}

	{
		LOCK(cs_wallet);
		
		if (pwalletdbEncryption)
		{
			return pwalletdbEncryption->WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
		}
		else
		{
			return CWalletDB(strWalletFile).WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
		}
	}

	return false;
}

bool CWallet::LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
	return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

bool CWallet::AddCScript(const CScript& redeemScript)
{
	if (!CCryptoKeyStore::AddCScript(redeemScript))
	{
		return false;
	}

	if (!fFileBacked)
	{
		return true;
	}

	return CWalletDB(strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
}

bool CWallet::LoadCScript(const CScript& redeemScript)
{
	/* A sanity check was added in pull #3843 to avoid adding redeemScripts
	 * that never can be redeemed. However, old wallets may still contain
	 * these. Do not add them to the wallet and warn. */
	if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
	{
		std::string strAddr = CDigitalNoteAddress(redeemScript.GetID()).ToString();
		
		LogPrintf(
			"%s: Warning: This wallet contains a redeemScript of size %u which exceeds maximum size %i thus can never be redeemed. Do not use address %s.\n",
			__func__,
			redeemScript.size(),
			MAX_SCRIPT_ELEMENT_SIZE,
			strAddr
		);
		
		return true;
	}

	return CCryptoKeyStore::AddCScript(redeemScript);
}

bool CWallet::AddWatchOnly(const CScript &dest)
{
	if (!CCryptoKeyStore::AddWatchOnly(dest))
	{
		return false;
	}

	nTimeFirstKey = 1; // No birthday information for watch-only keys.

	if (!fFileBacked)
	{
		return true;
	}

	return CWalletDB(strWalletFile).WriteWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript &dest)
{
	AssertLockHeld(cs_wallet);

	if (!CCryptoKeyStore::RemoveWatchOnly(dest))
	{
		return false;
	}

	if (!HaveWatchOnly())
	{
		NotifyWatchonlyChanged(false);
	}

	if (fFileBacked)
	{
		if (!CWalletDB(strWalletFile).EraseWatchOnly(dest))
		{
			return false;
		}
	}

	return true;
}

bool CWallet::LoadWatchOnly(const CScript &dest)
{
	return CCryptoKeyStore::AddWatchOnly(dest);
}

bool CWallet::Lock()
{
	if (IsLocked())
	{
		return true;
	}

	if (fDebug)
	{
		printf("Locking wallet.\n");
	}

	{
		LOCK(cs_wallet);
		
		CWalletDB wdb(strWalletFile);

		// -- load encrypted spend_secret of stealth addresses
		CStealthAddress sxAddrTemp;
		
		for (setStealthAddresses_t::iterator it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
		{
			if (it->scan_secret.size() < 32)
			{
				continue; // stealth address is not owned
			}
		
			// -- CStealthAddress are only sorted on spend_pubkey
			CStealthAddress &sxAddr = const_cast<CStealthAddress&>(*it);
			
			if (fDebug)
			{
				printf("Recrypting stealth key %s\n", sxAddr.Encoded().c_str());
			}
			
			sxAddrTemp.scan_pubkey = sxAddr.scan_pubkey;
			
			if (!wdb.ReadStealthAddress(sxAddrTemp))
			{
				printf("Error: Failed to read stealth key from db %s\n", sxAddr.Encoded().c_str());
				
				continue;
			}
			
			sxAddr.spend_secret = sxAddrTemp.spend_secret;
		}
	}

	bool result = LockKeyStore();
	if (result)
	{
		// Reset the fWalletUnlockStakingOnly state if wallet is locked
		fWalletUnlockStakingOnly = false;
	}

	return result;
};

bool CWallet::Unlock(const SecureString& strWalletPassphrase, bool anonymizeOnly, bool stakingOnly)
{
	SecureString strWalletPassphraseFinal;

	// If already fully unlocked, only update fWalletUnlockAnonymizeOnly
	// If unlocked for staking only, the passphrase is needed
	if(!IsLocked() && !fWalletUnlockStakingOnly)
	{
		fWalletUnlockAnonymizeOnly = anonymizeOnly;
		
		return true;
	}

	strWalletPassphraseFinal = strWalletPassphrase;

	CCrypter crypter;
	CKeyingMaterial vMasterKey;

	{
		LOCK(cs_wallet);
		
		for(const mapMasterKeys_t::value_type& pMasterKey : mapMasterKeys)
		{
			if(!crypter.SetKeyFromPassphrase(
					strWalletPassphraseFinal,
					pMasterKey.second.vchSalt,
					pMasterKey.second.nDeriveIterations,
					pMasterKey.second.nDerivationMethod
				)
			)
			{
				return false;
			}
			
			if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
			{
				return false;
			}
			
			if (!CCryptoKeyStore::Unlock(vMasterKey))
			{
				return false;
			}
			
			break;
		}

		fWalletUnlockAnonymizeOnly = anonymizeOnly;
		fWalletUnlockStakingOnly = stakingOnly;
		UnlockStealthAddresses(vMasterKey);
		DigitalNote::SMSG::WalletUnlocked();
		
		return true;
	}

	return false;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase)
{
	bool fWasLocked = IsLocked();

	SecureString strOldWalletPassphraseFinal;
	strOldWalletPassphraseFinal = strOldWalletPassphrase;

	{
		LOCK(cs_wallet);
		Lock();

		CCrypter crypter;
		CKeyingMaterial vMasterKey;
		
		for(mapMasterKeys_t::value_type& pMasterKey : mapMasterKeys)
		{
			if(!crypter.SetKeyFromPassphrase(
					strOldWalletPassphraseFinal,
					pMasterKey.second.vchSalt,
					pMasterKey.second.nDeriveIterations,
					pMasterKey.second.nDerivationMethod
				)
			)
			{
				return false;
			}
			
			if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
			{
				return false;
			}
			
			if (CCryptoKeyStore::Unlock(vMasterKey) && UnlockStealthAddresses(vMasterKey))
			{
				int64_t nStartTime = GetTimeMillis();
				crypter.SetKeyFromPassphrase(
					strNewWalletPassphrase,
					pMasterKey.second.vchSalt,
					pMasterKey.second.nDeriveIterations,
					pMasterKey.second.nDerivationMethod
				);
				pMasterKey.second.nDeriveIterations = pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime)));

				nStartTime = GetTimeMillis();
				crypter.SetKeyFromPassphrase(
					strNewWalletPassphrase,
					pMasterKey.second.vchSalt,
					pMasterKey.second.nDeriveIterations,
					pMasterKey.second.nDerivationMethod
				);
				pMasterKey.second.nDeriveIterations = (pMasterKey.second.nDeriveIterations + pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

				if (pMasterKey.second.nDeriveIterations < 25000)
				{
					pMasterKey.second.nDeriveIterations = 25000;
				}
				
				LogPrintf("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

				if (!crypter.SetKeyFromPassphrase(
						strNewWalletPassphrase,
						pMasterKey.second.vchSalt,
						pMasterKey.second.nDeriveIterations,
						pMasterKey.second.nDerivationMethod
					)
				)
				{
					return false;
				}
				
				if (!crypter.Encrypt(vMasterKey, pMasterKey.second.vchCryptedKey))
				{
					return false;
				}
				
				CWalletDB(strWalletFile).WriteMasterKey(pMasterKey.first, pMasterKey.second);
				
				if (fWasLocked)
				{
					Lock();
				}
				
				return true;
			}
		}
	}

	return false;
}

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase)
{
	if (IsCrypted())
	{
		return false;
	}

	CKeyingMaterial vMasterKey;
	RandAddSeedPerfmon();

	vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
	if (!GetRandBytes(&vMasterKey[0], WALLET_CRYPTO_KEY_SIZE))
	{
		return false;
	}

	CMasterKey kMasterKey(nDerivationMethodIndex);

	RandAddSeedPerfmon();
	kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);

	if (!GetRandBytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE))
	{
		return false;
	}

	CCrypter crypter;
	int64_t nStartTime = GetTimeMillis();
	crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25000, kMasterKey.nDerivationMethod);
	kMasterKey.nDeriveIterations = 2500000 / ((double)(GetTimeMillis() - nStartTime));

	nStartTime = GetTimeMillis();
	crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod);
	kMasterKey.nDeriveIterations = (kMasterKey.nDeriveIterations + kMasterKey.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

	if (kMasterKey.nDeriveIterations < 25000)
	{
		kMasterKey.nDeriveIterations = 25000;
	}

	LogPrintf("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

	if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod))
	{
		return false;
	}

	if (!crypter.Encrypt(vMasterKey, kMasterKey.vchCryptedKey))
	{
		return false;
	}

	{
		LOCK(cs_wallet);
		
		mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
		
		if (fFileBacked)
		{
			pwalletdbEncryption = new CWalletDB(strWalletFile);
			
			if (!pwalletdbEncryption->TxnBegin())
			{
				return false;
			}
			
			pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey);
		}

		if (!EncryptKeys(vMasterKey))
		{
			if (fFileBacked)
			{
				pwalletdbEncryption->TxnAbort();
			}
			
			exit(1); //We now probably have half of our keys encrypted in memory, and half not...die and let the user reload their unencrypted wallet.
		}
		
		for (setStealthAddresses_t::iterator it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
		{
			if (it->scan_secret.size() < 32)
			{
				continue; // stealth address is not owned
			}
			
			// -- CStealthAddress is only sorted on spend_pubkey
			CStealthAddress &sxAddr = const_cast<CStealthAddress&>(*it);

			if (fDebug)
			{
				printf("Encrypting stealth key %s\n", sxAddr.Encoded().c_str());
			}
			
			std::vector<unsigned char> vchCryptedSecret;

			CSecret vchSecret;
			vchSecret.resize(32);
			memcpy(&vchSecret[0], &sxAddr.spend_secret[0], 32);

			uint256 iv = Hash(sxAddr.spend_pubkey.begin(), sxAddr.spend_pubkey.end());
			if (!EncryptSecret(vMasterKey, vchSecret, iv, vchCryptedSecret))
			{
				printf("Error: Failed encrypting stealth key %s\n", sxAddr.Encoded().c_str());
				
				continue;
			}

			sxAddr.spend_secret = vchCryptedSecret;
			pwalletdbEncryption->WriteStealthAddress(sxAddr);
		};

		// Encryption was introduced in version 0.4.0
		SetMinVersion(FEATURE_WALLETCRYPT, pwalletdbEncryption, true);

		if (fFileBacked)
		{
			if (!pwalletdbEncryption->TxnCommit())
			{
				exit(1); //We now have keys encrypted in memory, but no on disk...die to avoid confusion and let the user reload their unencrypted wallet.
			}
			
			delete pwalletdbEncryption;
			
			pwalletdbEncryption = NULL;
		}

		Lock();
		Unlock(strWalletPassphrase);
		NewKeyPool();
		Lock();

		// Need to completely rewrite the wallet file; if we don't, bdb might keep
		// bits of the unencrypted private key in slack space in the database file.
		CDB::Rewrite(strWalletFile);
	}

	NotifyStatusChanged(this);

	return true;
}

void CWallet::GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const
{
	AssertLockHeld(cs_wallet); // mapKeyMetadata
	
	mapKeyBirth.clear();

	// get birth times for keys with metadata
	for (mapKeyMetadata_t::const_iterator it = mapKeyMetadata.begin(); it != mapKeyMetadata.end(); it++)
	{
		if (it->second.nCreateTime)
		{
			mapKeyBirth[it->first] = it->second.nCreateTime;
		}
	}

	// map in which we'll infer heights of other keys
	CBlockIndex *pindexMax = FindBlockByHeight(std::max(0, nBestHeight - 144)); // the tip can be reorganised; use a 144-block safety margin
	std::map<CKeyID, CBlockIndex*> mapKeyFirstBlock;
	std::set<CKeyID> setKeys;

	GetKeys(setKeys);

	for(const CKeyID &keyid : setKeys)
	{
		if (mapKeyBirth.count(keyid) == 0)
		{
			mapKeyFirstBlock[keyid] = pindexMax;
		}
	}

	setKeys.clear();

	// if there are no such keys, we're done
	if (mapKeyFirstBlock.empty())
	{
		return;
	}

	// find first block that affects those keys, if there are any left
	std::vector<CKeyID> vAffected;
	for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); it++)
	{
		// iterate over all wallet transactions...
		const CWalletTx &wtx = (*it).second;
		std::map<uint256, CBlockIndex*>::const_iterator blit = mapBlockIndex.find(wtx.hashBlock);
		
		if (blit != mapBlockIndex.end() && blit->second->IsInMainChain())
		{
			// ... which are already in a block
			int nHeight = blit->second->nHeight;
			
			for(const CTxOut &txout : wtx.vout)
			{
				// iterate over all their outputs
				::ExtractAffectedKeys(*this, txout.scriptPubKey, vAffected);
				
				for(const CKeyID &keyid : vAffected)
				{
					// ... and all their affected keys
					std::map<CKeyID, CBlockIndex*>::iterator rit = mapKeyFirstBlock.find(keyid);
					
					if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight)
					{
						rit->second = blit->second;
					}
				}
				
				vAffected.clear();
			}
		}
	}

	// Extract block timestamps for those keys
	for (std::map<CKeyID, CBlockIndex*>::const_iterator it = mapKeyFirstBlock.begin(); it != mapKeyFirstBlock.end(); it++)
	{
		mapKeyBirth[it->first] = it->second->nTime - 7200; // block times can be 2h off
	}
}

int64_t CWallet::IncOrderPosNext(CWalletDB *pwalletdb)
{
	AssertLockHeld(cs_wallet); // nOrderPosNext

	int64_t nRet = nOrderPosNext++;

	if (pwalletdb)
	{
		pwalletdb->WriteOrderPosNext(nOrderPosNext);
	}
	else
	{
		CWalletDB(strWalletFile).WriteOrderPosNext(nOrderPosNext);
	}

	return nRet;
}

void CWallet::MarkDirty()
{
	{
		LOCK(cs_wallet);
		
		for(std::pair<const uint256, CWalletTx>& item : mapWallet)
		{
			item.second.MarkDirty();
		}
	}
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn, bool fFromLoadWallet)
{
	uint256 hash = wtxIn.GetHash();

	if (fFromLoadWallet)
	{
		mapWallet[hash] = wtxIn;
		CWalletTx& wtx = mapWallet[hash];
		
		wtx.BindWallet(this);
		
		wtxOrdered.insert(std::make_pair(wtx.nOrderPos, TxPair(&wtx, (CAccountingEntry*)0)));
		
		AddToSpends(hash);
	}
	else
	{
		LOCK(cs_wallet);
		
		// Inserts only if not already there, returns tx inserted or tx found
		std::pair<mapWallet_t::iterator, bool> ret = mapWallet.insert(std::make_pair(hash, wtxIn));
		CWalletTx& wtx = (*ret.first).second;
		wtx.BindWallet(this);
		bool fInsertedNew = ret.second;
		
		if (fInsertedNew)
		{
			wtx.nTimeReceived = GetAdjustedTime();
			wtx.nOrderPos = IncOrderPosNext();
			wtxOrdered.insert(std::make_pair(wtx.nOrderPos, TxPair(&wtx, (CAccountingEntry*)0)));

			wtx.nTimeSmart = wtx.nTimeReceived;
			
			if (wtxIn.hashBlock != 0)
			{
				if (mapBlockIndex.count(wtxIn.hashBlock))
				{
					unsigned int latestNow = wtx.nTimeReceived;
					unsigned int latestEntry = 0;
					{
						// Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
						int64_t latestTolerated = latestNow + 300;
						const TxItems & txOrdered = wtxOrdered;
						
						for (TxItems::const_reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
						{
							CWalletTx *const pwtx = (*it).second.first;
							
							if (pwtx == &wtx)
							{
								continue;
							}
							
							CAccountingEntry *const pacentry = (*it).second.second;
							int64_t nSmartTime;
							
							if (pwtx)
							{
								nSmartTime = pwtx->nTimeSmart;
								
								if (!nSmartTime)
								{
									nSmartTime = pwtx->nTimeReceived;
								}
							}
							else
							{
								nSmartTime = pacentry->nTime;
							}
							
							if (nSmartTime <= latestTolerated)
							{
								latestEntry = nSmartTime;
								
								if (nSmartTime > latestNow)
								{
									latestNow = nSmartTime;
								}
								
								break;
							}
						}
					}

					unsigned int& blocktime = mapBlockIndex[wtxIn.hashBlock]->nTime;
					wtx.nTimeSmart = std::max(latestEntry, std::min(blocktime, latestNow));
				}
				else
				{
					LogPrintf("AddToWallet() : found %s in block %s not in index\n",
							 wtxIn.GetHash().ToString(),
							 wtxIn.hashBlock.ToString());
				}
			}
		}

		bool fUpdated = false;
		
		if (!fInsertedNew)
		{
			// Merge
			if (wtxIn.hashBlock != 0 && wtxIn.hashBlock != wtx.hashBlock)
			{
				wtx.hashBlock = wtxIn.hashBlock;
				fUpdated = true;
			}
			
			if (wtxIn.nIndex != -1 && (wtxIn.vMerkleBranch != wtx.vMerkleBranch || wtxIn.nIndex != wtx.nIndex))
			{
				wtx.vMerkleBranch = wtxIn.vMerkleBranch;
				wtx.nIndex = wtxIn.nIndex;
				fUpdated = true;
			}
			
			if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe)
			{
				wtx.fFromMe = wtxIn.fFromMe;
				fUpdated = true;
			}
			
			fUpdated |= wtx.UpdateSpent(wtxIn.vfSpent);
		}

		//// debug print
		LogPrintf("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString(), (fInsertedNew ? "new" : ""), (fUpdated ? "update" : ""));

		// Write to disk
		if (fInsertedNew || fUpdated)
		{
			if (!wtx.WriteToDisk())
			{
				return false;
			}
		}
		
		// Break debit/credit balance caches:
		wtx.MarkDirty();

		// Notify UI of new or updated transaction
		NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

		// notify an external script when a wallet transaction comes in or is updated
		std::string strCmd = GetArg("-walletnotify", "");

		if ( !strCmd.empty())
		{
			boost::replace_all(strCmd, "%s", wtxIn.GetHash().GetHex());
			boost::thread t(runCommand, strCmd); // thread runs free
		}
	}
	
	return true;
}

void CWallet::SyncTransaction(const CTransaction& tx, const CBlock* pblock, bool fConnect, bool fFixSpentCoins)
{
	LOCK2(cs_main, cs_wallet);

	if (!AddToWalletIfInvolvingMe(tx, pblock, true))
	{
		return; // Not one of ours
	}

	// If a transaction changes 'conflicted' state, that changes the balance
	// available of the outputs it spends. So force those to be
	// recomputed, also:
	for(const CTxIn& txin : tx.vin)
	{
		if (mapWallet.count(txin.prevout.hash))
		{
			mapWallet[txin.prevout.hash].MarkDirty();
		}
	}

	if (!fConnect)
	{
		// wallets need to refund inputs when disconnecting coinstake
		if (tx.IsCoinStake())
		{
			if (IsFromMe(tx))
			{
				DisableTransaction(tx);
			}
		}
		
		return;
	}

	AddToWalletIfInvolvingMe(tx, pblock, true);

	if (fFixSpentCoins)
	{
		// Mark old coins as spent
		std::set<CWalletTx*> setCoins;
		
		for(const CTxIn& txin : tx.vin)
		{
			CWalletTx &coin = mapWallet[txin.prevout.hash];
			
			coin.BindWallet(this);
			coin.MarkSpent(txin.prevout.n);
			coin.WriteToDisk();
			
			NotifyTransactionChanged(this, coin.GetHash(), CT_UPDATED);
		}
	}
}

// Add a transaction to the wallet, or update it.
// pblock is optional, but should be provided if the transaction is known to be in a block.
// If fUpdate is true, existing transactions will be updated.
bool CWallet::AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate)
{
	uint256 hash = tx.GetHash();

	{
		LOCK(cs_wallet);
		
		bool fExisted = mapWallet.count(hash);
		
		if (fExisted && !fUpdate)
		{
			return false;
		}
		
		mapValue_t mapNarr;
		FindStealthTransactions(tx, mapNarr);

		if (fExisted || IsMine(tx) || IsFromMe(tx))
		{
			CWalletTx wtx(this,tx);

			if (!mapNarr.empty())
			{
				wtx.mapValue.insert(mapNarr.begin(), mapNarr.end());
			}
			
			// Get merkle branch if transaction was found in a block
			if (pblock)
			{
				wtx.SetMerkleBranch(pblock);
			}
			
			return AddToWallet(wtx);
		}
	}

	return false;
}

void CWallet::EraseFromWallet(const uint256 &hash)
{
	if (!fFileBacked)
	{
		return;
	}

	{
		LOCK(cs_wallet);
		
		if (mapWallet.erase(hash))
		{
			CWalletDB(strWalletFile).EraseTx(hash);
		}
	}
}

// Scan the block chain (starting in pindexStart) for transactions
// from or to us. If fUpdate is true, found transactions that already
// exist in the wallet will be updated.
int CWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate)
{
	int ret = 0;
	CBlockIndex* pindex = pindexStart;

	{
		LOCK2(cs_main, cs_wallet);
		
		while (pindex)
		{
			// no need to read and scan block, if block was created before
			// our wallet birthday (as adjusted for block time variability)
			if (nTimeFirstKey && (pindex->nTime < (nTimeFirstKey - 7200)))
			{
				pindex = pindex->pnext;
				
				continue;
			}

			CBlock block;
			block.ReadFromDisk(pindex, true);
			
			for(CTransaction& tx : block.vtx)
			{
				if (AddToWalletIfInvolvingMe(tx, &block, fUpdate))
				{
					ret++;
				}
			}
			
			pindex = pindex->pnext;
		}
	}

	return ret;
}

void CWallet::ReacceptWalletTransactions()
{
	CTxDB txdb("r");
	bool fRepeat = true;

	while (fRepeat)
	{
		LOCK2(cs_main, cs_wallet);
		
		fRepeat = false;
		std::vector<CDiskTxPos> vMissingTx;
		
		for(std::pair<const uint256, CWalletTx>& item : mapWallet)
		{
			const uint256& wtxid = item.first;
			CWalletTx& wtx = item.second;
			
			assert(wtx.GetHash() == wtxid);

			int nDepth = wtx.GetDepthInMainChain();

			if (!wtx.IsCoinBase() && nDepth < 0)
			{
				// Try to add to memory pool
				LOCK(mempool.cs);
				
				wtx.AcceptToMemoryPool(false);
			}
			
			if ((wtx.IsCoinBase() && wtx.IsSpent(0)) || (wtx.IsCoinStake() && wtx.IsSpent(1)))
			{
				continue;
			}

			CTxIndex txindex;
			bool fUpdated = false;
			
			if (txdb.ReadTxIndex(wtx.GetHash(), txindex))
			{
				// Update fSpent if a tx got spent somewhere else by a copy of wallet.dat
				if (txindex.vSpent.size() != wtx.vout.size())
				{
					LogPrintf("ERROR: ReacceptWalletTransactions() : txindex.vSpent.size() %u != wtx.vout.size() %u\n",
						txindex.vSpent.size(),
						wtx.vout.size()
					);
					
					continue;
				}
				
				for (unsigned int i = 0; i < txindex.vSpent.size(); i++)
				{
					if (wtx.IsSpent(i))
					{
						continue;
					}
					
					if (!txindex.vSpent[i].IsNull() && IsMine(wtx.vout[i]))
					{
						wtx.MarkSpent(i);
						fUpdated = true;
						vMissingTx.push_back(txindex.vSpent[i]);
					}
				}
				
				if (fUpdated)
				{
					LogPrintf("ReacceptWalletTransactions found spent coin %s XDN %s\n",
						FormatMoney(wtx.GetCredit(ISMINE_ALL)),
						wtx.GetHash().ToString()
					);
					
					wtx.MarkDirty();
					wtx.WriteToDisk();
				}
			}
			else
			{
				// Re-accept any txes of ours that aren't already in a block
				if (!(wtx.IsCoinBase() || wtx.IsCoinStake()))
				{
					wtx.AcceptWalletTransaction(txdb);
				}
			}
		}
		
		if (!vMissingTx.empty())
		{
			// TODO: optimize this to scan just part of the block chain?
			if (ScanForWalletTransactions(pindexGenesisBlock))
			{
				fRepeat = true;  // Found missing transactions: re-do re-accept.
			}
		}
	}
}

void CWallet::ResendWalletTransactions(bool fForce)
{
	if (!fForce)
	{
		// Do this infrequently and randomly to avoid giving away
		// that these are our transactions.
		static int64_t nNextTime;
		
		if (GetTime() < nNextTime)
		{
			return;
		}
		
		bool fFirst = (nNextTime == 0);
		
		nNextTime = GetTime() + GetRand(30 * 60);
		
		if (fFirst)
		{
			return;
		}
		
		// Only do it if there's been a new block since last time
		static int64_t nLastTime;
		
		if (nTimeBestReceived < nLastTime)
		{
			return;
		}
		
		nLastTime = GetTime();
	}

	// Rebroadcast any of our txes that aren't in a block yet
	LogPrintf("ResendWalletTransactions()\n");

	CTxDB txdb("r");

	{
		LOCK(cs_wallet);
		// Sort them in chronological order
		std::multimap<unsigned int, CWalletTx*> mapSorted;
		
		for(std::pair<const uint256, CWalletTx>& item : mapWallet)
		{
			CWalletTx& wtx = item.second;
			
			// Don't rebroadcast until it's had plenty of time that
			// it should have gotten in already by now.
			if (fForce || nTimeBestReceived - (int64_t)wtx.nTimeReceived > 5 * 60)
			{
				mapSorted.insert(std::make_pair(wtx.nTimeReceived, &wtx));
			}
		}
		
		for(std::pair<const unsigned int, CWalletTx*>& item : mapSorted)
		{
			CWalletTx& wtx = *item.second;
			wtx.RelayWalletTransaction(txdb);
		}
	}
}

bool CWallet::ImportPrivateKey(CDigitalNoteSecret vchSecret, std::string strLabel, bool fRescan)
{
	if (fWalletUnlockStakingOnly)
	{
		return false;
	}

	CKey key = vchSecret.GetKey();
	CPubKey pubkey = key.GetPubKey();
	assert(key.VerifyPubKey(pubkey));
	CKeyID vchAddress = pubkey.GetID();

	{
		LOCK2(cs_main, cs_wallet);

		MarkDirty();
		SetAddressBookName(vchAddress, strLabel);

		// Don't throw error in case a key is already there
		if (HaveKey(vchAddress))
		{
			return true;
		}
		
		mapKeyMetadata[vchAddress].nCreateTime = 1;

		if (!AddKeyPubKey(key, pubkey))
		{
			return false;
		}
		
		// whenever a key is imported, we need to scan the whole chain
		nTimeFirstKey = 1; // 0 would be considered 'no value'

		if (fRescan)
		{
			ScanForWalletTransactions(pindexGenesisBlock, true);
			ReacceptWalletTransactions();
		}
	}

	return true;
}

CAmount CWallet::GetBalance() const
{
	CAmount nTotal = 0;

	{
		LOCK2(cs_main, cs_wallet);
		
		for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
		{
			const CWalletTx* pcoin = &(*it).second;
			
			if (pcoin->IsTrusted())
			{
				nTotal += pcoin->GetAvailableCredit();
			}
		}
	}

	return nTotal;
}

// ppcoin: total coins staked (non-spendable until maturity)
CAmount CWallet::GetStake() const
{
	CAmount nTotal = 0;

	LOCK2(cs_main, cs_wallet);

	for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
	{
		const CWalletTx* pcoin = &(*it).second;
		
		if (pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() > 0 && pcoin->GetDepthInMainChain() > 0)
		{
			nTotal += CWallet::GetCredit(*pcoin, ISMINE_ALL);
		}
	}

	return nTotal;
}

CAmount CWallet::GetNewMint() const
{
	CAmount nTotal = 0;

	LOCK2(cs_main, cs_wallet);

	for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
	{
		const CWalletTx* pcoin = &(*it).second;
		
		if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0 && pcoin->GetDepthInMainChain() > 0)
		{
			nTotal += CWallet::GetCredit(*pcoin, ISMINE_ALL);
		}
	}

	return nTotal;
}

CAmount CWallet::GetUnconfirmedBalance() const
{
	CAmount nTotal = 0;

	{
		LOCK2(cs_main, cs_wallet);
		
		for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
		{
			const CWalletTx* pcoin = &(*it).second;
			
			if (!IsFinalTx(*pcoin) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
			{
				nTotal += pcoin->GetAvailableCredit();
			}
		}
	}

	return nTotal;
}

CAmount CWallet::GetImmatureBalance() const
{
	CAmount nTotal = 0;

	{
		LOCK2(cs_main, cs_wallet);
		
		for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
		{
			const CWalletTx* pcoin = &(*it).second;
			nTotal += pcoin->GetImmatureCredit();
		}
	}

	return nTotal;
}

CAmount CWallet::GetWatchOnlyBalance() const
{
	CAmount nTotal = 0;

	{
		LOCK2(cs_main, cs_wallet);
		
		for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
		{
			const CWalletTx* pcoin = &(*it).second;
			
			if (pcoin->IsTrusted())
			{
				nTotal += pcoin->GetAvailableWatchOnlyCredit();
			}
		}
	}

	return nTotal;
}

CAmount CWallet::GetWatchOnlyStake() const
{
	CAmount nTotal = 0;

	LOCK2(cs_main, cs_wallet);

	for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
	{
		const CWalletTx* pcoin = &(*it).second;
		
		if (pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() > 0 && pcoin->GetDepthInMainChain() > 0)
		{
			nTotal += CWallet::GetCredit(*pcoin, ISMINE_WATCH_ONLY);
		}
	}

	return nTotal;
}

CAmount CWallet::GetUnconfirmedWatchOnlyBalance() const
{
	CAmount nTotal = 0;

	{
		LOCK2(cs_main, cs_wallet);
		
		for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
		{
			const CWalletTx* pcoin = &(*it).second;
			
			if (!IsFinalTx(*pcoin) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
			{
				nTotal += pcoin->GetAvailableWatchOnlyCredit();
			}
		}
	}

	return nTotal;
}

CAmount CWallet::GetImmatureWatchOnlyBalance() const
{
	CAmount nTotal = 0;

	{
		LOCK2(cs_main, cs_wallet);
		
		for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
		{
			const CWalletTx* pcoin = &(*it).second;
			
			nTotal += pcoin->GetImmatureWatchOnlyCredit();
		}
	}

	return nTotal;
}

bool CWallet::CreateTransaction(const std::vector<std::pair<CScript, int64_t> >& vecSend, CWalletTx& wtxNew, CReserveKey& reservekey,
		int64_t& nFeeRet, int32_t& nChangePos, std::string& strFailReason, const CCoinControl* coinControl,
		AvailableCoinsType coin_type, bool useIX)
{
	int64_t nValue = 0;

	for(const std::pair<CScript, int64_t>& s : vecSend)
	{
		if (nValue < 0)
		{
			strFailReason = ui_translate("Transaction amounts must be positive");
			
			return false;
		}
		
		nValue += s.second;
	}

	if (vecSend.empty() || nValue < 0)
	{
		strFailReason = ui_translate("Transaction amounts must be positive");
		
		return false;
	}

	wtxNew.fTimeReceivedIsTxTime = true;
	wtxNew.BindWallet(this);

	{
		// txdb must be opened before the mapWallet lock
		CTxDB txdb("r");
		
		LOCK2(cs_main, cs_wallet);
		
		{
			nFeeRet = nTransactionFee;
			
			if(useIX)
			{
				nFeeRet = std::max(CENT, nFeeRet);
			}
			
			while (true)
			{
				wtxNew.vin.clear();
				wtxNew.vout.clear();
				wtxNew.fFromMe = true;

				int64_t nTotalValue = nValue + nFeeRet;
				double dPriority = 0;
				
				// vouts to the payees
				for(const std::pair<CScript, int64_t>& s : vecSend)
				{
					CTxOut txout(s.second, s.first);
					bool fOpReturn = false;

					if(txout.IsNull() || (!txout.IsEmpty() && txout.nValue == 0))
					{
						txnouttype whichType;
						std::vector<valtype> vSolutions;
						
						if (!Solver(txout.scriptPubKey, whichType, vSolutions))
						{
							strFailReason = ui_translate("Invalid scriptPubKey");
							
							return false;
						}
						
						if(whichType == TX_NONSTANDARD)
						{
							strFailReason = ui_translate("Unknown transaction type");
							
							return false;
						}
						
						if(whichType == TX_NULL_DATA)
						{
							fOpReturn = true;
						}
					}

					if (!fOpReturn && txout.IsDust(MIN_RELAY_TX_FEE))
					{
						strFailReason = ui_translate("Transaction amount too small");
						
						return false;
					}
					
					wtxNew.vout.push_back(txout);
				}

				// Choose coins to use
				setCoins_t setCoins;
				int64_t nValueIn = 0;

				if (!SelectCoins(nTotalValue, wtxNew.nTime, setCoins, nValueIn, coinControl, coin_type, useIX))
				{
					if(coin_type == ALL_COINS)
					{
						strFailReason = ui_translate(" Insufficient funds.");
					}
					else if (coin_type == ONLY_NOT10000IFMN)
					{
						strFailReason = ui_translate(" Unable to locate enough MNengine non-denominated funds for this transaction.");
					}
					else if (coin_type == ONLY_NONDENOMINATED_NOT10000IFMN )
					{
						strFailReason = ui_translate(" Unable to locate enough MNengine non-denominated funds for this transaction that are not equal 1000 XDN.");
					}

					if(useIX)
					{
						strFailReason += ui_translate(" InstantX requires inputs with at least 10 confirmations, you might need to wait a few minutes and try again.");
					}
					
					return false;
				}
				
				for(pairCoin_t pcoin : setCoins)
				{
					int64_t nCredit = pcoin.first->vout[pcoin.second].nValue;
					//The coin age after the next block (depth+1) is used instead of the current,
					//reflecting an assumption the user would accept a bit more delay for
					//a chance at a free transaction.
					//But mempool inputs might still be in the mempool, so their age stays 0
					int age = pcoin.first->GetDepthInMainChain();
					
					if (age != 0)
					{
						age += 1;
					}
					
					dPriority += (double)nCredit * age;
				}

				int64_t nChange = nValueIn - nValue - nFeeRet;

				if (nChange > 0)
				{
					// Fill a vout to ourself
					// TODO: pass in scriptChange instead of reservekey so
					// change transaction isn't always pay-to-DigitalNote-address
					CScript scriptChange;

					// coin control: send change to custom address
					if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange))
					{
						scriptChange.SetDestination(coinControl->destChange);
					}
					// no coin control: send change to newly generated address
					else
					{
						// Note: We use a new key here to keep it from being obvious which side is the change.
						//  The drawback is that by not reusing a previous key, the change may be lost if a
						//  backup is restored, if the backup doesn't have the new private key for the change.
						//  If we reused the old key, it would be possible to add code to look for and
						//  rediscover unknown transactions that were written with keys of ours to recover
						//  post-backup change.

						// Reserve a new key pair from key pool
						CPubKey vchPubKey;
						bool ret;
						ret = reservekey.GetReservedKey(vchPubKey);
						
						assert(ret); // should never fail, as we just unlocked

						scriptChange.SetDestination(vchPubKey.GetID());
					}

					CTxOut newTxOut(nChange, scriptChange);

					// Never create dust outputs; if we would, just
					// add the dust to the fee.
					if (newTxOut.IsDust(MIN_RELAY_TX_FEE))
					{
						nFeeRet += nChange;
						nChange = 0;
						reservekey.ReturnKey();
					}
					else
					{
						// Insert change txn at random position:
						std::vector<CTxOut>::iterator position = wtxNew.vout.begin()+GetRandInt(wtxNew.vout.size()+1);
						wtxNew.vout.insert(position, newTxOut);
					}
				}
				else
				{
					reservekey.ReturnKey();
				}
				
				// Fill vin
				//
				// Note how the sequence number is set to max()-1 so that the
				// nLockTime set above actually works.
				for(const pairCoin_t& coin : setCoins)
				{
					wtxNew.vin.push_back(CTxIn(coin.first->GetHash(),coin.second));
				}
				
				// Sign
				int nIn = 0;
				for(const pairCoin_t& coin : setCoins)
				{
					if (!SignSignature(*this, *coin.first, wtxNew, nIn++))
					{
						strFailReason = ui_translate(" Signing transaction failed");
						
						return false;
					}
				}
				
				// Limit size
				unsigned int nBytes = ::GetSerializeSize(*(CTransaction*)&wtxNew, SER_NETWORK, PROTOCOL_VERSION);
				
				if (nBytes >= MAX_STANDARD_TX_SIZE)
				{
					strFailReason = ui_translate(" Transaction too large");
					
					return false;
				}
				
				dPriority = wtxNew.ComputePriority(dPriority, nBytes);

				// Check that enough fee is included
				int64_t nPayFee = nTransactionFee * (1 + (int64_t)nBytes / 1000);
				bool fAllowFree = AllowFree(dPriority);
				int64_t nMinFee = GetMinFee(wtxNew, nBytes, fAllowFree, GMF_SEND);

				if (nFeeRet < std::max(nPayFee, nMinFee))
				{
					nFeeRet = std::max(nPayFee, nMinFee);
					
					if(useIX)
					{
						nFeeRet = std::max(CENT, nFeeRet);
					}
					
					continue;
				}
				
				// Fill vtxPrev by copying from previous transactions vtxPrev
				wtxNew.AddSupportingTransactions(txdb);
				wtxNew.fTimeReceivedIsTxTime = true;

				break;
			}
		}
	}
	
	return true;
}

bool CWallet::CreateTransaction(CScript scriptPubKey, int64_t nValue, std::string& sNarr, CWalletTx& wtxNew,
		CReserveKey& reservekey, int64_t& nFeeRet, const CCoinControl* coinControl)
{
	std::vector<std::pair<CScript, int64_t> > vecSend;
	vecSend.push_back(std::make_pair(scriptPubKey, nValue));

	if (sNarr.length() > 0)
	{
		std::vector<uint8_t> vNarr(sNarr.c_str(), sNarr.c_str() + sNarr.length());
		std::vector<uint8_t> vNDesc;

		vNDesc.resize(2);
		vNDesc[0] = 'n';
		vNDesc[1] = 'p';

		CScript scriptN = CScript() << OP_RETURN << vNDesc << OP_RETURN << vNarr;

		vecSend.push_back(std::make_pair(scriptN, 0));
	}

	// -- CreateTransaction won't place change between value and narr output.
	//    narration output will be for preceding output

	int nChangePos;
	std::string strFailReason;
	bool rv = CreateTransaction(vecSend, wtxNew, reservekey, nFeeRet, nChangePos, strFailReason, coinControl);
	
	if(!strFailReason.empty())
	{
		LogPrintf("CreateTransaction(): ERROR: %s\n", strFailReason);
		
		return false;
	}
	
	// -- narration will be added to mapValue later in FindStealthTransactions From CommitTransaction
	return rv;
}

// Call after CreateTransaction unless you want to abort
bool CWallet::CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey, std::string strCommand)
{
	mapValue_t mapNarr;
	FindStealthTransactions(wtxNew, mapNarr);

	if (!mapNarr.empty())
	{
		for(const std::pair<std::string, std::string>& item : mapNarr)
		{
			wtxNew.mapValue[item.first] = item.second;
		}
	}

	{
		LOCK2(cs_main, cs_wallet);
		
		LogPrintf("CommitTransaction:\n%s", wtxNew.ToString());
		
		{
			// This is only to keep the database open to defeat the auto-flush for the
			// duration of this scope.  This is the only place where this optimization
			// maybe makes sense; please don't do it anywhere else.
			CWalletDB* pwalletdb = fFileBacked ? new CWalletDB(strWalletFile,"r") : NULL;

			// Take key pair from key pool so it won't be used again
			reservekey.KeepKey();

			// Add tx to wallet, because if it has change it's also ours,
			// otherwise just for transaction history.
			AddToWallet(wtxNew);

			// Mark old coins as spent
			std::set<CWalletTx*> setCoins;
			
			for(const CTxIn& txin : wtxNew.vin)
			{
				CWalletTx &coin = mapWallet[txin.prevout.hash];
				
				coin.BindWallet(this);
				coin.MarkSpent(txin.prevout.n);
				coin.WriteToDisk();
				
				NotifyTransactionChanged(this, coin.GetHash(), CT_UPDATED);
			}

			if (fFileBacked)
			{
				delete pwalletdb;
			}
		}

		// Track how many getdata requests our transaction gets
		mapRequestCount[wtxNew.GetHash()] = 0;

		// Broadcast
		if (!wtxNew.AcceptToMemoryPool(false))
		{
			// This must not fail. The transaction has already been signed and recorded.
			LogPrintf("CommitTransaction() : Error: Transaction not valid\n");
			
			return false;
		}
		
		wtxNew.RelayWalletTransaction(strCommand);
	}

	return true;
}

bool CWallet::AddAccountingEntry(const CAccountingEntry& acentry, CWalletDB & pwalletdb)
{
    if (!pwalletdb.WriteAccountingEntry_Backend(acentry))
	{
        return false;
	}
	
    laccentries.push_back(acentry);
    CAccountingEntry & entry = laccentries.back();
    wtxOrdered.insert(std::make_pair(entry.nOrderPos, TxPair((CWalletTx*)0, &entry)));

    return true;
}

uint64_t CWallet::GetStakeWeight() const
{
    // Choose coins to use
    int64_t nBalance = GetBalance();

    if (nBalance <= nReserveBalance)
	{
        return 0;
	}
	
    std::vector<const CWalletTx*> vwtxPrev;

    setCoins_t setCoins;
    int64_t nValueIn = 0;

    if (!SelectCoinsForStaking(nBalance - nReserveBalance, GetTime(), setCoins, nValueIn))
	{
        return 0;
	}
	
    if (setCoins.empty())
	{
        return 0;
	}
	
    uint64_t nWeight = 0;

    LOCK2(cs_main, cs_wallet);
	
    for(pairCoin_t pcoin : setCoins)
    {
        if (pcoin.first->GetDepthInMainChain() >= nStakeMinConfirmations)
		{
            nWeight += pcoin.first->vout[pcoin.second].nValue;
		}
    }

    return nWeight;
}

bool CWallet::CreateCoinStake(const CKeyStore& keystore, unsigned int nBits, int64_t nSearchInterval, int64_t nFees,
		CTransaction& txNew, CKey& key)
{
	CBlockIndex* pindexPrev = pindexBest;
	CBigNum bnTargetPerCoinDay;
	bnTargetPerCoinDay.SetCompact(nBits);

	txNew.vin.clear();
	txNew.vout.clear();

	// OLD IMPLEMENTATION COMMNETED OUT
	//
	// Determine our payment script for devops
	// CScript devopsScript;
	// devopsScript << OP_DUP << OP_HASH160 << ParseHex(Params().DevOpsPubKey()) << OP_EQUALVERIFY << OP_CHECKSIG;

	// Mark coin stake transaction
	CScript scriptEmpty;
	scriptEmpty.clear();
	txNew.vout.push_back(CTxOut(0, scriptEmpty));

	// Choose coins to use
	int64_t nBalance = GetBalance();

	if (nBalance <= nReserveBalance)
	{
		return false;
	}

	std::vector<const CWalletTx*> vwtxPrev;

	setCoins_t setCoins;
	int64_t nValueIn = 0;

	// Select coins with suitable depth
	if (!SelectCoinsForStaking(nBalance - nReserveBalance, txNew.nTime, setCoins, nValueIn))
	{
		return false;
	}

	if (setCoins.empty())
	{
		return false;
	}

	int64_t nCredit = 0;
	CScript scriptPubKeyKernel;
	CTxDB txdb("r");

	for(pairCoin_t pcoin : setCoins)
	{
		static int nMaxStakeSearchInterval = 60;
		bool fKernelFound = false;
		
		for (unsigned int n = 0;
			n < std::min(nSearchInterval, (int64_t)nMaxStakeSearchInterval) &&
			!fKernelFound &&
			pindexPrev == pindexBest;
			n++
		)
		{
			boost::this_thread::interruption_point();
			// Search backward in time from the given txNew timestamp
			// Search nSearchInterval seconds back up to nMaxStakeSearchInterval
			COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
			int64_t nBlockTime;
			
			if (CheckKernel(pindexPrev, nBits, txNew.nTime - n, prevoutStake, &nBlockTime))
			{
				// Found a kernel
				LogPrint("coinstake", "CreateCoinStake : kernel found\n");
				
				std::vector<valtype> vSolutions;
				txnouttype whichType;
				CScript scriptPubKeyOut;
				
				scriptPubKeyKernel = pcoin.first->vout[pcoin.second].scriptPubKey;
				
				if (!Solver(scriptPubKeyKernel, whichType, vSolutions))
				{
					LogPrint("coinstake", "CreateCoinStake : failed to parse kernel\n");
					
					break;
				}
				
				LogPrint("coinstake", "CreateCoinStake : parsed kernel type=%d\n", whichType);
				
				if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH)
				{
					LogPrint("coinstake", "CreateCoinStake : no support for kernel type=%d\n", whichType);
					
					break;  // only support pay to public key and pay to address
				}
				
				if (whichType == TX_PUBKEYHASH) // pay to address type
				{
					// convert to pay to public key type
					if (!keystore.GetKey(uint160(vSolutions[0]), key))
					{
						LogPrint("coinstake", "CreateCoinStake : failed to get key for kernel type=%d\n", whichType);
						
						break;  // unable to find corresponding public key
					}
					
					scriptPubKeyOut << key.GetPubKey() << OP_CHECKSIG;
				}
				
				if (whichType == TX_PUBKEY)
				{
					valtype& vchPubKey = vSolutions[0];
					
					if (!keystore.GetKey(Hash160(vchPubKey), key))
					{
						LogPrint("coinstake", "CreateCoinStake : failed to get key for kernel type=%d\n", whichType);
						
						break;  // unable to find corresponding public key
					}

					if (key.GetPubKey() != vchPubKey)
					{
						LogPrint("coinstake", "CreateCoinStake : invalid key for kernel type=%d\n", whichType);
						
						break; // keys mismatch
					}

					scriptPubKeyOut = scriptPubKeyKernel;
				}

				txNew.nTime -= n;
				txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
				nCredit += pcoin.first->vout[pcoin.second].nValue;
				vwtxPrev.push_back(pcoin.first);
				txNew.vout.push_back(CTxOut(0, scriptPubKeyOut));

				if(nCredit > GetStakeSplitThreshold())
				{
					txNew.vout.push_back(CTxOut(0, scriptPubKeyOut)); //split stake
				}
				
				LogPrint("coinstake", "CreateCoinStake : added kernel type=%d\n", whichType);
				
				fKernelFound = true;
				
				break;
			}
		}

		if (fKernelFound)
		{
			break; // if kernel is found stop searching
		}
	}

	if (nCredit == 0 || nCredit > nBalance - nReserveBalance)
	{
		return false;
	}

	for(pairCoin_t pcoin : setCoins)
	{
		// Attempt to add more inputs
		// Only add coins of the same key/address as kernel
		if (
			txNew.vout.size() == 2 &&
			(
				pcoin.first->vout[pcoin.second].scriptPubKey == scriptPubKeyKernel ||
				pcoin.first->vout[pcoin.second].scriptPubKey == txNew.vout[1].scriptPubKey
			) &&
			pcoin.first->GetHash() != txNew.vin[0].prevout.hash
		)
		{
			int64_t nTimeWeight = GetWeight((int64_t)pcoin.first->nTime, (int64_t)txNew.nTime);

			// Stop adding more inputs if already too many inputs
			if (txNew.vin.size() >= 100)
			{
				break;
			}
			
			// Stop adding more inputs if value is already pretty significant
			if (nCredit >= GetStakeCombineThreshold())
			{
				break;
			}
			
			// Stop adding inputs if reached reserve limit
			if (nCredit + pcoin.first->vout[pcoin.second].nValue > nBalance - nReserveBalance)
			{
				break;
			}
			
			// Do not add additional significant input
			if (pcoin.first->vout[pcoin.second].nValue >= GetStakeCombineThreshold())
			{
				continue;
			}
			
			// Do not add input that is still too young
			if (nTimeWeight < nStakeMinAge)
			{
				continue;
			}
			
			txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
			nCredit += pcoin.first->vout[pcoin.second].nValue;
			vwtxPrev.push_back(pcoin.first);
		}
	}

	// Calculate coin age reward
	int64_t nReward;
	{
		uint64_t nCoinAge;
		CTxDB txdb("r");
		
		if (!txNew.GetCoinAge(txdb, pindexPrev, nCoinAge))
		{
			return error("CreateCoinStake : failed to calculate coin age");
		}
		
		nReward = GetProofOfStakeReward(pindexPrev, nCoinAge, nFees);
		
		if (nReward <= 0)
		{
			return false;
		}
		
		nCredit += nReward;
	}

	// Set TX values
	CScript payee;
	CScript devpayee;
	CTxIn vin;
	nPoSageReward = nReward;

	// define address
	CBitcoinAddress devopaddress;
	if (Params().NetworkID() == CChainParams_Network::MAIN)
	{
		devopaddress = CBitcoinAddress(getDevelopersAdress(pindexBest));
	}
	else if (Params().NetworkID() == CChainParams_Network::TESTNET)
	{
		devopaddress = CBitcoinAddress("");
	}
	else if (Params().NetworkID() == CChainParams_Network::REGTEST)
	{
		devopaddress = CBitcoinAddress("");
	}

	// Masternode Payments
	int payments = 1;
	// start masternode payments
	bool bMasterNodePayment = false;

	if ( Params().NetworkID() == CChainParams_Network::TESTNET )
	{
		if (GetTime() > START_MASTERNODE_PAYMENTS_TESTNET )
		{
			bMasterNodePayment = true;
		}
	}
	else
	{
		if (GetTime() > START_MASTERNODE_PAYMENTS)
		{
			bMasterNodePayment = true;
		}
	}

	// stop masternode payments (for testing)
	if ( Params().NetworkID() == CChainParams_Network::TESTNET )
	{
		if (GetTime() > STOP_MASTERNODE_PAYMENTS_TESTNET )
		{
			bMasterNodePayment = false;
		}
	}
	else
	{
		if (GetTime() > STOP_MASTERNODE_PAYMENTS)
		{
			bMasterNodePayment = false;
		}
	}

	bool hasPayment = true;
	if(bMasterNodePayment)
	{
		//spork
		if(!masternodePayments.GetBlockPayee(pindexPrev->nHeight+1, payee, vin))
		{
			CMasternode* winningNode = mnodeman.GetCurrentMasterNode(1);
			
			if(winningNode)
			{
				payee = GetScriptForDestination(winningNode->pubkey.GetID());
			}
			else
			{
				payee = GetScriptForDestination(devopaddress.Get());
			}
		}
	}
	else
	{
		hasPayment = false;
	}

	if(hasPayment)
	{
		payments = txNew.vout.size() + 1;
		txNew.vout.resize(payments);

		txNew.vout[payments-1].scriptPubKey = payee;
		txNew.vout[payments-1].nValue = 0;

		CTxDestination address1;
		ExtractDestination(payee, address1);
		CDigitalNoteAddress address2(address1);

		LogPrintf("Masternode payment to %s\n", address2.ToString().c_str());
	}

	// TODO: Clean this up, it's a mess (could be done much more cleanly)
	//       Not an issue otherwise, merely a pet peev. Done in a rush...
	//
	// DevOps Payments
	int devoppay = 1;
	// start devops payments
	bool bDevOpsPayment = false;

	if ( Params().NetworkID() == CChainParams_Network::TESTNET )
	{
		if (GetTime() > START_DEVOPS_PAYMENTS_TESTNET )
		{
			bDevOpsPayment = true;
		}
	}
	else
	{
		if (GetTime() > START_DEVOPS_PAYMENTS)
		{
			bDevOpsPayment = true;
		}
	}

	// stop devops payments (for testing)
	if ( Params().NetworkID() == CChainParams_Network::TESTNET )
	{
		if (GetTime() > STOP_DEVOPS_PAYMENTS_TESTNET )
		{
			bDevOpsPayment = false;
		}
	}
	else
	{
		if (GetTime() > STOP_DEVOPS_PAYMENTS)
		{
			bDevOpsPayment = false;
		}
	}

	bool hasdevopsPay = true;
	if(bDevOpsPayment)
	{
		// verify address
		if(devopaddress.IsValid())
		{
			//spork
			if(pindexBest->GetBlockTime() > 1546123500)
			{ // ON  (Saturday, December 29, 2018 10:45 PM)
					devpayee = GetScriptForDestination(devopaddress.Get());
			}
			else
			{
				hasdevopsPay = false;
			}
		}
		else
		{
			return error("CreateCoinStake: Failed to detect dev address to pay\n");
		}
	}
	else
	{
		hasdevopsPay = false;
	}

	if(hasdevopsPay)
	{
		devoppay = txNew.vout.size() + 1;
		txNew.vout.resize(devoppay);

		txNew.vout[devoppay-1].scriptPubKey = devpayee;
		txNew.vout[devoppay-1].nValue = 0;

		CTxDestination address1;
		ExtractDestination(devpayee, address1);
		CDigitalNoteAddress address2(address1);

		LogPrintf("DevOps payment to %s\n", address2.ToString().c_str());
	}

	int64_t blockValue = nCredit;
	int64_t masternodePayment = GetMasternodePayment(pindexPrev->nHeight+1, nReward);
	int64_t devopsPayment = GetDevOpsPayment(pindexPrev->nHeight+1, nReward); // TODO: Activate devops

	// Set output amount
	// Standard stake (no Masternode or DevOps payments)
	if (!hasPayment && !hasdevopsPay)
	{
		if(txNew.vout.size() == 3)
		{ // 2 stake outputs, stake was split, no masternode payment
			txNew.vout[1].nValue = (blockValue / 2 / CENT) * CENT;
			txNew.vout[2].nValue = blockValue - txNew.vout[1].nValue;
		}
		else if(txNew.vout.size() == 2)
		{ // only 1 stake output, was not split, no masternode payment
			txNew.vout[1].nValue = blockValue;
		}
	}
	else if(hasPayment && !hasdevopsPay)
	{
		if(txNew.vout.size() == 4)
		{ // 2 stake outputs, stake was split, plus a masternode payment
			txNew.vout[payments-1].nValue = masternodePayment;
			blockValue -= masternodePayment;
			txNew.vout[1].nValue = (blockValue / 2 / CENT) * CENT;
			txNew.vout[2].nValue = blockValue - txNew.vout[1].nValue;
		}
		else if(txNew.vout.size() == 3)
		{ // only 1 stake output, was not split, plus a masternode payment
			txNew.vout[payments-1].nValue = masternodePayment;
			blockValue -= masternodePayment;
			txNew.vout[1].nValue = blockValue;
		}
	}
	else if(!hasPayment && hasdevopsPay)
	{
		if(txNew.vout.size() == 4)
		{ // 2 stake outputs, stake was split, plus a devops payment
			txNew.vout[devoppay-1].nValue = devopsPayment;
			blockValue -= devopsPayment;
			txNew.vout[1].nValue = (blockValue / 2 / CENT) * CENT;
			txNew.vout[2].nValue = blockValue - txNew.vout[1].nValue;
		}
		else if(txNew.vout.size() == 3)
		{ // only 1 stake output, was not split, plus a devops payment
			txNew.vout[devoppay-1].nValue = devopsPayment;
			blockValue -= devopsPayment;
			txNew.vout[1].nValue = blockValue;
		}
	}
	else if(hasPayment && hasdevopsPay)
	{
		if(txNew.vout.size() == 5)
		{ // 2 stake outputs, stake was split, plus a devops AND masternode payment
			txNew.vout[payments-1].nValue = masternodePayment;
			blockValue -= masternodePayment;
			txNew.vout[devoppay-1].nValue = devopsPayment;
			blockValue -= devopsPayment;
			txNew.vout[1].nValue = (blockValue / 2 / CENT) * CENT;
			txNew.vout[2].nValue = blockValue - txNew.vout[1].nValue;
		}
		else if(txNew.vout.size() == 4)
		{ // only 1 stake output, was not split, plus a devops AND masternode payment
			txNew.vout[payments-1].nValue = masternodePayment;
			blockValue -= masternodePayment;
			txNew.vout[devoppay-1].nValue = devopsPayment;
			blockValue -= devopsPayment;
			txNew.vout[1].nValue = blockValue;
		}
	}

	// Sign
	int nIn = 0;
	for(const CWalletTx* pcoin : vwtxPrev)
	{
		if (!SignSignature(*this, *pcoin, txNew, nIn++))
		{
			return error("CreateCoinStake : failed to sign coinstake");
		}
	}

	// Limit size
	unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
	if (nBytes >= MAX_BLOCK_SIZE_GEN/5)
	{
		return error("CreateCoinStake : exceeded coinstake size limit");
	}

	// Successfully generated coinstake
	return true;
}

std::string CWallet::SendMoney(CScript scriptPubKey, int64_t nValue, std::string& sNarr, CWalletTx& wtxNew, bool fAskFee)
{
	CReserveKey reservekey(this);
	int64_t nFeeRequired;

	if (IsLocked())
	{
		std::string strError = ui_translate("Error: Wallet locked, unable to create transaction!");
		
		LogPrintf("SendMoney() : %s", strError);
		
		return strError;
	}

	if (fWalletUnlockStakingOnly)
	{
		std::string strError = ui_translate("Error: Wallet unlocked for staking only, unable to create transaction.");
		
		LogPrintf("SendMoney() : %s", strError);
		
		return strError;
	}

	CWalletTx wtx;
	std::vector<std::pair<CScript, int64_t> > vecSend;
	vecSend.push_back(std::make_pair(scriptPubKey, nValue));
	std::string strError = "";

	if (!CreateTransaction(scriptPubKey, nValue, sNarr, wtxNew, reservekey, nFeeRequired))
	{
		std::string strError;
		
		if (nValue + nFeeRequired > GetBalance())
		{
			strError = strprintf(
				ui_translate(
					"Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!"
				),
				FormatMoney(nFeeRequired)
			);
		}
		else
		{
			strError = "Failed to Create transaction";
		}
		
		LogPrintf("SendMoney() : %s\n", strError);
		
		return strError;
	}

	if (!CommitTransaction(wtxNew, reservekey))
	{
		return ui_translate("Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
	}

	return "";
}

std::string CWallet::SendMoneyToDestination(const CTxDestination& address, int64_t nValue, std::string& sNarr,
		CWalletTx& wtxNew, bool fAskFee)
{
	// Check amount
	if (nValue <= 0)
	{
		return ui_translate("Invalid amount");
	}

	if (nValue + nTransactionFee > GetBalance())
	{
		return ui_translate("Insufficient funds");
	}

	// Parse DigitalNote address
	CScript scriptPubKey;
	scriptPubKey.SetDestination(address);

	return SendMoney(scriptPubKey, nValue, sNarr, wtxNew, fAskFee);
}

bool CWallet::NewStealthAddress(std::string& sError, std::string& sLabel, CStealthAddress& sxAddr)
{
	ec_secret scan_secret;
	ec_secret spend_secret;

	if (GenerateRandomSecret(scan_secret) != 0
		|| GenerateRandomSecret(spend_secret) != 0)
	{
		sError = "GenerateRandomSecret failed.";
		
		printf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
		
		return false;
	}

	ec_point scan_pubkey, spend_pubkey;

	if (SecretToPublicKey(scan_secret, scan_pubkey) != 0)
	{
		sError = "Could not get scan public key.";
		
		printf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
		
		return false;
	}

	if (SecretToPublicKey(spend_secret, spend_pubkey) != 0)
	{
		sError = "Could not get spend public key.";
		
		printf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
		
		return false;
	}

	if (fDebug)
	{
		printf("getnewstealthaddress: ");
		printf("scan_pubkey ");
		
		for (uint32_t i = 0; i < scan_pubkey.size(); ++i)
		{
			printf("%02x", scan_pubkey[i]);
		}
		
		printf("\n");
		printf("spend_pubkey ");
		
		for (uint32_t i = 0; i < spend_pubkey.size(); ++i)
		{
			printf("%02x", spend_pubkey[i]);
		}
		
		printf("\n");
	}

	sxAddr.label = sLabel;
	sxAddr.scan_pubkey = scan_pubkey;
	sxAddr.spend_pubkey = spend_pubkey;

	sxAddr.scan_secret.resize(32);
	memcpy(&sxAddr.scan_secret[0], &scan_secret.e[0], 32);
	sxAddr.spend_secret.resize(32);
	memcpy(&sxAddr.spend_secret[0], &spend_secret.e[0], 32);

	return true;
}

bool CWallet::AddStealthAddress(CStealthAddress& sxAddr)
{
	LOCK(cs_wallet);

	// must add before changing spend_secret
	stealthAddresses.insert(sxAddr);

	bool fOwned = sxAddr.scan_secret.size() == ec_secret_size;
	
	if (fOwned)
	{
		// -- owned addresses can only be added when wallet is unlocked
		if (IsLocked())
		{
			printf("Error: CWallet::AddStealthAddress wallet must be unlocked.\n");
			
			stealthAddresses.erase(sxAddr);
			
			return false;
		}

		if (IsCrypted())
		{
			std::vector<unsigned char> vchCryptedSecret;
			CSecret vchSecret;
			
			vchSecret.resize(32);
			memcpy(&vchSecret[0], &sxAddr.spend_secret[0], 32);

			uint256 iv = Hash(sxAddr.spend_pubkey.begin(), sxAddr.spend_pubkey.end());
			
			if (!EncryptSecret(vMasterKey, vchSecret, iv, vchCryptedSecret))
			{
				printf("Error: Failed encrypting stealth key %s\n", sxAddr.Encoded().c_str());
				
				stealthAddresses.erase(sxAddr);
				
				return false;
			}
			
			sxAddr.spend_secret = vchCryptedSecret;
		}
	}
	
	bool rv = CWalletDB(strWalletFile).WriteStealthAddress(sxAddr);

	if (rv)
	{
		NotifyAddressBookChanged(this, sxAddr, sxAddr.label, fOwned, CT_NEW);
	}
	
	return rv;
}

bool CWallet::UnlockStealthAddresses(const CKeyingMaterial& vMasterKeyIn)
{	
	for (setStealthAddresses_t::iterator it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
	{
		if (it->scan_secret.size() < 32)
		{
			continue; // stealth address is not owned
		}
		
		// -- CStealthAddress are only sorted on spend_pubkey
		CStealthAddress &sxAddr = const_cast<CStealthAddress&>(*it);

		if (fDebug)
		{
			printf("Decrypting stealth key %s\n", sxAddr.Encoded().c_str());
		}
		
		CSecret vchSecret;
		uint256 iv = Hash(sxAddr.spend_pubkey.begin(), sxAddr.spend_pubkey.end());
		
		if(!DecryptSecret(vMasterKeyIn, sxAddr.spend_secret, iv, vchSecret)
			|| vchSecret.size() != 32)
		{
			printf("Error: Failed decrypting stealth key %s\n", sxAddr.Encoded().c_str());
			
			continue;
		}

		ec_secret testSecret;
		memcpy(&testSecret.e[0], &vchSecret[0], 32);
		ec_point pkSpendTest;

		if (SecretToPublicKey(testSecret, pkSpendTest) != 0
			|| pkSpendTest != sxAddr.spend_pubkey)
		{
			printf("Error: Failed decrypting stealth key, public key mismatch %s\n", sxAddr.Encoded().c_str());
			
			continue;
		}

		sxAddr.spend_secret.resize(32);
		memcpy(&sxAddr.spend_secret[0], &vchSecret[0], 32);
	}

	CryptedKeyMap::iterator mi = mapCryptedKeys.begin();
	
	for (; mi != mapCryptedKeys.end(); ++mi)
	{
		CPubKey &pubKey = (*mi).second.first;
		std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
		
		if (vchCryptedSecret.size() != 0)
		{
			continue;
		}
		
		CKeyID ckid = pubKey.GetID();
		CDigitalNoteAddress addr(ckid);

		StealthKeyMetaMap::iterator mi = mapStealthKeyMeta.find(ckid);
		
		if (mi == mapStealthKeyMeta.end())
		{
			printf("Error: No metadata found to add secret for %s\n", addr.ToString().c_str());
			
			continue;
		}

		CStealthKeyMetadata& sxKeyMeta = mi->second;

		CStealthAddress sxFind;
		sxFind.scan_pubkey = sxKeyMeta.pkScan.Raw();

		setStealthAddresses_t::iterator si = stealthAddresses.find(sxFind);
		
		if (si == stealthAddresses.end())
		{
			printf("No stealth key found to add secret for %s\n", addr.ToString().c_str());
			
			continue;
		}

		if (fDebug)
		{
			printf("Expanding secret for %s\n", addr.ToString().c_str());
		}
		
		ec_secret sSpendR;
		ec_secret sSpend;
		ec_secret sScan;

		if (si->spend_secret.size() != ec_secret_size
			|| si->scan_secret.size() != ec_secret_size)
		{
			printf("Stealth address has no secret key for %s\n", addr.ToString().c_str());
			
			continue;
		}
		
		memcpy(&sScan.e[0], &si->scan_secret[0], ec_secret_size);
		memcpy(&sSpend.e[0], &si->spend_secret[0], ec_secret_size);

		ec_point pkEphem = sxKeyMeta.pkEphem.Raw();
		
		if (StealthSecretSpend(sScan, pkEphem, sSpend, sSpendR) != 0)
		{
			printf("StealthSecretSpend() failed.\n");
			
			continue;
		}

		ec_point pkTestSpendR;
		
		if (SecretToPublicKey(sSpendR, pkTestSpendR) != 0)
		{
			printf("SecretToPublicKey() failed.\n");
			
			continue;
		}

		CSecret vchSecret;
		vchSecret.resize(ec_secret_size);

		memcpy(&vchSecret[0], &sSpendR.e[0], ec_secret_size);
		CKey ckey;

		try
		{
			ckey.Set(vchSecret.begin(), vchSecret.end(), true);
			//ckey.SetSecret(vchSecret, true);
		}
		catch (std::exception& e)
		{
			printf("ckey.SetSecret() threw: %s.\n", e.what());
			
			continue;
		}

		CPubKey cpkT = ckey.GetPubKey();

		if (!cpkT.IsValid())
		{
			printf("cpkT is invalid.\n");
			
			continue;
		}

		if (cpkT != pubKey)
		{
			printf("Error: Generated secret does not match.\n");
			
			continue;
		}

		if (!ckey.IsValid())
		{
			printf("Reconstructed key is invalid.\n");
			
			continue;
		}

		if (fDebug)
		{
			CKeyID keyID = cpkT.GetID();
			CDigitalNoteAddress coinAddress(keyID);
			
			printf("Adding secret to key %s.\n", coinAddress.ToString().c_str());
		}

		if (!AddKey(ckey))
		{
			printf("AddKey failed.\n");
			
			continue;
		}

		if (!CWalletDB(strWalletFile).EraseStealthKeyMeta(ckid))
		{
			printf("EraseStealthKeyMeta failed for %s\n", addr.ToString().c_str());
		}
	}

	return true;
}

bool CWallet::UpdateStealthAddress(std::string &addr, std::string &label, bool addIfNotExist)
{
	if (fDebug)
	{
		printf("UpdateStealthAddress %s\n", addr.c_str());
	}

	CStealthAddress sxAddr;

	if (!sxAddr.SetEncoded(addr))
	{
		return false;
	}

	setStealthAddresses_t::iterator it = stealthAddresses.find(sxAddr);

	ChangeType nMode = CT_UPDATED;
	CStealthAddress sxFound;

	if (it == stealthAddresses.end())
	{
		if (addIfNotExist)
		{
			sxFound = sxAddr;
			sxFound.label = label;
			stealthAddresses.insert(sxFound);
			nMode = CT_NEW;
		}
		else
		{
			printf("UpdateStealthAddress %s, not in set\n", addr.c_str());
			
			return false;
		}
	}
	else
	{
		sxFound = const_cast<CStealthAddress&>(*it);

		if (sxFound.label == label)
		{
			// no change
			return true;
		}

		it->label = label; // update in .stealthAddresses

		if (sxFound.scan_secret.size() == ec_secret_size)
		{
			printf("UpdateStealthAddress: todo - update owned stealth address.\n");
			
			return false;
		}
	}

	sxFound.label = label;

	if (!CWalletDB(strWalletFile).WriteStealthAddress(sxFound))
	{
		printf("UpdateStealthAddress(%s) Write to db failed.\n", addr.c_str());
		
		return false;
	}

	bool fOwned = sxFound.scan_secret.size() == ec_secret_size;
	NotifyAddressBookChanged(this, sxFound, sxFound.label, fOwned, nMode);

	return true;
}

bool CWallet::CreateStealthTransaction(CScript scriptPubKey, int64_t nValue, std::vector<uint8_t>& P,
		std::vector<uint8_t>& narr, std::string& sNarr, CWalletTx& wtxNew, CReserveKey& reservekey, int64_t& nFeeRet,
		const CCoinControl* coinControl)
{
	std::vector<std::pair<CScript, int64_t> > vecSend;
	vecSend.push_back(std::make_pair(scriptPubKey, nValue));

	CScript scriptP = CScript() << OP_RETURN << P;

	if (narr.size() > 0)
	{
		scriptP = scriptP << OP_RETURN << narr;
	}

	vecSend.push_back(std::make_pair(scriptP, 0));

	// -- shuffle inputs, change output won't mix enough as it must be not fully random for plantext narrations
	std::shuffle(vecSend.begin(), vecSend.end(), std::mt19937(std::random_device()()));

	int nChangePos;
	std::string strFailReason;
	bool rv = CreateTransaction(vecSend, wtxNew, reservekey, nFeeRet, nChangePos, strFailReason, coinControl);

	if(!strFailReason.empty())
	{
		LogPrintf("CreateStealthTransaction(): %s\n", strFailReason);
	}

	// -- the change txn is inserted in a random pos, check here to match narr to output
	if (rv && narr.size() > 0)
	{
		for (unsigned int k = 0; k < wtxNew.vout.size(); ++k)
		{
			if (wtxNew.vout[k].scriptPubKey != scriptPubKey
				|| wtxNew.vout[k].nValue != nValue)
			{
				continue;
			}
			
			char key[64];
			
			if (snprintf(key, sizeof(key), "n_%u", k) < 1)
			{
				printf("CreateStealthTransaction(): Error creating narration key.");
				
				break;
			}
			
			wtxNew.mapValue[key] = sNarr;
			
			break;
		}
	}

	return rv;
}

std::string CWallet::SendStealthMoney(CScript scriptPubKey, int64_t nValue, std::vector<uint8_t>& P, std::vector<uint8_t>& narr, std::string& sNarr, CWalletTx& wtxNew, bool fAskFee)
{
	CReserveKey reservekey(this);
	int64_t nFeeRequired;

	if (IsLocked())
	{
		std::string strError = ui_translate("Error: Wallet locked, unable to create transaction  ");
		
		LogPrintf("SendStealthMoney() : %s\n", strError.c_str());
		
		return strError;
	}

	if (fWalletUnlockStakingOnly)
	{
		std::string strError = ui_translate("Error: Wallet unlocked for staking only, unable to create transaction.");
		LogPrintf("SendStealthMoney() : %s\n", strError.c_str());
		return strError;
	}

	if (!CreateStealthTransaction(scriptPubKey, nValue, P, narr, sNarr, wtxNew, reservekey, nFeeRequired))
	{
		std::string strError;
		
		if (nValue + nFeeRequired > GetBalance())
		{
			strError = strprintf(ui_translate("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds  "), FormatMoney(nFeeRequired).c_str());
		}
		else
		{
			strError = "Failed to Create transaction";
		}
		
		LogPrintf("SendStealthMoney() : %s\n", strError.c_str());
		
		return strError;
	}

	if (fAskFee && !uiInterface.ThreadSafeAskFee(nFeeRequired, ui_translate("Sending...")))
	{
		return "ABORTED";
	}

	if (!CommitTransaction(wtxNew, reservekey))
	{
		return ui_translate("Error: The transaction was rejected.  This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
	}

	return "";
}

bool CWallet::SendStealthMoneyToDestination(CStealthAddress& sxAddress, int64_t nValue, std::string& sNarr, CWalletTx& wtxNew, std::string& sError, bool fAskFee)
{
	// -- Check amount
	if (nValue <= 0)
	{
		sError = "Invalid amount";
		
		return false;
	}

	if (nValue + nTransactionFee + (1) > GetBalance())
	{
		sError = "Insufficient funds";
		
		return false;
	}

	ec_secret ephem_secret;
	ec_secret secretShared;
	ec_point pkSendTo;
	ec_point ephem_pubkey;

	if (GenerateRandomSecret(ephem_secret) != 0)
	{
		sError = "GenerateRandomSecret failed.";
		
		return false;
	}

	if (StealthSecret(ephem_secret, sxAddress.scan_pubkey, sxAddress.spend_pubkey, secretShared, pkSendTo) != 0)
	{
		sError = "Could not generate receiving public key.";
		
		return false;
	}

	CPubKey cpkTo(pkSendTo);

	if (!cpkTo.IsValid())
	{
		sError = "Invalid public key generated.";
		
		return false;
	}

	CKeyID ckidTo = cpkTo.GetID();

	CDigitalNoteAddress addrTo(ckidTo);

	if (SecretToPublicKey(ephem_secret, ephem_pubkey) != 0)
	{
		sError = "Could not generate ephem public key.";
		
		return false;
	}

	if (fDebug)
	{
		LogPrintf("Stealth send to generated pubkey %" PRIszu": %s\n", pkSendTo.size(), HexStr(pkSendTo).c_str());
		LogPrintf("hash %s\n", addrTo.ToString().c_str());
		LogPrintf("ephem_pubkey %" PRIszu": %s\n", ephem_pubkey.size(), HexStr(ephem_pubkey).c_str());
	}

	std::vector<unsigned char> vchNarr;

	// -- Parse DigitalNote address
	CScript scriptPubKey;
	scriptPubKey.SetDestination(addrTo.Get());

	if ((sError = SendStealthMoney(scriptPubKey, nValue, ephem_pubkey, vchNarr, sNarr, wtxNew, fAskFee)) != "")
	{
		return false;
	}

	return true;
}

bool CWallet::FindStealthTransactions(const CTransaction& tx, mapValue_t& mapNarr)
{
	if (fDebug)
	{
		LogPrintf("FindStealthTransactions() tx: %s\n", tx.GetHash().GetHex().c_str());
	}

	mapNarr.clear();

	LOCK(cs_wallet);

	ec_secret sSpendR;
	ec_secret sSpend;
	ec_secret sScan;
	ec_secret sShared;

	ec_point pkExtracted;

	std::vector<uint8_t> vchEphemPK;
	std::vector<uint8_t> vchDataB;
	std::vector<uint8_t> vchENarr;
	opcodetype opCode;
	char cbuf[256];

	int32_t nOutputIdOuter = -1;

	for(const CTxOut& txout : tx.vout)
	{
		nOutputIdOuter++;
		// -- for each OP_RETURN need to check all other valid outputs

		//printf("txout scriptPubKey %s\n",  txout.scriptPubKey.ToString().c_str());
		CScript::const_iterator itTxA = txout.scriptPubKey.begin();

		if (!txout.scriptPubKey.GetOp(itTxA, opCode, vchEphemPK) || opCode != OP_RETURN)
		{
			continue;
		}
		else if (!txout.scriptPubKey.GetOp(itTxA, opCode, vchEphemPK) || vchEphemPK.size() != 33)
		{
			// -- look for plaintext narrations
			if (vchEphemPK.size() > 1
				&& vchEphemPK[0] == 'n'
				&& vchEphemPK[1] == 'p')
			{
				if (txout.scriptPubKey.GetOp(itTxA, opCode, vchENarr)
					&& opCode == OP_RETURN
					&& txout.scriptPubKey.GetOp(itTxA, opCode, vchENarr)
					&& vchENarr.size() > 0)
				{
					std::string sNarr = std::string(vchENarr.begin(), vchENarr.end());

					snprintf(cbuf, sizeof(cbuf), "n_%d", nOutputIdOuter-1); // plaintext narration always matches preceding value output
					mapNarr[cbuf] = sNarr;
				}
				else
				{
					printf("Warning: FindStealthTransactions() tx: %s, Could not extract plaintext narration.\n", tx.GetHash().GetHex().c_str());
				}
			}

			continue;
		}

		int32_t nOutputId = -1;
		nStealth++;
		
		for(const CTxOut& txoutB : tx.vout)
		{
			nOutputId++;

			if (&txoutB == &txout)
			{
				continue;
			}
			
			bool txnMatch = false; // only 1 txn will match an ephem pk
			//printf("txoutB scriptPubKey %s\n",  txoutB.scriptPubKey.ToString().c_str());

			CTxDestination address;
			if (!ExtractDestination(txoutB.scriptPubKey, address))
			{
				continue;
			}
			
			if (address.type() != typeid(CKeyID))
			{
				continue;
			}
			
			CKeyID ckidMatch = boost::get<CKeyID>(address);

			if (HaveKey(ckidMatch)) // no point checking if already have key
			{
				continue;
			}
			
			for (setStealthAddresses_t::iterator it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
			{
				if (it->scan_secret.size() != ec_secret_size)
				{
					continue; // stealth address is not owned
				}
				
				//printf("it->Encodeded() %s\n",  it->Encoded().c_str());
				memcpy(&sScan.e[0], &it->scan_secret[0], ec_secret_size);

				if (StealthSecret(sScan, vchEphemPK, it->spend_pubkey, sShared, pkExtracted) != 0)
				{
					printf("StealthSecret failed.\n");
					
					continue;
				}
				
				//printf("pkExtracted %"PRIszu": %s\n", pkExtracted.size(), HexStr(pkExtracted).c_str());

				CPubKey cpkE(pkExtracted);

				if (!cpkE.IsValid())
				{
					continue;
				}
				
				CKeyID ckidE = cpkE.GetID();

				if (ckidMatch != ckidE)
				{
					continue;
				}
				
				if (fDebug)
				{
					printf("Found stealth txn to address %s\n", it->Encoded().c_str());
				}
				
				if (IsLocked())
				{
					if (fDebug)
					{
						printf("Wallet is locked, adding key without secret.\n");
					}
					
					// -- add key without secret
					std::vector<uint8_t> vchEmpty;
					AddCryptedKey(cpkE, vchEmpty);
					CKeyID keyId = cpkE.GetID();
					CDigitalNoteAddress coinAddress(keyId);
					std::string sLabel = it->Encoded();
					SetAddressBookName(keyId, sLabel);

					CPubKey cpkEphem(vchEphemPK);
					CPubKey cpkScan(it->scan_pubkey);
					CStealthKeyMetadata lockedSkMeta(cpkEphem, cpkScan);

					if (!CWalletDB(strWalletFile).WriteStealthKeyMeta(keyId, lockedSkMeta))
					{
						printf("WriteStealthKeyMeta failed for %s\n", coinAddress.ToString().c_str());
					}
					
					mapStealthKeyMeta[keyId] = lockedSkMeta;
					nFoundStealth++;
				}
				else
				{
					if (it->spend_secret.size() != ec_secret_size)
					{
						continue;
					}
					
					memcpy(&sSpend.e[0], &it->spend_secret[0], ec_secret_size);


					if (StealthSharedToSecretSpend(sShared, sSpend, sSpendR) != 0)
					{
						printf("StealthSharedToSecretSpend() failed.\n");
						
						continue;
					}

					ec_point pkTestSpendR;
					if (SecretToPublicKey(sSpendR, pkTestSpendR) != 0)
					{
						printf("SecretToPublicKey() failed.\n");
						
						continue;
					}

					CSecret vchSecret;
					vchSecret.resize(ec_secret_size);

					memcpy(&vchSecret[0], &sSpendR.e[0], ec_secret_size);
					CKey ckey;

					try
					{
						ckey.Set(vchSecret.begin(), vchSecret.end(), true);
						//ckey.SetSecret(vchSecret, true);
					}
					catch (std::exception& e)
					{
						printf("ckey.SetSecret() threw: %s.\n", e.what());
						
						continue;
					}

					CPubKey cpkT = ckey.GetPubKey();
					
					if (!cpkT.IsValid())
					{
						printf("cpkT is invalid.\n");
						
						continue;
					}

					if (!ckey.IsValid())
					{
						printf("Reconstructed key is invalid.\n");
						
						continue;
					}

					CKeyID keyID = cpkT.GetID();
					if (fDebug)
					{
						CDigitalNoteAddress coinAddress(keyID);
						
						printf("Adding key %s.\n", coinAddress.ToString().c_str());
					}

					if (!AddKey(ckey))
					{
						printf("AddKey failed.\n");
						
						continue;
					}

					std::string sLabel = it->Encoded();
					SetAddressBookName(keyID, sLabel);
					nFoundStealth++;
				}

				txnMatch = true;
				
				break;
			}
			
			if (txnMatch)
			{
				break;
			}
		}
	}

	return true;
}

bool CWallet::CreateCollateralTransaction(CTransaction& txCollateral, std::string& strReason)
{
	/*
		To doublespend a collateral transaction, it will require a fee higher than this. So there's
		still a significant cost.
	*/
	CAmount nFeeRet = 0.001*COIN;

	txCollateral.vin.clear();
	txCollateral.vout.clear();
	txCollateral.nTime = GetAdjustedTime();

	CReserveKey reservekey(this);
	CAmount nValueIn2 = 0;
	std::vector<CTxIn> vCoinsCollateral;

	if (!SelectCoinsCollateral(vCoinsCollateral, nValueIn2))
	{
		strReason = "Error: MNengine requires a collateral transaction and could not locate an acceptable input!";
		
		return false;
	}

	// make our change address
	CScript scriptChange;
	CPubKey vchPubKey;

	assert(reservekey.GetReservedKey(vchPubKey)); // should never fail, as we just unlocked

	scriptChange = GetScriptForDestination(vchPubKey.GetID());
	reservekey.KeepKey();

	for(CTxIn v : vCoinsCollateral)
	{
		txCollateral.vin.push_back(v);
	}

	if(nValueIn2 - MNengine_COLLATERAL - nFeeRet > 0)
	{
		//pay collateral charge in fees
		CTxOut vout3 = CTxOut(nValueIn2 - MNengine_COLLATERAL, scriptChange);
		txCollateral.vout.push_back(vout3);
	}

	int vinNumber = 0;
	for(CTxIn v : txCollateral.vin)
	{
		if(!SignSignature(*this, v.prevPubKey, txCollateral, vinNumber, int(SIGHASH_ALL|SIGHASH_ANYONECANPAY)))
		{
			for(CTxIn v : vCoinsCollateral)
			{
				UnlockCoin(v.prevout);
			}
			
			strReason = "CMNenginePool::Sign - Unable to sign collateral transaction! \n";
			
			return false;
		}
		
		vinNumber++;
	}

	return true;
}

bool CWallet::ConvertList(std::vector<CTxIn> vCoins, std::vector<int64_t>& vecAmounts)
{
	for(CTxIn i : vCoins)
	{
		if (mapWallet.count(i.prevout.hash))
		{
			CWalletTx& wtx = mapWallet[i.prevout.hash];
			
			if(i.prevout.n < wtx.vout.size())
			{
				vecAmounts.push_back(wtx.vout[i.prevout.n].nValue);
			}
		}
		else
		{
			LogPrintf("ConvertList -- Couldn't find transaction\n");
		}
	}

	return true;
}

//
// Mark old keypool keys as used,
// and generate all new keys
//
bool CWallet::NewKeyPool()
{
	{
		LOCK(cs_wallet);
		
		CWalletDB walletdb(strWalletFile);
		
		for(int64_t nIndex : setKeyPool)
		{
			walletdb.ErasePool(nIndex);
		}
		
		setKeyPool.clear();

		if (IsLocked())
		{
			return false;
		}
		
		fLiteMode = GetBoolArg("-litemode", false);
		int64_t nKeys;

		if (fLiteMode)
		{
			nKeys = std::max(GetArg("-keypool", 100), (int64_t)0);
		}
		else
		{
			nKeys = std::max(GetArg("-keypool", 1000), (int64_t)0);
		}
		
		for (int i = 0; i < nKeys; i++)
		{
			int64_t nIndex = i+1;
			walletdb.WritePool(nIndex, CKeyPool(GenerateNewKey()));
			setKeyPool.insert(nIndex);
		}
		
		LogPrintf("CWallet::NewKeyPool wrote %d new keys\n", nKeys);
	}

	return true;
}

bool CWallet::TopUpKeyPool(unsigned int nSize)
{
	{
		LOCK(cs_wallet);

		if (IsLocked())
		{
			return false;
		}
		
		CWalletDB walletdb(strWalletFile);

		// Top up key pool
		unsigned int nTargetSize;
		fLiteMode = GetBoolArg("-litemode", false);

		if (nSize > 0)
		{
			nTargetSize = nSize;
		}
		else if (fLiteMode)
		{
			nTargetSize = std::max(GetArg("-keypool", 100), (int64_t)0);
		}
		else
		{
			nTargetSize = std::max(GetArg("-keypool", 1000), (int64_t)0);
		}
		
		while (setKeyPool.size() < (nTargetSize + 1))
		{
			int64_t nEnd = 1;
			if (!setKeyPool.empty())
			{
				nEnd = *(--setKeyPool.end()) + 1;
			}
			
			if (!walletdb.WritePool(nEnd, CKeyPool(GenerateNewKey())))
			{
				throw std::runtime_error("TopUpKeyPool() : writing generated key failed");
			}
			
			setKeyPool.insert(nEnd);
			
			LogPrintf("keypool added key %d, size=%u\n", nEnd, setKeyPool.size());
			
			double dProgress = 100.f * nEnd / (nTargetSize + 1);
			std::string strMsg = strprintf(ui_translate("Loading wallet... (%3.2f %%)"), dProgress);
			uiInterface.InitMessage(strMsg);
		}
	}

	return true;
}

int64_t CWallet::AddReserveKey(const CKeyPool& keypool)
{
	{
		LOCK2(cs_main, cs_wallet);
		
		CWalletDB walletdb(strWalletFile);
		int64_t nIndex = 1 + *(--setKeyPool.end());
		
		if (!walletdb.WritePool(nIndex, keypool))
		{
			throw std::runtime_error("AddReserveKey() : writing added key failed");
		}
		
		setKeyPool.insert(nIndex);
		
		return nIndex;
	}
	
	return -1;
}

void CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool)
{
	nIndex = -1;
	keypool.vchPubKey = CPubKey();
	
	{
		LOCK(cs_wallet);

		if (!IsLocked())
		{
			TopUpKeyPool();
		}
		
		// Get the oldest key
		if(setKeyPool.empty())
		{
			return;
		}
		
		CWalletDB walletdb(strWalletFile);

		nIndex = *(setKeyPool.begin());
		setKeyPool.erase(setKeyPool.begin());
		
		if (!walletdb.ReadPool(nIndex, keypool))
		{
			throw std::runtime_error("ReserveKeyFromKeyPool() : read failed");
		}
		
		if (!HaveKey(keypool.vchPubKey.GetID()))
		{
			throw std::runtime_error("ReserveKeyFromKeyPool() : unknown key in key pool");
		}
		
		assert(keypool.vchPubKey.IsValid());
		
		LogPrintf("keypool reserve %d\n", nIndex);
	}
}

void CWallet::KeepKey(int64_t nIndex)
{
	// Remove from key pool
	if (fFileBacked)
	{
		CWalletDB walletdb(strWalletFile);
		walletdb.ErasePool(nIndex);
	}

	LogPrintf("keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex)
{
	// Return to key pool
	{
		LOCK(cs_wallet);
		
		setKeyPool.insert(nIndex);
	}

	LogPrintf("keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result)
{
	int64_t nIndex = 0;
	CKeyPool keypool;

	{
		LOCK(cs_wallet);
		
		ReserveKeyFromKeyPool(nIndex, keypool);
		
		if (nIndex == -1)
		{
			if (IsLocked())
			{
				return false;
			}
			
			result = GenerateNewKey();
			
			return true;
		}
		
		KeepKey(nIndex);
		result = keypool.vchPubKey;
	}

	return true;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
	int64_t nIndex = 0;
	CKeyPool keypool;
	ReserveKeyFromKeyPool(nIndex, keypool);

	if (nIndex == -1)
	{
		return GetTime();
	}

	ReturnKey(nIndex);

	return keypool.nTime;
}

void CWallet::GetAllReserveKeys(std::set<CKeyID>& setAddress) const
{
	setAddress.clear();

	CWalletDB walletdb(strWalletFile);

	LOCK2(cs_main, cs_wallet);

	for(const int64_t& id : setKeyPool)
	{
		CKeyPool keypool;
		if (!walletdb.ReadPool(id, keypool))
		{
			throw std::runtime_error("GetAllReserveKeyHashes() : read failed");
		}
		
		assert(keypool.vchPubKey.IsValid());
		
		CKeyID keyID = keypool.vchPubKey.GetID();
		
		if (!HaveKey(keyID))
		{
			throw std::runtime_error("GetAllReserveKeyHashes() : unknown key in key pool");
		}
		
		setAddress.insert(keyID);
	}
}

std::set<std::set<CTxDestination> > CWallet::GetAddressGroupings()
{
	AssertLockHeld(cs_wallet); // mapWallet
	
	std::set<std::set<CTxDestination> > groupings;
	std::set<CTxDestination> grouping;

	for(std::pair<uint256, CWalletTx> walletEntry : mapWallet)
	{
		CWalletTx *pcoin = &walletEntry.second;

		if (pcoin->vin.size() > 0 && IsMine(pcoin->vin[0]))
		{
			bool any_mine = false;
			
			// group all input addresses with each other
			for(CTxIn txin : pcoin->vin)
			{
				CTxDestination address;
				
				if(!IsMine(txin)) /* If this input isn't mine, ignore it */
				{
					continue;
				}
			
				if(!ExtractDestination(mapWallet[txin.prevout.hash].vout[txin.prevout.n].scriptPubKey, address))
				{
					continue;
				}
				
				grouping.insert(address);
				any_mine = true;
			}

			// group change with input addresses
			if (any_mine)
			{
				for(CTxOut txout : pcoin->vout)
				{
					if (IsChange(txout))
					{
						CWalletTx tx = mapWallet[pcoin->vin[0].prevout.hash];
						CTxDestination txoutAddr;
						
						if(!ExtractDestination(txout.scriptPubKey, txoutAddr))
						{
							continue;
						}
					
						grouping.insert(txoutAddr);
					}
				}
			}
			
			if (grouping.size() > 0)
			{
				groupings.insert(grouping);
				grouping.clear();
			}
		}

		// group lone addrs by themselves
		for (unsigned int i = 0; i < pcoin->vout.size(); i++)
		{
			if (IsMine(pcoin->vout[i]))
			{
				CTxDestination address;
				
				if(!ExtractDestination(pcoin->vout[i].scriptPubKey, address))
				{
					continue;
				}
				
				grouping.insert(address);
				groupings.insert(grouping);
				grouping.clear();
			}
		}
	}

	std::set<std::set<CTxDestination>* > uniqueGroupings; // a set of pointers to groups of addresses
	std::map<CTxDestination, std::set<CTxDestination>* > setmap;  // map addresses to the unique group containing it

	for(std::set<CTxDestination> grouping : groupings)
	{
		// make a set of all the groups hit by this new group
		std::set<std::set<CTxDestination>* > hits;
		std::map<CTxDestination, std::set<CTxDestination>* >::iterator it;
		
		for(CTxDestination address : grouping)
		{
			if ((it = setmap.find(address)) != setmap.end())
			{
				hits.insert((*it).second);
			}
		}
		
		// merge all hit groups into a new single group and delete old groups
		std::set<CTxDestination>* merged = new std::set<CTxDestination>(grouping);
		
		for(std::set<CTxDestination>* hit : hits)
		{
			merged->insert(hit->begin(), hit->end());
			
			uniqueGroupings.erase(hit);
			
			delete hit;
		}
		
		uniqueGroupings.insert(merged);

		// update setmap
		for(CTxDestination element : *merged)
		{
			setmap[element] = merged;
		}
	}

	std::set<std::set<CTxDestination> > ret;
	
	for(std::set<CTxDestination>* uniqueGrouping : uniqueGroupings)
	{
		ret.insert(*uniqueGrouping);
		
		delete uniqueGrouping;
	}

	return ret;
}

std::map<CTxDestination, int64_t> CWallet::GetAddressBalances()
{
	std::map<CTxDestination, int64_t> balances;

	{
		LOCK(cs_wallet);
		
		for(std::pair<uint256, CWalletTx> walletEntry : mapWallet)
		{
			CWalletTx *pcoin = &walletEntry.second;

			if (!IsFinalTx(*pcoin) || !pcoin->IsTrusted())
			{
				continue;
			}
			
			if ((pcoin->IsCoinBase() || pcoin->IsCoinStake()) && pcoin->GetBlocksToMaturity() > 0)
			{
				continue;
			}
			
			int nDepth = pcoin->GetDepthInMainChain();
			if (nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? 0 : 1))
			{
				continue;
			}
			
			for (unsigned int i = 0; i < pcoin->vout.size(); i++)
			{
				CTxDestination addr;
				if (!IsMine(pcoin->vout[i]))
				{
					continue;
				}
				
				if(!ExtractDestination(pcoin->vout[i].scriptPubKey, addr))
				{
					continue;
				}
				
				int64_t n = pcoin->IsSpent(i) ? 0 : pcoin->vout[i].nValue;

				if (!balances.count(addr))
				{
					balances[addr] = 0;
				}
				
				balances[addr] += n;
			}
		}
	}

	return balances;
}

isminetype CWallet::IsMine(const CTxIn &txin) const
{
	{
		LOCK(cs_wallet);
		
		mapWallet_t::const_iterator mi = mapWallet.find(txin.prevout.hash);
		
		if (mi != mapWallet.end())
		{
			const CWalletTx& prev = (*mi).second;
			
			if (txin.prevout.n < prev.vout.size())
			{
				return IsMine(prev.vout[txin.prevout.n]);
			}
		}
	}

	return ISMINE_NO;
}

CAmount CWallet::GetDebit(const CTxIn &txin, const isminefilter& filter) const
{
	{
		LOCK(cs_wallet);
		
		mapWallet_t::const_iterator mi = mapWallet.find(txin.prevout.hash);
		
		if (mi != mapWallet.end())
		{
			const CWalletTx& prev = (*mi).second;
			
			if (txin.prevout.n < prev.vout.size())
			{
				if (IsMine(prev.vout[txin.prevout.n]) & filter)
				{
					return prev.vout[txin.prevout.n].nValue;
				}
			}
		}
	}

	return 0;
}

isminetype CWallet::IsMine(const CTxOut& txout) const
{
	return ::IsMine(*this, txout.scriptPubKey);
}

CAmount CWallet::GetCredit(const CTxOut& txout, const isminefilter& filter) const
{
	if (!MoneyRange(txout.nValue))
	{
		throw std::runtime_error("CWallet::GetCredit() : value out of range");
	}

	return ((IsMine(txout) & filter) ? txout.nValue : 0);
}

bool CWallet::IsChange(const CTxOut& txout) const
{
	// TODO: fix handling of 'change' outputs. The assumption is that any
	// payment to a script that is ours, but is not in the address book
	// is change. That assumption is likely to break when we implement multisignature
	// wallets that return change back into a multi-signature-protected address;
	// a better way of identifying which outputs are 'the send' and which are
	// 'the change' will need to be implemented (maybe extend CWalletTx to remember
	// which output, if any, was change).
	if (::IsMine(*this, txout.scriptPubKey))
	{
		CTxDestination address;
		
		if (!ExtractDestination(txout.scriptPubKey, address))
		{
			return true;
		}
		
		LOCK(cs_wallet);
		
		if (!mapAddressBook.count(address))
		{
			return true;
		}
	}

	return false;
}

CAmount CWallet::GetChange(const CTxOut& txout) const
{
	if (!MoneyRange(txout.nValue))
	{
		throw std::runtime_error("CWallet::GetChange() : value out of range");
	}

	return (IsChange(txout) ? txout.nValue : 0);
}

bool CWallet::IsMine(const CTransaction& tx) const
{
	for(const CTxOut& txout : tx.vout)
	{
		if (IsMine(txout) && txout.nValue >= nMinimumInputValue)
		{
			return true;
		}
	}

	return false;
}

/** should probably be renamed to IsRelevantToMe */
bool CWallet::IsFromMe(const CTransaction& tx) const
{
	return (GetDebit(tx, ISMINE_ALL) > 0);
}

CAmount CWallet::GetDebit(const CTransaction& tx, const isminefilter& filter) const
{
	CAmount nDebit = 0;

	for(const CTxIn& txin : tx.vin)
	{
		nDebit += GetDebit(txin, filter);
		
		if (!MoneyRange(nDebit))
		{
			throw std::runtime_error("CWallet::GetDebit() : value out of range");
		}
	}

	return nDebit;
}

CAmount CWallet::GetCredit(const CTransaction& tx, const isminefilter& filter) const
{
	CAmount nCredit = 0;

	for(const CTxOut& txout : tx.vout)
	{
		nCredit += GetCredit(txout, filter);
		if (!MoneyRange(nCredit))
		{
			throw std::runtime_error("CWallet::GetCredit() : value out of range");
		}
	}

	return nCredit;
}

CAmount CWallet::GetChange(const CTransaction& tx) const
{
	CAmount nChange = 0;

	for(const CTxOut& txout : tx.vout)
	{
		nChange += GetChange(txout);
		
		if (!MoneyRange(nChange))
		{
			throw std::runtime_error("CWallet::GetChange() : value out of range");
		}
	}

	return nChange;
}

void CWallet::SetBestChain(const CBlockLocator& loc)
{
	CWalletDB walletdb(strWalletFile);
	walletdb.WriteBestBlock(loc);
}

DBErrors CWallet::LoadWallet(bool& fFirstRunRet)
{
	if (!fFileBacked)
	{
		return DB_LOAD_OK;
	}

	fFirstRunRet = false;
	DBErrors nLoadWalletRet = CWalletDB(strWalletFile,"cr+").LoadWallet(this);

	if (nLoadWalletRet == DB_NEED_REWRITE)
	{
		if (CDB::Rewrite(strWalletFile, "\x04pool"))
		{
			LOCK(cs_wallet);
			
			setKeyPool.clear();
			// Note: can't top-up keypool here, because wallet is locked.
			// User will be prompted to unlock wallet the next operation
			// the requires a new key.
		}
	}

	if (nLoadWalletRet != DB_LOAD_OK)
	{
		return nLoadWalletRet;
	}

	fFirstRunRet = !vchDefaultKey.IsValid();

	return DB_LOAD_OK;
}

bool CWallet::SetAddressBookName(const CTxDestination& address, const std::string& strName)
{
	// never update address book if this is web wallet as this will break account<>address mapping
	if (fWebWalletMode)
	{
		return true;
	}

	bool fUpdated = false;
	{
		LOCK(cs_wallet); // mapAddressBook
		
		mapAddressBook_t::iterator mi = mapAddressBook.find(address);
		
		fUpdated = mi != mapAddressBook.end();
		mapAddressBook[address] = strName;
	}

	NotifyAddressBookChanged(
		this,
		address,
		strName,
		::IsMine(*this, address) != ISMINE_NO,
		(fUpdated ? CT_UPDATED : CT_NEW)
	);

	if (!fFileBacked)
	{
		return false;
	}

	return CWalletDB(strWalletFile).WriteName(CDigitalNoteAddress(address).ToString(), strName);
}

bool CWallet::SetAddressAccountIdAssociation(const CTxDestination& address, const std::string& strName)
{
	if (!fWebWalletMode)
	{
		return true;
	}

	{
		LOCK(cs_wallet);
		
		// only allow to create association
		if (mapAddressBook[address] == "")
		{
			mapAddressBook[address] = strName;
		}
	}

	if (!fFileBacked)
	{
		return false;
	}

	return CWalletDB(strWalletFile).WriteName(CDigitalNoteAddress(address).ToString(), strName);
}

bool CWallet::DelAddressBookName(const CTxDestination& address)
{
	{
		LOCK(cs_wallet); // mapAddressBook

		mapAddressBook.erase(address);
	}

	NotifyAddressBookChanged(this, address, "", ::IsMine(*this, address) != ISMINE_NO, CT_DELETED);

	if (!fFileBacked)
		return false;

	CWalletDB(strWalletFile).EraseName(CDigitalNoteAddress(address).ToString());

	return CWalletDB(strWalletFile).EraseName(CDigitalNoteAddress(address).ToString());
}

bool CWallet::UpdatedTransaction(const uint256 &hashTx)
{
	{
		LOCK(cs_wallet);
		
		// Only notify UI if this transaction is in this wallet
		mapWallet_t::const_iterator mi = mapWallet.find(hashTx);
		
		if (mi != mapWallet.end())
		{
			NotifyTransactionChanged(this, hashTx, CT_UPDATED);
			
			return true;
		}
	}

	return false;
}

void CWallet::Inventory(const uint256 &hash)
{
	{
		LOCK(cs_wallet);
		
		mapRequestCount_t::iterator mi = mapRequestCount.find(hash);
		
		if (mi != mapRequestCount.end())
		{
			(*mi).second++;
		}
	}
}

unsigned int CWallet::GetKeyPoolSize()
{
	AssertLockHeld(cs_wallet); // setKeyPool

	return setKeyPool.size();
}

bool CWallet::SetDefaultKey(const CPubKey &vchPubKey)
{
	if (fFileBacked)
	{
		if (!CWalletDB(strWalletFile).WriteDefaultKey(vchPubKey))
		{
			return false;
		}
	}

	vchDefaultKey = vchPubKey;

	return true;
}

bool CWallet::SetMinVersion(enum WalletFeature nVersion, CWalletDB* pwalletdbIn, bool fExplicit)
{
	LOCK(cs_wallet); // nWalletVersion

	if (nWalletVersion >= nVersion)
	{
		return true;
	}

	// when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
	if (fExplicit && nVersion > nWalletMaxVersion)
	{
		nVersion = FEATURE_LATEST;
	}

	nWalletVersion = nVersion;

	if (nVersion > nWalletMaxVersion)
	{
		nWalletMaxVersion = nVersion;
	}

	if (fFileBacked)
	{
		CWalletDB* pwalletdb = pwalletdbIn ? pwalletdbIn : new CWalletDB(strWalletFile);
		
		if (nWalletVersion > 40000)
		{
			pwalletdb->WriteMinVersion(nWalletVersion);
		}
		
		if (!pwalletdbIn)
		{
			delete pwalletdb;
		}
	}

	return true;
}

bool CWallet::SetMaxVersion(int nVersion)
{
	LOCK(cs_wallet); // nWalletVersion, nWalletMaxVersion

	// cannot downgrade below current version
	if (nWalletVersion > nVersion)
	{
		return false;
	}

	nWalletMaxVersion = nVersion;

	return true;
}

int CWallet::GetVersion()
{
	LOCK(cs_wallet);

	return nWalletVersion;
}

std::set<uint256> CWallet::GetConflicts(const uint256& txid) const
{
	AssertLockHeld(cs_wallet);

	std::set<uint256> result;
	mapWallet_t::const_iterator it = mapWallet.find(txid);
	
	if (it == mapWallet.end())
	{
		return result;
	}
	const CWalletTx& wtx = it->second;

	mmTxSpendsRange_t range;

	for(const CTxIn& txin : wtx.vin)
	{
		if (mmTxSpends.count(txin.prevout) <= 1)
		{
			continue;  // No conflict if zero or one spends
		}
		
		range = mmTxSpends.equal_range(txin.prevout);
		
		for (mmTxSpends_t::const_iterator it = range.first; it != range.second; ++it)
		{
			result.insert(it->second);
		}
	}

	return result;
}

// ppcoin: check 'spent' consistency between wallet and txindex
// ppcoin: fix wallet spent state according to txindex
void CWallet::FixSpentCoins(int& nMismatchFound, int64_t& nBalanceInQuestion, bool fCheckOnly)
{
	nMismatchFound = 0;
	nBalanceInQuestion = 0;

	LOCK(cs_wallet);

	std::vector<CWalletTx*> vCoins;

	vCoins.reserve(mapWallet.size());

	for (mapWallet_t::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
	{
		vCoins.push_back(&(*it).second);
	}

	CTxDB txdb("r");

	for(CWalletTx* pcoin : vCoins)
	{
		// Find the corresponding transaction index
		CTxIndex txindex;
		
		if (!txdb.ReadTxIndex(pcoin->GetHash(), txindex))
		{
			continue;
		}
		
		for (unsigned int n=0; n < pcoin->vout.size(); n++)
		{
			if (IsMine(pcoin->vout[n]) && pcoin->IsSpent(n) && (txindex.vSpent.size() <= n || txindex.vSpent[n].IsNull()))
			{
				LogPrintf(
					"FixSpentCoins found lost coin %s XDN %s[%d], %s\n",
					FormatMoney(pcoin->vout[n].nValue),
					pcoin->GetHash().ToString(),
					n,
					fCheckOnly? "repair not attempted" : "repairing"
				);
				
				nMismatchFound++;
				nBalanceInQuestion += pcoin->vout[n].nValue;
				
				if (!fCheckOnly)
				{
					pcoin->MarkUnspent(n);
					pcoin->WriteToDisk();
				}
			}
			else if (IsMine(pcoin->vout[n]) && !pcoin->IsSpent(n) && (txindex.vSpent.size() > n && !txindex.vSpent[n].IsNull()))
			{
				LogPrintf(
					"FixSpentCoins found spent coin %s XDN %s[%d], %s\n",
					FormatMoney(pcoin->vout[n].nValue),
					pcoin->GetHash().ToString(),
					n,
					fCheckOnly? "repair not attempted" : "repairing"
				);
				
				nMismatchFound++;
				nBalanceInQuestion += pcoin->vout[n].nValue;
				
				if (!fCheckOnly)
				{
					pcoin->MarkSpent(n);
					pcoin->WriteToDisk();
				}
			}
		}
	}
}

// ppcoin: disable transaction (only for coinstake)
void CWallet::DisableTransaction(const CTransaction &tx)
{
	if (!tx.IsCoinStake() || !IsFromMe(tx))
	{
		return; // only disconnecting coinstake requires marking input unspent
	}

	LOCK(cs_wallet);

	for(const CTxIn& txin : tx.vin)
	{
		mapWallet_t::iterator mi = mapWallet.find(txin.prevout.hash);
		
		if (mi != mapWallet.end())
		{
			CWalletTx& prev = (*mi).second;
			
			if (txin.prevout.n < prev.vout.size() && IsMine(prev.vout[txin.prevout.n]))
			{
				prev.MarkUnspent(txin.prevout.n);
				prev.WriteToDisk();
			}
		}
	}
}

/**
	Extra function
*/
void ApproximateBestSubset(std::vector<std::pair<int64_t, std::pair<const CWalletTx*,unsigned int> > >vValue, int64_t nTotalLower,
		int64_t nTargetValue, std::vector<char>& vfBest, int64_t& nBest, int iterations)
{
	std::vector<char> vfIncluded;

	vfBest.assign(vValue.size(), true);
	nBest = nTotalLower;

	seed_insecure_rand();

	for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++)
	{
		vfIncluded.assign(vValue.size(), false);
		
		int64_t nTotal = 0;
		bool fReachedTarget = false;
		
		for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
		{
			for (unsigned int i = 0; i < vValue.size(); i++)
			{
				//The solver here uses a randomized algorithm,
				//the randomness serves no real security purpose but is just
				//needed to prevent degenerate behavior and it is important
				//that the rng fast. We do not use a constant random sequence,
				//because there may be some privacy improvement by making
				//the selection random.
				if (nPass == 0 ? insecure_rand()&1 : !vfIncluded[i])
				{
					nTotal += vValue[i].first;
					vfIncluded[i] = true;
					
					if (nTotal >= nTargetValue)
					{
						fReachedTarget = true;
						
						if (nTotal < nBest)
						{
							nBest = nTotal;
							vfBest = vfIncluded;
						}
						
						nTotal -= vValue[i].first;
						vfIncluded[i] = false;
					}
				}
			}
		}
	}
}

int64_t GetStakeCombineThreshold()
{
	return GetArg("-stakethreshold", 1000) * COIN;
}

int64_t GetStakeSplitThreshold()
{
	return 2 * GetStakeCombineThreshold();
}

