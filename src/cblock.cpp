#include "compat.h"

#include <algorithm>
#include <boost/thread.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "util.h"
#include "crypto/bmw/bmw512.h"
#include "ctransaction.h"
#include "txdb-leveldb.h"
#include "blocksizecalculator.h"
#include "blockparams.h"
#include "kernel.h"
#include "spork.h"
#include "cmasternodevotetracker.h"
#include "instantx.h"
#include "velocity.h"
#include "checkpoints.h"
#include "cblocklocator.h"
#include "cwallet.h"
#include "script.h"
#include "cinv.h"
#include "net/cnode.h"
#include "net.h"
#include "cmasternodeman.h"
#include "cmasternodepayments.h"
#include "masternodeman.h"
#include "masternode_extern.h"
#include "serialize.h"
#include "ctxmempool.h"
#include "ckey.h"
#include "ctxout.h"
#include "main_extern.h"
#include "ctxin.h"
#include "hash.h"
#include "types/valtype.h"
#include "cbitcoinaddress.h"
#include "cdigitalnoteaddress.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "thread.h"
#include "ui_translate.h"
#include "cblockindex.h"
#include "cdiskblockindex.h"
#include "cdisktxpos.h"
#include "ctxindex.h"
#include "util/backwards.h"
#include "cautofile.h"
#include "fork.h"
#include "cflatdata.h"

#include "cblock.h"

// Every received block is assigned a unique and increasing identifier, so we
// know which one to give priority in case of a fork.
CCriticalSection cs_nBlockSequenceId;

// Blocks loaded from disk are assigned id 0, so start the counter at 1.
uint32_t nBlockSequenceId = 1;

// v2.0.0.8 M4: Voted-consensus payment activation height.
//
// Activation gate is hybrid: a hardcoded floor (compile-time constant) AND
// an optional spork override (SPORK_15) where set.  Effective activation
// height = min(floor, spork) when spork>0, else floor.
//
// The min() prevents a compromised spork key from activating retroactively
// (spork can only LOWER the activation, never raise above the hardcoded
// floor that consensus already agrees on).  Mainnet ships with floor =
// INT_MAX (effectively never), so initial activation requires spork action.
// When mainnet is ready, a future release will bake the agreed activation
// height into VOTED_CONSENSUS_ACTIVATION_FLOOR.
//
// Testnet uses a low floor so UAT can run end-to-end activation tests
// without spork plumbing.  See M4-design-notes.md S4 Pattern 3.
namespace {

#ifdef VOTED_CONSENSUS_ACTIVATION_FLOOR_MAINNET
const int VOTED_CONSENSUS_ACTIVATION_FLOOR_MAINNET_VAL = VOTED_CONSENSUS_ACTIVATION_FLOOR_MAINNET;
#else
const int VOTED_CONSENSUS_ACTIVATION_FLOOR_MAINNET_VAL = INT_MAX;
#endif

#ifdef VOTED_CONSENSUS_ACTIVATION_FLOOR_TESTNET
const int VOTED_CONSENSUS_ACTIVATION_FLOOR_TESTNET_VAL = VOTED_CONSENSUS_ACTIVATION_FLOOR_TESTNET;
#else
// v2.0.0.8 voted-consensus determinism fix -- testnet floor raised 1000 -> 2000.
//
// The original testnet activation was height 1000.  The testnet chain ran
// past it (to ~1205) under the pre-fix, non-deterministic GetCanonicalWinner,
// which produced the payee-disagreement fork.  Raising the floor to 2000 puts
// activation back ABOVE the current tip: the chain resumes on the legacy
// (permissive) payee path -- voted consensus OFF -- and then re-activates
// cleanly at height 2000 on the fully-fixed code, giving a watchable
// pre-activation -> post-activation transition without a chain reset.
//
// This is a TESTNET-ONLY value.  The mainnet floor is unaffected.  Adjust or
// remove once the determinism fix is soak-proven and a real testnet
// activation rehearsal has been completed.
const int VOTED_CONSENSUS_ACTIVATION_FLOOR_TESTNET_VAL = 2000;
#endif

} // anonymous namespace

// v2.0.0.8 PB-16: moved out of the anonymous namespace above so that
// CMasternodeMan::FindOldestNotInVecChainDerived (in cmasternodeman.cpp)
// can read the activation height to clamp stale pre-activation `lastpaid`
// values.  Behaviour unchanged.  Declared in cblock.h.
int GetEffectiveVotedConsensusActivationHeight()
{
	int floor;
	if (TestNet())
	{
		floor = VOTED_CONSENSUS_ACTIVATION_FLOOR_TESTNET_VAL;
	}
	else
	{
		floor = VOTED_CONSENSUS_ACTIVATION_FLOOR_MAINNET_VAL;
	}

	int64_t sporkVal = GetSporkValue(SPORK_15_VOTED_CONSENSUS_ACTIVATION);

	// Spork=0 (default) means "no override".  Non-zero spork lowers the
	// activation height -- but never raises it above floor.
	if (sporkVal > 0 && sporkVal < floor)
	{
		return (int)sporkVal;
	}

	return floor;
}

// v2.0.0.8 M4: validation hook.  Returns the payee that consensus expects
// for nBlockHeight.  Three regimes:
//
//   1. Before activation: legacy behavior -- defer to masternodePayments.
//      GetBlockPayee, which reads winning entries from the M3-era PoS-only
//      vWinning map.  Returns false if no winner has been broadcast yet.
//
//   2. At/after activation, consensus formed: returns the canonical voted
//      payee from the vote tracker (M2/M3 machinery).
//
//   3. At/after activation, no consensus yet: PERMISSIVE FALLBACK.  Behaves
//      as before activation (soft-fork).  This avoids stalling the chain
//      if the voting fleet has insufficient coverage at activation height
//      (e.g. v2.0.0.7 holdouts, network partition).  See M4-design-notes.md
//      S5 Option 1.
bool GetEnforcedPayee(int nBlockHeight, CScript &payeeOut, CTxIn &vinOut)
{
	int activationHeight = GetEffectiveVotedConsensusActivationHeight();

	if (nBlockHeight >= activationHeight)
	{
		CScript votedPayee;
		// v2.0.0.8 M1Q: consensus is now read from the queue-based tracker
		// (GetCanonicalWinnerFromQueues) instead of the per-height point-vote
		// path (GetCanonicalWinner).  The contract is unchanged: returns
		// (true, payee) on consensus, false otherwise.  The per-height
		// GetCanonicalWinner / mapVotes path is retained in the tracker for
		// one release as defensive deserialization but is no longer the
		// consensus source.  See v208-M1Q-queue-based-voting-SPEC.md S12.
		if (voteTracker.GetCanonicalWinnerFromQueues(nBlockHeight, votedPayee))
		{
			payeeOut = votedPayee;
			// v2.0.0.8 latent-11: the vote tracker tracks payees by
			// scriptPubKey, not by vin, so there is no meaningful vin to
			// return on the voted path.  vinOut is explicitly CLEARED so
			// that any caller doing mnodeman.Find(vinOut) gets a
			// deterministic NULL rather than matching a stale leftover vin.
			// (The previous nLastPaid consumer in main.cpp ProcessBlock has
			// been removed entirely -- OnBlockConnected is now the sole
			// authority for the per-MN last-paid display field -- but the
			// clear is kept as correct defensive hygiene for the function's
			// contract: voted path => no vin.)
			vinOut = CTxIn();
			return true;
		}

		// Activation height reached but no consensus -- fall through to
		// legacy lookup (permissive).  Logged so operators can spot
		// extended consensus failures.
		if (fDebug)
		{
			LogPrintf("GetEnforcedPayee -- height %d at/after activation %d "
					  "but no consensus; falling back to legacy GetBlockPayee\n",
					  nBlockHeight, activationHeight);
		}
	}

	return masternodePayments.GetBlockPayee(nBlockHeight, payeeOut, vinOut);
}

bool CBlock::DoS(int nDoSIn, bool fIn) const
{
	nDoS += nDoSIn;
	
	return fIn;
}

CBlock::CBlock()
{
	SetNull();
}

unsigned int CBlock::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;

	s.nType = nType;
	s.nVersion = nVersion;
	
	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(hashPrevBlock);
	READWRITE(hashMerkleRoot);
	READWRITE(nTime);
	READWRITE(nBits);
	READWRITE(nNonce);

	// ConnectBlock depends on vtx following header to generate CDiskTxPos
	if (!(nType & (SER_GETHASH|SER_BLOCKHEADERONLY)))
	{
		READWRITE(vtx);
		READWRITE(vchBlockSig);
	}
	
	return nSerSize;
}

template<typename Stream>
void CBlock::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(hashPrevBlock);
	READWRITE(hashMerkleRoot);
	READWRITE(nTime);
	READWRITE(nBits);
	READWRITE(nNonce);

	// ConnectBlock depends on vtx following header to generate CDiskTxPos
	if (!(nType & (SER_GETHASH|SER_BLOCKHEADERONLY)))
	{
		READWRITE(vtx);
		READWRITE(vchBlockSig);
	}
}

template<typename Stream>
void CBlock::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(hashPrevBlock);
	READWRITE(hashMerkleRoot);
	READWRITE(nTime);
	READWRITE(nBits);
	READWRITE(nNonce);

	// ConnectBlock depends on vtx following header to generate CDiskTxPos
	if (!(nType & (SER_GETHASH|SER_BLOCKHEADERONLY)))
	{
		READWRITE(vtx);
		READWRITE(vchBlockSig);
	}
	else
	{
		const_cast<CBlock*>(this)->vtx.clear();
		const_cast<CBlock*>(this)->vchBlockSig.clear();
	}
}

template void CBlock::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CBlock::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
template void CBlock::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
template void CBlock::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);

void CBlock::SetNull()
{
	nVersion = CBlock::CURRENT_VERSION;
	hashPrevBlock = 0;
	hashMerkleRoot = 0;
	nTime = 0;
	nBits = 0;
	nNonce = 0;
	vtx.clear();
	vchBlockSig.clear();
	vMerkleTree.clear();
	nDoS = 0;
}

bool CBlock::IsNull() const
{
	return (nBits == 0);
}

uint256 CBlock::GetHash() const
{
	if (nVersion > 6)
	{
		return Hash_bmw512(BEGIN(nVersion), END(nNonce));
	}
	else
	{
		return GetPoWHash();
	}
}

uint256 CBlock::GetPoWHash() const
{
	return Hash_bmw512(BEGIN(nVersion), END(nNonce));
}

int64_t CBlock::GetBlockTime() const
{
	return (int64_t)nTime;
}

void CBlock::UpdateTime(const CBlockIndex* pindexPrev)
{
	nTime = std::max(GetBlockTime(), GetAdjustedTime());
}

// entropy bit for stake modifier if chosen by modifier
unsigned int CBlock::GetStakeEntropyBit() const
{
	// Take last bit of block hash as entropy bit
	unsigned int nEntropyBit = ((GetHash().Get64()) & 1llu);
	
	LogPrint("stakemodifier", "GetStakeEntropyBit: hashBlock=%s nEntropyBit=%u\n", GetHash().ToString(), nEntropyBit);
	
	return nEntropyBit;
}

// ppcoin: two types of block: proof-of-work or proof-of-stake
bool CBlock::IsProofOfStake() const
{
	return (vtx.size() > 1 && vtx[1].IsCoinStake());
}

bool CBlock::IsProofOfWork() const
{
	return !IsProofOfStake();
}

std::pair<COutPoint, unsigned int> CBlock::GetProofOfStake() const
{
	return IsProofOfStake()? std::make_pair(vtx[1].vin[0].prevout, vtx[1].nTime) : std::make_pair(COutPoint(), (unsigned int)0);
}

// ppcoin: get max transaction timestamp
int64_t CBlock::GetMaxTransactionTime() const
{
	int64_t maxTransactionTime = 0;

	for(const CTransaction& tx : vtx)
	{
		maxTransactionTime = std::max(maxTransactionTime, (int64_t)tx.nTime);
	}

	return maxTransactionTime;
}

uint256 CBlock::BuildMerkleTree() const
{
	vMerkleTree.clear();

	for(const CTransaction& tx : vtx)
	{
		vMerkleTree.push_back(tx.GetHash());
	}

	int j = 0;
	for (int nSize = vtx.size(); nSize > 1; nSize = (nSize + 1) / 2)
	{
		for (int i = 0; i < nSize; i += 2)
		{
			int i2 = std::min(i+1, nSize-1);
			vMerkleTree.push_back(
				Hash(
					BEGIN(vMerkleTree[j+i]),
					END(vMerkleTree[j+i]),
					BEGIN(vMerkleTree[j+i2]),
					END(vMerkleTree[j+i2])
				)
			);
		}
		j += nSize;
	}

	return (vMerkleTree.empty() ? 0 : vMerkleTree.back());
}

std::vector<uint256> CBlock::GetMerkleBranch(int nIndex) const
{
	if (vMerkleTree.empty())
	{
		BuildMerkleTree();
	}

	std::vector<uint256> vMerkleBranch;
	int j = 0;

	for (int nSize = vtx.size(); nSize > 1; nSize = (nSize + 1) / 2)
	{
		int i = std::min(nIndex^1, nSize-1);
		vMerkleBranch.push_back(vMerkleTree[j+i]);
		nIndex >>= 1;
		j += nSize;
	}

	return vMerkleBranch;
}

uint256 CBlock::CheckMerkleBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex)
{
	if (nIndex == -1)
	{
		return 0;
	}

	for(const uint256& otherside : vMerkleBranch)
	{
		if (nIndex & 1)
		{
			hash = Hash(BEGIN(otherside), END(otherside), BEGIN(hash), END(hash));
		}
		else
		{
			hash = Hash(BEGIN(hash), END(hash), BEGIN(otherside), END(otherside));
		}
		nIndex >>= 1;
	}

	return hash;
}

bool CBlock::WriteToDisk(unsigned int& nFileRet, unsigned int& nBlockPosRet)
{
	// Open history file to append
	CAutoFile fileout = CAutoFile(AppendBlockFile(nFileRet), SER_DISK, CLIENT_VERSION);

	if (!fileout)
	{
		return error("CBlock::WriteToDisk() : AppendBlockFile failed");
	}

	// Write index header
	unsigned int nSize = fileout.GetSerializeSize(*this);
	fileout << FLATDATA(Params().MessageStart()) << nSize;

	// Write block
	long fileOutPos = ftell(fileout);

	if (fileOutPos < 0)
	{
		return error("CBlock::WriteToDisk() : ftell failed");
	}

	nBlockPosRet = fileOutPos;
	fileout << *this;

	// Flush stdio buffers and commit to disk before returning
	fflush(fileout);

	if (!IsInitialBlockDownload() || (nBestHeight+1) % 500 == 0)
	{
		FileCommit(fileout);
	}

	return true;
}

bool CBlock::ReadFromDisk(unsigned int nFile, unsigned int nBlockPos, bool fReadTransactions)
{
	SetNull();

	// Open history file to read
	CAutoFile filein = CAutoFile(OpenBlockFile(nFile, nBlockPos, "rb"), SER_DISK, CLIENT_VERSION);
	if (!filein)
	{
		return error("CBlock::ReadFromDisk() : OpenBlockFile failed");
	}

	if (!fReadTransactions)
	{
		filein.nType |= SER_BLOCKHEADERONLY;
	}

	// Read block
	try
	{
		filein >> *this;
	}
	catch (std::exception &e)
	{
		return error("%s() : deserialize or I/O error", __PRETTY_FUNCTION__);
	}

	// Check the header
	if (fReadTransactions && IsProofOfWork() && !CheckProofOfWork(GetPoWHash(), nBits))
	{
		return error("CBlock::ReadFromDisk() : errors in block header");
	}

	return true;
}

std::string CBlock::ToString() const
{
	std::stringstream s;

	s << strprintf(
			"CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u, vchBlockSig=%s)\n",
			GetHash().ToString(),
			nVersion,
			hashPrevBlock.ToString(),
			hashMerkleRoot.ToString(),
			nTime, nBits, nNonce,
			vtx.size(),
			HexStr(vchBlockSig.begin(), vchBlockSig.end())
		);

	for (unsigned int i = 0; i < vtx.size(); i++)
	{
		s << "  " << vtx[i].ToString() << "\n";
	}

	s << "  vMerkleTree: ";
	for (unsigned int i = 0; i < vMerkleTree.size(); i++)
	{
		s << " " << vMerkleTree[i].ToString();
	}
	s << "\n";

	return s.str();
}

bool CBlock::DisconnectBlock(CTxDB& txdb, CBlockIndex* pindex)
{
	// Disconnect in reverse order
	for (int i = vtx.size()-1; i >= 0; i--)
	{
		if (!vtx[i].DisconnectInputs(txdb))
		{
			return false;
		}
	}

	// Update block index on disk without changing it in memory.
	// The memory index structure will be changed after the db commits.
	if (pindex->pprev)
	{
		CDiskBlockIndex blockindexPrev(pindex->pprev);
		blockindexPrev.hashNext = 0;
		
		if (!txdb.WriteBlockIndex(blockindexPrev))
		{
			return error("DisconnectBlock() : WriteBlockIndex failed");
		}
	}

	// ppcoin: clean up wallet after disconnecting coinstake
	for(CTransaction& tx : vtx)
	{
		SyncWithWallets(tx, this, false);
	}

	return true;
}

bool CBlock::ConnectBlock(CTxDB& txdb, CBlockIndex* pindex, bool fJustCheck)
{
	// Check it again in case a previous version let a bad block in, but skip BlockSig checking
	if (!CheckBlock(!fJustCheck, !fJustCheck, false))
	{
		return false;
	}

	unsigned int flags = SCRIPT_VERIFY_NOCACHE;

	//// issue here: it doesn't know the version
	unsigned int nTxPos;

	if (fJustCheck)
	{
		// FetchInputs treats CDiskTxPos(1,1,1) as a special "refer to memorypool" indicator
		// Since we're just checking the block and not actually connecting it, it might not (and probably shouldn't)
		// be on the disk to get the transaction from
		nTxPos = 1;
	}
	else
	{
		nTxPos = pindex->nBlockPos
			+ ::GetSerializeSize(CBlock(), SER_DISK, CLIENT_VERSION)
			- (2 * GetSizeOfCompactSize(0))
			+ GetSizeOfCompactSize(vtx.size());
	}

	std::map<uint256, CTxIndex> mapQueuedChanges;
	int64_t nFees = 0;
	int64_t nValueIn = 0;
	int64_t nValueOut = 0;
	int64_t nStakeReward = 0;
	unsigned int nSigOps = 0;
	int nInputs = 0;

	MAX_BLOCK_SIZE = BlockSizeCalculator::ComputeBlockSize(pindex);
	MAX_BLOCK_SIGOPS = MAX_BLOCK_SIZE/50;
	MAX_TX_SIGOPS = MAX_BLOCK_SIGOPS/5;

	for(CTransaction& tx : vtx)
	{
		uint256 hashTx = tx.GetHash();
		nInputs += tx.vin.size();
		nSigOps += GetLegacySigOpCount(tx);

		if (nSigOps > MAX_BLOCK_SIGOPS)
		{
			return DoS(100, error("ConnectBlock() : too many sigops"));
		}
		
		CDiskTxPos posThisTx(pindex->nFile, pindex->nBlockPos, nTxPos);
		if (!fJustCheck)
		{
			nTxPos += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
		}
		
		mapPrevTx_t mapInputs;
		if (tx.IsCoinBase())
		{
			nValueOut += tx.GetValueOut();
		}
		else
		{
			bool fInvalid;
			
			if (!tx.FetchInputs(txdb, mapQueuedChanges, true, false, mapInputs, fInvalid))
			{
				return false;
			}
			
			// Add in sigops done by pay-to-script-hash inputs;
			// this is to prevent a "rogue miner" from creating
			// an incredibly-expensive-to-validate block.
			nSigOps += GetP2SHSigOpCount(tx, mapInputs);
			
			if (nSigOps > MAX_BLOCK_SIGOPS)
			{
				return DoS(100, error("ConnectBlock() : too many sigops"));
			}
			
			int64_t nTxValueIn = tx.GetValueMapIn(mapInputs);
			int64_t nTxValueOut = tx.GetValueOut();
			nValueIn += nTxValueIn;
			nValueOut += nTxValueOut;
			
			if (!tx.IsCoinStake())
			{
				nFees += nTxValueIn - nTxValueOut;
			}
			
			if (tx.IsCoinStake())
			{
				nStakeReward = nTxValueOut - nTxValueIn;
			}

			if (!tx.ConnectInputs(txdb, mapInputs, mapQueuedChanges, posThisTx, pindex, true, false, flags))
			{
				return false;
			}
		}

		mapQueuedChanges[hashTx] = CTxIndex(posThisTx, tx.vout.size());
	}

	if (IsProofOfWork())
	{
		int64_t nReward = GetProofOfWorkReward(pindex->nHeight, nFees);
		
		// Check coinbase reward
		if (vtx[0].GetValueOut() > nReward)
		{
			return DoS(50,
				error(
					"ConnectBlock() : coinbase reward exceeded (actual=%d vs calculated=%d)",
					vtx[0].GetValueOut(),
					nReward
				)
			);
		}
	}
	
	if (IsProofOfStake())
	{
		// ppcoin: coin stake tx earns reward instead of paying fee
		uint64_t nCoinAge;
		
		if (!vtx[1].GetCoinAge(txdb, pindex->pprev, nCoinAge))
		{
			return error("ConnectBlock() : %s unable to get coin age for coinstake", vtx[1].GetHash().ToString());
		}
		
		int64_t nCalculatedStakeReward = GetProofOfStakeReward(pindex->pprev, nCoinAge, nFees);

		if (nStakeReward > nCalculatedStakeReward)
		{
			return DoS(100,
				error(
					"ConnectBlock() : coinstake pays too much(actual=%d vs calculated=%d)",
					nStakeReward,
					nCalculatedStakeReward
				)
			);
		}
	}

	// ppcoin: track money supply and mint amount info
	pindex->nMint = nValueOut - nValueIn + nFees;
	pindex->nMoneySupply = (pindex->pprev? pindex->pprev->nMoneySupply : 0) + nValueOut - nValueIn;
	
	if (!txdb.WriteBlockIndex(CDiskBlockIndex(pindex)))
	{
		return error("Connect() : WriteBlockIndex for pindex failed");
	}
	
	if (fJustCheck)
	{
		return true;
	}
	
	// Write queued txindex changes
	for (std::pair<const uint256, CTxIndex>& item : mapQueuedChanges)
	{
		if (!txdb.UpdateTxIndex(item.first, item.second))
		{
			return error("ConnectBlock() : UpdateTxIndex failed");
		}
	}

	if(GetBoolArg("-addrindex", false))
	{
		// Write Address Index
		for(CTransaction& tx : vtx)
		{
			uint256 hashTx = tx.GetHash();
			// inputs
			if(!tx.IsCoinBase())
			{
				mapPrevTx_t mapInputs;
				std::map<uint256, CTxIndex> mapQueuedChangesT;
				bool fInvalid;
				
				if (!tx.FetchInputs(txdb, mapQueuedChangesT, true, false, mapInputs, fInvalid))
				{
					return false;
				}
				
				for(const std::pair<const uint256, std::pair<CTxIndex, CTransaction>>& item : mapInputs)
				{
					for(const CTxOut &atxout : item.second.second.vout)
					{
						std::vector<uint160> addrIds;
						if(BuildAddrIndex(atxout.scriptPubKey, addrIds))
						{
							for(uint160 addrId : addrIds)
							{
								if(!txdb.WriteAddrIndex(addrId, hashTx))
								{
									LogPrintf(
										"ConnectBlock(): txins WriteAddrIndex failed addrId: %s txhash: %s\n",
										addrId.ToString().c_str(),
										hashTx.ToString().c_str()
									);
								}
							}
						}
					}
				}
			}

			// outputs
			for(const CTxOut &atxout : tx.vout)
			{
				std::vector<uint160> addrIds;
				
				if(BuildAddrIndex(atxout.scriptPubKey, addrIds))
				{
					for(uint160 addrId : addrIds)
					{
						if(!txdb.WriteAddrIndex(addrId, hashTx))
						{
							LogPrintf(
								"ConnectBlock(): txouts WriteAddrIndex failed addrId: %s txhash: %s\n",
								addrId.ToString().c_str(),
								hashTx.ToString().c_str()
							);
						}
					}
				}
			}
		}
	}

	// Update block index on disk without changing it in memory.
	// The memory index structure will be changed after the db commits.
	if (pindex->pprev)
	{
		CDiskBlockIndex blockindexPrev(pindex->pprev);
		blockindexPrev.hashNext = pindex->GetBlockHash();
		
		if (!txdb.WriteBlockIndex(blockindexPrev))
		{
			return error("ConnectBlock() : WriteBlockIndex failed");
		}
	}

	// Watch for transactions paying to me
	for(CTransaction& tx : vtx)
	{
		SyncWithWallets(tx, this);
	}

	return true;
}

bool CBlock::ReadFromDisk(const CBlockIndex* pindex, bool fReadTransactions)
{
	if (!fReadTransactions)
	{
		*this = pindex->GetBlockHeader();
		
		return true;
	}

	if (!ReadFromDisk(pindex->nFile, pindex->nBlockPos, fReadTransactions))
	{
		return false;
	}

	if (GetHash() != pindex->GetBlockHash())
	{
		return error("CBlock::ReadFromDisk() : GetHash() doesn't match index");
	}

	return true;
}

bool CBlock::SetBestChain(CTxDB& txdb, CBlockIndex* pindexNew)
{
	uint256 hash = GetHash();

	if (!txdb.TxnBegin())
	{
		return error("SetBestChain() : TxnBegin failed");
	}

	if (pindexGenesisBlock == NULL && hash == Params().HashGenesisBlock())
	{
		txdb.WriteHashBestChain(hash);
		if (!txdb.TxnCommit())
		{
			return error("SetBestChain() : TxnCommit failed");
		}
		
		pindexGenesisBlock = pindexNew;
	}
	else if (hashPrevBlock == hashBestChain)
	{
		if (!SetBestChainInner(txdb, pindexNew))
		{
			return error("SetBestChain() : SetBestChainInner failed");
		}
	}
	else
	{
		// the first block in the new chain that will cause it to become the new best chain
		CBlockIndex *pindexIntermediate = pindexNew;

		// list of blocks that need to be connected afterwards
		std::vector<CBlockIndex*> vpindexSecondary;

		// Reorganize is costly in terms of db load, as it works in a single db transaction.
		// Try to limit how much needs to be done inside
		while (pindexIntermediate->pprev && pindexIntermediate->pprev->nChainTrust > pindexBest->nChainTrust)
		{
			vpindexSecondary.push_back(pindexIntermediate);
			pindexIntermediate = pindexIntermediate->pprev;
		}

		if (!vpindexSecondary.empty())
		{
			LogPrintf("Postponing %u reconnects\n", vpindexSecondary.size());
		}
		
		// Switch to new best branch
		if (!Reorganize(txdb, pindexIntermediate))
		{
			txdb.TxnAbort();
			InvalidChainFound(pindexNew);
			
			return error("SetBestChain() : Reorganize failed");
		}

		// Connect further blocks
		for(CBlockIndex *pindex : backwards<std::vector<CBlockIndex*>>(vpindexSecondary))
		{
			CBlock block;
			if (!block.ReadFromDisk(pindex))
			{
				LogPrintf("SetBestChain() : ReadFromDisk failed\n");
				
				break;
			}
			
			if (!txdb.TxnBegin())
			{
				LogPrintf("SetBestChain() : TxnBegin 2 failed\n");
				
				break;
			}
			
			// errors now are not fatal, we still did a reorganisation to a new chain in a valid way
			if (!block.SetBestChainInner(txdb, pindex))
			{
				break;
			}
		}
	}

	// Update best block in wallet (so we can detect restored wallets)
	bool fIsInitialDownload = IsInitialBlockDownload();
	
	if ((pindexNew->nHeight % 20160) == 0 || (!fIsInitialDownload && (pindexNew->nHeight % 144) == 0))
	{
		const CBlockLocator locator(pindexNew);
		
		g_signals.SetBestChain(locator);
	}

	// New best block
	hashBestChain = hash;
	pindexBest = pindexNew;
	pblockindexFBBHLast = NULL;
	nBestHeight = pindexBest->nHeight;
	nBestChainTrust = pindexNew->nChainTrust;
	nTimeBestReceived = GetTime();
	mempool.AddTransactionsUpdated(1);

	uint256 nBestBlockTrust = pindexBest->nHeight != 0 ? (pindexBest->nChainTrust - pindexBest->pprev->nChainTrust) : pindexBest->nChainTrust;

	LogPrintf(
		"SetBestChain: new best=%s  height=%d  trust=%s  blocktrust=%d  date=%s\n",
		hashBestChain.ToString(),
		nBestHeight,
		CBigNum(nBestChainTrust).ToString(),
		nBestBlockTrust.Get64(),
		DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime())
	);

	// Check the version of the last 100 blocks to see if we need to upgrade:
	if (!fIsInitialDownload)
	{
		int nUpgraded = 0;
		const CBlockIndex* pindex = pindexBest;
		
		for (int i = 0; i < 100 && pindex != NULL; i++)
		{
			if (pindex->nVersion > CBlock::CURRENT_VERSION)
			{
				++nUpgraded;
			}
			
			pindex = pindex->pprev;
		}
		
		if (nUpgraded > 0)
		{
			LogPrintf("SetBestChain: %d of last 100 blocks above version %d\n", nUpgraded, (int)CBlock::CURRENT_VERSION);
		}
		
		if (nUpgraded > 100/2)
		{
			// strMiscWarning is read by GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
			strMiscWarning = ui_translate("Warning: This version is obsolete, upgrade required!");
		}
	}

	std::string strCmd = GetArg("-blocknotify", "");

	if (!fIsInitialDownload && !strCmd.empty())
	{
		boost::replace_all(strCmd, "%s", hashBestChain.GetHex());
		boost::thread t(runCommand, strCmd); // thread runs free
	}

	return true;
}

bool CBlock::AddToBlockIndex(unsigned int nFile, unsigned int nBlockPos, const uint256& hashProof)
{
	AssertLockHeld(cs_main);

	// Check for duplicate
	uint256 hash = GetHash();
	
	if (mapBlockIndex.count(hash))
	{
		return error("AddToBlockIndex() : %s already exists", hash.ToString());
	}
	
	// Construct new block index object
	CBlockIndex* pindexNew = new CBlockIndex(nFile, nBlockPos, *this);
	{
		LOCK(cs_nBlockSequenceId);
		
		pindexNew->nSequenceId = nBlockSequenceId++;
	}
	
	if (!pindexNew)
	{
		return error("AddToBlockIndex() : new CBlockIndex failed");
	}
	
	pindexNew->phashBlock = &hash;
	std::map<uint256, CBlockIndex*>::iterator miPrev = mapBlockIndex.find(hashPrevBlock);
	
	if (miPrev != mapBlockIndex.end())
	{
		pindexNew->pprev = (*miPrev).second;
		pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
	}

	// ppcoin: compute chain trust score
	pindexNew->nChainTrust = (pindexNew->pprev ? pindexNew->pprev->nChainTrust : 0) + pindexNew->GetBlockTrust();

	// ppcoin: compute stake entropy bit for stake modifier
	if (!pindexNew->SetStakeEntropyBit(GetStakeEntropyBit()))
	{
		return error("AddToBlockIndex() : SetStakeEntropyBit() failed");
	}
	
	// Record proof hash value
	pindexNew->hashProof = hashProof;

	// ppcoin: compute stake modifier
	uint64_t nStakeModifier = 0;
	bool fGeneratedStakeModifier = false;
	
	if (!ComputeNextStakeModifier(pindexNew->pprev, nStakeModifier, fGeneratedStakeModifier))
	{
		return error("AddToBlockIndex() : ComputeNextStakeModifier() failed");
	}
	
	pindexNew->SetStakeModifier(nStakeModifier, fGeneratedStakeModifier);
	pindexNew->bnStakeModifierV2 = ComputeStakeModifierV2(pindexNew->pprev, IsProofOfWork() ? hash : vtx[1].vin[0].prevout.hash);

	// Add to mapBlockIndex
	std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
	
	if (pindexNew->IsProofOfStake())
	{
		setStakeSeen.insert(std::make_pair(pindexNew->prevoutStake, pindexNew->nStakeTime));
	}
	
	pindexNew->phashBlock = &((*mi).first);

	// Write to disk block index
	CTxDB txdb;
	
	if (!txdb.TxnBegin())
	{
		return false;
	}
	
	txdb.WriteBlockIndex(CDiskBlockIndex(pindexNew));
	
	if (!txdb.TxnCommit())
	{
		return false;
	}
	
	// New best
	if (pindexNew->nChainTrust > nBestChainTrust)
	{
		if (!SetBestChain(txdb, pindexNew))
		{
			return false;
		}
	}
	
	if (pindexNew == pindexBest)
	{
		// Notify UI to display prev block's coinbase if it was ours
		static uint256 hashPrevBestCoinBase;
		
		g_signals.UpdatedTransaction(hashPrevBestCoinBase);
		
		// v2.0.0.8 CW10: also notify UI of the CURRENT block's coinbase.
		//
		// Without this line the GUI's transaction list lags by exactly
		// one block for locally-mined PoW coinbases: the AddToWallet-
		// fired NotifyTransactionChanged(CT_NEW) ran during ConnectBlock
		// (above) at a moment when the wtx's IsInMainChain() was still
		// false -- pindexNew->pprev->pnext had not yet been set, and
		// pindexBest had not yet been promoted to pindexNew.  The GUI's
		// static handler captures showTransaction at notify-time, so
		// the queued updateTransaction event arrived with
		// showTransaction=false and priv->updateWallet silently dropped
		// the row.
		//
		// By the time execution reaches HERE, both pnext and pindexBest
		// are set correctly (SetBestChain has completed all chain
		// updates), so the re-notify reads IsInMainChain()=true,
		// computes showTransaction=true, and the GUI inserts the row.
		//
		// UpdatedTransaction is a no-op for tx that aren't in our
		// mapWallet (see CWallet::UpdatedTransaction), so this is safe
		// to fire for every connected block regardless of whether we
		// mined it.  Local PoW miners see their coinbases immediately;
		// nodes that didn't mine see no behaviour change.
		//
		// PoS stakers were never affected -- coinstake (vtx[1]) is not
		// a coinbase, so the IsCoinBase()/IsInMainChain() filter in
		// TransactionRecord::showTransaction never excluded it.
		g_signals.UpdatedTransaction(vtx[0].GetHash());
		
		hashPrevBestCoinBase = vtx[0].GetHash();
	}

	return true;
}

bool CBlock::CheckBlock(bool fCheckPOW, bool fCheckMerkleRoot, bool fCheckSig) const
{	
	// These are checks that are independent of context
	// that can be verified before saving an orphan block.
	
	// Size limits
	if (vtx.empty() || vtx.size() > MAX_BLOCK_SIZE || ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
	{
		return DoS(100, error("CheckBlock() : size limits failed"));
	}

	// Check proof of work matches claimed amount
	if (fCheckPOW && IsProofOfWork() && !CheckProofOfWork(GetPoWHash(), nBits))
	{
		return DoS(50, error("CheckBlock() : proof of work failed"));
	}

	// Check timestamp
	if (GetBlockTime() > FutureDrift(GetAdjustedTime()))
	{
		return error("CheckBlock() : block timestamp too far in the future");
	}

	// First transaction must be coinbase, the rest must not be
	if (vtx.empty() || !vtx[0].IsCoinBase())
	{
		return DoS(100, error("CheckBlock() : first tx is not coinbase"));
	}

	for (unsigned int i = 1; i < vtx.size(); i++)
	{
		if (vtx[i].IsCoinBase())
		{
			return DoS(100, error("CheckBlock() : more than one coinbase"));
		}
	}

	if (IsProofOfStake())
	{
		// Coinbase output should be empty if proof-of-stake block
		if (vtx[0].vout.size() != 1 || !vtx[0].vout[0].IsEmpty())
		{
			return DoS(100, error("CheckBlock() : coinbase output not empty for proof-of-stake block"));
		}
		
		// Second transaction must be coinstake, the rest must not be
		if (vtx.empty() || !vtx[1].IsCoinStake())
		{
			return DoS(100, error("CheckBlock() : second tx is not coinstake"));
		}
		
		for (unsigned int i = 2; i < vtx.size(); i++)
		{
			if (vtx[i].IsCoinStake())
			{
				return DoS(100, error("CheckBlock() : more than one coinstake"));
			}
		}
	}

	// Check proof-of-stake block signature
	if (fCheckSig && !CheckBlockSignature())
	{
		return DoS(100, error("CheckBlock() : bad proof-of-stake block signature"));
	}

	// ----------- instantX transaction scanning -----------

	if(IsSporkActive(SPORK_3_INSTANTX_BLOCK_FILTERING))
	{
		for(const CTransaction& tx : vtx)
		{
			if (!tx.IsCoinBase())
			{
				//only reject blocks when it's based on complete consensus
				for(const CTxIn& in : tx.vin)
				{
					if(mapLockedInputs.count(in.prevout) && mapLockedInputs[in.prevout] != tx.GetHash())
					{
						if(fDebug)
						{
							LogPrintf(
								"CheckBlock() : found conflicting transaction with transaction lock %s %s\n",
								mapLockedInputs[in.prevout].ToString().c_str(),
								tx.GetHash().ToString().c_str()
							);
						}
						
						return DoS(0, error("CheckBlock() : found conflicting transaction with transaction lock"));
					}
				}
			}
		}
	}
	else if(fDebug)
	{
		LogPrintf("CheckBlock() : skipping transaction locking checks\n");
	}

	// ----------- masternode / devops - payments -----------
	//
	// v2.0.0.8: the legacy masternode/devops payee-verification block
	// that stood here has been REMOVED.  It was dead code: present
	// unchanged since at least v2.0.0.6, it verified payments via a
	// "foundPaymentAndPayee" test that required a single output to
	// carry BOTH the masternode payee script AND the (mis-seeded,
	// last-vout = devops) payment amount -- a condition that is
	// unsatisfiable whenever the masternode and devops amounts differ.
	// It never enforced anything in production: it was either bypassed
	// by its own permissive branch (legacy GetBlockPayee returning
	// false) or skipped during IBD.  All real masternode/devops
	// payment verification is, and always has been, done by the
	// "Verify coinbase/coinstake tx includes devops payment" block
	// below (the nProofOfIndexMasternode / fBlockHasPayments block),
	// which handles PoW and PoS correctly.  Voted-consensus payee
	// enforcement (GetEnforcedPayee) is folded into that block.
	
	uint256 hashBlock = this->GetHash();
	
	if (mapBlockIndex.count(hashBlock))
	{
		const CBlockIndex* pindex = mapBlockIndex[hashBlock];
	
		// Per-block height echo: useful only when tracing the verify pass /
		// block validation in detail.  Gated so it does not spray the normal
		// log (it fired once per block, incl. every block of the 500-block
		// startup verify).  -debug=masternode (or =1) restores it.
		LogPrint("checkblock", "pindex->nHeight = %d\n", pindex->nHeight);

		// v2.0.0.8: fIsInitialDownload was previously declared in the now-
		// removed legacy payee block above; this block depends on it for
		// the masternode-checks-delay timing below, so it is declared here.
		bool fIsInitialDownload = IsInitialBlockDownload();
		
		// Verify coinbase/coinstake tx includes devops payment -
		// first check for start of devops payments
		int64_t pindexBestBlockTime = pindex->GetBlockTime();

		// Fork toggle for payment upgrade
		bool bDevOpsPayment = (pindexBestBlockTime > VERION_1_0_0_0_MANDATORY_UPDATE_START);
		
		// Run checks if at fork height
		if(bDevOpsPayment)
		{
			int64_t nStandardPayment = 0;
			int64_t nMasternodePayment = 0;
			int64_t nDevopsPayment = 0;
			int64_t nProofOfIndexMasternode = 0;
			int64_t nProofOfIndexDevops = 0;
			int64_t nMasterNodeChecksDelay = 45 * 60;
			int64_t nMasterNodeChecksEngageTime = 0;
			const CBlockIndex* pindexPrev = pindex->pprev;
			bool isProofOfStake = !IsProofOfWork();
			bool fBlockHasPayments = true;
			std::string strVfyDevopsAddress;
			// Define primitives depending if PoW/PoS

			if (isProofOfStake)
			{
				nProofOfIndexMasternode = 2;
				nProofOfIndexDevops = 3;

				if (vtx[isProofOfStake].vout.size() != 4)
				{
					if (vtx[isProofOfStake].vout.size() != 5)
					{
						LogPrintf("CheckBlock() : PoS submission doesn't include devops and/or masternode payment\n");
						fBlockHasPayments = false;
					}
					else
					{
						nProofOfIndexMasternode = 3;
						nProofOfIndexDevops = 4;
					}
				}

				nStandardPayment = GetProofOfStakeReward(pindexPrev, 0, 0);
			}
			else
			{
				nProofOfIndexMasternode = 1;
				nProofOfIndexDevops = 2;
				if (vtx[isProofOfStake].vout.size() != 3)
				{
					LogPrintf("CheckBlock() : PoW submission doesn't include devops and/or masternode payment\n");
					fBlockHasPayments = false;
				}
				
				nStandardPayment = GetProofOfWorkReward(pindex->nHeight, 0);
			}
			
			// Set payout values depending if PoW/PoS
			nMasternodePayment = GetMasternodePayment(pindex->nHeight, nStandardPayment) / COIN;
			nDevopsPayment = GetDevOpsPayment(pindex->nHeight, nStandardPayment) / COIN;
			
			LogPrint("checkblock", "Hardset MasternodePayment: %lu | Hardset DevOpsPayment: %lu \n", nMasternodePayment, nDevopsPayment);
			
			// Increase time for Masternode checks delay during sync per-block
			if (fIsInitialDownload)
			{
				nMasterNodeChecksDelayBaseTime = GetTime();
			}
			else
			{
				nMasterNodeChecksEngageTime = nMasterNodeChecksDelayBaseTime + nMasterNodeChecksDelay;
			}
			
			strVfyDevopsAddress = getDevelopersAdress(pindex);
			
			// Check PoW or PoS payments for current block
			for (unsigned int i=0; i < vtx[isProofOfStake].vout.size(); i++)
			{
				// Define values
				CScript rawPayee = vtx[isProofOfStake].vout[i].scriptPubKey;
				CTxDestination address;
				ExtractDestination(vtx[isProofOfStake].vout[i].scriptPubKey, address);
				CBitcoinAddress addressOut(address);
				int64_t nAmount = vtx[isProofOfStake].vout[i].nValue / COIN;
				int64_t nIndexedMasternodePayment = vtx[isProofOfStake].vout[nProofOfIndexMasternode].nValue / COIN;
				int64_t nIndexedDevopsPayment = vtx[isProofOfStake].vout[nProofOfIndexDevops].nValue / COIN;
				// LogPrintf(" - vtx[%d].vout[%d] Address: %s Amount: %lu \n", isProofOfStake, i, addressOut.ToString(), nAmount);
				
				// PoS Checks
				if (isProofOfStake)
				{
					// Check for PoS masternode payment.
					//
					// v2.0.0.8 Spec C fix (corrected): the masternode-payee
					// verification below (weak check + voted-consensus
					// enforcement) depends on runtime, present-moment state
					// -- the MN list (vMasternodes) and the vote tracker.
					// It is meaningful ONLY when checking a brand-new block
					// at the live chain tip on a synced node.  It must be
					// skipped during (a) IBD catch-up and (b) the startup
					// "Verifying last N blocks" re-validation pass -- in
					// both, that state is absent and the check would
					// wrongly reject the node's own valid history
					// (observed: repeated multi-hundred-block rollbacks).
					//
					// The pre-2.0.0.8 legacy payee block had TWO guards for
					// exactly these two cases; v2.0.0.8 deleted the block
					// and both guards.  This reinstates both, faithfully:
					//   - !fIsInitialDownload : skip during IBD catch-up.
					//   - hashPrevBlock == hashBestChain : this block EXTENDS
					//     the current best tip.  This is the original
					//     2.0.0.6 / 2.0.0.7 guard (legacy block:
					//     pindex->GetBlockHash() == hashPrevBlock with
					//     pindex = pindexBest), restored verbatim in meaning.
					//     A genuinely new block being connected at the live
					//     tip has hashPrevBlock == the still-current
					//     hashBestChain (pindexBest/hashBestChain are updated
					//     only AFTER ConnectBlock, at SetBestChain ~953-954),
					//     so the check RUNS.  During the startup "Verifying
					//     last N blocks" pass NO block satisfies this -- not
					//     even the stored tip, whose hashPrevBlock points at
					//     tip-1, not at itself -- so the whole verify pass is
					//     skipped.  During a reorg the connecting blocks do
					//     not extend the pre-reorg tip either, so the check
					//     skips there and falls through to legacy (the safe
					//     direction; those blocks were already strict-checked
					//     on first receipt via ProcessBlock).
					// SUPERSEDES the session-14b `pindex->pnext == NULL`
					// substitute, which was NOT equivalent: pnext == NULL is
					// true for BOTH a live new tip and the stored tip during
					// the verify pass, so it could not distinguish them and
					// rejected the node's own tip on post-activation restart.
					// NOTE: !fIsInitialDownload ALONE is insufficient --
					// IsInitialBlockDownload() is a staleness heuristic and
					// returns false during the startup verify pass whenever
					// the stored tip is recent (<8h old), i.e. on every
					// normal restart.  The hashPrevBlock guard is what covers
					// that.  The devops + miner-reward checks are NOT gated --
					// they do not depend on MN-list state and are valid at
					// startup (and always ran there historically).
					if (i == nProofOfIndexMasternode && !fIsInitialDownload &&
						hashPrevBlock == hashBestChain)
					{
						if (mnodeman.IsPayeeAValidMasternode(rawPayee) ||
							addressOut.ToString() == strVfyDevopsAddress)
						{
							LogPrint("checkblock", "CheckBlock() : PoS Recipient masternode address validity succesfully verified\n");
						}
						else
						{
							// v2.0.0.8 CW12: gate the weak mn-list check on
							// voted-consensus activation height -- the canonical
							// "post-activation?" check (same gate used by the
							// strong voted-consensus check, via GetEnforcedPayee).
							//
							// HISTORY.  v2.0.0.6 had this strict check gated by
							// `fMnAdvRelay` (defaulting to false, never toggled
							// to true on production mainnet).  The effective
							// v2.0.0.6 mainnet behaviour was: this check never
							// fired.  v2.0.0.8 "Spec C D2" removed the
							// fMnAdvRelay gate entirely on the principle that
							// "consensus enforcement must never ship gated
							// behind an undocumented flag" -- correct in
							// principle, but it removed the SOLE gate keeping
							// the weak check from firing pre-activation.
							//
							// The pre-activation firing surfaced as a
							// network-partition-class bug on testnet:
							// block 206 saw 5 of 8 nodes ban the LAN gateway
							// IP (via NAT hairpin) and stall for hours.  Root
							// cause: an honest peer relaying a block paying
							// a newly-registered mn was instant-banned
							// (DoS 100) by every node that hadn't yet
							// received the mn's dseep broadcast -- a normal
							// gossip propagation race, not byzantine
							// behaviour.
							//
							// ARCHITECTURE.  This check ("is the payee a
							// registered mn?") is logically a SUBSET of the
							// voted-consensus check ("is the payee the
							// SPECIFIC voted-consensus mn?").  Post-
							// activation, the voted-consensus check
							// supersedes it (any voted payee is necessarily
							// a registered mn).  Pre-activation, the legacy
							// CMasternodePayments path doesn't require any
							// specific mn-list membership of the payee, so
							// enforcing this check pre-activation enforces a
							// rule that didn't exist in v2.0.0.6.
							//
							// GATE.  Using GetEffectiveVotedConsensusActivationHeight()
							// gives us:
							//   - Pre-spork on mainnet (floor = INT_MAX): never
							//     fires, matching v2.0.0.6 effective behaviour
							//     byte-for-byte
							//   - On testnet (floor = 2000): fires from height
							//     2000 onwards, the same height at which
							//     voted-consensus also activates
							//   - SPORK_15 lowers both gates together:
							//     coordinated rollout, no awkward partial state
							const int nWeakCheckActivationHeight =
								GetEffectiveVotedConsensusActivationHeight();

							if (nMasterNodeChecksEngageTime != 0 &&
								pindex->nHeight >= nWeakCheckActivationHeight)
							{
								LogPrintf("CheckBlock() : PoS Recipient masternode address validity could not be verified -- rejecting\n");

								fBlockHasPayments = false;
							}
						}

						// v2.0.0.8 PB-POWENF: voted-consensus payee enforcement
						// for PoS blocks.  Mirror of the PoW-side block below.
						//
						// The weak check above only verifies the payee is SOME
						// registered masternode.  The legacy payee-verification
						// block that previously held the PoS GetEnforcedPayee
						// hook has been removed (it was dead code -- see the
						// note where it stood).  Without this, the PoS path
						// would have NO voted-consensus enforcement while the
						// PoW path does -- an asymmetry.  This closes it: a
						// staker that pays a valid-but-not-voted masternode has
						// its block rejected, identically to the PoW path.
						//
						// GetEnforcedPayee returns the voted-consensus payee
						// only when past activation height AND consensus formed;
						// otherwise false/empty, and we fall through to the
						// weak check above (preserving soft-fork rollout --
						// nothing strict happens pre-activation or with no
						// 60% vote).
						//
						// Gated identically to the PoW block: the post-startup
						// checks-delay warmup must have elapsed
						// (nMasterNodeChecksEngageTime != 0), so PoS and PoW
						// enforce under identical conditions.
						// v2.0.0.8 Spec C: fMnAdvRelay gate removed.  The strict
						// voted-consensus check now engages on its own merits --
						// warmup elapsed (nMasterNodeChecksEngageTime != 0) plus
						// GetEnforcedPayee returning an enforceable payee (which
						// itself self-gates on activation height + consensus).
						if (nMasterNodeChecksEngageTime != 0)
						{
							CScript enforcedPayee;
							CTxIn   enforcedVin;

							if (GetEnforcedPayee(pindex->nHeight, enforcedPayee, enforcedVin) &&
								enforcedPayee != CScript())
							{
								if (rawPayee == enforcedPayee)
								{
									LogPrint("checkblock", "CheckBlock() : PoS masternode payee matches voted consensus\n");
								}
								else if (addressOut.ToString() == strVfyDevopsAddress)
								{
									// v2.0.0.8 Spec C D3/D4: the block pays the devops
									// address in the MN slot -- the rare "masternode
									// cannot be determined" fallback.  The weak check
									// allows this (see the IsPayeeAValidMasternode ||
									// devops test above); the strict check must not
									// reject it or it would reject blocks the weak
									// check passes.  ALLOW it -- but loudly: at/after
									// activation this means voted consensus produced
									// NO payee for this height, which should not
									// happen if consensus is healthy.  Unconditional
									// LogPrintf (NOT fDebug) -- monitored signal.
									{
										int nVotedActivation = GetEffectiveVotedConsensusActivationHeight();

										if (pindex->nHeight >= nVotedActivation)
										{
											LogPrintf("CheckBlock() : NOTICE - PoS height %d at/after "
													  "voted-consensus activation %d but block pays the "
													  "devops fallback in the masternode slot -- voted "
													  "consensus produced no payee for this height. "
													  "Consensus coverage gap; investigate.\n",
													  pindex->nHeight, nVotedActivation);
										}
									}
								}
								else
								{
									CTxDestination encDest;
									ExtractDestination(enforcedPayee, encDest);
									CBitcoinAddress encAddr(encDest);

									LogPrintf("CheckBlock() : PoS masternode payee %s does NOT match "
											  "voted-consensus payee %s at height %d -- rejecting\n",
											  addressOut.ToString().c_str(),
											  encAddr.ToString().c_str(),
											  pindex->nHeight);

									fBlockHasPayments = false;
								}
							}
							else
							{
								if (fDebug)
								{
									LogPrintf("CheckBlock() : PoS no enforceable voted payee at height %d "
											  "(pre-activation or no consensus) -- weak check only\n",
											  pindex->nHeight);
								}
							}
						}

						if (nIndexedMasternodePayment == nMasternodePayment)
						{
							LogPrint("checkblock", "CheckBlock() : PoS Recipient masternode amount validity succesfully verified\n");
						}
						else
						{
							LogPrintf("CheckBlock() : PoS Recipient masternode amount validity could not be verified\n");

							fBlockHasPayments = false;
						}
					}
					
					// Check for PoS devops payment
					if (i == nProofOfIndexDevops)
					{
						if (addressOut.ToString() == strVfyDevopsAddress)
						{
							LogPrint("checkblock", "CheckBlock() : PoS Recipient devops address validity succesfully verified\n");
						}
						else
						{
							LogPrintf("CheckBlock() : PoS Recipient devops address validity could not be verified -- expected %s, got %s\n",
								strVfyDevopsAddress.c_str(),
								addressOut.ToString().c_str());
							
							// v2.0.0.8 CW9: re-enable strict devops-address
							// enforcement, height-gated.
							//
							// Pre-rotation: lax (log-only).  Preserves canonical
							// chain history where some blocks paid addresses
							// the ladder doesn't predict, due to the
							// v1.0.1.5/v1.0.1.6/v1.0.1.7 transition mess
							// (Jul 2-4 2019) and the v1.0.4.2 chain-correction.
							//
							// Post-rotation: strict.  All v2.0.0.8+ producers
							// compute the same expected address via
							// getDevelopersAdressForHeight(), so any block
							// reaching here with a mismatch is either a forgery
							// or a misconfiguration.  Either case is rejected.
							const int nStrictHeight = TestNet()
								? VERION_2_0_1_0_TESTNET_UPDATE_BLOCK
								: VERION_2_0_1_0_MANDATORY_UPDATE_BLOCK;
							
							if (pindex->nHeight >= nStrictHeight)
							{
								fBlockHasPayments = false;
							}
						}
						
						if (nIndexedDevopsPayment == nDevopsPayment)
						{
							LogPrint("checkblock", "CheckBlock() : PoS Recipient devops amount validity succesfully verified\n");
						}
						else
						{
							if (pindexBestBlockTime < VERION_1_0_1_5_MANDATORY_UPDATE_START)
							{
								LogPrintf("CheckBlock() : PoS Recipient devops amount validity could not be verified\n");

								fBlockHasPayments = false;
							}
							else
							{
								if (nIndexedDevopsPayment >= nDevopsPayment)
								{
									LogPrintf("CheckBlock() : PoS Reciepient devops amount is abnormal due to large fee paid");
								}
								else
								{
									LogPrintf("CheckBlock() : PoS Reciepient devops amount validity could not be verified\n");

									fBlockHasPayments = false;
								}
							}
						}
					}
				}
				// PoW Checks
				else
				{
					// Check for PoW masternode payment.
					// v2.0.0.8 Spec C fix (corrected): two-guard gate --
					// see the PoS counterpart above for full rationale.
					// !fIsInitialDownload skips IBD catch-up;
					// hashPrevBlock == hashBestChain restricts the check to a
					// block that EXTENDS the current best tip -- the original
					// 2.0.0.6 / 2.0.0.7 guard.  This skips the startup verify
					// pass (no stored block, not even the tip, extends the
					// current tip) and reorg connects, while still running on
					// a live new tip block.  SUPERSEDES the session-14b
					// pindex->pnext == NULL substitute, which rejected the
					// node's own tip on post-activation restart.
					if (i == nProofOfIndexMasternode && !fIsInitialDownload &&
						hashPrevBlock == hashBestChain)
					{
						if (mnodeman.IsPayeeAValidMasternode(rawPayee) ||
							addressOut.ToString() == strVfyDevopsAddress)
						{
						  LogPrint("checkblock", "CheckBlock() : PoW Recipient masternode address validity succesfully verified\n");
						}
						else
						{
							// v2.0.0.8 CW12: gate the weak mn-list check on
							// voted-consensus activation height.  See PoS
							// counterpart above for full rationale.  Mirror.
							const int nWeakCheckActivationHeight =
								GetEffectiveVotedConsensusActivationHeight();

							if (nMasterNodeChecksEngageTime != 0 &&
								pindex->nHeight >= nWeakCheckActivationHeight)
							{
								LogPrintf("CheckBlock() : PoW Recipient masternode address validity could not be verified -- rejecting\n");
								fBlockHasPayments = false;
							}
						}

						// v2.0.0.8 PB-POWENF: voted-consensus payee enforcement
						// for PoW blocks.
						//
						// The weak check above only verifies the payee is SOME
						// registered masternode.  Pre-this-patch, the PoW path
						// stopped there -- so a PoW miner could pay any valid MN
						// (e.g. one it controls) instead of the consensus-voted
						// winner, and the block was accepted.  The PoS path is
						// strict (GetEnforcedPayee match enforced in the earlier
						// masternode-payment block); the PoW path was not.  This
						// closes that asymmetry: a PoW miner that ignores the
						// vote now has its block rejected.
						//
						// GetEnforcedPayee returns the voted-consensus payee
						// only when past activation height AND consensus formed;
						// otherwise it returns false / empty.  When it does NOT
						// return an enforceable payee we fall through to the
						// weak check above -- this preserves the soft-fork
						// rollout behaviour (nothing strict happens until the
						// chain is past activation and a 60% vote exists).
						//
						// Gated the same way as the weak check:
						//   - nMasterNodeChecksEngageTime != 0 : the post-startup
						//     "checks delay" warmup has elapsed (the node has a
						//     settled MN list and synced vote state).  Enforcing
						//     before this would wrongly reject good blocks.
						//
						// Companion to the PoS-side enforcement; see also
						// GetEnforcedPayee in this file and miner.cpp (the block
						// CREATOR already routes through GetEnforcedPayee, so an
						// honest miner is unaffected -- only a miner that pays
						// the wrong MN is rejected).
						// v2.0.0.8 Spec C: fMnAdvRelay gate removed.  See the PoS
						// counterpart above for rationale.
						if (nMasterNodeChecksEngageTime != 0)
						{
							CScript enforcedPayee;
							CTxIn   enforcedVin;

							if (GetEnforcedPayee(pindex->nHeight, enforcedPayee, enforcedVin) &&
								enforcedPayee != CScript())
							{
								if (rawPayee == enforcedPayee)
								{
									LogPrint("checkblock", "CheckBlock() : PoW masternode payee matches voted consensus\n");
								}
								else if (addressOut.ToString() == strVfyDevopsAddress)
								{
									// v2.0.0.8 Spec C D3/D4: devops-fallback payee in
									// the MN slot.  Allow (the weak check does); but
									// loudly NOTICE it at/after activation -- see the
									// PoS counterpart above for full rationale.
									{
										int nVotedActivation = GetEffectiveVotedConsensusActivationHeight();

										if (pindex->nHeight >= nVotedActivation)
										{
											LogPrintf("CheckBlock() : NOTICE - PoW height %d at/after "
													  "voted-consensus activation %d but block pays the "
													  "devops fallback in the masternode slot -- voted "
													  "consensus produced no payee for this height. "
													  "Consensus coverage gap; investigate.\n",
													  pindex->nHeight, nVotedActivation);
										}
									}
								}
								else
								{
									CTxDestination encDest;
									ExtractDestination(enforcedPayee, encDest);
									CBitcoinAddress encAddr(encDest);

									LogPrintf("CheckBlock() : PoW masternode payee %s does NOT match "
											  "voted-consensus payee %s at height %d -- rejecting\n",
											  addressOut.ToString().c_str(),
											  encAddr.ToString().c_str(),
											  pindex->nHeight);

									fBlockHasPayments = false;
								}
							}
							else
							{
								if (fDebug)
								{
									LogPrintf("CheckBlock() : PoW no enforceable voted payee at height %d "
											  "(pre-activation or no consensus) -- weak check only\n",
											  pindex->nHeight);
								}
							}
						}

						if (nAmount == nMasternodePayment)
						{
							LogPrint("checkblock", "CheckBlock() : PoW Recipient masternode amount validity succesfully verified\n");
						}
						else
						{
							LogPrintf("CheckBlock() : PoW Recipient masternode amount validity could not be verified\n");
							fBlockHasPayments = false;
						}
					}
					
					// Check for PoW devops payment
					if (i == nProofOfIndexDevops)
					{
						if (addressOut.ToString() == strVfyDevopsAddress)
						{
							LogPrint("checkblock", "CheckBlock() : PoW Recipient devops address validity succesfully verified\n");
						}
						else
						{
							LogPrintf("CheckBlock() : PoW Recipient devops address validity could not be verified -- expected %s, got %s\n",
								strVfyDevopsAddress.c_str(),
								addressOut.ToString().c_str());
							
							// v2.0.0.8 CW9: re-enable strict devops-address
							// enforcement, height-gated.  Symmetric with the
							// PoS path above (same rationale).
							const int nStrictHeight = TestNet()
								? VERION_2_0_1_0_TESTNET_UPDATE_BLOCK
								: VERION_2_0_1_0_MANDATORY_UPDATE_BLOCK;
							
							if (pindex->nHeight >= nStrictHeight)
							{
								fBlockHasPayments = false;
							}
						}
					   
						if (nAmount == nDevopsPayment)
						{
							LogPrint("checkblock", "CheckBlock() : PoW Recipient devops amount validity succesfully verified\n");
						}
						else
						{
							if (pindexBestBlockTime < VERION_1_0_1_5_MANDATORY_UPDATE_START)
							{
								LogPrintf("CheckBlock() : PoW Recipient devops amount validity could not be verified\n");
								fBlockHasPayments = false;
							}
							else
							{
								if (nIndexedDevopsPayment >= nDevopsPayment)
								{
									LogPrintf("CheckBlock() : PoW Reciepient devops amount is abnormal due to large fee paid");
								}
								else
								{
									LogPrintf("CheckBlock() : PoW Reciepient devops amount validity could not be verified");
									fBlockHasPayments = false;
								}
							}
						}
					}
				}
			}
			
			// Final checks (DevOps/Masternode payments)
			if (fBlockHasPayments)
			{
				LogPrint("checkblock", "CheckBlock() : PoW/PoS non-miner reward payments succesfully verified\n");
			}
			else
			{
				LogPrintf("CheckBlock() : PoW/PoS non-miner reward payments could not be verified\n");

				// v2.0.0.8: raised from DoS(10) to DoS(100).  A block that
				// reaches here with fBlockHasPayments == false has a genuine
				// payment violation -- wrong masternode/devops amount, wrong
				// coinbase/coinstake output structure, or (post warmup, with
				// advisory relay on) a masternode payee that does not match
				// voted consensus.  All of these are hard consensus failures,
				// not minor misbehaviour, so the peer is scored accordingly.
				// The startup checks-delay grace window is handled UPSTREAM:
				// the address-validity branches only set fBlockHasPayments =
				// false once nMasterNodeChecksEngageTime != 0, so a node still
				// in warmup never reaches this DoS for an address reason.
				return DoS(100, error("CheckBlock() : PoW/PoS invalid payments in current block\n"));
			}
		}

	}
	else
	{
		LogPrintf("pindex->nHeight = ???\n");
	}
	
	
	// Check transactions
	for(const CTransaction& tx : vtx)
	{
		if (!tx.CheckTransaction())
		{
			return DoS(tx.nDoS, error("CheckBlock() : CheckTransaction failed"));
		}
		
		// ppcoin: check transaction timestamp
		if (GetBlockTime() < (int64_t)tx.nTime)
		{
			return DoS(50, error("CheckBlock() : block timestamp earlier than transaction timestamp"));
		}
	}

	// Check for duplicate txids. This is caught by ConnectInputs(),
	// but catching it earlier avoids a potential DoS attack:
	std::set<uint256> uniqueTx;
	for(const CTransaction& tx : vtx)
	{
		uniqueTx.insert(tx.GetHash());
	}

	if (uniqueTx.size() != vtx.size())
	{
		return DoS(100, error("CheckBlock() : duplicate transaction"));
	}

	unsigned int nSigOps = 0;
	for(const CTransaction& tx : vtx)
	{
		nSigOps += GetLegacySigOpCount(tx);
	}

	if (nSigOps > MAX_BLOCK_SIGOPS)
	{
		return DoS(100, error("CheckBlock() : out-of-bounds SigOpCount"));
	}

	// Check merkle root
	if (fCheckMerkleRoot && hashMerkleRoot != BuildMerkleTree())
	{
		return DoS(100, error("CheckBlock() : hashMerkleRoot mismatch"));
	}

	return true;
}

// ---------------------------------------------------------------------------
// CW8 v2.0.0.8: Historical mainnet nBits exception list
//
// Two categorically distinct classes; both are canonical chain history,
// both must be honoured by any conforming validator, but they have
// unrelated origins:
//
// Class A — controlled fork operations (pre-existing, 4 entries):
//   - 46921, 46923, 46924: v1.0.1.5 mandatory-update activation cluster,
//     May 2019.  Three blocks within ~3 minutes, all at floor difficulty
//     (1f00ffff), all carrying the activation transition for the mandatory
//     upgrade gated by VERION_1_0_1_5_MANDATORY_UPDATE_START.
//   - 403116: predecessor block to the v1.0.4.2 chain-correction hardfork
//     at height 403117.  Floor difficulty (1f00ffff) was set to provide a
//     deterministic, instantly-mineable anchor block.  Block 403117 itself
//     carries the one-shot 1,000,000,000 XDN treasury operation via the
//     `nHeight == VERION_1_0_4_2_MANDATORY_UPDATE_BLOCK` branch in
//     GetDevOpsPayment; 403117's own nBits is consensus-derivable so it is
//     NOT in this list.
//
// Class B — stall-recovery archaeology (v2.0.0.8 D.1.4, 26 entries):
//   Blocks where v2.0.0.6's broken VRX_ThreadCurve produced different
//   nBits than v2.0.0.8's working curve computes.  v2.0.0.6's recovery
//   loop never engaged (difTime was always zero on PoW retarget); during
//   long stalls the miner computed difficulty from the standard NORMAL
//   retarget path while v2.0.0.8's working curve correctly drops
//   difficulty toward the floor.  Each Class B block is a stall-recovery
//   event somewhere in mainnet history.  Two extreme cases (394624,
//   423410) hit the 1f00ffff floor on v2.0.0.8's working curve.
//
// Architectural property preserved: the strict nBits check remains fully
// active.  There is no tolerance band, no leniency, no relaxed
// comparison.  This is a height-keyed allow-list that the validator
// consults before applying the strict check -- a forgery at any
// non-exception height fails immediately, and a forgery at an exception
// height would require winning the chain-work race for that historical
// block (computationally infeasible).
//
// Exception list closure: every block mined under v2.0.0.8 produces
// nBits from the deterministic working curve, so miner and validator
// necessarily agree (CW7 closes the residual hourly-boundary risk).
// This list never grows after v2.0.0.8 tag.
// ---------------------------------------------------------------------------
static const int nBitsExceptions[] = {
	// Class A originals (controlled fork operations):
	46921, 46923, 46924,
	83725,
	130076, 131170,
	137697, 138092, 138895,
	210236,
	294248, 296125,
	318904,
	394624,
	403116,                     // <-- Class A: v1.0.4.2 rollback anchor
	403375,
	423410,
	514282,
	638810,
	668693,
	735105, 753107,
	783207, 786402,
	842448, 847854, 856744, 862212,
	900058,
	1010584,
};

static bool IsNBitsExceptionHeight(int nHeight)
{
	// Compile-time assertion that the list stays sorted; std::binary_search
	// returns nonsense otherwise.  Cheap to verify; protects against
	// merge-conflict-induced disorder when adding any future entry.
	return std::binary_search(
		std::begin(nBitsExceptions),
		std::end(nBitsExceptions),
		nHeight
	);
}

bool CBlock::AcceptBlock()
{
	AssertLockHeld(cs_main);

	// Check for duplicate
	uint256 hash = GetHash();
	
	if (mapBlockIndex.count(hash))
	{
		return error("AcceptBlock() : block already in mapBlockIndex");
	}

	// Get prev block index
	std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashPrevBlock);
	if (mi == mapBlockIndex.end())
	{
		return DoS(10, error("AcceptBlock() : prev block not found"));
	}

	CBlockIndex* pindexPrev = (*mi).second;
	int nHeight = pindexPrev->nHeight+1;
	
	// Check created block for version control
	if (nVersion < 7)
	{
		return DoS(100, error("AcceptBlock() : reject too old nVersion = %d", nVersion));
	}
	else if (nVersion > 7)
	{
		return DoS(100, error("AcceptBlock() : reject too new nVersion = %d", nVersion));
	}

	// Check block against Velocity parameters
	if(Velocity_check(nHeight))
	{
		// Announce Velocity constraint failure
		if(!Velocity(pindexPrev, this))
		{
			return DoS(100, error("AcceptBlock() : Velocity rejected block %d, required parameters not met", nHeight));
		}
	}

	uint256 hashProof;
	if (IsProofOfWork() && nHeight > Params().EndPoWBlock())
	{
		return DoS(100, error("AcceptBlock() : reject proof-of-work at height %d", nHeight));
	}
	else
	{
		// PoW is checked in CheckBlock()
		if (IsProofOfWork())
		{
			hashProof = GetPoWHash();
		}
	}

	if (IsProofOfStake() && nHeight < Params().StartPoSBlock())
	{
		return DoS(100, error("AcceptBlock() : reject proof-of-stake at height <= %d", nHeight));
	}

	// Check coinbase timestamp
	if (GetBlockTime() > FutureDrift((int64_t)vtx[0].nTime) && IsProofOfStake())
	{
		return DoS(50, error("AcceptBlock() : coinbase timestamp is too early"));
	}

	// Check coinstake timestamp
	if (IsProofOfStake() && !CheckCoinStakeTimestamp(nHeight, GetBlockTime(), (int64_t)vtx[1].nTime))
	{
		return DoS(50, error("AcceptBlock() : coinstake timestamp violation nTimeBlock=%d nTimeTx=%u", GetBlockTime(), vtx[1].nTime));
	}
	
	// Check proof-of-work or proof-of-stake
	/*
		The following block has this case:
			46921, 46923, 46924
	*/
	// v2.0.0.8 RESYNC FIX: pass this block's OWN timestamp (GetBlockTime())
	// as nNewBlockTime.  The VRX difficulty-recovery curve must measure
	// stall time from the targeted block's fixed timestamp, not from the
	// validating node's wall clock -- otherwise re-validating a historical
	// block during a resync computes a different nBits than the block
	// carries and AcceptBlock rejects the entire chain past height 130.
	// GetBlockTime() here is the candidate block's real, committed
	// timestamp -- identical on every node, now and forever.
	//
	// v2.0.0.8 DIAGNOSTIC: compute the required target once into a local
	// so the failure path can log BOTH the value the block carries and the
	// value this node computed.  Pure logging -- no behaviour change.
	unsigned int nBitsRequired = GetNextTargetRequired(pindexPrev, IsProofOfStake(), GetBlockTime());

	// v2.0.0.8 CW8: height-keyed exception list now sits in a sorted
	// constant array consulted via IsNBitsExceptionHeight().  See the
	// list definition above this function for the two-class provenance
	// (Class A controlled fork operations + Class B stall-recovery
	// archaeology).
	if (!IsNBitsExceptionHeight(nHeight) && nBits != nBitsRequired)
	{
		LogPrintf("AcceptBlock() : nBits MISMATCH at height %d [%s] -- block carries nBits=%08x, this node computed=%08x, blockTime=%d\n",
				  nHeight, IsProofOfStake() ? "PoS" : "PoW", nBits, nBitsRequired, (int64_t)GetBlockTime());

		return DoS(100, error("AcceptBlock() : incorrect %s", IsProofOfWork() ? "proof-of-work" : "proof-of-stake"));
	}

	// Check timestamp against prev
	if (GetBlockTime() <= pindexPrev->GetPastTimeLimit() || FutureDrift(GetBlockTime()) < pindexPrev->GetBlockTime())
	{
		return error("AcceptBlock() : block's timestamp is too early");
	}

	// Check that all transactions are finalized
	for(const CTransaction& tx : vtx)
	{
		if (!IsFinalTx(tx, nHeight, GetBlockTime()))
		{
			return DoS(10, error("AcceptBlock() : contains a non-final transaction"));
		}
	}
	
	//
	// Extra transaction check to protect minting attack aka Monte Spoof Attack
	//
	try
	{
		//
		// First 10000 blocks ignored with extra check because of PoS with exceptions.
		// This will be handled by checkpoint.cpp
		//
		if(nHeight > 170)
		{
			// Set logged values
			CAmount tx_inputs_values = 0;
			CAmount tx_outputs_values = 0;
			CAmount block_reward = GetProofOfWorkReward(nHeight, 0);
			
			// Check that all transactions are finalized
			for(const CTransaction& tx : vtx)
			{
				mapPrevTx_t mapInputs;
				CAmount tx_MapIn_values, tx_MapOut_values;
				
				// Translate input hashes to transactions
				if(!tx.GetMapTxInputs(mapInputs, true))
				{
					return DoS(10, error("AcceptBlock() : can not map tx inputs."));
				}

				// Get transaction inputs/outputs values
				tx_MapIn_values = tx.GetValueMapIn(mapInputs);
				tx_MapOut_values = tx.GetValueOut();

				// Increase total inputs values
				if(tx_inputs_values + tx_MapIn_values >= 0)
				{
					tx_inputs_values += tx_MapIn_values;
				}
				else
				{
					return DoS(100, error("AcceptBlock(): overflow detected tx_inputs_values + tx.GetValueMapIn(mapInputs)\n"));
				}
				
				// Increase total output values
				if(tx_outputs_values + tx_MapOut_values >= 0)
				{
					tx_outputs_values += tx_MapOut_values;
				}
				else
				{
					return DoS(100, error("AcceptBlock(): overflow detected tx_outputs_values + tx.GetValueOut()\n"));
				}
			}
			
			//
			// Check if all transactions added up looks valid
			//
			if((tx_inputs_values + block_reward) < tx_outputs_values)
			{
				CAmount tx_diff = tx_outputs_values - tx_inputs_values - (300 * COIN);
				
				return DoS(100, error("AcceptBlock() : Transactions inside Block %d contains inputs that is less than outputs. diff = %s\n", nHeight, FormatMoney(tx_diff).c_str()));
			}
		}
	}
	//
	// GetValueMapIn can trigger an exception when transaction input can not be translated to a value 
	//
	catch(...)
	{
		//
		// Existing Blocks that will have transactions double spend in one block will give a warning.
		// New blocks will be stopped to protect agains attack
		//
		if(nHeight > 403084)
		{
			return DoS(100, error("AcceptBlock(): Block %d contains at least two transactions that uses the same coin.\n", nHeight));
		}
		else
		{
			printf("AcceptBlock(): can't check block %d with input/output check.\n", nHeight);
		}
	}
	
	// Check that the block chain matches the known block chain up to a checkpoint
	if (!Checkpoints::CheckHardened(nHeight, hash))
	{
		return DoS(100, error("AcceptBlock() : rejected by hardened checkpoint lock-in at %d", nHeight));
	}

	// Verify hash target and signature of coinstake tx
	if (IsProofOfStake())
	{
		uint256 targetProofOfStake;
		if (!CheckProofOfStake(pindexPrev, vtx[1], nBits, hashProof, targetProofOfStake))
		{
			return error("AcceptBlock() : check proof-of-stake failed for block %s", hash.ToString());
		}
	}

	// Check that the block satisfies synchronized checkpoint
	if (!Checkpoints::CheckSync(nHeight))
	{
		return error("AcceptBlock() : rejected by synchronized checkpoint");
	}
	
	// Enforce rule that the coinbase starts with serialized block height
	CScript expect = CScript() << nHeight;
	if (
		vtx[0].vin[0].scriptSig.size() < expect.size() ||
		!std::equal(expect.begin(), expect.end(), vtx[0].vin[0].scriptSig.begin())
	)
	{
		return DoS(100, error("AcceptBlock() : block height mismatch in coinbase"));
	}

	// Write block to history file
	if (!CheckDiskSpace(::GetSerializeSize(*this, SER_DISK, CLIENT_VERSION)))
	{
		return error("AcceptBlock() : out of disk space");
	}

	unsigned int nFile = -1;
	unsigned int nBlockPos = 0;
	
	if (!WriteToDisk(nFile, nBlockPos))
	{
		return error("AcceptBlock() : WriteToDisk failed");
	}

	if (!AddToBlockIndex(nFile, nBlockPos, hashProof))
	{
		return error("AcceptBlock() : AddToBlockIndex failed");
	}

	// Relay inventory, but don't relay old inventory during initial block download
	int nBlockEstimate = Checkpoints::GetTotalBlocksEstimate();
	
	if (hashBestChain == hash)
	{
		LOCK(cs_vNodes);
		
		for(CNode* pnode : vNodes)
		{
			if (nBestHeight > (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate))
			{
				pnode->PushInventory(CInv(MSG_BLOCK, hash));
			}
		}
	}

	return true;
}

#ifdef ENABLE_WALLET
// novacoin: attempt to generate suitable proof-of-stake
bool CBlock::SignBlock(CWallet& wallet, int64_t nFees)
{
	// if we are trying to sign
	// something except proof-of-stake block template
	if (!vtx[0].vout[0].IsEmpty())
	{
		return false;
	}

	// if we are trying to sign
	// a complete proof-of-stake block
	if (IsProofOfStake())
	{
		return true;
	}
	
	static int64_t nLastCoinStakeSearchTime = GetAdjustedTime(); // startup timestamp

	CKey key;
	CTransaction txCoinStake;
	txCoinStake.nTime &= ~STAKE_TIMESTAMP_MASK;

	int64_t nSearchTime = txCoinStake.nTime; // search to current time

	if (nSearchTime > nLastCoinStakeSearchTime)
	{
		int64_t nSearchInterval = 1;
		
		if (wallet.CreateCoinStake(wallet, nBits, nSearchInterval, nFees, txCoinStake, key))
		{
			if (txCoinStake.nTime >= pindexBest->GetPastTimeLimit()+1)
			{
				// make sure coinstake would meet timestamp protocol
				// as it would be the same as the block timestamp
				vtx[0].nTime = nTime = txCoinStake.nTime;

				// we have to make sure that we have no future timestamps in
				// our transactions set
				for (std::vector<CTransaction>::iterator it = vtx.begin(); it != vtx.end();)
				{
					if (it->nTime > nTime)
					{
						it = vtx.erase(it);
					}
					else
					{
						++it;
					}
				}
				
				vtx.insert(vtx.begin() + 1, txCoinStake);
				hashMerkleRoot = BuildMerkleTree();

				// append a signature to our block
				return key.Sign(GetHash(), vchBlockSig);
			}
		}
		
		nLastCoinStakeSearchInterval = nSearchTime - nLastCoinStakeSearchTime;
		nLastCoinStakeSearchTime = nSearchTime;
	}

	return false;
}
#endif

bool CBlock::CheckBlockSignature() const
{
	if (IsProofOfWork())
	{
		return vchBlockSig.empty();
	}

	if (vchBlockSig.empty())
	{
		return false;
	}
	
	std::vector<valtype> vSolutions;
	txnouttype whichType;

	const CTxOut& txout = vtx[1].vout[1];

	if (!Solver(txout.scriptPubKey, whichType, vSolutions))
	{
		return false;
	}
	
	if (whichType == TX_PUBKEY)
	{
		valtype& vchPubKey = vSolutions[0];
		
		return CPubKey(vchPubKey).Verify(GetHash(), vchBlockSig);
	}

	return false;
}

void CBlock::RebuildAddressIndex(CTxDB& txdb)
{
	for(CTransaction& tx : vtx)
	{
		uint256 hashTx = tx.GetHash();
		
		// inputs
		if(!tx.IsCoinBase())
		{
			mapPrevTx_t mapInputs;
			std::map<uint256, CTxIndex> mapQueuedChangesT;
			bool fInvalid;
			
			if (!tx.FetchInputs(txdb, mapQueuedChangesT, true, false, mapInputs, fInvalid))
			{
				return;
			}
			
			for(mapPrevTx_t::const_iterator mi = mapInputs.begin(); mi != mapInputs.end(); ++mi)
			{
				for(const CTxOut &atxout : (*mi).second.second.vout)
				{
					std::vector<uint160> addrIds;
					
					if(BuildAddrIndex(atxout.scriptPubKey, addrIds))
					{
						for(uint160 addrId : addrIds)
						{
							if(!txdb.WriteAddrIndex(addrId, hashTx))
							{
								LogPrintf(
									"RebuildAddressIndex(): txins WriteAddrIndex failed addrId: %s txhash: %s\n",
									addrId.ToString().c_str(),
									hashTx.ToString().c_str()
								);
							}
						}
					}
				}
			}
		}
		
		// outputs
		for(const CTxOut &atxout : tx.vout)
		{
			std::vector<uint160> addrIds;
			if(BuildAddrIndex(atxout.scriptPubKey, addrIds))
			{
				for(uint160 addrId : addrIds)
				{
					if(!txdb.WriteAddrIndex(addrId, hashTx))
					{
						LogPrintf(
							"RebuildAddressIndex(): txouts WriteAddrIndex failed addrId: %s txhash: %s\n",
							addrId.ToString().c_str(),
							hashTx.ToString().c_str()
						);
					}
				}
			}
		}
	}
}

// Called from inside SetBestChain: attaches a block to the new best chain being built
bool CBlock::SetBestChainInner(CTxDB& txdb, CBlockIndex *pindexNew)
{
	uint256 hash = GetHash();

	// Adding to current best branch
	if (!ConnectBlock(txdb, pindexNew) || !txdb.WriteHashBestChain(hash))
	{
		txdb.TxnAbort();
		
		InvalidChainFound(pindexNew);
		
		return false;
	}

	if (!txdb.TxnCommit())
	{
		return error("SetBestChain() : TxnCommit failed");
	}

	// Add to current best branch
	pindexNew->pprev->pnext = pindexNew;

	// Delete redundant memory transactions
	for(CTransaction& tx : vtx)
	{
		mempool.remove(tx);
	}

	// v2.0.0.8 PB-NEW: update the chain-derived lastPaidHeight cache here,
	// once per block that joins the main chain, with THIS block and ITS
	// OWN height.
	//
	// Previously the only normal-path hook was in ProcessBlock, called
	// once per ProcessBlock() invocation with (*pblock, pindexBest->nHeight).
	// ProcessBlock connects the handed block AND recursively connects any
	// orphan blocks chained off it -- so a single ProcessBlock call can
	// advance the tip by many blocks.  The single post-loop hook therefore:
	//   1. recorded only the FIRST block's payee (the other connected
	//      blocks' MN payments were never cached at all), and
	//   2. recorded it at the FINAL tip height, not the block's own height.
	// The cache ended up sparse and height-shifted, so
	// FindOldestNotInVecChainDerived kept selecting MNs whose payments had
	// never registered -- producing the persistent vote-rotation looping
	// seen on testnet (e.g. tFdB winning heights 1058-1061 despite being
	// paid repeatedly in that range).
	//
	// SetBestChainInner is the correct home: it is invoked exactly once
	// for every block that joins the main chain -- both the normal
	// single-block extension (SetBestChain branch hashPrevBlock ==
	// hashBestChain) and each secondary reconnect block.  `this` is the
	// block, `pindexNew->nHeight` is that block's own height.  The
	// Reorganize() path has its own per-block OnBlockConnected call and
	// does NOT route through SetBestChainInner, so there is no double
	// counting.
	mnodeman.OnBlockConnected(*this, pindexNew->nHeight);

	return true;
}

