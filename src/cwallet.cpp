#include "compat.h"
#include <bip39/bip39_passphrase.h>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

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
#include "init.h"
#include "cblockindex.h"
#include "ctxindex.h"
#include "serialize.h"
#include "webwallet.h"

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
					
					if(this->IsSpent(pcoin->GetHash(), i) || !IsMine(pcoin->vout[i]))   // v2.0.0.8 CW4 Fix C: mmTxSpends-based reader
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
			
			// NOTE: Previous versions filtered out the ENTIRE transaction if
			// any vout happened to equal the masternode collateral amount
			// (2,000,000 XDN) or any vout passed IsCollateralAmount().  That
			// heuristic punished:
			//   - Innocent recipients of 2M XDN payments (entire tx excluded
			//     from staking, including unrelated change outputs)
			//   - Tx whose change happened to land at a "collateral amount"
			//   - Users who genuinely received 2M but didn't intend to use
			//     it as masternode collateral
			//
			// The correct approach is per-OUTPOINT: only exclude an output
			// if the user has explicitly locked it (via Coin Control,
			// lockunspent RPC, or the masternode UI) -- which sets a flag
			// in setLockedCoins.  Other outputs of the same transaction
			// remain stakeable.

			for (unsigned int i = 0; i < pcoin->vout.size(); i++)
			{
				// Skip explicitly-locked outpoints (e.g. masternode collateral
				// the user has locked, or any UTXO they've locked via
				// lockunspent RPC).
				if (IsLockedCoin(pcoin->GetHash(), i))
				{
					continue;
				}

				isminetype mine = IsMine(pcoin->vout[i]);
				
				if (
					!(this->IsSpent(pcoin->GetHash(), i)) &&   // v2.0.0.8 CW4 Fix C: mmTxSpends-based reader
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
					!(this->IsSpent(pcoin->GetHash(), i)) &&   // v2.0.0.8 CW4 Fix C: mmTxSpends-based reader
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
		AvailableCoinsType coin_type, bool useIX, bool fIncludeLockedMN) const
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
					!(this->IsSpent(pcoin->GetHash(), i)) &&   // v2.0.0.8 CW4 Fix C: mmTxSpends-based reader
					mine != ISMINE_NO &&
					(fIncludeLockedMN || !IsLockedCoin((*it).first, i)) &&
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

	// Persist so the lock state survives wallet restart.  Older wallet
	// binaries silently ignore unknown record types on load, so this
	// is forward-compatible without a schema bump.
	if (fFileBacked)
	{
		CWalletDB(strWalletFile).WriteLockedOutput(output);
	}
}

void CWallet::UnlockCoin(COutPoint& output)
{
	AssertLockHeld(cs_wallet); // setLockedCoins

	setLockedCoins.erase(output);

	if (fFileBacked)
	{
		CWalletDB(strWalletFile).EraseLockedOutput(output);
	}
}

void CWallet::UnlockAllCoins()
{
	AssertLockHeld(cs_wallet); // setLockedCoins

	if (fFileBacked)
	{
		CWalletDB walletdb(strWalletFile);
		for (const COutPoint& outpt : setLockedCoins)
		{
			COutPoint copy = outpt; // EraseLockedOutput takes non-const, harmless
			walletdb.EraseLockedOutput(copy);
		}
	}

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
		MarkAllTxCachesDirty();
		return true;
	}

	if (!IsCrypted())
	{
		bool fOk = CWalletDB(strWalletFile).WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);
		if (fOk) MarkAllTxCachesDirty();
		return fOk;
	}

	MarkAllTxCachesDirty();
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
		MarkAllTxCachesDirty();
		return true;
	}

	{
		LOCK(cs_wallet);
		
		bool fOk;
		if (pwalletdbEncryption)
		{
			fOk = pwalletdbEncryption->WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
		}
		else
		{
			fOk = CWalletDB(strWalletFile).WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
		}
		if (fOk) MarkAllTxCachesDirty();
		return fOk;
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
		MarkAllTxCachesDirty();
		return true;
	}

	bool fOk = CWalletDB(strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
	if (fOk) MarkAllTxCachesDirty();
	return fOk;
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

	// Fire the GUI notification so the wallet model picks up the new
	// state, shows the watch-only column on overview / transactions
	// page, and triggers a balance refresh.  Without this the wallet
	// model only learned about watch-only state on next restart.
	NotifyWatchonlyChanged(true);

	// Invalidate balance caches so historical txes that newly become
	// watch-only-mine recompute on next access.
	MarkAllTxCachesDirty();

	if (!fFileBacked)
	{
		return true;
	}

	return CWalletDB(strWalletFile).WriteWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript &dest, const RemoveProgressFn& progressCb)
{
	AssertLockHeld(cs_wallet);

	if (!CCryptoKeyStore::RemoveWatchOnly(dest))
	{
		return false;
	}

	if (progressCb) progressCb(0, "Scanning wallet for orphan transactions");

	// Phase A: After the script is gone, find any transactions in mapWallet that
	// are now orphaned -- they had no inputs from us, and their outputs
	// were watch-only via the script we just removed (or another script
	// we no longer have in any keystore).  These become ghost rows in
	// the GUI ("(n/a)" + amount 0) if not pruned, because IsMine() now
	// returns ISMINE_NO for all their outputs.  Collect them first,
	// then erase from mapWallet, then notify the GUI.
	//
	// This is the slow phase: per-tx IsMine() evaluates every output's
	// script.  For an address with thousands of historical txs it
	// dominates wall-clock time.  Report progress every 100 entries.
	std::vector<uint256> vToErase;
	const size_t nWalletSize = mapWallet.size();
	size_t nScanned = 0;

	for (mapWallet_t::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
	{
		const CWalletTx& wtx = it->second;

		if (!IsMine(wtx) && !IsFromMe(wtx))
		{
			vToErase.push_back(it->first);
		}

		++nScanned;
		if (progressCb && (nScanned % 100 == 0) && nWalletSize > 0)
		{
			// Phase A occupies 0-60% of the progress bar.
			int pct = static_cast<int>(60.0 * nScanned / nWalletSize);
			progressCb(pct, "Scanning wallet for orphan transactions");
		}
	}

	if (progressCb) progressCb(60, "Removing orphaned transactions");

	// Phase B: Erase orphans (BDB write per entry).
	const size_t nToErase = vToErase.size();
	size_t nErased = 0;

	for (const uint256& hash : vToErase)
	{
		mapWallet.erase(hash);

		if (fFileBacked)
		{
			CWalletDB(strWalletFile).EraseTx(hash);
		}

		NotifyTransactionChanged(this, hash, CT_DELETED);

		++nErased;
		if (progressCb && (nErased % 50 == 0) && nToErase > 0)
		{
			// Phase B occupies 60-90% of the progress bar.
			int pct = 60 + static_cast<int>(30.0 * nErased / nToErase);
			progressCb(pct, "Removing orphaned transactions");
		}
	}

	if (progressCb) progressCb(90, "Refreshing remaining transactions");

	// Phase C: Clear cached balance/credit values on remaining transactions.
	// Their watch-only credit sums need to recompute now that the
	// scripts they referenced are no longer "ours".  Fast (just a flag
	// flip per tx) so no per-iteration progress reporting needed.
	for (std::pair<const uint256, CWalletTx>& item : mapWallet)
	{
		item.second.MarkDirty();
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

	if (progressCb) progressCb(100, "Done");

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
}

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
	bool unlocked = false;

	{
		LOCK(cs_wallet);

		for(const mapMasterKeys_t::value_type& pMasterKey : mapMasterKeys)
		{
			// Use continue (not return false) so ALL master keys are tried.
			// This allows both password and mnemonic hex to unlock the wallet:
			// each envelope has different KDF params + ciphertext, so the
			// password decrypts CMasterKey[1] and the mnemonic-derived hex
			// decrypts CMasterKey[2].  Whichever envelope matches wins.
			if(!crypter.SetKeyFromPassphrase(
					strWalletPassphraseFinal,
					pMasterKey.second.vchSalt,
					pMasterKey.second.nDeriveIterations,
					pMasterKey.second.nDerivationMethod
				)
			)
			{
				continue;
			}

			if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
			{
				continue;
			}

			if (!CCryptoKeyStore::Unlock(vMasterKey))
			{
				continue;
			}

			// Successfully unlocked with this master key
			unlocked = true;
			break;
		}

		if (!unlocked)
		{
			// None of the envelopes matched.  Do NOT call UnlockStealthAddresses
			// or set the unlock flags -- the keystore is still locked.
			LogPrintf("CWallet::Unlock: no master key matched supplied passphrase/mnemonic\n");
			return false;
		}

		fWalletUnlockAnonymizeOnly = anonymizeOnly;
		fWalletUnlockStakingOnly = stakingOnly;
		UnlockStealthAddresses(vMasterKey);
		DigitalNote::SMSG::WalletUnlocked();

		// Encrypted-wallet outputs were not visible as ISMINE while the
		// wallet was locked (CCryptoKeyStore::HaveKey returns false for
		// encrypted keys without the master key in memory). Now that
		// keys are available, balance caches computed while locked would
		// have wrong (zero) values for those outputs. Invalidate so the
		// next balance poll recomputes against the now-complete keystore.
		MarkAllTxCachesDirty();

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
				continue;
			}
			
			if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
			{
				continue;
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
		}

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

	// Mark wallet as recovery-phrase capable (custom key, older wallets ignore it)
	SetRecoveryPhraseFlag();

	NotifyStatusChanged(this);

	return true;
}

bool CWallet::VerifyPassphrase(const SecureString& strWalletPassphrase) const
{
	if (!IsCrypted())
		return true;

	CCrypter crypter;
	CKeyingMaterial vMasterKey;

	{
		LOCK(cs_wallet);

		for (const mapMasterKeys_t::value_type& pMasterKey : mapMasterKeys)
		{
			// Step 1: Derive AES key from passphrase using stored salt/iterations
			if (!crypter.SetKeyFromPassphrase(
					strWalletPassphrase,
					pMasterKey.second.vchSalt,
					pMasterKey.second.nDeriveIterations,
					pMasterKey.second.nDerivationMethod))
				return false;

			// Step 2: Decrypt the stored master key
			if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
				return false;

			// Step 3: Verify by decrypting one actual wallet key - no state change
			if (mapCryptedKeys.empty())
				return true; // No keys to verify against - trust the master key decrypt

			const CryptedKeyMap::const_iterator mi = mapCryptedKeys.begin();
			const CPubKey& vchPubKey = mi->second.first;
			const std::vector<unsigned char>& vchCryptedSecret = mi->second.second;
			CKeyingMaterial vchSecret;

			if (!DecryptSecret(vMasterKey, vchCryptedSecret, vchPubKey.GetHash(), vchSecret))
				return false;

			// Valid key material is always 32 bytes
			return vchSecret.size() == 32;
		}
	}
	return false;
}

// NOT CALLED — retained for future use.
// This function fully decrypts the wallet.dat, removing all encryption.
// It uses a two-phase commit: write all plain keys first (WriteKeyOverwrite),
// then erase encrypted records. If interrupted mid-Phase-A wallet.dat is safe
// (both plain and ckey records exist); if interrupted mid-Phase-B the wallet
// loader handles duplicate keys gracefully.
// To enable: add Decrypt mode back to askpassphrasedialog and call from GUI.
bool CWallet::DecryptWallet(const SecureString& strWalletPassphrase)
{
	if (!IsCrypted())
		return false;

	CWalletDB walletdb(strWalletFile);
	CKeyingMaterial vMasterKey;
	boost::filesystem::path backupPath = GetDataDir() / "decrypt_wallet_backup.txt";
	bool bBackupCreated = false;

	{
		LOCK(cs_wallet);

		// Step 1: Verify passphrase and obtain vMasterKey (single lock scope)
		bool bUnlockOk = false;
		for (const auto& pMasterKey : mapMasterKeys)
		{
			CCrypter crypter;
			if (!crypter.SetKeyFromPassphrase(strWalletPassphrase,
				pMasterKey.second.vchSalt,
				pMasterKey.second.nDeriveIterations,
				pMasterKey.second.nDerivationMethod))
				return false;
			if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
				return false;
			if (CCryptoKeyStore::Unlock(vMasterKey))
			{
				bUnlockOk = true;
				break;
			}
			LockKeyStore();
			return false;
		}
		if (!bUnlockOk)
			return false;

		// Safety backup: dump all private keys before modifying any records
		{
			std::ofstream backupFile(backupPath.string().c_str());
			if (backupFile.is_open())
			{
				backupFile << "# DigitalNote DecryptWallet safety backup\n";
				backupFile << "# Safe to delete after confirming wallet works\n\n";
				for (const auto& mi : mapCryptedKeys)
				{
					const CPubKey& vchPubKey = mi.second.first;
					CKeyingMaterial vchSecret;
					if (DecryptSecret(vMasterKey, mi.second.second, vchPubKey.GetHash(), vchSecret))
					{
						CKey key;
						key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
						backupFile << CDigitalNoteSecret(key).ToString()
						           << " # addr=" << CDigitalNoteAddress(vchPubKey.GetID()).ToString() << "\n";
					}
				}
				backupFile.close();
				bBackupCreated = true;
				LogPrintf("DecryptWallet: safety backup written to %s\n", backupPath.string());
			}
		}

		// Step 2: Two-phase commit for safety
		// Phase A: Write ALL plain keys with overwrite=true
		// If this fails partway, ckey records still exist - wallet is safe
		for (const auto& mi : mapCryptedKeys)
		{
			const CPubKey& vchPubKey = mi.second.first;
			const std::vector<unsigned char>& vchCryptedSecret = mi.second.second;

			CKeyingMaterial vchSecret;
			if (!DecryptSecret(vMasterKey, vchCryptedSecret, vchPubKey.GetHash(), vchSecret))
				return false;

			CKey key;
			key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());

			if (!walletdb.WriteKeyOverwrite(vchPubKey, key.GetPrivKey(), mapKeyMetadata[vchPubKey.GetID()]))
				return false;
		}

		// Phase B: All plain keys written - now safe to erase encrypted records
		for (const auto& mi : mapCryptedKeys)
		{
			const CPubKey& vchPubKey = mi.second.first;
			CKeyingMaterial vchSecret;
			CKey key;
			DecryptSecret(vMasterKey, mi.second.second, vchPubKey.GetHash(), vchSecret);
			key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
			walletdb.EraseCryptedKey(vchPubKey);
			CBasicKeyStore::AddKeyPubKey(key, vchPubKey);
		}

		// Step 3: Decrypt stealth address spend secrets
		for (setStealthAddresses_t::iterator it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
		{
			if (it->scan_secret.size() < 32)
				continue;

			CStealthAddress& sxAddr = const_cast<CStealthAddress&>(*it);
			CKeyingMaterial vchSecret;
			uint256 iv = Hash(sxAddr.spend_pubkey.begin(), sxAddr.spend_pubkey.end());

			if (!DecryptSecret(vMasterKey, sxAddr.spend_secret, iv, vchSecret))
			{
				LogPrintf("DecryptWallet: failed to decrypt stealth key %s\n", sxAddr.Encoded().c_str());
				continue;
			}

			sxAddr.spend_secret.assign(vchSecret.begin(), vchSecret.begin() + 32);
			walletdb.WriteStealthAddress(sxAddr);
		}

		// Step 4: Erase master keys from DB and memory
		for (const auto& mk : mapMasterKeys)
			walletdb.EraseMasterKey(mk.first);
		mapMasterKeys.clear();

		// Step 5: Clear crypto state via CCryptoKeyStore
		mapCryptedKeys.clear();
		if (!CCryptoKeyStore::SetUnencrypted())
			return false;

	} // end LOCK(cs_wallet)

	// Step 6: Rewrite skipped - CDB::Rewrite can deadlock when called from
	// a worker thread while the wallet DB is still open. The wallet is fully
	// functional without it - encrypted records are already erased above.

	// Delete safety backup on success
	if (bBackupCreated)
	{
		boost::system::error_code ec;
		boost::filesystem::remove(backupPath, ec);
		if (!ec)
			LogPrintf("DecryptWallet: backup deleted after successful decryption\n");
		else
			LogPrintf("DecryptWallet: could not delete backup at %s\n", backupPath.string());
	}

	NotifyStatusChanged(this);

	return true;
}


bool CWallet::RemoveMnemonicMasterKey()
{
	if (!IsCrypted())
		return false;

	if (!HasMnemonicMasterKey())
		return true; // nothing to remove

	LOCK(cs_wallet);

	// Find and remove the mnemonic master key
	// The mnemonic key is any key after the first one (index > 1)
	// We identify it by trying to decrypt with a known-invalid passphrase
	// and keeping only the primary (password) key
	std::vector<unsigned int> toErase;
	for (const auto& pMasterKey : mapMasterKeys)
	{
		if (pMasterKey.first > 1)
			toErase.push_back(pMasterKey.first);
	}

	for (unsigned int id : toErase)
	{
		mapMasterKeys.erase(id);
		if (fFileBacked)
			CWalletDB(strWalletFile).EraseMasterKey(id);
	}

	// Clear the recovery phrase flag so AddMnemonicMasterKey can run again
	CWalletDB(strWalletFile).EraseRecoveryPhraseFlag();

	return !toErase.empty();
}

bool CWallet::HasMnemonicMasterKey() const
{
	// In D2, the wallet has CMasterKey[1] (password-encrypted vMasterKey)
	// and optionally CMasterKey[2] (phrase-encrypted vMasterKey).  Any
	// entry beyond the first is the phrase envelope -- there's no other
	// reason for a second master key in this wallet design.
	//
	// Note: this is a STRUCTURAL check on the keystore.  The
	// HasRecoveryPhraseFlag() flag is a separate concept ("this wallet
	// supports recovery phrase generation"), set by EncryptWallet at
	// encryption time so older pre-D2 wallets without the flag can be
	// detected.  Don't conflate the two.
	LOCK(cs_wallet);
	return mapMasterKeys.size() > 1;
}
// ---------------------------------------------------------------------------
// AddMnemonicMasterKey — D2 version (vMasterKey-derived, no password param)
// ---------------------------------------------------------------------------
//
// Derives the recovery-phrase entropy from the current vMasterKey, encrypts
// vMasterKey under that entropy as a 32-byte AES key, and stores the result
// as a second CMasterKey envelope (CMasterKey[2]).
//
// Pre-conditions:
//   * Wallet is encrypted (IsCrypted() == true).
//   * Wallet is unlocked (vMasterKey is in CCryptoKeyStore::vMasterKey).
//   * No mnemonic master key already exists (early-return success otherwise).
//
// Post-condition on success:
//   * mapMasterKeys contains the new envelope at id == ++nMasterKeyMaxID.
//   * wallet.dat has the new master-key record persisted.
//   * recovery-phrase flag is set.

bool CWallet::AddMnemonicMasterKey()
{
	LogPrintf("AddMnemonicMasterKey: ENTRY (mapMasterKeys.size=%u, IsCrypted=%d, IsLocked=%d)\n",
	          (unsigned)mapMasterKeys.size(), (int)IsCrypted(), (int)IsLocked());

	if (!IsCrypted()) {
		LogPrintf("AddMnemonicMasterKey: bail - not encrypted\n");
		return false;
	}

	if (HasMnemonicMasterKey()) {
		LogPrintf("AddMnemonicMasterKey: idempotent skip - already have mnemonic key\n");
		return true;
	}

	if (IsLocked()) {
		LogPrintf("AddMnemonicMasterKey: bail - locked\n");
		return false;
	}

	// Step 1: Snapshot vMasterKey under the keystore mutex.
	CKeyingMaterial vMasterKeyCopy;
	{
		LOCK(cs_KeyStore);
		if (CCryptoKeyStore::vMasterKey.empty()) {
			LogPrintf("AddMnemonicMasterKey: bail - vMasterKey empty\n");
			return false;
		}
		vMasterKeyCopy = CCryptoKeyStore::vMasterKey;
	}

	// Step 2: Derive mnemonic from vMasterKey, then re-derive the 32-byte
	// AES key (as a 64-char hex SecureString).
	SecureString mnemonic, mnemonicHex;
	if (BIP39Passphrase::mnemonicFromVMasterKey(vMasterKeyCopy, mnemonic) != BIP39Passphrase::Result::OK)
	{
		LogPrintf("AddMnemonicMasterKey: bail - mnemonicFromVMasterKey failed\n");
		OPENSSL_cleanse(vMasterKeyCopy.data(), vMasterKeyCopy.size());
		return false;
	}
	if (BIP39Passphrase::passphraseFromMnemonic(mnemonic, mnemonicHex) != BIP39Passphrase::Result::OK)
	{
		LogPrintf("AddMnemonicMasterKey: bail - passphraseFromMnemonic failed\n");
		OPENSSL_cleanse(const_cast<char*>(mnemonic.data()), mnemonic.size());
		OPENSSL_cleanse(vMasterKeyCopy.data(), vMasterKeyCopy.size());
		return false;
	}
	OPENSSL_cleanse(const_cast<char*>(mnemonic.data()), mnemonic.size());

	// Step 3: Build a new CMasterKey envelope.
	CCrypter crypter;
	CMasterKey kMnemonicKey;
	kMnemonicKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
	if (!GetRandBytes(&kMnemonicKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE))
	{
		LogPrintf("AddMnemonicMasterKey: bail - GetRandBytes for salt failed\n");
		OPENSSL_cleanse(const_cast<char*>(mnemonicHex.data()), mnemonicHex.size());
		OPENSSL_cleanse(vMasterKeyCopy.data(), vMasterKeyCopy.size());
		return false;
	}

	kMnemonicKey.nDeriveIterations = 25000;
	kMnemonicKey.nDerivationMethod = 0;

	if (!crypter.SetKeyFromPassphrase(mnemonicHex, kMnemonicKey.vchSalt,
			kMnemonicKey.nDeriveIterations, kMnemonicKey.nDerivationMethod))
	{
		LogPrintf("AddMnemonicMasterKey: bail - SetKeyFromPassphrase failed\n");
		OPENSSL_cleanse(const_cast<char*>(mnemonicHex.data()), mnemonicHex.size());
		OPENSSL_cleanse(vMasterKeyCopy.data(), vMasterKeyCopy.size());
		return false;
	}

	if (!crypter.Encrypt(vMasterKeyCopy, kMnemonicKey.vchCryptedKey))
	{
		LogPrintf("AddMnemonicMasterKey: bail - Encrypt failed\n");
		OPENSSL_cleanse(const_cast<char*>(mnemonicHex.data()), mnemonicHex.size());
		OPENSSL_cleanse(vMasterKeyCopy.data(), vMasterKeyCopy.size());
		return false;
	}

	OPENSSL_cleanse(const_cast<char*>(mnemonicHex.data()), mnemonicHex.size());
	OPENSSL_cleanse(vMasterKeyCopy.data(), vMasterKeyCopy.size());

	// Step 4: Persist.
	{
		LOCK(cs_wallet);
		unsigned int newId = ++nMasterKeyMaxID;
		mapMasterKeys[newId] = kMnemonicKey;
		LogPrintf("AddMnemonicMasterKey: in-memory mapMasterKeys.size=%u, newId=%u, fFileBacked=%d\n",
		          (unsigned)mapMasterKeys.size(), newId, (int)fFileBacked);

		if (fFileBacked) {
			bool ok = CWalletDB(strWalletFile).WriteMasterKey(newId, kMnemonicKey);
			LogPrintf("AddMnemonicMasterKey: WriteMasterKey returned %d for id=%u\n",
			          (int)ok, newId);
			if (!ok) {
				LogPrintf("AddMnemonicMasterKey: WARNING - WriteMasterKey failed, in-memory state has 2 entries but disk has only 1\n");
			}
		}
	}

	SetRecoveryPhraseFlag();
	LogPrintf("AddMnemonicMasterKey: SUCCESS (mapMasterKeys.size=%u)\n",
	          (unsigned)mapMasterKeys.size());
	return true;
}


// ---------------------------------------------------------------------------
// GetCurrentMnemonic — re-derive the mnemonic from the current vMasterKey
// ---------------------------------------------------------------------------
//
// Useful for "show me my recovery phrase" UI: at any moment when the wallet
// is unlocked, the mnemonic can be re-derived deterministically from
// vMasterKey.  No state is changed.

bool CWallet::GetCurrentMnemonic(SecureString& mnemonicOut) const
{
	mnemonicOut.clear();

	if (!IsCrypted() || IsLocked())
		return false;

	CKeyingMaterial vMasterKeyCopy;
	{
		LOCK(cs_KeyStore);
		if (CCryptoKeyStore::vMasterKey.empty())
			return false;
		vMasterKeyCopy = CCryptoKeyStore::vMasterKey;
	}

	BIP39Passphrase::Result r =
		BIP39Passphrase::mnemonicFromVMasterKey(vMasterKeyCopy, mnemonicOut);

	OPENSSL_cleanse(vMasterKeyCopy.data(), vMasterKeyCopy.size());
	return r == BIP39Passphrase::Result::OK;
}


// ---------------------------------------------------------------------------
// RotateMnemonicMasterKey — replace vMasterKey, re-encrypt all keys
// ---------------------------------------------------------------------------
//
// This is the heavyweight rotation: produces a new vMasterKey, re-wraps every
// CKey in the keystore and every stealth-address spend secret under it, and
// replaces both CMasterKey envelopes (password-encrypted and phrase-encrypted).
//
// On success the OLD recovery phrase will no longer decrypt this wallet.
//
// Caller responsibility:
//   * UI must obtain explicit user confirmation (wall-of-text dialog).
//   * UI must show the returned newMnemonicOut to the user immediately
//     and confirm they have written it down.
//   * Wallet must be unlocked when this is called (we Unlock-with-password
//     ourselves to verify, and snapshot the existing vMasterKey).
//
// Failure semantics:
//   * In-memory state (mapCryptedKeys, stealthAddresses, mapMasterKeys) is
//     only mutated AFTER all crypto operations have succeeded.  If any step
//     fails partway, we abort before mutating state.
//   * Disk state is written under a single CWalletDB session, with a final
//     TxnCommit.  If the commit fails, the on-disk state is unchanged.
//
// Notes:
//   * The wallet remains unlocked at the new vMasterKey on success.
//   * Caller may want to Lock() afterwards to force the user to re-enter
//     the (possibly new) password before doing more.

// =====================================================================
// DEBUG-INSTRUMENTED REPLACEMENT for CWallet::RotateMnemonicMasterKey
// =====================================================================
//
// Replace the existing RotateMnemonicMasterKey function in src/cwallet.cpp
// with this version.  Identical logic to the original; the only difference
// is a LogPrintf("RotateMnemonic FAIL: ...") added before every "return
// false" path so debug.log will tell us exactly where the rotation is
// failing.
//
// After we identify the failing path we can either fix the underlying
// cause and revert this back to the silent version, or keep the
// LogPrintfs as permanent diagnostic output (they're cheap and only fire
// on the failure paths, so production cost is zero).
//
// Output goes to <datadir>/debug.log -- on Windows that's typically
// %APPDATA%\DigitalNote\debug.log.
// =====================================================================

bool CWallet::RotateMnemonicMasterKey(const SecureString& strCurrentPassword,
                                       SecureString& newMnemonicOut)
{
	newMnemonicOut.clear();

	if (!IsCrypted()) {
		LogPrintf("RotateMnemonic FAIL: wallet is not encrypted\n");
		return false;
	}

	// We need both the OLD vMasterKey (to decrypt existing keys) and the
	// password (to encrypt the NEW vMasterKey under the user's existing
	// password as CMasterKey[1]).  Verify the password works first.

	CKeyingMaterial vOldMasterKey;
	CMasterKey      kOldPasswordEntry;
	unsigned int    oldPasswordEntryId = 0;
	{
		LOCK(cs_wallet);

		CCrypter crypter;
		bool foundPasswordEntry = false;

		LogPrintf("RotateMnemonic: trying %u master key entries\n",
		          (unsigned)mapMasterKeys.size());

		for (const auto& mk : mapMasterKeys)
		{
			if (!crypter.SetKeyFromPassphrase(strCurrentPassword,
					mk.second.vchSalt,
					mk.second.nDeriveIterations,
					mk.second.nDerivationMethod))
			{
				LogPrintf("RotateMnemonic: SetKeyFromPassphrase failed for mkey id=%u (continuing)\n",
				          mk.first);
				continue;
			}

			if (!crypter.Decrypt(mk.second.vchCryptedKey, vOldMasterKey))
			{
				LogPrintf("RotateMnemonic: Decrypt failed for mkey id=%u (continuing)\n",
				          mk.first);
				continue;
			}

			// Found the password-encrypted envelope.
			LogPrintf("RotateMnemonic: password decrypts mkey id=%u (vMasterKey size=%u)\n",
			          mk.first, (unsigned)vOldMasterKey.size());
			foundPasswordEntry = true;
			kOldPasswordEntry  = mk.second;
			oldPasswordEntryId = mk.first;
			break;
		}

		if (!foundPasswordEntry) {
			LogPrintf("RotateMnemonic FAIL: no master key entry decrypted with this password\n");
			return false;
		}
	}

	// Sanity check: the decrypted vMasterKey should match what's currently
	// in the keystore (i.e. the wallet should already be unlocked under it).
	{
		LOCK(cs_KeyStore);
		bool isEmpty = CCryptoKeyStore::vMasterKey.empty();
		bool sizesEqual = (CCryptoKeyStore::vMasterKey.size() == vOldMasterKey.size());
		bool bytesEqual = !isEmpty && (CCryptoKeyStore::vMasterKey == vOldMasterKey);

		LogPrintf("RotateMnemonic: vMasterKey check -- in-memory empty=%d size=%u, decrypted size=%u, sizes_equal=%d, bytes_equal=%d\n",
		          (int)isEmpty,
		          (unsigned)CCryptoKeyStore::vMasterKey.size(),
		          (unsigned)vOldMasterKey.size(),
		          (int)sizesEqual,
		          (int)bytesEqual);

		if (isEmpty || !bytesEqual)
		{
			LogPrintf("RotateMnemonic FAIL: vMasterKey mismatch -- wallet is locked or has different master key\n");
			OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
			return false;
		}
	}

	// Step 1: Generate the NEW vMasterKey.
	CKeyingMaterial vNewMasterKey;
	RandAddSeedPerfmon();
	vNewMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
	if (!GetRandBytes(&vNewMasterKey[0], WALLET_CRYPTO_KEY_SIZE))
	{
		LogPrintf("RotateMnemonic FAIL: GetRandBytes for new vMasterKey\n");
		OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
		return false;
	}
	LogPrintf("RotateMnemonic: generated new vMasterKey (%u bytes)\n",
	          (unsigned)vNewMasterKey.size());

	// Step 2: Re-encrypt every CKey in mapCryptedKeys under vNewMasterKey.
	// Build a complete replacement map first; only swap on success.
	CryptedKeyMap newCryptedKeys;
	{
		LOCK(cs_KeyStore);
		LogPrintf("RotateMnemonic: re-encrypting %u CKeys\n",
		          (unsigned)mapCryptedKeys.size());

		size_t keyIdx = 0;
		for (const auto& mi : mapCryptedKeys)
		{
			const CPubKey&                       pub        = mi.second.first;
			const std::vector<unsigned char>&    cryptedOld = mi.second.second;

			CKeyingMaterial vchSecret;
			if (!DecryptSecret(vOldMasterKey, cryptedOld, pub.GetHash(), vchSecret))
			{
				LogPrintf("RotateMnemonic FAIL: DecryptSecret failed for CKey index %u (pubkey hash=%s)\n",
				          (unsigned)keyIdx, pub.GetHash().ToString().c_str());
				OPENSSL_cleanse(vOldMasterKey.data(),  vOldMasterKey.size());
				OPENSSL_cleanse(vNewMasterKey.data(),  vNewMasterKey.size());
				return false;
			}

			std::vector<unsigned char> cryptedNew;
			if (!EncryptSecret(vNewMasterKey, vchSecret, pub.GetHash(), cryptedNew))
			{
				LogPrintf("RotateMnemonic FAIL: EncryptSecret failed for CKey index %u\n",
				          (unsigned)keyIdx);
				OPENSSL_cleanse(vchSecret.data(),     vchSecret.size());
				OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
				OPENSSL_cleanse(vNewMasterKey.data(), vNewMasterKey.size());
				return false;
			}

			OPENSSL_cleanse(vchSecret.data(), vchSecret.size());
			newCryptedKeys[mi.first] = std::make_pair(pub, cryptedNew);
			++keyIdx;
		}
		LogPrintf("RotateMnemonic: re-encrypted %u CKeys successfully\n",
		          (unsigned)keyIdx);
	}

	// Step 3: Re-encrypt every stealth-address spend_secret under vNewMasterKey.
	std::vector<std::pair<ec_point, std::vector<unsigned char>>> newStealthSecrets;
	{
		size_t stealthCount = 0;
		size_t stealthRotated = 0;
		for (auto it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
		{
			++stealthCount;
			if (it->scan_secret.size() < 32 || it->spend_secret.size() == 0)
				continue;

			CStealthAddress& sx = const_cast<CStealthAddress&>(*it);

			CKeyingMaterial plainSecret(sx.spend_secret.begin(),
			                            sx.spend_secret.begin()
			                              + (sx.spend_secret.size() >= 32 ? 32
			                                                              : sx.spend_secret.size()));
			uint256 iv = Hash(sx.spend_pubkey.begin(), sx.spend_pubkey.end());

			std::vector<unsigned char> cryptedSpend;
			if (!EncryptSecret(vNewMasterKey, plainSecret, iv, cryptedSpend))
			{
				LogPrintf("RotateMnemonic FAIL: EncryptSecret failed for stealth address %u\n",
				          (unsigned)stealthCount);
				OPENSSL_cleanse(plainSecret.data(), plainSecret.size());
				OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
				OPENSSL_cleanse(vNewMasterKey.data(), vNewMasterKey.size());
				return false;
			}
			OPENSSL_cleanse(plainSecret.data(), plainSecret.size());

			newStealthSecrets.emplace_back(sx.spend_pubkey, std::move(cryptedSpend));
			++stealthRotated;
		}
		LogPrintf("RotateMnemonic: re-encrypted %u of %u stealth addresses\n",
		          (unsigned)stealthRotated, (unsigned)stealthCount);
	}

	// Step 4: Build the two new CMasterKey envelopes.

	// [a] New password envelope -- same password, same KDF parameters.
	CMasterKey kNewPasswordEntry = kOldPasswordEntry;
	{
		CCrypter crypter;
		if (!crypter.SetKeyFromPassphrase(strCurrentPassword,
				kNewPasswordEntry.vchSalt,
				kNewPasswordEntry.nDeriveIterations,
				kNewPasswordEntry.nDerivationMethod))
		{
			LogPrintf("RotateMnemonic FAIL: SetKeyFromPassphrase for new password envelope\n");
			OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
			OPENSSL_cleanse(vNewMasterKey.data(), vNewMasterKey.size());
			return false;
		}
		if (!crypter.Encrypt(vNewMasterKey, kNewPasswordEntry.vchCryptedKey))
		{
			LogPrintf("RotateMnemonic FAIL: Encrypt for new password envelope\n");
			OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
			OPENSSL_cleanse(vNewMasterKey.data(), vNewMasterKey.size());
			return false;
		}
	}

	// [b] New mnemonic envelope from the new vMasterKey.
	SecureString newMnemonic, newMnemonicHex;
	if (BIP39Passphrase::mnemonicFromVMasterKey(vNewMasterKey, newMnemonic)
	    != BIP39Passphrase::Result::OK)
	{
		LogPrintf("RotateMnemonic FAIL: mnemonicFromVMasterKey\n");
		OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
		OPENSSL_cleanse(vNewMasterKey.data(), vNewMasterKey.size());
		return false;
	}
	if (BIP39Passphrase::passphraseFromMnemonic(newMnemonic, newMnemonicHex)
	    != BIP39Passphrase::Result::OK)
	{
		LogPrintf("RotateMnemonic FAIL: passphraseFromMnemonic (new)\n");
		OPENSSL_cleanse(const_cast<char*>(newMnemonic.data()), newMnemonic.size());
		OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
		OPENSSL_cleanse(vNewMasterKey.data(), vNewMasterKey.size());
		return false;
	}

	CMasterKey kNewMnemonicEntry;
	kNewMnemonicEntry.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
	if (!GetRandBytes(&kNewMnemonicEntry.vchSalt[0], WALLET_CRYPTO_SALT_SIZE))
	{
		LogPrintf("RotateMnemonic FAIL: GetRandBytes for new mnemonic salt\n");
		OPENSSL_cleanse(const_cast<char*>(newMnemonic.data()),    newMnemonic.size());
		OPENSSL_cleanse(const_cast<char*>(newMnemonicHex.data()), newMnemonicHex.size());
		OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
		OPENSSL_cleanse(vNewMasterKey.data(), vNewMasterKey.size());
		return false;
	}
	kNewMnemonicEntry.nDeriveIterations = 25000;
	kNewMnemonicEntry.nDerivationMethod = 0;
	{
		CCrypter crypter;
		if (!crypter.SetKeyFromPassphrase(newMnemonicHex,
				kNewMnemonicEntry.vchSalt,
				kNewMnemonicEntry.nDeriveIterations,
				kNewMnemonicEntry.nDerivationMethod))
		{
			LogPrintf("RotateMnemonic FAIL: SetKeyFromPassphrase for new mnemonic envelope\n");
			OPENSSL_cleanse(const_cast<char*>(newMnemonic.data()),    newMnemonic.size());
			OPENSSL_cleanse(const_cast<char*>(newMnemonicHex.data()), newMnemonicHex.size());
			OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
			OPENSSL_cleanse(vNewMasterKey.data(), vNewMasterKey.size());
			return false;
		}
		if (!crypter.Encrypt(vNewMasterKey, kNewMnemonicEntry.vchCryptedKey))
		{
			LogPrintf("RotateMnemonic FAIL: Encrypt for new mnemonic envelope\n");
			OPENSSL_cleanse(const_cast<char*>(newMnemonic.data()),    newMnemonic.size());
			OPENSSL_cleanse(const_cast<char*>(newMnemonicHex.data()), newMnemonicHex.size());
			OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
			OPENSSL_cleanse(vNewMasterKey.data(), vNewMasterKey.size());
			return false;
		}
	}
	OPENSSL_cleanse(const_cast<char*>(newMnemonicHex.data()), newMnemonicHex.size());
	LogPrintf("RotateMnemonic: built both new master key envelopes\n");

	// Step 5: Commit.  All crypto succeeded; now atomically swap in-memory
	// state and persist to disk.

	if (fFileBacked)
	{
		LogPrintf("RotateMnemonic: starting BDB transaction\n");
		CWalletDB walletdb(strWalletFile);
		if (!walletdb.TxnBegin())
		{
			LogPrintf("RotateMnemonic FAIL: walletdb.TxnBegin\n");
			OPENSSL_cleanse(const_cast<char*>(newMnemonic.data()), newMnemonic.size());
			OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
			OPENSSL_cleanse(vNewMasterKey.data(), vNewMasterKey.size());
			return false;
		}

		bool ok = true;
		const char* failPoint = NULL;

		// Erase old master keys, write new ones.
		for (const auto& mk : mapMasterKeys)
		{
			if (!walletdb.EraseMasterKey(mk.first))
			{
				ok = false;
				failPoint = "EraseMasterKey";
				break;
			}
		}

		// Write password entry (re-using its id).
		if (ok && !walletdb.WriteMasterKey(oldPasswordEntryId, kNewPasswordEntry)) {
			ok = false;
			failPoint = "WriteMasterKey(password)";
		}

		// Write mnemonic entry at a fresh id.
		unsigned int newMnemonicId = nMasterKeyMaxID + 1;
		if (ok && !walletdb.WriteMasterKey(newMnemonicId, kNewMnemonicEntry)) {
			ok = false;
			failPoint = "WriteMasterKey(mnemonic)";
		}

		// Re-write all crypted keys with the new envelope.
		// Note: WriteCryptedKey writes the "ckey" record with overwrite=false
		// (the helper was designed for initial encryption, not re-encryption).
		// During rotation the records already exist, so we must Erase first.
		// keymeta uses overwrite=true so it doesn't need this treatment.
		if (ok)
		{
			for (const auto& mi : newCryptedKeys)
			{
				walletdb.EraseCryptedKey(mi.second.first);

				if (!walletdb.WriteCryptedKey(mi.second.first, mi.second.second,
				                              mapKeyMetadata[mi.first]))
				{
					ok = false;
					failPoint = "WriteCryptedKey";
					break;
				}
			}
		}

		if (!ok || !walletdb.TxnCommit())
		{
			if (!ok) {
				LogPrintf("RotateMnemonic FAIL: BDB write step failed at %s\n",
				          failPoint ? failPoint : "(unknown)");
			} else {
				LogPrintf("RotateMnemonic FAIL: walletdb.TxnCommit\n");
			}
			walletdb.TxnAbort();
			OPENSSL_cleanse(const_cast<char*>(newMnemonic.data()), newMnemonic.size());
			OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
			OPENSSL_cleanse(vNewMasterKey.data(), vNewMasterKey.size());
			return false;
		}

		LogPrintf("RotateMnemonic: BDB transaction committed\n");

		// Update in-memory map of master keys to mirror disk.
		{
			LOCK(cs_wallet);
			mapMasterKeys.clear();
			mapMasterKeys[oldPasswordEntryId] = kNewPasswordEntry;
			mapMasterKeys[newMnemonicId]      = kNewMnemonicEntry;
			if (newMnemonicId > nMasterKeyMaxID)
				nMasterKeyMaxID = newMnemonicId;
		}
	}
	else
	{
		LOCK(cs_wallet);
		mapMasterKeys.clear();
		mapMasterKeys[oldPasswordEntryId]   = kNewPasswordEntry;
		mapMasterKeys[oldPasswordEntryId+1] = kNewMnemonicEntry;
		nMasterKeyMaxID = std::max(nMasterKeyMaxID, oldPasswordEntryId + 1);
	}

	// Step 6: Swap in-memory crypted-keys map and update stealth addresses.
	{
		LOCK(cs_KeyStore);
		mapCryptedKeys = std::move(newCryptedKeys);

		for (auto& pair : newStealthSecrets)
		{
			for (auto it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
			{
				if (it->spend_pubkey == pair.first)
				{
					CStealthAddress& sx = const_cast<CStealthAddress&>(*it);
					sx.spend_secret    = pair.second;
					break;
				}
			}
		}

		// Replace the live vMasterKey so subsequent operations use the new key.
		CCryptoKeyStore::vMasterKey = vNewMasterKey;
	}

	SetRecoveryPhraseFlag();

	// Hand the new mnemonic back to the caller.
	newMnemonicOut = newMnemonic;
	OPENSSL_cleanse(const_cast<char*>(newMnemonic.data()), newMnemonic.size());

	OPENSSL_cleanse(vOldMasterKey.data(), vOldMasterKey.size());
	// vNewMasterKey is now in CCryptoKeyStore::vMasterKey -- do not clear.

	LogPrintf("RotateMnemonic: SUCCESS\n");
	return true;
}

bool CWallet::HasRecoveryPhraseFlag() const
{
	if (!fFileBacked)
		return false;
	return CWalletDB(strWalletFile).HasRecoveryPhraseFlag();
}

void CWallet::SetRecoveryPhraseFlag()
{
	if (fFileBacked)
		CWalletDB(strWalletFile).WriteRecoveryPhraseFlag();
}

// NOTE: HasRecoveryPhraseUpgradeDeclined / SetRecoveryPhraseUpgradeDeclined /
// NeedsRecoveryPhraseUpgrade have moved out of CWallet.  The dismissal flag
// is a UI preference (per-wallet, stored in QSettings via src/qt/guistate.h)
// rather than wallet data, and the upgrade decision itself lives in
// WalletModel::needsRecoveryPhraseUpgrade() in the Qt layer.  The daemon-only
// build no longer needs to know about the prompt at all.
//
// The walletdb-level Has/Set/Erase helpers in walletdb.cpp are retained but
// unused, on the principle that removing them is churn without benefit.  Any
// stale "recovery_phrase_upgrade_declined" record left in a tester's
// wallet.dat is silently ignored by future loads.

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

void CWallet::MarkAllTxCachesDirty()
{
	// Defensive cache invalidation: any keystore change can flip IsMine
	// for previously-loaded txes (e.g. importprivkey makes outputs that
	// were previously not-mine become spendable). The fAvailableCreditCached
	// / fCreditCached / etc. fields on CWalletTx do not auto-invalidate on
	// keystore changes, so we do it eagerly here.
	//
	// During initial wallet load, this is a no-op: every wtx loaded after
	// the key change gets fresh caches via BindWallet() anyway, and the
	// fWalletLoadComplete gate at GUI poll callbacks prevents premature
	// reads of stale caches for txes loaded BEFORE the key change.
	//
	// After load, this fires for every importprivkey / importaddress /
	// addwatchonly / wallet-unlock etc. and walks all of mapWallet -- 8
	// boolean writes per tx, sub-second on any wallet size.
	if (!fWalletLoadComplete)
	{
		return;
	}

	LOCK(cs_wallet);

	for (std::pair<const uint256, CWalletTx>& item : mapWallet)
	{
		item.second.MarkDirty();
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

			// Register this tx's inputs with the spends index so that
			// IsSpent() correctly identifies prior outputs as consumed.
			// Previously this was only called in the wallet-load path
			// (fFromLoadWallet branch), which meant mmTxSpends was empty
			// for any tx added during a rescan or live operation -- and
			// IsSpent therefore returned false for outputs that had in
			// fact been spent.  Symptom: watch-only balance during a
			// fresh import of an active address summed to roughly the
			// total ever received rather than the current unspent
			// balance.  The bug self-healed on restart because wallet
			// load re-populated mmTxSpends from scratch.
			AddToSpends(hash);

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
					// If the block is genuinely older than any tx we already
					// have (latestEntry), this is almost certainly a rescan
					// discovering historical transactions (e.g. importaddress
					// rescan).  Use the blocktime directly rather than
					// clamping UP to the most recent existing tx's time --
					// otherwise all rescan-discovered txs end up timestamped
					// at the most recent existing tx, which is wrong.
					if (blocktime < latestEntry)
					{
						wtx.nTimeSmart = blocktime;
					}
					else
					{
						wtx.nTimeSmart = std::max(latestEntry, std::min(blocktime, latestNow));
					}
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

	// Tell the wallet model to start queueing transaction notifications
	// rather than dispatching them immediately to the main thread.
	// Without this, a heavy rescan (e.g. importaddress on an address
	// with thousands of transactions) floods the Qt event queue with
	// per-tx invokeMethod calls and toast notifications, hanging the
	// main thread.  ShowProgress(100) at the end drains the queue with
	// at-most-10-balloons safety to prevent toast spam.
	ShowProgress(ui_translate("Rescanning..."), 0);

	// Determine total blocks for progress percentage.
	int nStartHeight = pindexStart ? pindexStart->nHeight : 0;
	int nEndHeight = pindexBest ? pindexBest->nHeight : nStartHeight;
	int nTotal = std::max(1, nEndHeight - nStartHeight);

	// Splash feedback during init-time rescan.  The ShowProgress signal
	// above goes to wallet->ShowProgress listeners, but during
	// init.cpp's startup -rescan path no GUI listener is wired yet
	// (subscribeToCoreSignals runs from the WalletModel constructor,
	// AFTER AppInit2 returns).  uiInterface.InitMessage paints
	// synchronously to splashref while the splash is alive and is a
	// harmless no-op once it's torn down, so the same call serves both
	// the startup-rescan and runtime-import-rescan paths cheaply.
	uiInterface.InitMessage(strprintf(ui_translate("Rescanning... 0 / %d"), nTotal));

	{
		LOCK2(cs_main, cs_wallet);
		
		unsigned int nBlocksScanned = 0;

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

			// Periodic progress (also refreshes splash if visible).
			// 1..99 keeps the queue active (only 0 / 100 are special).
			if ((++nBlocksScanned % 5000) == 0 && pindex)
			{
				int pct = std::max(1, std::min(99, (pindex->nHeight - nStartHeight) * 100 / nTotal));
				ShowProgress(strprintf(ui_translate("Rescanning... block %d"), pindex->nHeight), pct);
				// Splash mirror — see entry-point comment above.
				uiInterface.InitMessage(strprintf(
					ui_translate("Rescanning... block %d / %d"),
					pindex->nHeight, nEndHeight));
			}
		}
	}

	// Drain the queued notifications.  This dispatches at most 10 toast
	// balloons (per the existing batch logic in walletmodel.cpp /
	// transactiontablemodel.cpp) and updates the UI for the rest in
	// silent mode.
	ShowProgress("", 100);

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

			// Coinstakes are never valid as loose mempool entries; they
			// must arrive as the second transaction of a PoS block.  The
			// pre-fix code only excluded coinbase here, so every orphaned
			// coinstake (nDepth < 0) was pushed through AcceptToMemoryPool
			// and got rejected with "coinstake as individual tx", spamming
			// the debug log with ~one error per coinstake at every launch.
			// The check at line 3005 below already treats coinbase and
			// coinstake symmetrically; mirror that here.
			if (!wtx.IsCoinBase() && !wtx.IsCoinStake() && nDepth < 0)
			{
				// Try to add to memory pool
				LOCK(mempool.cs);
				
				wtx.AcceptToMemoryPool(false);
			}
			
			// v2.0.0.8 CW4 Fix C: mmTxSpends-based reader (extract hash once for reuse below)
			const uint256 wtxHash = wtx.GetHash();
			if ((wtx.IsCoinBase() && this->IsSpent(wtxHash, 0)) || (wtx.IsCoinStake() && this->IsSpent(wtxHash, 1)))
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
					if (this->IsSpent(wtxHash, i))   // v2.0.0.8 CW4 Fix C: mmTxSpends-based reader (wtxHash from above)
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
			// FIX: was ISMINE_ALL, which included watch-only stake -- the
			// "Spendable Stake" column on the dashboard then showed
			// watch-only stake added to spendable (and matched the
			// "Watch-only Stake" column exactly when the wallet had no
			// real spendable stake).  Watch-only stake is reported
			// separately by GetWatchOnlyStake().
			nTotal += CWallet::GetCredit(*pcoin, ISMINE_SPENDABLE);
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
			// FIX: was ISMINE_ALL, which would include watch-only mining
			// rewards in the wallet's own immature mint count.  Same bug
			// pattern as GetStake() above.
			nTotal += CWallet::GetCredit(*pcoin, ISMINE_SPENDABLE);
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

	// v2.0.0.8 CW9: route both mainnet and testnet through the height-based
	// ladder.  Producer asks the ladder about the height of the block being
	// mined (pindexBest->nHeight + 1), not the tip -- off-by-one fix.
	CBitcoinAddress devopaddress;
	if (Params().NetworkID() == CChainParams_Network::MAIN ||
	    Params().NetworkID() == CChainParams_Network::TESTNET)
	{
		devopaddress = CBitcoinAddress(
			getDevelopersAdressForHeight(
				pindexBest->nHeight + 1,
				GetAdjustedTime()
			)
		);
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
		// v2.0.0.8 M5: route through GetEnforcedPayee.  See PoW counterpart
		// in miner.cpp for full rationale -- creator must agree with
		// validator post-activation.  Also replaces the
		// GetCurrentMasterNode(1) fallback with FindOldestNotInVec (same
		// fix as miner.cpp's PoW path got in M3p5).
		if(!GetEnforcedPayee(pindexPrev->nHeight+1, payee, vin))
		{
			CMasternode* pmn = mnodeman.FindOldestNotInVec(std::vector<CTxIn>(), 0);

			if(pmn)
			{
				payee = GetScriptForDestination(pmn->pubkey.GetID());
			}
			else
			{
				payee = GetScriptForDestination(devopaddress.Get());
			}
		}
		else
		{
			// v2.0.0.8 Mechanism 2: GetEnforcedPayee returned a consensus
			// winner, but the winner may have gone offline inside the
			// recast window.  See miner.cpp PoW counterpart for full
			// rationale.  This site mirrors that one -- the predicate
			// symmetry (builder uses the same reachability test as the
			// validator's weak check) is what closes the C3 stall.
			CTxDestination addrDest;
			ExtractDestination(payee, addrDest);
			CBitcoinAddress addrOut(addrDest);
			// v2.0.0.8 CW9: ask the ladder about the block being mined,
			// not the tip.
			std::string strDevopsAddress = getDevelopersAdressForHeight(
				pindexPrev->nHeight + 1,
				GetAdjustedTime()
			);

			if (!mnodeman.IsPayeeAValidMasternode(payee) &&
				addrOut.ToString() != strDevopsAddress)
			{
				LogPrintf("NOTICE - voted consensus winner for height %d "
						  "(%s) is not in local list; falling back to "
						  "legacy payee selection\n",
						  pindexPrev->nHeight + 1,
						  addrOut.ToString().c_str());

				// Demote to the legacy path -- same as the no-consensus
				// branch above.
				payee = CScript();
				vin = CTxIn();

				if (!masternodePayments.GetBlockPayee(pindexPrev->nHeight + 1, payee, vin))
				{
					CMasternode* pmn = mnodeman.FindOldestNotInVec(std::vector<CTxIn>(), 0);

					if(pmn)
					{
						payee = GetScriptForDestination(pmn->pubkey.GetID());
					}
					else
					{
						payee = GetScriptForDestination(devopaddress.Get());
					}
				}
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
				
				int64_t n = this->IsSpent(pcoin->GetHash(), i) ? 0 : pcoin->vout[i].nValue;   // v2.0.0.8 CW4 Fix C: mmTxSpends-based reader

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
	if (DigitalNote::Webwallet::ext_mode)
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
		(::IsMine(*this, address) & ISMINE_SPENDABLE) != 0,
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
	if (!DigitalNote::Webwallet::ext_mode)
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

	NotifyAddressBookChanged(this, address, "", (::IsMine(*this, address) & ISMINE_SPENDABLE) != 0, CT_DELETED);

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

