#ifndef CWALLETTX_H
#define CWALLETTX_H

#include <string>
#include <vector>
#include <set>
#include <list>
#include <utility>

#include "cmerkletx.h"
#include "types/isminefilter.h"
#include "types/mapvalue_t.h"
#include "types/camount.h"
#include "enums/changetype.h"
#include "types/ctxdestination.h"

class CWallet;

void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue);
void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue);

/** A transaction with a bunch of additional info that only the owner cares about.
 * It includes any unrecorded transactions needed to link it back to the block chain.
 */
class CWalletTx : public CMerkleTx
{
private:
	const CWallet* pwallet;

public:
	std::vector<CMerkleTx> vtxPrev;
	mapValue_t mapValue;
	std::vector<std::pair<std::string, std::string> > vOrderForm;
	unsigned int fTimeReceivedIsTxTime;
	unsigned int nTimeReceived;  // time received by this node
	unsigned int nTimeSmart;
	char fFromMe;
	std::string strFromAccount;
	std::vector<char> vfSpent; // which outputs are already spent
	int64_t nOrderPos;  // position in ordered transaction list

	// memory only
	mutable bool fDebitCached;
	mutable bool fCreditCached;
	mutable bool fImmatureCreditCached;
	mutable bool fAvailableCreditCached;
	mutable bool fWatchDebitCached;
	mutable bool fWatchCreditCached;
	mutable bool fImmatureWatchCreditCached;
	mutable bool fAvailableWatchCreditCached;
	mutable bool fAnonymizableCreditCached;
	mutable bool fAnonymizedCreditCached;
	mutable bool fChangeCached;

	mutable int64_t nDebitCached;
	mutable int64_t nCreditCached;
	mutable int64_t nImmatureCreditCached;
	mutable int64_t nAvailableCreditCached;
	mutable CAmount nAnonymizableCreditCached;
	mutable CAmount nAnonymizedCreditCached;
	mutable CAmount nWatchDebitCached;
	mutable CAmount nWatchCreditCached;
	mutable CAmount nImmatureWatchCreditCached;
	mutable CAmount nAvailableWatchCreditCached;
	mutable int64_t nChangeCached;

	CWalletTx();
	CWalletTx(const CWallet* pwalletIn);
	CWalletTx(const CWallet* pwalletIn, const CMerkleTx& txIn);
	CWalletTx(const CWallet* pwalletIn, const CTransaction& txIn);
	void Init(const CWallet* pwalletIn);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	// marks certain txout's as spent
	// returns true if any update took place
	bool UpdateSpent(const std::vector<char>& vfNewSpent);

	// make sure balances are recalculated
	void MarkDirty();
	void BindWallet(CWallet *pwalletIn);
	void MarkSpent(unsigned int nOut);
	void MarkUnspent(unsigned int nOut);
	bool IsSpent(unsigned int nOut) const;
	CAmount GetDebit(const isminefilter& filter) const;
	CAmount GetCredit(const isminefilter& filter) const;
	CAmount GetImmatureCredit(bool fUseCache=true) const;
	CAmount GetAvailableCredit(bool fUseCache=true) const;
	CAmount GetImmatureWatchOnlyCredit(const bool& fUseCache=true) const;
	CAmount GetAvailableWatchOnlyCredit(const bool& fUseCache=true) const;
	CAmount GetChange() const;
	void GetAmounts(std::list<std::pair<CTxDestination, int64_t> >& listReceived,
					std::list<std::pair<CTxDestination, int64_t> >& listSent, CAmount& nFee, std::string& strSentAccount, const isminefilter& filter) const;

	void GetAccountAmounts(const std::string& strAccount, CAmount& nReceived,
						   CAmount& nSent, CAmount& nFee, const isminefilter& filter) const;

	bool IsFromMe(const isminefilter& filter) const;
	bool IsTrusted() const;
	bool WriteToDisk();

	int64_t GetTxTime() const;
	int GetRequestCount() const;

	void AddSupportingTransactions(CTxDB& txdb);

	bool AcceptWalletTransaction(CTxDB& txdb);
	bool AcceptWalletTransaction();

	void RelayWalletTransaction(CTxDB& txdb, std::string strCommand="tx");
	void RelayWalletTransaction(std::string strCommand="tx");

	std::set<uint256> GetConflicts() const;

	/**
		Missing function
	*/
	CAmount GetAnonymizableCredit(bool fUseCache=true) const;
};

#endif // CWALLETTX_H
