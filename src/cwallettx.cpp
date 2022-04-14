#include "compat.h"

#include "cinv.h"
#include "cwallet.h"
#include "cblockindex.h"
#include "mining.h"
#include "txdb-leveldb.h"
#include "instantx.h"
#include "walletdb.h"
#include "wallet.h"
#include "script.h"
#include "net.h"
#include "serialize.h"
#include "ctxmempool.h"
#include "main_extern.h"
#include "ctxout.h"
#include "ctxin.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "thread.h"
#include "util.h"

#include "cwallettx.h"

CWalletTx::CWalletTx()
{
	Init(NULL);
}

CWalletTx::CWalletTx(const CWallet* pwalletIn)
{
	Init(pwalletIn);
}

CWalletTx::CWalletTx(const CWallet* pwalletIn, const CMerkleTx& txIn) : CMerkleTx(txIn)
{
	Init(pwalletIn);
}

CWalletTx::CWalletTx(const CWallet* pwalletIn, const CTransaction& txIn) : CMerkleTx(txIn)
{
	Init(pwalletIn);
}

unsigned int CWalletTx::GetSerializeSize(int nType, int nVersion) const
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
	
	CWalletTx* pthis = const_cast<CWalletTx*>(this);
	
	if (fRead)
	{
		pthis->Init(NULL);
	}
	
	char fSpent = false;

	if (!fRead)
	{
		pthis->mapValue["fromaccount"] = pthis->strFromAccount;

		std::string str;
		
		for(char f : vfSpent)
		{
			str += (f ? '1' : '0');
			
			if (f)
			{
				fSpent = true;
			}
		}
		
		pthis->mapValue["spent"] = str;

		WriteOrderPos(pthis->nOrderPos, pthis->mapValue);

		if (nTimeSmart)
		{
			pthis->mapValue["timesmart"] = strprintf("%u", nTimeSmart);
		}
	}

	nSerSize += SerReadWrite(s, *(CMerkleTx*)this, nType, nVersion,ser_action);
	
	READWRITE(vtxPrev);
	READWRITE(mapValue);
	READWRITE(vOrderForm);
	READWRITE(fTimeReceivedIsTxTime);
	READWRITE(nTimeReceived);
	READWRITE(fFromMe);
	READWRITE(fSpent);

	if (fRead)
	{
		pthis->strFromAccount = pthis->mapValue["fromaccount"];

		if (mapValue.count("spent"))
		{
			for(char c : pthis->mapValue["spent"])
			{
				pthis->vfSpent.push_back(c != '0');
			}
		}
		else
		{
			pthis->vfSpent.assign(vout.size(), fSpent);
		}
		
		ReadOrderPos(pthis->nOrderPos, pthis->mapValue);

		pthis->nTimeSmart = mapValue.count("timesmart") ? (unsigned int)atoi64(pthis->mapValue["timesmart"]) : 0;
	}

	pthis->mapValue.erase("fromaccount");
	pthis->mapValue.erase("version");
	pthis->mapValue.erase("spent");
	pthis->mapValue.erase("n");
	pthis->mapValue.erase("timesmart");
	
	return nSerSize;
}

template<typename Stream>
void CWalletTx::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	CWalletTx* pthis = const_cast<CWalletTx*>(this);
	
	if (fRead)
	{
		pthis->Init(NULL);
	}
	
	char fSpent = false;

	if (!fRead)
	{
		pthis->mapValue["fromaccount"] = pthis->strFromAccount;

		std::string str;
		
		for(char f : vfSpent)
		{
			str += (f ? '1' : '0');
			
			if (f)
			{
				fSpent = true;
			}
		}
		
		pthis->mapValue["spent"] = str;

		WriteOrderPos(pthis->nOrderPos, pthis->mapValue);

		if (nTimeSmart)
		{
			pthis->mapValue["timesmart"] = strprintf("%u", nTimeSmart);
		}
	}

	nSerSize += SerReadWrite(s, *(CMerkleTx*)this, nType, nVersion,ser_action);
	
	READWRITE(vtxPrev);
	READWRITE(mapValue);
	READWRITE(vOrderForm);
	READWRITE(fTimeReceivedIsTxTime);
	READWRITE(nTimeReceived);
	READWRITE(fFromMe);
	READWRITE(fSpent);

	if (fRead)
	{
		pthis->strFromAccount = pthis->mapValue["fromaccount"];

		if (mapValue.count("spent"))
		{
			for(char c : pthis->mapValue["spent"])
			{
				pthis->vfSpent.push_back(c != '0');
			}
		}
		else
		{
			pthis->vfSpent.assign(vout.size(), fSpent);
		}
		
		ReadOrderPos(pthis->nOrderPos, pthis->mapValue);

		pthis->nTimeSmart = mapValue.count("timesmart") ? (unsigned int)atoi64(pthis->mapValue["timesmart"]) : 0;
	}

	pthis->mapValue.erase("fromaccount");
	pthis->mapValue.erase("version");
	pthis->mapValue.erase("spent");
	pthis->mapValue.erase("n");
	pthis->mapValue.erase("timesmart");
}

template<typename Stream>
void CWalletTx::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	CWalletTx* pthis = const_cast<CWalletTx*>(this);
	
	if (fRead)
	{
		pthis->Init(NULL);
	}
	
	char fSpent = false;

	if (!fRead)
	{
		pthis->mapValue["fromaccount"] = pthis->strFromAccount;

		std::string str;
		
		for(char f : vfSpent)
		{
			str += (f ? '1' : '0');
			
			if (f)
			{
				fSpent = true;
			}
		}
		
		pthis->mapValue["spent"] = str;

		WriteOrderPos(pthis->nOrderPos, pthis->mapValue);

		if (nTimeSmart)
		{
			pthis->mapValue["timesmart"] = strprintf("%u", nTimeSmart);
		}
	}

	nSerSize += SerReadWrite(s, *(CMerkleTx*)this, nType, nVersion,ser_action);
	
	READWRITE(vtxPrev);
	READWRITE(mapValue);
	READWRITE(vOrderForm);
	READWRITE(fTimeReceivedIsTxTime);
	READWRITE(nTimeReceived);
	READWRITE(fFromMe);
	READWRITE(fSpent);

	if (fRead)
	{
		pthis->strFromAccount = pthis->mapValue["fromaccount"];

		if (mapValue.count("spent"))
		{
			for(char c : pthis->mapValue["spent"])
			{
				pthis->vfSpent.push_back(c != '0');
			}
		}
		else
		{
			pthis->vfSpent.assign(vout.size(), fSpent);
		}
		
		ReadOrderPos(pthis->nOrderPos, pthis->mapValue);

		pthis->nTimeSmart = mapValue.count("timesmart") ? (unsigned int)atoi64(pthis->mapValue["timesmart"]) : 0;
	}

	pthis->mapValue.erase("fromaccount");
	pthis->mapValue.erase("version");
	pthis->mapValue.erase("spent");
	pthis->mapValue.erase("n");
	pthis->mapValue.erase("timesmart");
}

template void CWalletTx::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CWalletTx::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

void CWalletTx::Init(const CWallet* pwalletIn)
{
	pwallet = pwalletIn;
	vtxPrev.clear();
	mapValue.clear();
	vOrderForm.clear();
	fTimeReceivedIsTxTime = false;
	nTimeReceived = 0;
	nTimeSmart = 0;
	fFromMe = false;
	strFromAccount.clear();
	vfSpent.clear();
	fDebitCached = false;
	fCreditCached = false;
	fImmatureCreditCached = false;
	fAvailableCreditCached = false;
	fWatchDebitCached = false;
	fWatchCreditCached = false;
	fImmatureWatchCreditCached = false;
	fAvailableWatchCreditCached = false;
	fChangeCached = false;
	nDebitCached = 0;
	nCreditCached = 0;
	nAvailableCreditCached = 0;
	nWatchDebitCached = 0;
	nWatchCreditCached = 0;
	nAvailableWatchCreditCached = 0;
	nImmatureWatchCreditCached = 0;
	nChangeCached = 0;
	nOrderPos = -1;
}

// marks certain txout's as spent
// returns true if any update took place
bool CWalletTx::UpdateSpent(const std::vector<char>& vfNewSpent)
{
	bool fReturn = false;
	
	for (unsigned int i = 0; i < vfNewSpent.size(); i++)
	{
		if (i == vfSpent.size())
		{
			break;
		}

		if (vfNewSpent[i] && !vfSpent[i])
		{
			vfSpent[i] = true;
			fReturn = true;
			fAvailableCreditCached = false;
		}
	}
	
	return fReturn;
}

// make sure balances are recalculated
void CWalletTx::MarkDirty()
{
	fCreditCached = false;
	fAvailableCreditCached = false;
	fWatchDebitCached = false;
	fWatchCreditCached = false;
	fAvailableWatchCreditCached = false;
	fImmatureWatchCreditCached = false;
	fDebitCached = false;
	fChangeCached = false;
}

void CWalletTx::BindWallet(CWallet *pwalletIn)
{
	pwallet = pwalletIn;
	MarkDirty();
}

void CWalletTx::MarkSpent(unsigned int nOut)
{
	if (nOut >= vout.size())
	{
		throw std::runtime_error("CWalletTx::MarkSpent() : nOut out of range");
	}
	
	vfSpent.resize(vout.size());
	
	if (!vfSpent[nOut])
	{
		vfSpent[nOut] = true;
		fAvailableCreditCached = false;
	}
}

void CWalletTx::MarkUnspent(unsigned int nOut)
{
	if (nOut >= vout.size())
	{
		throw std::runtime_error("CWalletTx::MarkUnspent() : nOut out of range");
	}
	
	vfSpent.resize(vout.size());
	
	if (vfSpent[nOut])
	{
		vfSpent[nOut] = false;
		fAvailableCreditCached = false;
	}
}

bool CWalletTx::IsSpent(unsigned int nOut) const
{
	if (nOut >= vout.size())
	{
		throw std::runtime_error("CWalletTx::IsSpent() : nOut out of range");
	}
	
	if (nOut >= vfSpent.size())
	{
		return false;
	}
	
	return (!!vfSpent[nOut]);
}

CAmount CWalletTx::GetDebit(const isminefilter& filter) const
{
	if (vin.empty())
	{
		return 0;
	}
	
	CAmount debit = 0;
	
	if(filter & ISMINE_SPENDABLE)
	{
		if (fDebitCached)
		{
			debit += nDebitCached;
		}
		else
		{
			nDebitCached = pwallet->GetDebit(*this, ISMINE_SPENDABLE);
			fDebitCached = true;
			debit += nDebitCached;
		}
	}
	
	if(filter & ISMINE_WATCH_ONLY)
	{
		if(fWatchDebitCached)
		{
			debit += nWatchDebitCached;
		}
		else
		{
			nWatchDebitCached = pwallet->GetDebit(*this, ISMINE_WATCH_ONLY);
			fWatchDebitCached = true;
			debit += nWatchDebitCached;
		}
	}
	
	return debit;
}

CAmount CWalletTx::GetCredit(const isminefilter& filter) const
{
	// Must wait until coinbase is safely deep enough in the chain before valuing it
	if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0)
	{
		return 0;
	}
	
	int64_t credit = 0;
	
	if (filter & ISMINE_SPENDABLE)
	{
		// GetBalance can assume transactions in mapWallet won't change
		if (fCreditCached)
		{
			credit += nCreditCached;
		}
		else
		{
			nCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE);
			fCreditCached = true;
			credit += nCreditCached;
		}
	}
	
	if (filter & ISMINE_WATCH_ONLY)
	{
		if (fWatchCreditCached)
		{
			credit += nWatchCreditCached;
		}
		else
		{
			nWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY);
			fWatchCreditCached = true;
			credit += nWatchCreditCached;
		}
	}
	
	return credit;
}

CAmount CWalletTx::GetImmatureCredit(bool fUseCache) const
{
	if (IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain())
	{
		if (fUseCache && fImmatureCreditCached)
		{
			return nImmatureCreditCached;
		}
		
		nImmatureCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE);
		fImmatureCreditCached = true;
		
		return nImmatureCreditCached;
	}

	return 0;
}

CAmount CWalletTx::GetAvailableCredit(bool fUseCache) const
{
	if (pwallet == 0)
	{
		return 0;
	}
	
	// Must wait until coinbase is safely deep enough in the chain before valuing it
	if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0)
	{
		return 0;
	}
	
	if (fUseCache && fAvailableCreditCached)
	{
		return nAvailableCreditCached;
	}
	
	CAmount nCredit = 0;
	
	for (unsigned int i = 0; i < vout.size(); i++)
	{
		if (!IsSpent(i))
		{
			const CTxOut &txout = vout[i];
			nCredit += pwallet->GetCredit(txout, ISMINE_SPENDABLE);
			
			if (!MoneyRange(nCredit))
			{
				throw std::runtime_error("CWalletTx::GetAvailableCredit() : value out of range");
			}
		}
	}

	nAvailableCreditCached = nCredit;
	fAvailableCreditCached = true;
	
	return nCredit;
}

/**
	Since 1.0.0 version jan 2019 this function could never have been used.
	Because CWallet::GetInputMNengineRounds got removed for some reason?!
*/
/*
CAmount CWalletTx::GetAnonymizableCredit(bool fUseCache) const
{
	if (pwallet == 0)
		return 0;

	// Must wait until coinbase is safely deep enough in the chain before valuing it
	if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0)
		return 0;

	if (fUseCache && fAnonymizableCreditCached)
		return nAnonymizableCreditCached;

	CAmount nCredit = 0;
	uint256 hashTx = GetHash();
	for (unsigned int i = 0; i < vout.size(); i++)
	{
		const CTxOut &txout = vout[i];
		const CTxIn vin = CTxIn(hashTx, i);

		if(pwallet->IsSpent(hashTx, i) || pwallet->IsLockedCoin(hashTx, i)) continue;
		if(fMasterNode && vout[i].nValue == MasternodeCollateral(pindexBest->nHeight)*COIN) continue; // do not count MN-like outputs

		const int rounds = pwallet->GetInputMNengineRounds(vin);
		if(rounds >=-2 && rounds < nMNengineRounds) {
			nCredit += pwallet->GetCredit(txout, ISMINE_SPENDABLE);
			if (!MoneyRange(nCredit))
				throw std::runtime_error("CWalletTx::GetAnonamizableCredit() : value out of range");
		}
	}

	nAnonymizableCreditCached = nCredit;
	fAnonymizableCreditCached = true;
	return nCredit;
}
*/

CAmount CWalletTx::GetImmatureWatchOnlyCredit(const bool& fUseCache) const
{
	if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0 && IsInMainChain())
	{
		if (fUseCache && fImmatureWatchCreditCached)
		{
			return nImmatureWatchCreditCached;
		}
		
		nImmatureWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY);
		fImmatureWatchCreditCached = true;
		
		return nImmatureWatchCreditCached;
	}

	return 0;
}

CAmount CWalletTx::GetAvailableWatchOnlyCredit(const bool& fUseCache) const
{
	if (pwallet == 0)
	{
		return 0;
	}
	
	// Must wait until coinbase is safely deep enough in the chain before valuing it
	if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0)
	{
		return 0;
	}
	
	if (fUseCache && fAvailableWatchCreditCached)
	{
		return nAvailableWatchCreditCached;
	}
	
	CAmount nCredit = 0;
	uint256 hashTx = GetHash();
	
	for (unsigned int i = 0; i < vout.size(); i++)
	{
		if (!pwallet->IsSpent(hashTx, i))
		{
			const CTxOut &txout = vout[i];
			
			nCredit += pwallet->GetCredit(txout, ISMINE_WATCH_ONLY);
			
			if (!MoneyRange(nCredit))
			{
				throw std::runtime_error("CWalletTx::GetAvailableCredit() : value out of range");
			}
		}
	}

	nAvailableWatchCreditCached = nCredit;
	fAvailableWatchCreditCached = true;
	
	return nCredit;
}

CAmount CWalletTx::GetChange() const
{
	if (fChangeCached)
	{
		return nChangeCached;
	}
	
	nChangeCached = pwallet->GetChange(*this);
	fChangeCached = true;
	
	return nChangeCached;
}

void CWalletTx::GetAmounts(std::list<std::pair<CTxDestination, int64_t> >& listReceived,
		std::list<std::pair<CTxDestination, int64_t> >& listSent, CAmount& nFee, std::string& strSentAccount,
		const isminefilter& filter) const
{
	LOCK(pwallet->cs_wallet);

	nFee = 0;
	listReceived.clear();
	listSent.clear();
	strSentAccount = strFromAccount;

	// Compute fee:
	CAmount nDebit = GetDebit(filter);
	
	if (nDebit > 0) // debit>0 means we signed/sent this transaction
	{
		CAmount nValueOut = GetValueOut();
		nFee = nDebit - nValueOut;
	}

	// Sent/received.
	for(const CTxOut& txout : vout)
	{
		// Skip special stake out
		if (txout.scriptPubKey.empty())
		{
			continue;
		}
		
		isminetype fIsMine = pwallet->IsMine(txout);
		
		// Only need to handle txouts if AT LEAST one of these is true:
		//   1) they debit from us (sent)
		//   2) the output is to us (received)
		if (nDebit > 0)
		{
			// Don't report 'change' txouts
		// TXNOTE: CoinControl possible fix related... with HD wallet we need to report change?
			if (pwallet->IsChange(txout))
			{
				continue;
			}
		}
		else if (!(fIsMine & filter))
		{
			continue;
		}
		
		// In either case, we need to get the destination address
		CTxDestination address;
		if (!ExtractDestination(txout.scriptPubKey, address))
		{
			LogPrintf(
				"CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n",
				this->GetHash().ToString()
			);
			
			address = CNoDestination();
		}

		// If we are debited by the transaction, add the output as a "sent" entry
		if (nDebit > 0)
		{
			listSent.push_back(std::make_pair(address, txout.nValue));
		}
		
		// If we are receiving the output, add it as a "received" entry
		if (fIsMine & filter)
		{
			listReceived.push_back(std::make_pair(address, txout.nValue));
		}
	}
}

void CWalletTx::GetAccountAmounts(const std::string& strAccount, CAmount& nReceived, CAmount& nSent,
		CAmount& nFee, const isminefilter& filter) const
{
	nReceived = nSent = nFee = 0;

	CAmount allFee;
	std::string strSentAccount;
	std::list<std::pair<CTxDestination, int64_t> > listReceived;
	std::list<std::pair<CTxDestination, int64_t> > listSent;

	GetAmounts(listReceived, listSent, allFee, strSentAccount, filter);

	if (strAccount == strSentAccount)
	{
		for(const std::pair<CTxDestination,int64_t>& s : listSent)
		{
			nSent += s.second;
		}
		
		nFee = allFee;
	}

	{
		LOCK(pwallet->cs_wallet);
		
		for(const std::pair<CTxDestination,int64_t>& r : listReceived)
		{
			if (pwallet->mapAddressBook.count(r.first))
			{
				mapAddressBook_t::const_iterator mi = pwallet->mapAddressBook.find(r.first);
				
				if (mi != pwallet->mapAddressBook.end() && (*mi).second == strAccount)
				{
					nReceived += r.second;
				}
			}
			else if (strAccount.empty())
			{
				nReceived += r.second;
			}
		}
	}
}

bool CWalletTx::IsFromMe(const isminefilter& filter) const
{
	return (GetDebit(filter) > 0);
}

bool CWalletTx::IsTrusted() const
{
	// Quick answer in most cases
	if (!IsFinalTx(*this))
	{
		return false;
	}

	int nDepth = GetDepthInMainChain();
	if (nDepth >= 1)
	{
		return true;
	}

	if (nDepth < 0)
	{
		return false;
	}

	if (fConfChange || !IsFromMe(ISMINE_ALL))// using wtx's cached debit
	{
		return false;
	}

	// Trusted if all inputs are from us and are in the mempool:
	for(const CTxIn& txin : vin)
	{
		// Transactions not sent by us: not trusted
		const CWalletTx* parent = pwallet->GetWalletTx(txin.prevout.hash);
		
		if (parent == NULL)
		{
			return false;
		}
		
		const CTxOut& parentOut = parent->vout[txin.prevout.n];
		if (pwallet->IsMine(parentOut) != ISMINE_SPENDABLE)
		{
			return false;
		}
	}

	return true;
}

bool CWalletTx::WriteToDisk()
{
	return CWalletDB(pwallet->strWalletFile).WriteTx(GetHash(), *this);
}

int64_t CWalletTx::GetTxTime() const
{
	int64_t n = nTimeSmart;

	return n ? n : nTimeReceived;
}

int CWalletTx::GetRequestCount() const
{
	// Returns -1 if it wasn't being tracked
	int nRequests = -1;

	{
		LOCK(pwallet->cs_wallet);
		
		if (IsCoinBase() || IsCoinStake())
		{
			// Generated block
			if (hashBlock != 0)
			{
				mapRequestCount_t::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
				
				if (mi != pwallet->mapRequestCount.end())
				{
					nRequests = (*mi).second;
				}
			}
		}
		else
		{
			// Did anyone request this transaction?
			mapRequestCount_t::const_iterator mi = pwallet->mapRequestCount.find(GetHash());
			
			if (mi != pwallet->mapRequestCount.end())
			{
				nRequests = (*mi).second;

				// How about the block it's in?
				if (nRequests == 0 && hashBlock != 0)
				{
					mapRequestCount_t::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
					
					if (mi != pwallet->mapRequestCount.end())
					{
						nRequests = (*mi).second;
					}
					else
					{
						nRequests = 1; // If it's in someone else's block it must have got out
					}
				}
			}
		}
	}

	return nRequests;
}

void CWalletTx::AddSupportingTransactions(CTxDB& txdb)
{
	vtxPrev.clear();

	const int COPY_DEPTH = 3;

	if (SetMerkleBranch() < COPY_DEPTH)
	{
		std::vector<uint256> vWorkQueue;
		
		for(const CTxIn& txin : vin)
		{
			vWorkQueue.push_back(txin.prevout.hash);
		}
		
		// This critsect is OK because txdb is already open
		{
			LOCK(pwallet->cs_wallet);
			
			std::map<uint256, const CMerkleTx*> mapWalletPrev;
			std::set<uint256> setAlreadyDone;
			
			for (unsigned int i = 0; i < vWorkQueue.size(); i++)
			{
				uint256 hash = vWorkQueue[i];
				
				if (setAlreadyDone.count(hash))
				{
					continue;
				}
				
				setAlreadyDone.insert(hash);

				CMerkleTx tx;
				mapWallet_t::const_iterator mi = pwallet->mapWallet.find(hash);
				
				if (mi != pwallet->mapWallet.end())
				{
					tx = (*mi).second;
					
					for(const CMerkleTx& txWalletPrev : (*mi).second.vtxPrev)
					{
						mapWalletPrev[txWalletPrev.GetHash()] = &txWalletPrev;
					}
				}
				else if (mapWalletPrev.count(hash))
				{
					tx = *mapWalletPrev[hash];
				}
				//else if (txdb.ReadDiskTx(hash, tx))
				//{
				//
				//}
				else
				{
					LogPrintf("ERROR: AddSupportingTransactions() : unsupported transaction\n");
					continue;
				}

				int nDepth = tx.SetMerkleBranch();
				vtxPrev.push_back(tx);

				if (nDepth < COPY_DEPTH)
				{
					for(const CTxIn& txin : tx.vin)
					{
						vWorkQueue.push_back(txin.prevout.hash);
					}
				}
			}
		}
	}

	reverse(vtxPrev.begin(), vtxPrev.end());
}

bool CWalletTx::AcceptWalletTransaction(CTxDB& txdb)
{
	{
		// Add previous supporting transactions first
		for(CMerkleTx& tx : vtxPrev)
		{
			if (!(tx.IsCoinBase() || tx.IsCoinStake()))
			{
				uint256 hash = tx.GetHash();
				
				if (!mempool.exists(hash) && !txdb.ContainsTx(hash))
				{
					tx.AcceptToMemoryPool(false);
				}
			}
		}
		
		return AcceptToMemoryPool(false);
	}

	return false;
}

bool CWalletTx::AcceptWalletTransaction()
{
	CTxDB txdb("r");

	return AcceptWalletTransaction(txdb);
}

void CWalletTx::RelayWalletTransaction(CTxDB& txdb, std::string strCommand)
{
	if (!(IsCoinBase() || IsCoinStake()))
	{
		if (GetDepthInMainChain() == 0)
		{
			uint256 hash = GetHash();
			
			if(strCommand == "txlreq")
			{
				LogPrintf("Relaying txlreq %s\n", hash.ToString());
				
				mapTxLockReq.insert(std::make_pair(hash, ((CTransaction)*this)));
				CreateNewLock(((CTransaction)*this));
				RelayTransactionLockReq((CTransaction)*this, true);
			}
			else
			{
				LogPrintf("Relaying wtx %s\n", hash.ToString());
				
				RelayTransaction((CTransaction)*this, hash);
			}
		}
	}
}

void CWalletTx::RelayWalletTransaction(std::string strCommand)
{
	CTxDB txdb("r");

	RelayWalletTransaction(txdb, strCommand);
}

std::set<uint256> CWalletTx::GetConflicts() const
{
	std::set<uint256> result;

	if (pwallet != NULL)
	{
		uint256 myHash = GetHash();
		
		result = pwallet->GetConflicts(myHash);
		
		result.erase(myHash);
	}

	return result;
}

/**
	Extra functions
*/
void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue)
{
	if (!mapValue.count("n"))
	{
		nOrderPos = -1; // TODO: calculate elsewhere
		
		return;
	}

	nOrderPos = atoi64(mapValue["n"].c_str());
}

void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue)
{
	if (nOrderPos == -1)
	{
		return;
	}

	mapValue["n"] = i64tostr(nOrderPos);
}

