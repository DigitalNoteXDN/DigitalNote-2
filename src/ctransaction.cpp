#include "compat.h"

#include "uint/uint256.h"
#include "txdb-leveldb.h"
#include "cdisktxpos.h"
#include "ctxindex.h"
#include "mining.h"
#include "serialize.h"
#include "main.h"
#include "main_extern.h"
#include "main_const.h"
#include "ctxmempool.h"
#include "chashwriter.h"
#include "hash.h"
#include "ctxout.h"
#include "ctxin.h"
#include "cbignum.h"
#include "script.h"
#include "util.h"
#include "cautofile.h"
#include "cdatastream.h"
#include "checkpoints.h"

#include "ctransaction.h"

bool CTransaction::DoS(int nDoSIn, bool fIn) const
{
	nDoS += nDoSIn;

	return fIn;
}

CTransaction::CTransaction(int nVersion, unsigned int nTime, const std::vector<CTxIn>& vin,
		const std::vector<CTxOut>& vout, unsigned int nLockTime)
		: nVersion(nVersion), nTime(nTime), vin(vin), vout(vout), nLockTime(nLockTime), nDoS(0)
{
	
}

CTransaction::CTransaction()
{
	this->SetNull();
}

unsigned int CTransaction::GetSerializeSize(int nType, int nVersion) const
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

	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(nTime);
	READWRITE(vin);
	READWRITE(vout);
	READWRITE(nLockTime);

	return nSerSize;
}

template<typename Stream>
void CTransaction::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */

	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(nTime);
	READWRITE(vin);
	READWRITE(vout);
	READWRITE(nLockTime);
}

template<typename Stream>
void CTransaction::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */

	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(nTime);
	READWRITE(vin);
	READWRITE(vout);
	READWRITE(nLockTime);
}

template void CTransaction::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CTransaction::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
template void CTransaction::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
template void CTransaction::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);
template void CTransaction::Serialize<CHashWriter>(CHashWriter& s, int nType, int nVersion) const;

void CTransaction::SetNull()
{
	nVersion = CTransaction::CURRENT_VERSION;
	nTime = GetAdjustedTime();
	vin.clear();
	vout.clear();
	nLockTime = 0;
	nDoS = 0;  // Denial-of-service prevention
}

bool CTransaction::IsNull() const
{
	return (vin.empty() && vout.empty());
}

uint256 CTransaction::GetHash() const
{
	return SerializeHash(*this);
}

bool CTransaction::IsCoinBase() const
{
	return (vin.size() == 1 && vin[0].prevout.IsNull() && vout.size() >= 1);
}

bool CTransaction::IsCoinStake() const
{
	// ppcoin: the coin stake transaction is marked with the first output empty
	return (vin.size() > 0 && (!vin[0].prevout.IsNull()) && vout.size() >= 2 && vout[0].IsEmpty());
}

double CTransaction::ComputePriority(double dPriorityInputs, unsigned int nTxSize) const
{
	// In order to avoid disincentivizing cleaning up the UTXO set we don't count
	// the constant overhead for each txin and up to 110 bytes of scriptSig (which
	// is enough to cover a compressed pubkey p2sh redemption) for priority.
	// Providing any more cleanup incentive than making additional inputs free would
	// risk encouraging people to create junk outputs to redeem later.
	if (nTxSize == 0)
	{
		nTxSize = ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
	}

	for(const CTxIn& txin : vin)
	{
		unsigned int offset = 41U + std::min(110U, (unsigned int)txin.scriptSig.size());
		
		if (nTxSize > offset)
		{
			nTxSize -= offset;
		}
	}

	if (nTxSize == 0)
	{
		return 0.0;
	}

	return dPriorityInputs / nTxSize;
}

int64_t CTransaction::GetValueOut() const
{
	int64_t nValueOut = 0;

	for(const CTxOut& txout : vout)
	{
		nValueOut += txout.nValue;
		
		if (!MoneyRange(txout.nValue) || !MoneyRange(nValueOut))
		{
			throw std::runtime_error("CTransaction::GetValueOut() : value out of range");
		}
	}

	return nValueOut;
}

int64_t CTransaction::GetValueMapIn(const mapPrevTx_t& inputs) const
{
	if (IsCoinBase())
	{
		return 0;
	}

	int64_t nResult = 0;

	for (unsigned int i = 0; i < vin.size(); i++)
	{
		nResult += GetOutputFor(vin[i], inputs).nValue;
	}

	return nResult;
}

bool operator==(const CTransaction& a, const CTransaction& b)
{
	return (a.nVersion == b.nVersion &&
			a.nTime == b.nTime &&
			a.vin == b.vin &&
			a.vout == b.vout &&
			a.nLockTime == b.nLockTime);
}

bool operator!=(const CTransaction& a, const CTransaction& b)
{
	return !(a == b);
}

std::string CTransaction::ToString() const
{
	std::string str;

	str += IsCoinBase()? "Coinbase" : (IsCoinStake()? "Coinstake" : "CTransaction");
	str += strprintf("(hash=%s, nTime=%d, ver=%d, vin.size=%u, vout.size=%u, nLockTime=%d)\n",
		GetHash().ToString(),
		nTime,
		nVersion,
		vin.size(),
		vout.size(),
		nLockTime
	);

	for (unsigned int i = 0; i < vin.size(); i++)
	{
		str += "    " + vin[i].ToString() + "\n";
	}

	for (unsigned int i = 0; i < vout.size(); i++)
	{
		str += "    " + vout[i].ToString() + "\n";
	}

	return str;
}

bool CTransaction::ReadFromDisk(CDiskTxPos pos, FILE** pfileRet)
{
	CAutoFile filein = CAutoFile(OpenBlockFile(pos.nFile, 0, pfileRet ? "rb+" : "rb"), SER_DISK, CLIENT_VERSION);

	if (!filein)
	{
		return error("CTransaction::ReadFromDisk() : OpenBlockFile failed");
	}

	// Read transaction
	if (fseek(filein, pos.nTxPos, SEEK_SET) != 0)
	{
		return error("CTransaction::ReadFromDisk() : fseek failed");
	}

	try
	{
		filein >> *this;
	}
	catch (std::exception &e)
	{
		return error("%s() : deserialize or I/O error", __PRETTY_FUNCTION__);
	}

	// Return file pointer
	if (pfileRet)
	{
		if (fseek(filein, pos.nTxPos, SEEK_SET) != 0)
		{
			return error("CTransaction::ReadFromDisk() : second fseek failed");
		}
		
		*pfileRet = filein.release();
	}

	return true;
}

bool CTransaction::ReadFromDisk(CTxDB& txdb, const uint256& hash, CTxIndex& txindexRet)
{
	SetNull();

	if (!txdb.ReadTxIndex(hash, txindexRet))
	{
		return false;
	}

	if (!ReadFromDisk(txindexRet.pos))
	{
		return false;
	}

	return true;
}

bool CTransaction::ReadFromDisk(CTxDB& txdb, COutPoint prevout, CTxIndex& txindexRet)
{
	if (!ReadFromDisk(txdb, prevout.hash, txindexRet))
	{
		return false;
	}

	if (prevout.n >= vout.size())
	{
		SetNull();
		
		return false;
	}

	return true;
}

bool CTransaction::ReadFromDisk(CTxDB& txdb, COutPoint prevout)
{
	CTxIndex txindex;

	return ReadFromDisk(txdb, prevout, txindex);
}

bool CTransaction::ReadFromDisk(COutPoint prevout)
{
	CTxDB txdb("r");
	CTxIndex txindex;

	return ReadFromDisk(txdb, prevout, txindex);
}

bool CTransaction::DisconnectInputs(CTxDB& txdb)
{
	// Relinquish previous transactions' spent pointers
	if (!IsCoinBase())
	{
		for(const CTxIn& txin : vin)
		{
			COutPoint prevout = txin.prevout;

			// Get prev txindex from disk
			CTxIndex txindex;
			
			if (!txdb.ReadTxIndex(prevout.hash, txindex))
			{
				return error("DisconnectInputs() : ReadTxIndex failed");
			}
			
			if (prevout.n >= txindex.vSpent.size())
			{
				return error("DisconnectInputs() : prevout.n out of range");
			}
			
			// Mark outpoint as not spent
			txindex.vSpent[prevout.n].SetNull();

			// Write back
			if (!txdb.UpdateTxIndex(prevout.hash, txindex))
			{
				return error("DisconnectInputs() : UpdateTxIndex failed");
			}
		}
	}

	// Remove transaction from index
	// This can fail if a duplicate of this transaction was in a chain that got
	// reorganized away. This is only possible if this transaction was completely
	// spent, so erasing it would be a no-op anyway.
	txdb.EraseTxIndex(*this);

	return true;
}

bool CTransaction::FetchInputs(CTxDB& txdb, const std::map<uint256, CTxIndex>& mapTestPool,
		bool fBlock, bool fMiner, mapPrevTx_t& inputsRet, bool& fInvalid) const
{
	// FetchInputs can return false either because we just haven't seen some inputs
	// (in which case the transaction should be stored as an orphan)
	// or because the transaction is malformed (in which case the transaction should
	// be dropped).  If tx is definitely invalid, fInvalid will be set to true.
	fInvalid = false;

	if (IsCoinBase())
	{
		return true; // Coinbase transactions have no inputs to fetch.
	}
	
	for (unsigned int i = 0; i < vin.size(); i++)
	{
		COutPoint prevout = vin[i].prevout;
		
		if (inputsRet.count(prevout.hash))
		{
			continue; // Got it already
		}
		
		// Read txindex
		CTxIndex& txindex = inputsRet[prevout.hash].first;
		
		bool fFound = true;
		if ((fBlock || fMiner) && mapTestPool.count(prevout.hash))
		{
			// Get txindex from current proposed changes
			txindex = mapTestPool.find(prevout.hash)->second;
		}
		else
		{
			// Read txindex from txdb
			fFound = txdb.ReadTxIndex(prevout.hash, txindex);
		}
		
		if (!fFound && (fBlock || fMiner))
		{
			return fMiner ? false : error("FetchInputs() : %s prev tx %s index entry not found", GetHash().ToString(),  prevout.hash.ToString());
		}
		
		// Read txPrev
		CTransaction& txPrev = inputsRet[prevout.hash].second;
		if (!fFound || txindex.pos == CDiskTxPos(1,1,1))
		{
			// Get prev tx from single transactions in memory
			if (!mempool.lookup(prevout.hash, txPrev))
			{
				return error("FetchInputs() : %s mempool Tx prev not found %s", GetHash().ToString(),  prevout.hash.ToString());
			}
			
			if (!fFound)
			{
				txindex.vSpent.resize(txPrev.vout.size());
			}
		}
		else
		{
			// Get prev tx from disk
			if (!txPrev.ReadFromDisk(txindex.pos))
			{
				return error("FetchInputs() : %s ReadFromDisk prev tx %s failed", GetHash().ToString(),  prevout.hash.ToString());
			}
		}
	}

	// Make sure all prevout.n indexes are valid:
	for (unsigned int i = 0; i < vin.size(); i++)
	{
		const COutPoint prevout = vin[i].prevout;
		assert(inputsRet.count(prevout.hash) != 0);
		const CTxIndex& txindex = inputsRet[prevout.hash].first;
		const CTransaction& txPrev = inputsRet[prevout.hash].second;
		
		if (prevout.n >= txPrev.vout.size() || prevout.n >= txindex.vSpent.size())
		{
			// Revisit this if/when transaction replacement is implemented and allows
			// adding inputs:
			fInvalid = true;
			
			return DoS(100, error("FetchInputs() : %s prevout.n out of range %d %u %u prev tx %s\n%s", GetHash().ToString(), prevout.n, txPrev.vout.size(), txindex.vSpent.size(), prevout.hash.ToString(), txPrev.ToString()));
		}
	}

	return true;
}

bool CTransaction::ConnectInputs(CTxDB& txdb, mapPrevTx_t inputs, std::map<uint256, CTxIndex>& mapTestPool, const CDiskTxPos& posThisTx,
		const CBlockIndex* pindexBlock, bool fBlock, bool fMiner, unsigned int flags, bool fValidateSig)
{
	// Take over previous transactions' spent pointers
	// fBlock is true when this is called from AcceptBlock when a new best-block is added to the blockchain
	// fMiner is true when called from the internal bitcoin miner
	// ... both are false when called from CTransaction::AcceptToMemoryPool
	if (!IsCoinBase())
	{
		int64_t nValueIn = 0;
		int64_t nFees = 0;
		
		for (unsigned int i = 0; i < vin.size(); i++)
		{
			COutPoint prevout = vin[i].prevout;
			
			assert(inputs.count(prevout.hash) > 0);
			
			CTxIndex& txindex = inputs[prevout.hash].first;
			CTransaction& txPrev = inputs[prevout.hash].second;

			if (prevout.n >= txPrev.vout.size() || prevout.n >= txindex.vSpent.size())
			{
				return DoS(100,
					error("ConnectInputs() : %s prevout.n out of range %d %u %u prev tx %s\n%s",
						GetHash().ToString(),
						prevout.n,
						txPrev.vout.size(),
						txindex.vSpent.size(),
						prevout.hash.ToString(),
						txPrev.ToString()
					)
				);
			}
			
			// If prev is coinbase or coinstake, check that it's matured
			if (txPrev.IsCoinBase() || txPrev.IsCoinStake())
			{
				int nSpendDepth;
				
				if (IsConfirmedInNPrevBlocks(txindex, pindexBlock, nCoinbaseMaturity, nSpendDepth))
				{
					return error("ConnectInputs() : tried to spend %s at depth %d",
						txPrev.IsCoinBase() ? "coinbase" : "coinstake",
						nSpendDepth
					);
				}
			}

			// ppcoin: check transaction timestamp
			if (txPrev.nTime > nTime)
			{
				return DoS(100, error("ConnectInputs() : transaction timestamp earlier than input transaction"));
			}
			
			if (txPrev.vout[prevout.n].IsEmpty())
			{
				return DoS(1, error("ConnectInputs() : special marker is not spendable"));
			}
			
			// Check for negative or overflow input values
			nValueIn += txPrev.vout[prevout.n].nValue;
			
			if (!MoneyRange(txPrev.vout[prevout.n].nValue) || !MoneyRange(nValueIn))
			{
				return DoS(100, error("ConnectInputs() : txin values out of range"));
			}
		}
		// The first loop above does all the inexpensive checks.
		// Only if ALL inputs pass do we perform expensive ECDSA signature checks.
		// Helps prevent CPU exhaustion attacks.
		for (unsigned int i = 0; i < vin.size(); i++)
		{
			COutPoint prevout = vin[i].prevout;
			assert(inputs.count(prevout.hash) > 0);
			CTxIndex& txindex = inputs[prevout.hash].first;
			CTransaction& txPrev = inputs[prevout.hash].second;

			// Check for conflicts (double-spend)
			// This doesn't trigger the DoS code on purpose; if it did, it would make it easier
			// for an attacker to attempt to split the network.
			if (!txindex.vSpent[prevout.n].IsNull())
			{
				return fMiner ? false : error(
					"ConnectInputs() : %s prev tx already used at %s",
					GetHash().ToString(),
					txindex.vSpent[prevout.n].ToString()
				);
			}
			
			if(fValidateSig)
			{
				// Skip ECDSA signature verification when connecting blocks (fBlock=true)
				// before the last blockchain checkpoint. This is safe because block merkle hashes are
				// still computed and checked, and any change will be caught at the next checkpoint.
				if (!(fBlock && (nBestHeight < Checkpoints::GetTotalBlocksEstimate())))
				{
					// Verify signature
					if (!VerifySignature(txPrev, *this, i, flags, 0))
					{
						if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS)
						{
							// Check whether the failure was caused by a
							// non-mandatory script verification check, such as
							// non-null dummy arguments;
							// if so, don't trigger DoS protection to
							// avoid splitting the network between upgraded and
							// non-upgraded nodes.
							if (VerifySignature(txPrev, *this, i, flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, 0))
							{
								return error("ConnectInputs() : %s non-mandatory VerifySignature failed", GetHash().ToString());
							}
						}
						
						// Failures of other flags indicate a transaction that is
						// invalid in new blocks, e.g. a invalid P2SH. We DoS ban
						// such nodes as they are not following the protocol. That
						// said during an upgrade careful thought should be taken
						// as to the correct behavior - we may want to continue
						// peering with non-upgraded nodes even after a soft-fork
						// super-majority vote has passed.
						return DoS(100,error("ConnectInputs() : %s VerifySignature failed", GetHash().ToString()));
					}
				}
			}

			// Mark outpoints as spent
			txindex.vSpent[prevout.n] = posThisTx;

			// Write back
			if (fBlock || fMiner)
			{
				mapTestPool[prevout.hash] = txindex;
			}
		}

		if (!IsCoinStake())
		{
			if (nValueIn < GetValueOut())
			{
				return DoS(100, error("ConnectInputs() : %s value in < value out", GetHash().ToString()));
			}
			
			// Tally transaction fees
			int64_t nTxFee = nValueIn - GetValueOut();
			
			if (nTxFee < 0)
			{
				return DoS(100, error("ConnectInputs() : %s nTxFee < 0", GetHash().ToString()));
			}
			
			nFees += nTxFee;
			
			if (!MoneyRange(nFees))
			{
				return DoS(100, error("ConnectInputs() : nFees out of range"));
			}
		}
	}

	return true;
}

bool CTransaction::CheckTransaction() const
{
	// Basic checks that don't depend on any context
	if (vin.empty())
	{
		return DoS(10, error("CTransaction::CheckTransaction() : vin empty"));
	}

	if (vout.empty())
	{
		return DoS(10, error("CTransaction::CheckTransaction() : vout empty"));
	}

	// Size limits
	if (::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
	{
		return DoS(100, error("CTransaction::CheckTransaction() : size limits failed"));
	}

	// Check for negative or overflow output values
	int64_t nValueOut = 0;
	for (const CTxOut& txout : vout)
	{
		if (txout.IsEmpty() && !IsCoinBase() && !IsCoinStake())
		{
			return DoS(100, error("CTransaction::CheckTransaction() : txout empty for user transaction"));
		}
		
		if (txout.nValue < 0)
		{
			return DoS(100, error("CTransaction::CheckTransaction() : txout.nValue negative"));
		}
		
		if (txout.nValue > MAX_SINGLE_TX)
		{
			return DoS(100, error("CTransaction::CheckTransaction() : txout.nValue too high"));
		}
		
		nValueOut += txout.nValue;
		
		if (!MoneyRange(nValueOut))
		{
			return DoS(100, error("CTransaction::CheckTransaction() : txout total out of range"));
		}
	}

	// Check for duplicate inputs
	std::set<COutPoint> vInOutPoints;
	for(const CTxIn& txin : vin)
	{
		if (vInOutPoints.count(txin.prevout))
		{
			return false;
		}
		
		vInOutPoints.insert(txin.prevout);
	}

	if (IsCoinBase())
	{
		if (vin[0].scriptSig.size() < 2 || vin[0].scriptSig.size() > 100)
		{
			return DoS(100, error("CTransaction::CheckTransaction() : coinbase script size is invalid"));
		}
	}
	else
	{
		for(const CTxIn& txin : vin)
		{
			if (txin.prevout.IsNull())
			{
				return DoS(10, error("CTransaction::CheckTransaction() : prevout is null"));
			}
		}
	}

	return true;
}

// ppcoin: total coin age spent in transaction, in the unit of coin-days.
// Only those coins meeting minimum age requirement counts. As those
// transactions not in main chain are not currently indexed so we
// might not find out about their coin age. Older transactions are
// guaranteed to be in main chain by sync-checkpoint. This rule is
// introduced to help nodes establish a consistent view of the coin
// age (trust score) of competing branches.
bool CTransaction::GetCoinAge(CTxDB& txdb, const CBlockIndex* pindexPrev, uint64_t& nCoinAge) const
{
	CBigNum bnCentSecond = 0;  // coin age in the unit of cent-seconds
	nCoinAge = 0;

	if (IsCoinBase())
	{
		return true;
	}

	for(const CTxIn& txin : vin)
	{
		// First try finding the previous transaction in database
		CTransaction txPrev;
		CTxIndex txindex;
		
		if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
		{
			continue;  // previous transaction not in main chain
		}
		
		if (nTime < txPrev.nTime)
		{
			return false;  // Transaction timestamp violation
		}
		
		int nSpendDepth;
		if (IsConfirmedInNPrevBlocks(txindex, pindexPrev, nStakeMinConfirmations - 1, nSpendDepth))
		{
			LogPrint("coinage", "coin age skip nSpendDepth=%d\n", nSpendDepth + 1);
			
			continue; // only count coins meeting min confirmations requirement
		}

		int64_t nValueIn = txPrev.vout[txin.prevout.n].nValue;
		bnCentSecond += CBigNum(nValueIn) * (nTime-txPrev.nTime) / CENT;

		LogPrint("coinage", "coin age nValueIn=%d nTimeDiff=%d bnCentSecond=%s\n", nValueIn, nTime - txPrev.nTime, bnCentSecond.ToString());
	}

	CBigNum bnCoinDay = bnCentSecond * CENT / COIN / (24 * 60 * 60);
	nCoinAge = bnCoinDay.getuint64();

	LogPrint("coinage", "coin age bnCoinDay=%s\n", bnCoinDay.ToString());

	return true;
}

const CTxOut& CTransaction::GetOutputFor(const CTxIn& input, const mapPrevTx_t& inputs) const
{
	mapPrevTx_t::const_iterator mi = inputs.find(input.prevout.hash);

	if (mi == inputs.end())
	{
		throw std::runtime_error("CTransaction::GetOutputFor() : prevout.hash not found");
	}

	const CTransaction& txPrev = (mi->second).second;
	
	if (input.prevout.n >= txPrev.vout.size())
	{
		LogPrintf("CTransaction::GetOutputFor() : input.prevout.n = %d\n", input.prevout.n);
		LogPrintf("CTransaction::GetOutputFor() : txPrev.vout.size() = %d\n", txPrev.vout.size());
		
		throw std::runtime_error("CTransaction::GetOutputFor() : prevout.n out of range");
	}

	return txPrev.vout[input.prevout.n];
}

bool CTransaction::GetMapTxInputs(mapPrevTx_t& mapInputs, bool fBlock, bool fMiner) const
{
	CTxDB txdb("r");
	std::map<uint256, CTxIndex> mapUnused;
	bool fInvalid = false;

	if (!this->FetchInputs(txdb, mapUnused, fBlock, fMiner, mapInputs, fInvalid))
	{
		if (fInvalid)
		{
			LogPrintf("Invalid TX attempted to set in GetMapTXInputs\n");
			
			return false;
		}
	}

	return true;
}

