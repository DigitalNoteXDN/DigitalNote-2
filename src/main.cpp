#include "compat.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>

#include "calert.h"
#include "blocksizecalculator.h"
#include "blockparams.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "init.h"
#include "kernel.h"
#include "net.h"
#include "txdb-leveldb.h"
#include "ctxmempool.h"
#include "velocity.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "cmasternodepaymentwinner.h"
#include "cmasternodepayments.h"
#include "masternodeman.h"
#include "masternode-payments.h"
#include "masternode_extern.h"
#include "spork.h"
#include "util.h"
#include "cnodestatestats.h"
#include "cwalletinterface.h"
#include "cblock.h"
#include "cblocklocator.h"
#include "cdiskblockpos.h"
#include "script.h"
#include "net/cnode.h"
#include "net/cnetmessage.h"
#include "instantx.h"
#include "ctransactionlock.h"
#include "cconsensusvote.h"
#include "csporkmessage.h"
#include "coutpoint.h"
#include "cinpoint.h"
#include "smsg.h"
#include "smsg_extern.h"
#include "crypto/bmw/bmw512.h"
#include "protocol.h"
#include "cinv.h"
#include "caddrman.h"
#include "cmainsignals.h"
#include "main_extern.h"
#include "hash.h"
#include "main.h"
#include "ctxout.h"
#include "cmnenginebroadcasttx.h"
#include "cmnenginesigner.h"
#include "cmnenginepool.h"
#include "mnengine_extern.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "thread.h"
#include "ui_interface.h"
#include "ui_translate.h"
#include "cblockindex.h"
#include "ctxindex.h"
#include "util/backwards.h"
#include "cautofile.h"
#include "serialize.h"

//
// Global state
//

CCriticalSection cs_setpwalletRegistered;
std::set<CWallet*> setpwalletRegistered;

CCriticalSection cs_main;

CTxMemPool mempool;

std::map<uint256, CBlockIndex*> mapBlockIndex;
std::set<std::pair<COutPoint, unsigned int> > setStakeSeen;

CBlockIndex* pindexGenesisBlock = NULL;
int nBestHeight = -1;
uint256 nBestChainTrust = 0;
uint256 nBestInvalidTrust = 0;
uint256 hashBestChain = 0;
CBlockIndex* pindexBest = NULL;
int64_t nTimeBestReceived = 0;
bool fImporting = false;
bool fReindex = false;
bool fAddrIndex = false;
bool fHaveGUI = false;

struct COrphanBlock
{
	uint256 hashBlock;
	uint256 hashPrev;
	std::pair<COutPoint, unsigned int> stake;
	std::vector<unsigned char> vchBlock;
};
std::map<uint256, COrphanBlock*> mapOrphanBlocks;
std::multimap<uint256, COrphanBlock*> mapOrphanBlocksByPrev;
std::set<std::pair<COutPoint, unsigned int> > setStakeSeenOrphan;

std::map<uint256, CTransaction> mapOrphanTransactions;
std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev;

// Constant stuff for coinbase transactions we create:
CScript COINBASE_FLAGS;

const std::string strMessageMagic = "DigitalNote Signed Message:\n";

std::set<uint256> setValidatedTx;

CMainSignals g_signals;

namespace
{

// Sources of received blocks, to be able to send them reject messages or ban
// them, if processing happens afterwards. Protected by cs_main.
std::map<uint256, NodeId> mapBlockSource;

// Blocks that are in flight, and that are in the queue to be downloaded.
// Protected by cs_main.
struct QueuedBlock
{
	uint256 hash;
	int64_t nTime;  // Time of "getdata" request in microseconds.
	int nQueuedBefore;  // Number of blocks in flight at the time of request.
};
std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> > mapBlocksInFlight;
std::map<uint256, std::pair<NodeId, std::list<uint256>::iterator> > mapBlocksToDownload;
}

bool MoneyRange(int64_t nValue)
{
	return (nValue >= 0 && nValue <= MAX_SINGLE_TX);
}

/** Future drift params */
int64_t FutureDrift(int64_t nTime)
{
	return nTime + nDrift;
}

bool AllowFree(double dPriority)
{
	// Large (in bytes) low-priority (new, small-coin) transactions
	// need a fee.
	return dPriority > COIN * 576 / 250;
}

//////////////////////////////////////////////////////////////////////////////
//
// dispatching functions
//

// These functions dispatch to one or all registered wallets

void RegisterWallet(CWalletInterface* pwalletIn)
{
	g_signals.SyncTransaction.connect(
		boost::bind(
			&CWalletInterface::SyncTransaction,
			pwalletIn,
			boost::placeholders::_1,
			boost::placeholders::_2,
			boost::placeholders::_3,
			boost::placeholders::_4
		)
	);
	
	g_signals.EraseTransaction.connect(
		boost::bind(
			&CWalletInterface::EraseFromWallet,
			pwalletIn,
			boost::placeholders::_1
		)
	);
	
	g_signals.UpdatedTransaction.connect(
		boost::bind(
			&CWalletInterface::UpdatedTransaction,
			pwalletIn,
			boost::placeholders::_1
		)
	);
	
	g_signals.SetBestChain.connect(
		boost::bind(
			&CWalletInterface::SetBestChain,
			pwalletIn,
			boost::placeholders::_1
		)
	);
	
	g_signals.Inventory.connect(
		boost::bind(
			&CWalletInterface::Inventory,
			pwalletIn,
			boost::placeholders::_1
		)
	);
	
	g_signals.Broadcast.connect(
		boost::bind(
			&CWalletInterface::ResendWalletTransactions,
			pwalletIn,
			boost::placeholders::_1
		)
	);
}

void UnregisterWallet(CWalletInterface* pwalletIn)
{
	g_signals.Broadcast.disconnect(
		boost::bind(
			&CWalletInterface::ResendWalletTransactions,
			pwalletIn,
			boost::placeholders::_1
		)
	);
	
	g_signals.Inventory.disconnect(
		boost::bind(
			&CWalletInterface::Inventory,
			pwalletIn,
			boost::placeholders::_1
		)
	);
	
	g_signals.SetBestChain.disconnect(
		boost::bind(
			&CWalletInterface::SetBestChain,
			pwalletIn,
			boost::placeholders::_1
		)
	);
	
	g_signals.UpdatedTransaction.disconnect(
		boost::bind(
			&CWalletInterface::UpdatedTransaction,
			pwalletIn,
			boost::placeholders::_1
		)
	);
	
	g_signals.EraseTransaction.disconnect(
		boost::bind(
			&CWalletInterface::EraseFromWallet,
			pwalletIn,
			boost::placeholders::_1
		)
	);
	
	g_signals.SyncTransaction.disconnect(
		boost::bind(
			&CWalletInterface::SyncTransaction,
			pwalletIn,
			boost::placeholders::_1,
			boost::placeholders::_2,
			boost::placeholders::_3,
			boost::placeholders::_4
		)
	);
}

void UnregisterAllWallets()
{
	g_signals.Broadcast.disconnect_all_slots();
	g_signals.Inventory.disconnect_all_slots();
	g_signals.SetBestChain.disconnect_all_slots();
	g_signals.UpdatedTransaction.disconnect_all_slots();
	g_signals.EraseTransaction.disconnect_all_slots();
	g_signals.SyncTransaction.disconnect_all_slots();
}

void SyncWithWallets(const CTransaction &tx, const CBlock *pblock, bool fConnect, bool fFixSpentCoins)
{
    g_signals.SyncTransaction(tx, pblock, fConnect, fFixSpentCoins);
}

void ResendWalletTransactions(bool fForce)
{
    g_signals.Broadcast(fForce);
}


//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//

namespace
{

struct CBlockReject
{
    unsigned char chRejectCode;
    std::string strRejectReason;
    uint256 hashBlock;
};

// Maintain validation-specific state about nodes, protected by cs_main, instead
// by CNode's own locks. This simplifies asynchronous operation, where
// processing of incoming data is done after the ProcessMessage call returns,
// and we're no longer holding the node's locks.
struct CNodeState
{
    // Accumulated misbehaviour score for this peer.
    int nMisbehavior;
    // Whether this peer should be disconnected and banned.
    bool fShouldBan;
    std::string name;
    // List of asynchronously-determined block rejections to notify this peer about.
    std::vector<CBlockReject> rejects;
    std::list<QueuedBlock> vBlocksInFlight;
    int nBlocksInFlight;
    std::list<uint256> vBlocksToDownload;
    int nBlocksToDownload;
    int64_t nLastBlockReceive;
    int64_t nLastBlockProcess;

    CNodeState()
	{
        nMisbehavior = 0;
        fShouldBan = false;
        nBlocksToDownload = 0;
        nBlocksInFlight = 0;
        nLastBlockReceive = 0;
        nLastBlockProcess = 0;
    }
};

std::map<NodeId, CNodeState> mapNodeState;

// Requires cs_main.
CNodeState *State(NodeId pnode)
{
	std::map<NodeId, CNodeState>::iterator it = mapNodeState.find(pnode);

	if (it == mapNodeState.end())
	{
		return NULL;
	}

	return &it->second;
}

int GetHeight()
{
	while(true)
	{
		TRY_LOCK(cs_main, lockMain);
		
		if(!lockMain)
		{
			MilliSleep(50);
			
			continue;
		}
		
		return pindexBest->nHeight;
	}
}

void InitializeNode(NodeId nodeid, const CNode *pnode)
{
	LOCK(cs_main);

	CNodeState &state = mapNodeState.insert(std::make_pair(nodeid, CNodeState())).first->second;

	state.name = pnode->addrName;
}

void FinalizeNode(NodeId nodeid)
{
	LOCK(cs_main);

	CNodeState *state = State(nodeid);

	for(const QueuedBlock& entry : state->vBlocksInFlight)
	{
		mapBlocksInFlight.erase(entry.hash);
	}

	for(const uint256& hash : state->vBlocksToDownload)
	{
		mapBlocksToDownload.erase(hash);
	}

	mapNodeState.erase(nodeid);
}

// Requires cs_main.
void MarkBlockAsReceived(const uint256 &hash, NodeId nodeFrom = -1)
{
	std::map<uint256, std::pair<NodeId, std::list<uint256>::iterator> >::iterator itToDownload = mapBlocksToDownload.find(hash);

	if (itToDownload != mapBlocksToDownload.end())
	{
		CNodeState *state = State(itToDownload->second.first);
		
		state->vBlocksToDownload.erase(itToDownload->second.second);
		state->nBlocksToDownload--;
		
		mapBlocksToDownload.erase(itToDownload);
	}

	std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> >::iterator itInFlight = mapBlocksInFlight.find(hash);

	if (itInFlight != mapBlocksInFlight.end())
	{
		CNodeState *state = State(itInFlight->second.first);
		
		state->vBlocksInFlight.erase(itInFlight->second.second);
		state->nBlocksInFlight--;
		
		if (itInFlight->second.first == nodeFrom)
		{
			state->nLastBlockReceive = GetTimeMicros();
		}
		
		mapBlocksInFlight.erase(itInFlight);
	}
}

// Requires cs_main.
bool AddBlockToQueue(NodeId nodeid, const uint256 &hash)
{
	if (mapBlocksToDownload.count(hash) || mapBlocksInFlight.count(hash))
	{
		return false;
	}

	CNodeState *state = State(nodeid);
	if (state == NULL)
	{
		return false;
	}

	std::list<uint256>::iterator it = state->vBlocksToDownload.insert(state->vBlocksToDownload.end(), hash);
	state->nBlocksToDownload++;

	if (state->nBlocksToDownload > 5000)
	{
		Misbehaving(nodeid, 10);
	}

	mapBlocksToDownload[hash] = std::make_pair(nodeid, it);

	return true;
}

// Requires cs_main.
void MarkBlockAsInFlight(NodeId nodeid, const uint256 &hash)
{
	CNodeState *state = State(nodeid);

	assert(state != NULL);

	// Make sure it's not listed somewhere already.
	MarkBlockAsReceived(hash);

	QueuedBlock newentry = {hash, GetTimeMicros(), state->nBlocksInFlight};

	if (state->nBlocksInFlight == 0)
	{
		state->nLastBlockReceive = newentry.nTime; // Reset when a first request is sent.
	}

	std::list<QueuedBlock>::iterator it = state->vBlocksInFlight.insert(state->vBlocksInFlight.end(), newentry);

	state->nBlocksInFlight++;

	mapBlocksInFlight[hash] = std::make_pair(nodeid, it);
}

} // namespace

bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats)
{
	LOCK(cs_main);

	CNodeState *state = State(nodeid);

	if (state == NULL)
	{
		return false;
	}

	stats.nMisbehavior = state->nMisbehavior;

	return true;
}

void RegisterNodeSignals(CNodeSignals& nodeSignals)
{
	nodeSignals.GetHeight.connect(&GetHeight);
	nodeSignals.ProcessMessages.connect(&ProcessMessages);
	nodeSignals.SendMessages.connect(&SendMessages);
	nodeSignals.InitializeNode.connect(&InitializeNode);
	nodeSignals.FinalizeNode.connect(&FinalizeNode);
}

void UnregisterNodeSignals(CNodeSignals& nodeSignals)
{
	nodeSignals.GetHeight.disconnect(&GetHeight);
	nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
	nodeSignals.SendMessages.disconnect(&SendMessages);
	nodeSignals.InitializeNode.disconnect(&InitializeNode);
	nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
}

bool AbortNode(const std::string &strMessage, const std::string &userMessage)
{
	strMiscWarning = strMessage;

	LogPrintf("*** %s\n", strMessage);

	uiInterface.ThreadSafeMessageBox(
		userMessage.empty() ? ui_translate("Error: A fatal internal error occured, see debug.log for details") : userMessage,
		"",
		CClientUIInterface::MSG_ERROR
	);

	StartShutdown();

	return false;
}

//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//

bool AddOrphanTx(const CTransaction& tx)
{
	uint256 hash = tx.GetHash();

	if (mapOrphanTransactions.count(hash))
	{
		return false;
	}

	// Ignore big transactions, to avoid a
	// send-big-orphans memory exhaustion attack. If a peer has a legitimate
	// large transaction with a missing parent then we assume
	// it will rebroadcast it later, after the parent transaction(s)
	// have been mined or received.
	// 10,000 orphans, each of which is at most 5,000 bytes big is
	// at most 500 megabytes of orphans:

	size_t nSize = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);

	if (nSize > 5000)
	{
		LogPrint("mempool", "ignoring large orphan tx (size: %u, hash: %s)\n", nSize, hash.ToString());
		
		return false;
	}

	mapOrphanTransactions[hash] = tx;
	for(const CTxIn& txin : tx.vin)
	{
		mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);
	}

	LogPrint(
		"mempool",
		"stored orphan tx %s (mapsz %u)\n",
		hash.ToString(),
		mapOrphanTransactions.size()
	);

	return true;
}

void static EraseOrphanTx(uint256 hash)
{
	std::map<uint256, CTransaction>::iterator it = mapOrphanTransactions.find(hash);
	if (it == mapOrphanTransactions.end())
	{
		return;
	}

	for(const CTxIn& txin : it->second.vin)
	{
		std::map<uint256, std::set<uint256> >::iterator itPrev = mapOrphanTransactionsByPrev.find(txin.prevout.hash);
		
		if (itPrev == mapOrphanTransactionsByPrev.end())
		{
			continue;
		}

		itPrev->second.erase(hash);
		
		if (itPrev->second.empty())
		{
			mapOrphanTransactionsByPrev.erase(itPrev);
		}
	}

	mapOrphanTransactions.erase(it);
}

unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans)
{
	unsigned int nEvicted = 0;

	while (mapOrphanTransactions.size() > nMaxOrphans)
	{
		// Evict a random orphan:
		uint256 randomhash = GetRandHash();
		std::map<uint256, CTransaction>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
		
		if (it == mapOrphanTransactions.end())
		{
			it = mapOrphanTransactions.begin();
		}

		EraseOrphanTx(it->first);
		++nEvicted;
	}

	return nEvicted;
}

//////////////////////////////////////////////////////////////////////////////
//
// CTransaction and CTxIndex
//

bool IsStandardTx(const CTransaction& tx, std::string& reason)
{
	if (tx.nVersion > CTransaction::CURRENT_VERSION || tx.nVersion < 1)
	{
		reason = "version";
		
		return false;
	}

	// Treat non-final transactions as non-standard to prevent a specific type
	// of double-spend attack, as well as DoS attacks. (if the transaction
	// can't be mined, the attacker isn't expending resources broadcasting it)
	// Basically we don't want to propagate transactions that can't be included in
	// the next block.
	//
	// However, IsFinalTx() is confusing... Without arguments, it uses
	// chainActive.Height() to evaluate nLockTime; when a block is accepted, chainActive.Height()
	// is set to the value of nHeight in the block. However, when IsFinalTx()
	// is called within CBlock::AcceptBlock(), the height of the block *being*
	// evaluated is what is used. Thus if we want to know if a transaction can
	// be part of the *next* block, we need to call IsFinalTx() with one more
	// than chainActive.Height().
	//
	// Timestamps on the other hand don't get any special treatment, because we
	// can't know what timestamp the next block will have, and there aren't
	// timestamp applications where it matters.
	if (!IsFinalTx(tx, nBestHeight + 1))
	{
		reason = "non-final";
		
		return false;
	}

	// nTime has different purpose from nLockTime but can be used in similar attacks
	if (tx.nTime > FutureDrift(GetAdjustedTime()))
	{
		reason = "time-too-new";
		
		return false;
	}

	// Extremely large transactions with lots of inputs can cost the network
	// almost as much to process as they cost the sender in fees, because
	// computing signature hashes is O(ninputs*txsize). Limiting transactions
	// to MAX_STANDARD_TX_SIZE mitigates CPU exhaustion attacks.
	unsigned int sz = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
	if (sz >= MAX_STANDARD_TX_SIZE)
	{
		reason = "tx-size";
		
		return false;
	}

	for(const CTxIn& txin : tx.vin)
	{
		// Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
		// keys. (remember the 520 byte limit on redeemScript size) That works
		// out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
		// bytes of scriptSig, which we round off to 1650 bytes for some minor
		// future-proofing. That's also enough to spend a 20-of-20
		// CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
		// considered standard)
		if (txin.scriptSig.size() > 1650)
		{
			reason = "scriptsig-size";
			
			return false;
		}
		
		if (!txin.scriptSig.IsPushOnly())
		{
			reason = "scriptsig-not-pushonly";
			
			return false;
		}
		
		if (!txin.scriptSig.HasCanonicalPushes())
		{
			reason = "scriptsig-non-canonical-push";
			
			return false;
		}
	}

	unsigned int nDataOut = 0;

	txnouttype whichType;
	for(const CTxOut& txout : tx.vout)
	{
		if (!::IsStandard(txout.scriptPubKey, whichType))
		{
			reason = "scriptpubkey";
			
			return false;
		}
		
		if (whichType == TX_NULL_DATA)
		{
			nDataOut++;
		}
		else if (txout.nValue == 0)
		{
			reason = "dust";
			
			return false;
		}
		
		if (!txout.scriptPubKey.HasCanonicalPushes())
		{
			reason = "scriptpubkey-non-canonical-push";
			
			return false;
		}
	}

	// not more than one data txout per non-data txout is permitted
	// only one data txout is permitted too
	if (nDataOut > 1 && nDataOut > tx.vout.size() / 2)
	{
		reason = "multi-op-return";
		
		return false;
	}

	return true;
}

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
	AssertLockHeld(cs_main);

	// Time based nLockTime implemented in 0.1.6
	if (tx.nLockTime == 0)
	{
		return true;
	}

	if (nBlockHeight == 0)
	{
		nBlockHeight = nBestHeight;
	}

	if (nBlockTime == 0)
	{
		nBlockTime = GetAdjustedTime();
	}

	if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
	{
		return true;
	}

	for(const CTxIn& txin : tx.vin)
	{
		if (!txin.IsFinal())
		{
			return false;
		}
	}

	return true;
}

//
// Check transaction inputs to mitigate two
// potential denial-of-service attacks:
//
// 1. scriptSigs with extra data stuffed into them,
//    not consumed by scriptPubKey (or P2SH script)
// 2. P2SH scripts with a crazy number of expensive
//    CHECKSIG/CHECKMULTISIG operations
//
bool AreInputsStandard(const CTransaction& tx, const mapPrevTx_t& mapInputs)
{
	if (tx.IsCoinBase())
	{
		return true; // Coinbases don't use vin normally
	}

	for (unsigned int i = 0; i < tx.vin.size(); i++)
	{
		const CTxOut& prev = tx.GetOutputFor(tx.vin[i], mapInputs);

		std::vector<std::vector<unsigned char> > vSolutions;
		txnouttype whichType;
		// get the scriptPubKey corresponding to this input:
		const CScript& prevScript = prev.scriptPubKey;
		
		if (!Solver(prevScript, whichType, vSolutions))
		{
			return false;
		}
		
		int nArgsExpected = ScriptSigArgsExpected(whichType, vSolutions);
		
		if (nArgsExpected < 0)
		{
			return false;
		}
		
		// Transactions with extra stuff in their scriptSigs are
		// non-standard. Note that this EvalScript() call will
		// be quick, because if there are any operations
		// beside "push data" in the scriptSig
		// IsStandard() will have already returned false
		// and this method isn't called.
		std::vector<std::vector<unsigned char> > stack;
		if (!EvalScript(stack, tx.vin[i].scriptSig, tx, i, SCRIPT_VERIFY_NONE, 0))
		{
			return false;
		}
		
		if (whichType == TX_SCRIPTHASH)
		{
			if (stack.empty())
			{
				return false;
			}
			
			CScript subscript(stack.back().begin(), stack.back().end());
			std::vector<std::vector<unsigned char> > vSolutions2;
			txnouttype whichType2;
			
			if (Solver(subscript, whichType2, vSolutions2))
			{
				int tmpExpected = ScriptSigArgsExpected(whichType2, vSolutions2);

				if (whichType2 == TX_SCRIPTHASH)
				{
					return false;
				}
				
				if (tmpExpected < 0)
				{
					return false;
				}
				
				nArgsExpected += tmpExpected;
			}
			else
			{
				// Any other Script with less than 15 sigops OK:
				unsigned int sigops = subscript.GetSigOpCount(true);
				// ... extra data left on the stack after execution is OK, too:
				return (sigops <= MAX_P2SH_SIGOPS);
			}
		}

		if (stack.size() != (unsigned int)nArgsExpected)
		{
			return false;
		}
	}

	return true;
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
	unsigned int nSigOps = 0;

	for(const CTxIn& txin : tx.vin)
	{
		nSigOps += txin.scriptSig.GetSigOpCount(false);
	}

	for(const CTxOut& txout : tx.vout)
	{
		nSigOps += txout.scriptPubKey.GetSigOpCount(false);
	}

	return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const mapPrevTx_t& inputs)
{
	if (tx.IsCoinBase())
	{
		return 0;
	}

	unsigned int nSigOps = 0;
	for (unsigned int i = 0; i < tx.vin.size(); i++)
	{
		const CTxOut& prevout = tx.GetOutputFor(tx.vin[i], inputs);
		
		if (prevout.scriptPubKey.IsPayToScriptHash())
		{
			nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
		}
	}

	return nSigOps;
}

int64_t GetMinFee(const CTransaction& tx, unsigned int nBytes, bool fAllowFree, enum GetMinFee_mode mode)
{
	// Base fee is either MIN_TX_FEE or MIN_RELAY_TX_FEE
	int64_t nBaseFee = (mode == GMF_RELAY) ? MIN_RELAY_TX_FEE : MIN_TX_FEE;

	int64_t nMinFee = (1 + (int64_t)nBytes / 1000) * nBaseFee;

	if (fAllowFree)
	{
		// There is a free transaction area in blocks created by most miners,
		// * If we are relaying we allow transactions up to DEFAULT_BLOCK_PRIORITY_SIZE - 1000
		//   to be considered to fall into this category. We don't want to encourage sending
		//   multiple transactions instead of one big transaction to avoid fees.
		// * If we are creating a transaction we allow transactions up to 1,000 bytes
		//   to be considered safe and assume they can likely make it into this section.
		if (nBytes < (mode == GMF_SEND ? 1000 : (DEFAULT_BLOCK_PRIORITY_SIZE - 1000)))
		{
			nMinFee = 0;
		}
	}

	// This code can be removed after enough miners have upgraded to version 0.9.
	// Until then, be safe when sending and require a fee if any output
	// is less than CENT:
	if (nMinFee < nBaseFee && mode == GMF_SEND)
	{
		for(const CTxOut& txout : tx.vout)
		{
			if (txout.nValue < CENT)
			{
				nMinFee = nBaseFee;
			}
		}
	}

	if (!MoneyRange(nMinFee))
	{
		nMinFee = MAX_SINGLE_TX;
	}

	return nMinFee;
}

bool AcceptToMemoryPool(CTxMemPool& pool, CTransaction &tx, bool fLimitFree,
		bool* pfMissingInputs, bool fRejectInsaneFee, bool ignoreFees, bool fFixSpentCoins)
{
	AssertLockHeld(cs_main);

	if (pfMissingInputs)
	{
		*pfMissingInputs = false;
	}

	if (!tx.CheckTransaction())
	{
		return error("AcceptToMemoryPool : CheckTransaction failed");
	}

	// Coinbase is only valid in a block, not as a loose transaction
	if (tx.IsCoinBase())
	{
		return tx.DoS(100, error("AcceptToMemoryPool : coinbase as individual tx"));
	}

	// ppcoin: coinstake is also only valid in a block, not as a loose transaction
	if (tx.IsCoinStake())
	{
		return tx.DoS(100, error("AcceptToMemoryPool : coinstake as individual tx"));
	}

	// Rather not work on nonstandard transactions (unless -testnet)
	std::string reason;
	if (!TestNet() && !IsStandardTx(tx, reason))
	{
		return error("AcceptToMemoryPool : nonstandard transaction: %s", reason);
	}

	// is it already in the memory pool?
	uint256 hash = tx.GetHash();
	if (pool.exists(hash))
	{
		return false;
	}

	// ----------- instantX transaction scanning -----------

	for(const CTxIn& in : tx.vin)
	{
		if(mapLockedInputs.count(in.prevout))
		{
			if(mapLockedInputs[in.prevout] != tx.GetHash())
			{
				return tx.DoS(0, error("AcceptToMemoryPool : conflicts with existing transaction lock: %s", reason));
			}
		}
	}

	// Check for conflicts with in-memory transactions
	{
		LOCK(pool.cs); // protect pool.mapNextTx
		
		for (unsigned int i = 0; i < tx.vin.size(); i++)
		{
			COutPoint outpoint = tx.vin[i].prevout;
			
			if (pool.mapNextTx.count(outpoint))
			{
				// Disable replacement feature for now
				return false;
			}
		}
	}

	{
		CTxDB txdb("r");

		// do we already have it?
		if (txdb.ContainsTx(hash))
		{
			return false;
		}
		
		// do all inputs exist?
		// Note that this does not check for the presence of actual outputs (see the next check for that),
		// only helps filling in pfMissingInputs (to determine missing vs spent).
		for(const CTxIn txin : tx.vin)
		{
			if (!txdb.ContainsTx(txin.prevout.hash))
			{
				if (pfMissingInputs)
				{
					*pfMissingInputs = true;
				}
				
				return false;
			}
		}
		
		mapPrevTx_t mapInputs;
		std::map<uint256, CTxIndex> mapUnused;
		bool fInvalid = false;
		
		if (!tx.FetchInputs(txdb, mapUnused, false, false, mapInputs, fInvalid))
		{
			if (fInvalid)
			{
				return error("AcceptToMemoryPool : FetchInputs found invalid tx %s", hash.ToString());
			}
			
			return false;
		}

		// Check for non-standard pay-to-script-hash in inputs
		if (!TestNet() && !AreInputsStandard(tx, mapInputs))
		{
			return error("AcceptToMemoryPool : nonstandard transaction input");
		}
		
		// Check that the transaction doesn't have an excessive number of
		// sigops, making it impossible to mine. Since the coinbase transaction
		// itself can contain sigops MAX_TX_SIGOPS is less than
		// MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
		// merely non-standard transaction.
		unsigned int nSigOps = GetLegacySigOpCount(tx);
		nSigOps += GetP2SHSigOpCount(tx, mapInputs);
		
		if (nSigOps > MAX_TX_SIGOPS)
		{
			return tx.DoS(0,error("AcceptToMemoryPool : too many sigops %s, %d > %d", hash.ToString(), nSigOps, MAX_TX_SIGOPS));
		}
		
		int64_t nFees = tx.GetValueMapIn(mapInputs)-tx.GetValueOut();
		unsigned int nSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

		// Don't accept it if it can't get into a block
		// but prioritise dstx and don't check fees for it
		if(mapMNengineBroadcastTxes.count(hash))
		{
			// Normally we would PrioritiseTransaction But currently it is unimplemented
			// mempool.PrioritiseTransaction(hash, hash.ToString(), 1000, 0.1*COIN);
		}
		else if(!ignoreFees)
		{
			int64_t txMinFee = GetMinFee(tx, nSize, true, GMF_RELAY);
			
			if (fLimitFree && nFees < txMinFee)
			{
				return error("AcceptToMemoryPool : not enough fees %s, %d < %d", hash.ToString(), nFees, txMinFee);
			}
			
			// Continuously rate-limit free transactions
			// This mitigates 'penny-flooding' -- sending thousands of free transactions just to
			// be annoying or make others' transactions take longer to confirm.
			if (fLimitFree && nFees < MIN_RELAY_TX_FEE)
			{
				static CCriticalSection csFreeLimiter;
				static double dFreeCount;
				static int64_t nLastTime;
				int64_t nNow = GetTime();

				LOCK(csFreeLimiter);

				// Use an exponentially decaying ~10-minute window:
				dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
				nLastTime = nNow;
				
				// -limitfreerelay unit is thousand-bytes-per-minute
				// At default rate it would take over a month to fill 1GB
				if (dFreeCount > GetArg("-limitfreerelay", 15)*10*1000)
				{
					return error("AcceptableInputs : free transaction rejected by rate limiter");
				}
				
				LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
				
				dFreeCount += nSize;
			}
		}

		if (fRejectInsaneFee && nFees > MIN_RELAY_TX_FEE * 10000)
		{
			return error("AcceptableInputs: : insane fees %s, %d > %d", hash.ToString(), nFees, MIN_RELAY_TX_FEE * 10000);
		}
		
		// Check against previous transactions
		// This is done last to help prevent CPU exhaustion denial-of-service attacks.
		if (!tx.ConnectInputs(txdb, mapInputs, mapUnused, CDiskTxPos(1,1,1), pindexBest, false, false, STANDARD_SCRIPT_VERIFY_FLAGS))
		{
			return error("AcceptToMemoryPool : ConnectInputs failed %s", hash.ToString());
		}

		// Check again against just the consensus-critical mandatory script
		// verification flags, in case of bugs in the standard flags that cause
		// transactions to pass as valid when they're actually invalid. For
		// instance the STRICTENC flag was incorrectly allowing certain
		// CHECKSIG NOT scripts to pass, even though they were invalid.
		//
		// There is a similar check in CreateNewBlock() to prevent creating
		// invalid blocks, however allowing such transactions into the mempool
		// can be exploited as a DoS attack.
		if (!tx.ConnectInputs(txdb, mapInputs, mapUnused, CDiskTxPos(1,1,1), pindexBest, false, false, MANDATORY_SCRIPT_VERIFY_FLAGS))
		{
			return error("AcceptToMemoryPool: : BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s", hash.ToString());
		}
	}

	// Store transaction in memory
	pool.addUnchecked(hash, tx);
	setValidatedTx.insert(hash);

	SyncWithWallets(tx, NULL, true, fFixSpentCoins);

	LogPrint(
		"mempool",
		"AcceptToMemoryPool : accepted %s (poolsz %u)\n",
		hash.ToString(),
		pool.mapTx.size()
	);

	return true;
}

bool AcceptableInputs(CTxMemPool& pool, const CTransaction &txo, bool fLimitFree,
                         bool* pfMissingInputs, bool fRejectInsaneFee, bool isDSTX)
{
	AssertLockHeld(cs_main);

	if (pfMissingInputs)
	{
		*pfMissingInputs = false;
	}

	CTransaction tx(txo);
	std::string reason;

	if (!tx.CheckTransaction())
	{
		return error("AcceptableInputs : CheckTransaction failed");
	}

	// Coinbase is only valid in a block, not as a loose transaction
	if (tx.IsCoinBase())
	{
		return tx.DoS(100, error("AcceptableInputs : coinbase as individual tx"));
	}

	// ppcoin: coinstake is also only valid in a block, not as a loose transaction
	if (tx.IsCoinStake())
	{
		return tx.DoS(100, error("AcceptableInputs : coinstake as individual tx"));
	}

	// is it already in the memory pool?
	uint256 hash = tx.GetHash();
	if (pool.exists(hash))
	{
		return false;
	}

	// ----------- instantX transaction scanning -----------
	for(const CTxIn& in : tx.vin)
	{
		if(mapLockedInputs.count(in.prevout))
		{
			if(mapLockedInputs[in.prevout] != tx.GetHash())
			{
				return tx.DoS(0, error("AcceptableInputs : conflicts with existing transaction lock: %s", reason));
			}
		}
	}

	// Check for conflicts with in-memory transactions
	{
		LOCK(pool.cs); // protect pool.mapNextTx
		
		for (unsigned int i = 0; i < tx.vin.size(); i++)
		{
			COutPoint outpoint = tx.vin[i].prevout;
			
			if (pool.mapNextTx.count(outpoint))
			{
				// Disable replacement feature for now
				return false;
			}
		}
	}

	{
		CTxDB txdb("r");

		// do we already have it?
		if (txdb.ContainsTx(hash))
		{
			return false;
		}
		
		mapPrevTx_t mapInputs;
		std::map<uint256, CTxIndex> mapUnused;
		bool fInvalid = false;
		
		if (!tx.FetchInputs(txdb, mapUnused, false, false, mapInputs, fInvalid))
		{
			if (fInvalid)
			{
				return error("AcceptableInputs : FetchInputs found invalid tx %s", hash.ToString());
			}
			
			return false;
		}

		// Check for non-standard pay-to-script-hash in inputs
		//if (!TestNet() && !AreInputsStandard(tx, mapInputs))
		  //  return error("AcceptToMemoryPool : nonstandard transaction input");

		// Check that the transaction doesn't have an excessive number of
		// sigops, making it impossible to mine. Since the coinbase transaction
		// itself can contain sigops MAX_TX_SIGOPS is less than
		// MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
		// merely non-standard transaction.
		unsigned int nSigOps = GetLegacySigOpCount(tx);
		nSigOps += GetP2SHSigOpCount(tx, mapInputs);
		
		if (nSigOps > MAX_TX_SIGOPS)
		{
			return tx.DoS(0,error("AcceptableInputs : too many sigops %s, %d > %d", hash.ToString(), nSigOps, MAX_TX_SIGOPS));
		}
		
		int64_t nFees = tx.GetValueMapIn(mapInputs)-tx.GetValueOut();
		unsigned int nSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
		int64_t txMinFee = GetMinFee(tx, nSize, true, GMF_RELAY);

		// Don't accept it if it can't get into a block
		if(isDSTX)
		{
			// Normally we would PrioritiseTransaction But currently it is unimplemented
			// mempool.PrioritiseTransaction(hash, hash.ToString(), 1000, 0.1*COIN);
		}
		else
		{ // same as !ignoreFees for AcceptToMemoryPool
			if (fLimitFree && nFees < txMinFee)
			{
				return error("AcceptableInputs : not enough fees %s, %d < %d",
					hash.ToString(),
					nFees, txMinFee
				);
			}
			
			// Continuously rate-limit free transactions
			// This mitigates 'penny-flooding' -- sending thousands of free transactions just to
			// be annoying or make others' transactions take longer to confirm.
			if (fLimitFree && nFees < MIN_RELAY_TX_FEE)
			{
				static CCriticalSection csFreeLimiter;
				static double dFreeCount;
				static int64_t nLastTime;
				int64_t nNow = GetTime();

				LOCK(csFreeLimiter);

				// Use an exponentially decaying ~10-minute window:
				dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
				nLastTime = nNow;
				
				// -limitfreerelay unit is thousand-bytes-per-minute
				// At default rate it would take over a month to fill 1GB
				if (dFreeCount > GetArg("-limitfreerelay", 15)*10*1000)
				{
					return error("AcceptableInputs : free transaction rejected by rate limiter");
				}
				
				LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
				
				dFreeCount += nSize;
			}
		}

		if (fRejectInsaneFee && nFees > txMinFee * 10000)
		{
			return error(
				"AcceptableInputs: : insane fees %s, %d > %d",
				hash.ToString(),
				nFees,
				MIN_RELAY_TX_FEE * 10000
			);
		}
		
		// Check against previous transactions
		// This is done last to help prevent CPU exhaustion denial-of-service attacks.
		if (!tx.ConnectInputs(txdb, mapInputs, mapUnused, CDiskTxPos(1,1,1), pindexBest,
			true, false, STANDARD_SCRIPT_VERIFY_FLAGS, false))
		{
			return error("AcceptableInputs : ConnectInputs failed %s", hash.ToString());
		}
	}

	/*LogPrint("mempool", "AcceptableInputs : accepted %s (poolsz %u)\n",
		   hash.ToString(),
		   pool.mapTx.size());
	*/

	return true;
}

int GetInputAge(CTxIn& vin)
{
	const uint256& prevHash = vin.prevout.hash;
	CTransaction tx;
	uint256 hashBlock;

	bool fFound = GetTransaction(prevHash, tx, hashBlock);

	if(fFound)
	{
		if(mapBlockIndex.find(hashBlock) != mapBlockIndex.end())
		{
			return pindexBest->nHeight - mapBlockIndex[hashBlock]->nHeight;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		return 0;
	}
}

int GetInputAgeIX(uint256 nTXHash, CTxIn& vin)
{
	int sigs = 0;
	int nResult = GetInputAge(vin);

	if(nResult < 0)
	{
		nResult = 0;
	}

	if (nResult < 6)
	{
		std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(nTXHash);
		
		if (i != mapTxLocks.end())
		{
			sigs = (*i).second.CountSignatures();
		}
		
		if(sigs >= INSTANTX_SIGNATURES_REQUIRED)
		{
			return nInstantXDepth+nResult;
		}
	}

	return -1;
}

int GetIXConfirmations(uint256 nTXHash)
{
	int sigs = 0;
	std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(nTXHash);

	if (i != mapTxLocks.end())
	{
		sigs = (*i).second.CountSignatures();
	}

	if(sigs >= INSTANTX_SIGNATURES_REQUIRED)
	{
		return nInstantXDepth;
	}

	return 0;
}

// Return transaction in tx, and if it was found inside a block, its hash is placed in hashBlock
bool GetTransaction(const uint256 &hash, CTransaction &tx, uint256 &hashBlock)
{
	{
		LOCK(cs_main);
		
		if (mempool.lookup(hash, tx))
		{
			return true;
		}
		
		CTxDB txdb("r");
		CTxIndex txindex;
		
		if (tx.ReadFromDisk(txdb, hash, txindex))
		{
			CBlock block;
			if (block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
			{
				hashBlock = block.GetHash();
			}
			
			return true;
		}
		
		// look for transaction in disconnected blocks to find orphaned CoinBase and CoinStake transactions
		for(std::pair<const uint256, CBlockIndex*>& item : mapBlockIndex)
		{
			CBlockIndex* pindex = item.second;
			if (pindex == pindexBest || pindex->pnext != 0)
			{
				continue;
			}
			
			CBlock block;
			if (!block.ReadFromDisk(pindex))
			{
				continue;
			}
			
			for(const CTransaction& txOrphan : block.vtx)
			{
				if (txOrphan.GetHash() == hash)
				{
					tx = txOrphan;
					
					return true;
				}
			}
		}
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

CBlockIndex* pblockindexFBBHLast;

CBlockIndex* FindBlockByHeight(int nHeight)
{
	CBlockIndex *pblockindex;

	if (nHeight < nBestHeight / 2)
	{
		pblockindex = pindexGenesisBlock;
	}
	else
	{
		pblockindex = pindexBest;
	}

	if (pblockindexFBBHLast && abs(nHeight - pblockindex->nHeight) > abs(nHeight - pblockindexFBBHLast->nHeight))
	{
		pblockindex = pblockindexFBBHLast;
	}

	while (pblockindex->nHeight > nHeight)
	{
		pblockindex = pblockindex->pprev;
	}

	while (pblockindex->nHeight < nHeight)
	{
		pblockindex = pblockindex->pnext;
	}

	pblockindexFBBHLast = pblockindex;

	return pblockindex;
}

uint256 static GetOrphanRoot(const uint256& hash)
{
	std::map<uint256, COrphanBlock*>::iterator it = mapOrphanBlocks.find(hash);

	if (it == mapOrphanBlocks.end())
	{
		return hash;
	}

	// Work back to the first block in the orphan chain
	while(true)
	{
		std::map<uint256, COrphanBlock*>::iterator it2 = mapOrphanBlocks.find(it->second->hashPrev);
		
		if (it2 == mapOrphanBlocks.end())
		{
			return it->first;
		}
		
		it = it2;
	}
}

// ppcoin: find block wanted by given orphan block
uint256 WantedByOrphan(const COrphanBlock* pblockOrphan)
{
	// Work back to the first block in the orphan chain
	while (mapOrphanBlocks.count(pblockOrphan->hashPrev))
	{
		pblockOrphan = mapOrphanBlocks[pblockOrphan->hashPrev];
	}

	return pblockOrphan->hashPrev;
}

// Remove a random orphan block (which does not have any dependent orphans).
void static PruneOrphanBlocks()
{
	if (mapOrphanBlocksByPrev.size() <= (size_t)std::max((int64_t)0, GetArg("-maxorphanblocks", DEFAULT_MAX_ORPHAN_BLOCKS)))
	{
		return;
	}

	// Pick a random orphan block.
	int pos = insecure_rand() % mapOrphanBlocksByPrev.size();
	std::multimap<uint256, COrphanBlock*>::iterator it = mapOrphanBlocksByPrev.begin();

	while (pos--)
	{
		it++;
	}

	// As long as this block has other orphans depending on it, move to one of those successors.
	while(true)
	{
		std::multimap<uint256, COrphanBlock*>::iterator it2 = mapOrphanBlocksByPrev.find(it->second->hashBlock);
		
		if (it2 == mapOrphanBlocksByPrev.end())
		{
			break;
		}
		
		it = it2;
	}

	setStakeSeenOrphan.erase(it->second->stake);
	uint256 hash = it->second->hashBlock;

	delete it->second;

	mapOrphanBlocksByPrev.erase(it);
	mapOrphanBlocks.erase(hash);
}

// ppcoin: find last block index up to pindex
const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake)
{
	while (pindex && pindex->pprev && (pindex->IsProofOfStake() != fProofOfStake))
	{
		pindex = pindex->pprev;
	}

	return pindex;
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits)
{
	CBigNum bnTarget;
	bnTarget.SetCompact(nBits);

	// Check range
	if (bnTarget <= 0 || bnTarget > Params().ProofOfWorkLimit())
	{
		return error("CheckProofOfWork() : nBits below minimum work");
	}

	// Check proof of work matches claimed amount
	if (hash > bnTarget.getuint256())
	{
		return error("CheckProofOfWork() : hash doesn't match nBits");
	}

	return true;
}

bool IsInitialBlockDownload()
{
	LOCK(cs_main);

	if (pindexBest == NULL || nBestHeight < Checkpoints::GetTotalBlocksEstimate())
	{
		return true;
	}

	static int64_t nLastUpdate;
	static CBlockIndex* pindexLastBest;

	if (pindexBest != pindexLastBest)
	{
		pindexLastBest = pindexBest;
		nLastUpdate = GetTime();
	}

	return (GetTime() - nLastUpdate < 15 && pindexBest->GetBlockTime() < GetTime() - 8 * 60 * 60);
}

void InvalidChainFound(CBlockIndex* pindexNew)
{
	if (pindexNew->nChainTrust > nBestInvalidTrust)
	{
		nBestInvalidTrust = pindexNew->nChainTrust;
		
		CTxDB().WriteBestInvalidTrust(CBigNum(nBestInvalidTrust));
	}

	uint256 nBestInvalidBlockTrust = pindexNew->nChainTrust - pindexNew->pprev->nChainTrust;
	uint256 nBestBlockTrust = pindexBest->nHeight != 0 ? (pindexBest->nChainTrust - pindexBest->pprev->nChainTrust) : pindexBest->nChainTrust;

	LogPrintf(
		"InvalidChainFound: invalid block=%s  height=%d  trust=%s  blocktrust=%d  date=%s\n",
		pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
		CBigNum(pindexNew->nChainTrust).ToString(), nBestInvalidBlockTrust.Get64(),
		DateTimeStrFormat("%x %H:%M:%S", pindexNew->GetBlockTime())
	);

	LogPrintf(
		"InvalidChainFound:  current best=%s  height=%d  trust=%s  blocktrust=%d  date=%s\n",
		hashBestChain.ToString(), nBestHeight,
		CBigNum(pindexBest->nChainTrust).ToString(),
		nBestBlockTrust.Get64(),
		DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime())
	);
}

bool IsConfirmedInNPrevBlocks(const CTxIndex& txindex, const CBlockIndex* pindexFrom, int nMaxDepth, int& nActualDepth)
{
	for (const CBlockIndex* pindex = pindexFrom; pindex && pindexFrom->nHeight - pindex->nHeight < nMaxDepth; pindex = pindex->pprev)
	{
		if (pindex->nBlockPos == txindex.pos.nBlockPos && pindex->nFile == txindex.pos.nFile)
		{
			nActualDepth = pindexFrom->nHeight - pindex->nHeight;
			
			return true;
		}
	}

	return false;
}

bool BuildAddrIndex(const CScript &script, std::vector<uint160>& addrIds)
{
	CScript::const_iterator pc = script.begin();
	CScript::const_iterator pend = script.end();
	std::vector<unsigned char> data;
	opcodetype opcode;
	bool fHaveData = false;

	while (pc < pend)
	{
		script.GetOp(pc, opcode, data);
		
		if (0 <= opcode && opcode <= OP_PUSHDATA4 && data.size() >= 8)
		{ // data element
			uint160 addrid = 0;
			
			if (data.size() <= 20)
			{
				memcpy(&addrid, &data[0], data.size());
			}
			else
			{
				addrid = Hash160(data);
			}
			
			addrIds.push_back(addrid);
			fHaveData = true;
		}
	}

	if (!fHaveData)
	{
		uint160 addrid = Hash160(script);
		
		addrIds.push_back(addrid);
		
		return true;
	}
	else
	{
		if(addrIds.size() > 0)
		{
			return true;
		}
		else
		{
			return false;
		}
	}
}

bool FindTransactionsByDestination(const CTxDestination &dest, std::vector<uint256> &vtxhash)
{
	uint160 addrid = 0;
	const CKeyID *pkeyid = boost::get<CKeyID>(&dest);

	if (pkeyid)
	{
		addrid = static_cast<uint160>(*pkeyid);
	}

	if (!addrid)
	{
		const CScriptID *pscriptid = boost::get<CScriptID>(&dest);
		
		if (pscriptid)
		{
			addrid = static_cast<uint160>(*pscriptid);
		}
	}

	if (!addrid)
	{
		LogPrintf("FindTransactionsByDestination(): Couldn't parse dest into addrid\n");
		
		return false;
	}

	LOCK(cs_main);

	CTxDB txdb("r");

	if(!txdb.ReadAddrIndex(addrid, vtxhash))
	{
		LogPrintf("FindTransactionsByDestination(): txdb.ReadAddrIndex failed\n");
		
		return false;
	}

	return true;
}

bool Reorganize(CTxDB& txdb, CBlockIndex* pindexNew)
{
	LogPrintf("REORGANIZE\n");

	// Find the fork
	CBlockIndex* pfork = pindexBest;
	CBlockIndex* plonger = pindexNew;

	while (pfork != plonger)
	{
		while (plonger->nHeight > pfork->nHeight)
		{
			if (!(plonger = plonger->pprev))
			{
				return error("Reorganize() : plonger->pprev is null");
			}
		}
		
		if (pfork == plonger)
		{
			break;
		}
		
		if (!(pfork = pfork->pprev))
		{
			return error("Reorganize() : pfork->pprev is null");
		}
	}

	// List of what to disconnect
	std::vector<CBlockIndex*> vDisconnect;
	for (CBlockIndex* pindex = pindexBest; pindex != pfork; pindex = pindex->pprev)
	{
		vDisconnect.push_back(pindex);
	}

	// List of what to connect
	std::vector<CBlockIndex*> vConnect;
	for (CBlockIndex* pindex = pindexNew; pindex != pfork; pindex = pindex->pprev)
	{
		vConnect.push_back(pindex);
	}

	reverse(vConnect.begin(), vConnect.end());

	LogPrintf("REORGANIZE: Disconnect %u blocks; %s..%s\n", vDisconnect.size(), pfork->GetBlockHash().ToString(), pindexBest->GetBlockHash().ToString());
	LogPrintf("REORGANIZE: Connect %u blocks; %s..%s\n", vConnect.size(), pfork->GetBlockHash().ToString(), pindexNew->GetBlockHash().ToString());

	// Disconnect shorter branch
	std::list<CTransaction> vResurrect;

	for(CBlockIndex* pindex : vDisconnect)
	{
		CBlock block;
		
		if (!block.ReadFromDisk(pindex))
		{
			return error("Reorganize() : ReadFromDisk for disconnect failed");
		}
		
		if (!block.DisconnectBlock(txdb, pindex))
		{
			return error("Reorganize() : DisconnectBlock %s failed", pindex->GetBlockHash().ToString());
		}
		
		// Queue memory transactions to resurrect.
		// We only do this for blocks after the last checkpoint (reorganisation before that
		// point should only happen with -reindex/-loadblock, or a misbehaving peer.
		for(const CTransaction& tx : backwards<std::vector<CTransaction>>(block.vtx))
		{
			if (!(tx.IsCoinBase() || tx.IsCoinStake()) && pindex->nHeight > Checkpoints::GetTotalBlocksEstimate())
			{
				vResurrect.push_front(tx);
			}
		}
	}

	// Connect longer branch
	std::vector<CTransaction> vDelete;

	for (unsigned int i = 0; i < vConnect.size(); i++)
	{
		CBlockIndex* pindex = vConnect[i];
		CBlock block;
		
		if (!block.ReadFromDisk(pindex))
		{
			return error("Reorganize() : ReadFromDisk for connect failed");
		}
		
		if (!block.ConnectBlock(txdb, pindex))
		{
			// Invalid block
			return error("Reorganize() : ConnectBlock %s failed", pindex->GetBlockHash().ToString());
		}

		// Queue memory transactions to delete
		for(const CTransaction& tx : block.vtx)
		{
			vDelete.push_back(tx);
		}
	}

	if (!txdb.WriteHashBestChain(pindexNew->GetBlockHash()))
	{
		return error("Reorganize() : WriteHashBestChain failed");
	}

	// Make sure it's successfully written to disk before changing memory structure
	if (!txdb.TxnCommit())
	{
		return error("Reorganize() : TxnCommit failed");
	}

	// Disconnect shorter branch
	for(CBlockIndex* pindex : vDisconnect)
	{
		if (pindex->pprev)
		{
			pindex->pprev->pnext = NULL;
		}
	}

	// Connect longer branch
	for(CBlockIndex* pindex : vConnect)
	{
		if (pindex->pprev)
		{
			pindex->pprev->pnext = pindex;
		}
	}

	// Resurrect memory transactions that were in the disconnected branch
	for(CTransaction& tx : vResurrect)
	{
		AcceptToMemoryPool(mempool, tx, false, NULL);
	}

	// Delete redundant memory transactions that are in the connected branch
	for(CTransaction& tx : vDelete)
	{
		mempool.remove(tx);
		mempool.removeConflicts(tx);
	}

	LogPrintf("REORGANIZE: done\n");

	return true;
}

void PushGetBlocks(CNode* pnode, CBlockIndex* pindexBegin, uint256 hashEnd)
{
	// Filter out duplicate requests
	if (pindexBegin == pnode->pindexLastGetBlocksBegin && hashEnd == pnode->hashLastGetBlocksEnd)
	{
		return;
	}

	pnode->pindexLastGetBlocksBegin = pindexBegin;
	pnode->hashLastGetBlocksEnd = hashEnd;

	pnode->PushMessage("getblocks", CBlockLocator(pindexBegin), hashEnd);
}

bool static IsCanonicalBlockSignature(CBlock* pblock)
{
	if (pblock->IsProofOfWork())
	{
		return pblock->vchBlockSig.empty();
	}

	return IsDERSignature(pblock->vchBlockSig, false);
}

void Misbehaving(NodeId pnode, int howmuch)
{
	if (howmuch == 0)
	{
		return;
	}

	CNodeState *state = State(pnode);

	if (state == NULL)
	{
		return;
	}

	state->nMisbehavior += howmuch;

	if (state->nMisbehavior >= GetArg("-banscore", 100))
	{
		LogPrintf("Misbehaving: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n",
			state->name.c_str(),
			state->nMisbehavior-howmuch,
			state->nMisbehavior
		);
		
		state->fShouldBan = true;
	}
	else
	{
		LogPrintf("Misbehaving: %s (%d -> %d)\n", state->name.c_str(), state->nMisbehavior-howmuch, state->nMisbehavior);
	}
}

bool ProcessBlock(CNode* pfrom, CBlock* pblock)
{
	AssertLockHeld(cs_main);

	// Check for duplicate
	uint256 hash = pblock->GetHash();

	if (mapBlockIndex.count(hash))
	{
		return error("ProcessBlock() : already have block %d %s", mapBlockIndex[hash]->nHeight, hash.ToString());
	}

	if (mapOrphanBlocks.count(hash))
	{
		return error("ProcessBlock() : already have block (orphan) %s", hash.ToString());
	}

	// ppcoin: check proof-of-stake
	// Limited duplicity on stake: prevents block flood attack
	// Duplicate stake allowed only when there is orphan child block
	if (!fReindex &&
		!fImporting &&
		pblock->IsProofOfStake() &&
		setStakeSeen.count(pblock->GetProofOfStake()) &&
		!mapOrphanBlocksByPrev.count(hash)
	)
	{
		return error("ProcessBlock() : duplicate proof-of-stake (%s, %d) for block %s",
			pblock->GetProofOfStake().first.ToString(),
			pblock->GetProofOfStake().second,
			hash.ToString()
		);
	}


	if (pblock->hashPrevBlock != hashBestChain)
	{
		// Extra checks to prevent "fill up memory by spamming with bogus blocks"
		const CBlockIndex* pcheckpoint = Checkpoints::AutoSelectSyncCheckpoint();
		int64_t deltaTime = pblock->GetBlockTime() - pcheckpoint->nTime;
		
		if (deltaTime < 0)
		{
			if (pfrom)
			{
				Misbehaving(pfrom->GetId(), 1);
			}
			
			return error("ProcessBlock() : block with timestamp before last checkpoint");
		}
	}

	// Block signature can be malleated in such a way that it increases block size up to maximum allowed by protocol
	// For now we just strip garbage from newly received blocks
	if (!IsCanonicalBlockSignature(pblock))
	{
		return error("ProcessBlock(): bad block signature encoding");
	}

	// Preliminary checks
	if (!pblock->CheckBlock())
	{
		return error("ProcessBlock() : CheckBlock FAILED");
	}

	// If we don't already have its previous block, shunt it off to holding area until we get it
	if (!mapBlockIndex.count(pblock->hashPrevBlock))
	{
		LogPrintf("ProcessBlock: ORPHAN BLOCK %lu, prev=%s\n",
			(unsigned long)mapOrphanBlocks.size(),
			pblock->hashPrevBlock.ToString()
		);

		// Accept orphans as long as there is a node to request its parents from
		if (pfrom)
		{
			// ppcoin: check proof-of-stake
			if (pblock->IsProofOfStake())
			{
				// Limited duplicity on stake: prevents block flood attack
				// Duplicate stake allowed only when there is orphan child block
				if (setStakeSeenOrphan.count(pblock->GetProofOfStake()) && !mapOrphanBlocksByPrev.count(hash))
				{
					return error("ProcessBlock() : duplicate proof-of-stake (%s, %d) for orphan block %s",
						pblock->GetProofOfStake().first.ToString(),
						pblock->GetProofOfStake().second,
						hash.ToString()
					);
				}
			}
			
			PruneOrphanBlocks();
			COrphanBlock* pblock2 = new COrphanBlock();
			
			{
				CDataStream ss(SER_DISK, CLIENT_VERSION);
				
				ss << *pblock;
				
				pblock2->vchBlock = std::vector<unsigned char>(ss.begin(), ss.end());
			}
			
			pblock2->hashBlock = hash;
			pblock2->hashPrev = pblock->hashPrevBlock;
			pblock2->stake = pblock->GetProofOfStake();
			
			mapOrphanBlocks.insert(std::make_pair(hash, pblock2));
			mapOrphanBlocksByPrev.insert(std::make_pair(pblock2->hashPrev, pblock2));
			
			if (pblock->IsProofOfStake())
			{
				setStakeSeenOrphan.insert(pblock->GetProofOfStake());
			}
			
			// Ask this guy to fill in what we're missing
			PushGetBlocks(pfrom, pindexBest, GetOrphanRoot(hash));
			
			// ppcoin: getblocks may not obtain the ancestor block rejected
			// earlier by duplicate-stake check so we ask for it again directly
			if (!IsInitialBlockDownload())
			{
				pfrom->AskFor(CInv(MSG_BLOCK, WantedByOrphan(pblock2)));
			}
		}
		
		return true;
	}

	// Store to disk
	if (!pblock->AcceptBlock())
	{
		return error("ProcessBlock() : AcceptBlock FAILED");
	}

	// Recursively process any orphan blocks that depended on this one
	std::vector<uint256> vWorkQueue;
	vWorkQueue.push_back(hash);

	for (unsigned int i = 0; i < vWorkQueue.size(); i++)
	{
		uint256 hashPrev = vWorkQueue[i];
		
		for (std::multimap<uint256, COrphanBlock*>::iterator mi = mapOrphanBlocksByPrev.lower_bound(hashPrev);
			 mi != mapOrphanBlocksByPrev.upper_bound(hashPrev);
			 mi++
		)
		{
			CBlock block;
			
			{
				CDataStream ss(mi->second->vchBlock, SER_DISK, CLIENT_VERSION);
				
				ss >> block;
			}
			
			block.BuildMerkleTree();
			
			if (block.AcceptBlock())
			{
				vWorkQueue.push_back(mi->second->hashBlock);
			}
			
			mapOrphanBlocks.erase(mi->second->hashBlock);
			setStakeSeenOrphan.erase(block.GetProofOfStake());
			
			delete mi->second;
		}
		
		mapOrphanBlocksByPrev.erase(hashPrev);
	}

	// If initial sync or we can't find a masternode in our list
	if(!IsInitialBlockDownload())
	{
		CScript payee;
		CTxIn vin;

		// If we're in LiteMode disable mnengine features without disabling masternodes
		if (!fLiteMode && !fImporting && !fReindex && pindexBest->nHeight > Checkpoints::GetTotalBlocksEstimate())
		{
			if(masternodePayments.GetBlockPayee(pindexBest->nHeight, payee, vin))
			{
				//UPDATE MASTERNODE LAST PAID TIME
				CMasternode* pmn = mnodeman.Find(vin);
				
				if(pmn != NULL)
				{
					pmn->nLastPaid = GetAdjustedTime();
				}

				LogPrintf("ProcessBlock() : Update Masternode Last Paid Time - %d\n", pindexBest->nHeight);
			}

			mnEnginePool.CheckTimeout();
			mnEnginePool.NewBlock();
			masternodePayments.ProcessBlock(GetHeight()+10);

		}
		else if (fLiteMode && !fImporting && !fReindex && pindexBest->nHeight > Checkpoints::GetTotalBlocksEstimate())
		{
			if(masternodePayments.GetBlockPayee(pindexBest->nHeight, payee, vin))
			{
				//UPDATE MASTERNODE LAST PAID TIME
				CMasternode* pmn = mnodeman.Find(vin);
				
				if(pmn != NULL)
				{
					pmn->nLastPaid = GetAdjustedTime();
				}

				LogPrintf("ProcessBlock() : Update Masternode Last Paid Time - %d\n", pindexBest->nHeight);
			}
			
			masternodePayments.ProcessBlock(GetHeight()+10);
		}
	}

	LogPrintf("ProcessBlock: ACCEPTED\n");

	return true;
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
	uint64_t nFreeBytesAvailable = boost::filesystem::space(GetDataDir()).available;

	// Check for nMinDiskSpace bytes (currently 50MB)
	if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
	{
		std::string strMessage = ui_translate("Error: Disk space is low!");
		strMiscWarning = strMessage;
		
		LogPrintf("*** %s\n", strMessage);
		uiInterface.ThreadSafeMessageBox(strMessage, "", CClientUIInterface::MSG_ERROR);
		
		StartShutdown();
		
		return false;
	}

	return true;
}

static boost::filesystem::path BlockFilePath(unsigned int nFile)
{
	std::string strBlockFn = strprintf("blk%04u.dat", nFile);

	return GetDataDir() / strBlockFn;
}

FILE* OpenBlockFile(unsigned int nFile, unsigned int nBlockPos, const char* pszMode)
{
	if ((nFile < 1) || (nFile == (unsigned int) -1))
	{
		return NULL;
	}

	FILE* file = fopen(BlockFilePath(nFile).string().c_str(), pszMode);

	if (!file)
	{
		return NULL;
	}

	if (nBlockPos != 0 && !strchr(pszMode, 'a') && !strchr(pszMode, 'w'))
	{
		if (fseek(file, nBlockPos, SEEK_SET) != 0)
		{
			fclose(file);
			
			return NULL;
		}
	}

	return file;
}

static unsigned int nCurrentBlockFile = 1;

FILE* AppendBlockFile(unsigned int& nFileRet)
{
	nFileRet = 0;

	while (true)
	{
		FILE* file = OpenBlockFile(nCurrentBlockFile, 0, "ab");
		
		if (!file)
		{
			return NULL;
		}
		
		if (fseek(file, 0, SEEK_END) != 0)
		{
			return NULL;
		}
		
		// FAT32 file size max 4GB, fseek and ftell max 2GB, so we must stay under 2GB
		if (ftell(file) < (long)(0x7F000000 - MAX_MESSAGE_SIZE))
		{
			nFileRet = nCurrentBlockFile;
			
			return file;
		}
		
		fclose(file);
		
		nCurrentBlockFile++;
	}
}

bool LoadBlockIndex(bool fAllowNew)
{
	LOCK(cs_main);

	//
	// Load block index
	//
	CTxDB txdb("cr+");

	if (!txdb.LoadBlockIndex())
	{
		return false;
	}

	//
	// Init with genesis block
	//
	if (mapBlockIndex.empty())
	{
		if (!fAllowNew)
		{
			return false;
		}
		
		CBlock &block = const_cast<CBlock&>(Params().GenesisBlock());
		
		// Start new block file
		unsigned int nFile;
		unsigned int nBlockPos;
		
		if (!block.WriteToDisk(nFile, nBlockPos))
		{
			return error("LoadBlockIndex() : writing genesis block to disk failed");
		}
		
		if (!block.AddToBlockIndex(nFile, nBlockPos, Params().HashGenesisBlock()))
		{
			return error("LoadBlockIndex() : genesis block not accepted");
		}
	}

	return true;
}

void PrintBlockTree()
{
    AssertLockHeld(cs_main);
	
    // pre-compute tree structure
    std::map<CBlockIndex*, std::vector<CBlockIndex*> > mapNext;
    
	for (std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
    {
        CBlockIndex* pindex = (*mi).second;
        
		mapNext[pindex->pprev].push_back(pindex);
        // test
        //while (rand() % 3 == 0)
        //    mapNext[pindex->pprev].push_back(pindex);
    }

    std::vector<std::pair<int, CBlockIndex*> > vStack;
    int nPrevCol = 0;
    
	vStack.push_back(std::make_pair(0, pindexGenesisBlock));

    while (!vStack.empty())
    {
        int nCol = vStack.back().first;
        CBlockIndex* pindex = vStack.back().second;
        
		vStack.pop_back();

        // print split or gap
        if (nCol > nPrevCol)
        {
            for (int i = 0; i < nCol-1; i++)
			{
                LogPrintf("| ");
			}
			
            LogPrintf("|\\\n");
        }
        else if (nCol < nPrevCol)
        {
            for (int i = 0; i < nCol; i++)
			{
                LogPrintf("| ");
			}
			
            LogPrintf("|\n");
       }

		nPrevCol = nCol;

		// print columns
		for (int i = 0; i < nCol; i++)
		{
			LogPrintf("| ");
		}
		
		// print item
		CBlock block;
		
		block.ReadFromDisk(pindex);
		
		LogPrintf("%d (%u,%u) %s  %08x  %s  mint %7s  tx %u",
		pindex->nHeight,
		pindex->nFile,
		pindex->nBlockPos,
		block.GetHash().ToString(),
		block.nBits,
		DateTimeStrFormat("%x %H:%M:%S", block.GetBlockTime()),
		FormatMoney(pindex->nMint),
		block.vtx.size());

		// put the main time-chain first
		std::vector<CBlockIndex*>& vNext = mapNext[pindex];
		
		for (unsigned int i = 0; i < vNext.size(); i++)
		{
			if (vNext[i]->pnext)
			{
				boost::swap(vNext[0], vNext[i]);
				
				break;
			}
		}

		// iterate children
		for (unsigned int i = 0; i < vNext.size(); i++)
		{
			vStack.push_back(std::make_pair(nCol+i, vNext[i]));
		}
    }
}

bool LoadExternalBlockFile(FILE* fileIn)
{
	int64_t nStart = GetTimeMillis();
	int nLoaded = 0;
	
	{
		try
		{
			CAutoFile blkdat(fileIn, SER_DISK, CLIENT_VERSION);
			
			unsigned int nPos = 0;
			
			while (nPos != (unsigned int)-1 && blkdat.good())
			{
				boost::this_thread::interruption_point();
				unsigned char pchData[65536];
				
				while(true)
				{
					fseek(blkdat, nPos, SEEK_SET);
					
					int nRead = fread(pchData, 1, sizeof(pchData), blkdat);
					
					if (nRead <= 8)
					{
						nPos = (unsigned int)-1;
						
						break;
					}
					
					void* nFind = memchr(pchData, Params().MessageStart()[0], nRead+1-MESSAGE_START_SIZE);
					
					if (nFind)
					{
						if (memcmp(nFind, Params().MessageStart(), MESSAGE_START_SIZE)==0)
						{
							nPos += ((unsigned char*)nFind - pchData) + MESSAGE_START_SIZE;
							
							break;
						}
						
						nPos += ((unsigned char*)nFind - pchData) + 1;
					}
					else
					{
						nPos += sizeof(pchData) - MESSAGE_START_SIZE + 1;
					}
					
					boost::this_thread::interruption_point();
				}
				
				if (nPos == (unsigned int)-1)
					break;
				
				fseek(blkdat, nPos, SEEK_SET);
				
				unsigned int nSize;
				blkdat >> nSize;
				
				if (nSize > 0 && nSize <= MAX_BLOCK_SIZE)
				{
					CBlock block;
					blkdat >> block;
					
					LOCK(cs_main);
					
					if (ProcessBlock(NULL,&block))
					{
						nLoaded++;
						nPos += 4 + nSize;
					}
				}
			}
		}
		catch (std::exception &e)
		{
			LogPrintf("%s() : Deserialize or I/O error caught during load\n", __PRETTY_FUNCTION__);
		}
	}
	
	LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
	
	return nLoaded > 0;
}

struct CImportingNow
{
	CImportingNow()
	{
		assert(fImporting == false);
		fImporting = true;
	}

	~CImportingNow()
	{
		assert(fImporting == true);
		fImporting = false;
	}
};

void ThreadImport(std::vector<boost::filesystem::path> vImportFiles)
{
	RenameThread("DigitalNote-loadblk");

	CImportingNow imp;

	// -loadblock=
	for(boost::filesystem::path &path : vImportFiles)
	{
		FILE *file = fopen(path.string().c_str(), "rb");
		
		if (file)
		{
			LoadExternalBlockFile(file);
		}
	}

	// hardcoded $DATADIR/bootstrap.dat
	boost::filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";

	if (boost::filesystem::exists(pathBootstrap))
	{
		FILE *file = fopen(pathBootstrap.string().c_str(), "rb");
		
		if (file)
		{
			boost::filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
			
			LoadExternalBlockFile(file);
			RenameOver(pathBootstrap, pathBootstrapOld);
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//

extern std::map<uint256, CAlert> mapAlerts;
extern CCriticalSection cs_mapAlerts;

std::string GetWarnings(const std::string &strFor)
{
	int nPriority = 0;
	std::string strStatusBar;
	std::string strRPC;

	if (GetBoolArg("-testsafemode", false))
	{
		strRPC = "test";
	}

	if (!CLIENT_VERSION_IS_RELEASE)
	{
		strStatusBar = ui_translate("This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");
	}

	// Misc warnings like out of disk space and clock is wrong
	if (strMiscWarning != "")
	{
		nPriority = 1000;
		strStatusBar = strMiscWarning;
	}

	// Alerts
	{
		LOCK(cs_mapAlerts);
		
		for(std::pair<const uint256, CAlert>& item : mapAlerts)
		{
			const CAlert& alert = item.second;
			if (alert.AppliesToMe() && alert.nPriority > nPriority)
			{
				nPriority = alert.nPriority;
				strStatusBar = alert.strStatusBar;
				
				if (nPriority > 1000)
				{
					strRPC = strStatusBar;
				}
			}
		}
	}

	if (strFor == "statusbar")
	{
		return strStatusBar;
	}
	else if (strFor == "rpc")
	{
		return strRPC;
	}

	assert(!"GetWarnings() : invalid parameter");

	return "error";
}

//////////////////////////////////////////////////////////////////////////////
//
// Messages
//

bool static AlreadyHave(CTxDB& txdb, const CInv& inv)
{
	switch (inv.type)
	{
		case MSG_DSTX:
			return mapMNengineBroadcastTxes.count(inv.hash);
		
		case MSG_TX:
			return mempool.exists(inv.hash) || mapOrphanTransactions.count(inv.hash) || txdb.ContainsTx(inv.hash);
		
		case MSG_BLOCK:
			return mapBlockIndex.count(inv.hash) || mapOrphanBlocks.count(inv.hash);
		
		case MSG_TXLOCK_REQUEST:
			return mapTxLockReq.count(inv.hash) || mapTxLockReqRejected.count(inv.hash);
		
		case MSG_TXLOCK_VOTE:
			return mapTxLockVote.count(inv.hash);
		
		case MSG_SPORK:
			return mapSporks.count(inv.hash);
		
		case MSG_MASTERNODE_WINNER:
			return mapSeenMasternodeVotes.count(inv.hash);
	}
	
	// Don't know what it is, just say we already got one
	return true;
}

void static ProcessGetData(CNode* pfrom)
{
	std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();

	std::vector<CInv> vNotFound;

	LOCK(cs_main);

	while (it != pfrom->vRecvGetData.end())
	{
		// Don't bother if send buffer is too full to respond anyway
		if (pfrom->nSendSize >= SendBufferSize())
		{
			break;
		}
		
		const CInv &inv = *it;
		
		{
			boost::this_thread::interruption_point();
			
			it++;

			if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK)
			{
				// Send block from disk
				std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(inv.hash);
				
				if (mi != mapBlockIndex.end())
				{
					CBlock block;
					block.ReadFromDisk((*mi).second);
					
					pfrom->PushMessage("block", block);

					// Trigger them to send a getblocks request for the next batch of inventory
					if (inv.hash == pfrom->hashContinue)
					{
						// Bypass PushInventory, this must send even if redundant,
						// and we want it right after the last block so they don't
						// wait for other stuff first.
						std::vector<CInv> vInv;
						vInv.push_back(CInv(MSG_BLOCK, hashBestChain));
						
						pfrom->PushMessage("inv", vInv);
						pfrom->hashContinue = 0;
					}
				}
			}
			else if (inv.IsKnownType())
			{
				if(fDebug)
				{
					LogPrintf("ProcessGetData -- Starting \n");
				}
				
				// Send stream from relay memory
				bool pushed = false;
				
				/*{
					LOCK(cs_mapRelay);
					std::map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
					if (mi != mapRelay.end()) {
						pfrom->PushMessage(inv.GetCommand(), (*mi).second);
						if(fDebug) LogPrintf("ProcessGetData -- pushed = true Rest will fail\n");
						pushed = true;
					}
				}*/
				if (!pushed && inv.type == MSG_TX) {

					CTransaction tx;
					
					if (mempool.lookup(inv.hash, tx))
					{
						CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
						
						ss.reserve(1000);
						ss << tx;
						
						pfrom->PushMessage("tx", ss);
						
						pushed = true;
					}
				}
				if (!pushed && inv.type == MSG_TXLOCK_VOTE)
				{
					if(mapTxLockVote.count(inv.hash))
					{
						CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
						
						ss.reserve(1000);
						ss << mapTxLockVote[inv.hash];
						
						pfrom->PushMessage("txlvote", ss);
						
						pushed = true;
					}
				}
				if (!pushed && inv.type == MSG_TXLOCK_REQUEST)
				{
					if(mapTxLockReq.count(inv.hash))
					{
						CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
						
						ss.reserve(1000);
						ss << mapTxLockReq[inv.hash];
						
						pfrom->PushMessage("txlreq", ss);
						
						pushed = true;
					}
				}
				if (!pushed && inv.type == MSG_SPORK)
				{
					if(mapSporks.count(inv.hash))
					{
						CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
						
						ss.reserve(1000);
						ss << mapSporks[inv.hash];
						
						pfrom->PushMessage("spork", ss);
						
						pushed = true;
					}
				}
				if (!pushed && inv.type == MSG_MASTERNODE_WINNER)
				{
					if(mapSeenMasternodeVotes.count(inv.hash))
					{
						CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
						
						ss.reserve(1000);
						ss << mapSeenMasternodeVotes[inv.hash];
						
						pfrom->PushMessage("mnw", ss);
						
						pushed = true;
					}
				}
				if (!pushed && inv.type == MSG_DSTX)
				{
					if(mapMNengineBroadcastTxes.count(inv.hash))
					{
						CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
						
						ss.reserve(1000);
						ss <<
							mapMNengineBroadcastTxes[inv.hash].tx <<
							mapMNengineBroadcastTxes[inv.hash].vin <<
							mapMNengineBroadcastTxes[inv.hash].vchSig <<
							mapMNengineBroadcastTxes[inv.hash].sigTime;

						pfrom->PushMessage("dstx", ss);
						
						pushed = true;
					}
				}
				
				if (!pushed)
				{
					vNotFound.push_back(inv);
				}
			}

			// Track requests for our stuff.
			g_signals.Inventory(inv.hash);

			if (inv.type == MSG_BLOCK  || inv.type == MSG_FILTERED_BLOCK)
			{
				break;
			}
		}
	}

	pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

	if (!vNotFound.empty())
	{
		// Let the peer know that we didn't find what it asked for, so it doesn't
		// have to wait around forever. Currently only SPV clients actually care
		// about this message: it's needed when they are recursively walking the
		// dependencies of relevant unconfirmed transactions. SPV clients want to
		// do that because they want to know about (and store and rebroadcast and
		// risk analyze) the dependencies of transactions relevant to them, without
		// having to download the entire memory pool.
		pfrom->PushMessage("notfound", vNotFound);
	}
}

bool static ProcessMessage(CNode* pfrom, std::string strCommand, CDataStream& vRecv)
{
	// this is a snapshot node. will only sync until certain block
	if (maxBlockHeight != -1 && pindexBest->nHeight >= maxBlockHeight)
	{
		return true;
	}

	RandAddSeedPerfmon();

	LogPrint("net", "received: %s (%u bytes)\n", strCommand, vRecv.size());

	if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
	{
		LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n");
		
		return true;
	}

	{
		LOCK(cs_main);
		
		State(pfrom->GetId())->nLastBlockProcess = GetTimeMicros();
	}

	if (strCommand == "version")
	{
		// Each connection can only send one version message
		if (pfrom->nVersion != 0)
		{
			Misbehaving(pfrom->GetId(), 1);
			
			return false;
		}

		int64_t nTime;
		CAddress addrMe;
		CAddress addrFrom;
		uint64_t nNonce = 1;
		
		vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;
		
		if(pfrom->nVersion <= (PROTOCOL_VERSION - 1))
		{
			if(pindexBest->GetBlockTime() > HRD_LEGACY_CUTOFF)
			{
				// disconnect from peers older than legacy cutoff allows : Disconnect message 02
				LogPrintf("partner %s using obsolete version %i; disconnecting DCM:02\n", pfrom->addr.ToString(), pfrom->nVersion);
				
				pfrom->fDisconnect = true;
				
				return false;
			}
			else if(pfrom->nVersion < MIN_PEER_PROTO_VERSION)
			{
				// disconnect from peers older than this proto version : Disconnect message 01
				LogPrintf("partner %s using obsolete version %i; disconnecting DCM:01\n", pfrom->addr.ToString(), pfrom->nVersion);
				
				pfrom->fDisconnect = true;
				
				return false;
			}
		}

		if (pfrom->nVersion == 10300)
		{
			pfrom->nVersion = 300;
		}
		
		if (!vRecv.empty())
		{
			vRecv >> addrFrom >> nNonce;
		}
		
		if (!vRecv.empty())
		{
			vRecv >> pfrom->strSubVer;
			pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);
		}
		
		if (!vRecv.empty())
		{
			vRecv >> pfrom->nStartingHeight;
		}
		
		if (!vRecv.empty())
		{
			pfrom->fRelayTxes = true;
		}
		
		// Disconnect if we connected to ourself
		if (nNonce == nLocalHostNonce && nNonce > 1)
		{
			LogPrintf("connected to self at %s, disconnecting\n", pfrom->addr.ToString());
			
			pfrom->fDisconnect = true;
			
			return true;
		}

		if (pfrom->fInbound && addrMe.IsRoutable())
		{
			pfrom->addrLocal = addrMe;
			
			SeenLocal(addrMe);
		}

		// Be shy and don't send version until we hear
		if (pfrom->fInbound)
		{
			pfrom->PushVersion();
		}
		
		pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

		// Change version
		pfrom->PushMessage("verack");
		pfrom->ssSend.SetVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));

		if (!pfrom->fInbound)
		{
			// Advertise our address
			if (!fNoListen && !IsInitialBlockDownload())
			{
				CAddress addr = GetLocalAddress(&pfrom->addr);
				
				if (addr.IsRoutable())
				{
					pfrom->PushAddress(addr);
				}
				else if (IsPeerAddrLocalGood(pfrom))
				{
					addr.SetIP(pfrom->addrLocal);
					pfrom->PushAddress(addr);
				}
			}

			// Get recent addresses
			if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000)
			{
				pfrom->PushMessage("getaddr");
				pfrom->fGetAddr = true;
			}
			addrman.Good(pfrom->addr);
		}
		else
		{
			if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
			{
				addrman.Add(addrFrom, addrFrom);
				addrman.Good(addrFrom);
			}
		}

		// Relay alerts
		{
			LOCK(cs_mapAlerts);
			
			for(std::pair<const uint256, CAlert>& item : mapAlerts)
			{
				item.second.RelayTo(pfrom);
			}
		}

		pfrom->fSuccessfullyConnected = true;

		LogPrintf("receive version message: version %d, blocks=%d, us=%s, them=%s, peer=%s\n",
			pfrom->nVersion,
			pfrom->nStartingHeight,
			addrMe.ToString(),
			addrFrom.ToString(),
			pfrom->addr.ToString()
		);

		if (GetBoolArg("-synctime", true))
		{
			AddTimeData(pfrom->addr, nTime);
		}
	}
	else if (pfrom->nVersion == 0)
	{
		// Must have a version message before anything else
		Misbehaving(pfrom->GetId(), 1);
		
		return false;
	}
	else if (strCommand == "verack")
	{
		pfrom->SetRecvVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));
	}
	else if (strCommand == "addr")
	{
		std::vector<CAddress> vAddr;
		
		vRecv >> vAddr;

		// Don't want addr from older versions unless seeding
		if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
		{
			return true;
		}
		
		if (vAddr.size() > 1000)
		{
			Misbehaving(pfrom->GetId(), 20);
			
			return error("message addr size() = %u", vAddr.size());
		}

		// Store the new addresses
		std::vector<CAddress> vAddrOk;
		int64_t nNow = GetAdjustedTime();
		int64_t nSince = nNow - 10 * 60;
		
		for(CAddress& addr : vAddr)
		{
			boost::this_thread::interruption_point();

			if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
			{
				addr.nTime = nNow - 5 * 24 * 60 * 60;
			}
			
			pfrom->AddAddressKnown(addr);
			
			bool fReachable = IsReachable(addr);
			
			if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
			{
				// Relay to a limited number of other nodes
				{
					LOCK(cs_vNodes);
					
					// Use deterministic randomness to send to the same nodes for 24 hours
					// at a time so the setAddrKnowns of the chosen nodes prevent repeats
					static uint256 hashSalt;
					if (hashSalt == 0)
					{
						hashSalt = GetRandHash();
					}
					
					uint64_t hashAddr = addr.GetHash();
					uint256 hashRand = hashSalt ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60));
					hashRand = Hash_bmw512(BEGIN(hashRand), END(hashRand));
					std::multimap<uint256, CNode*> mapMix;
					
					for(CNode* pnode : vNodes)
					{
						if (pnode->nVersion < CADDR_TIME_VERSION)
						{
							continue;
						}
						
						unsigned int nPointer;
						memcpy(&nPointer, &pnode, sizeof(nPointer));
						
						uint256 hashKey = hashRand ^ nPointer;
						hashKey = Hash_bmw512(BEGIN(hashKey), END(hashKey));
						
						mapMix.insert(std::make_pair(hashKey, pnode));
					}
					
					int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
					for (std::multimap<uint256, CNode*>::iterator mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
					{
						((*mi).second)->PushAddress(addr);
					}
				}
			}
			
			// Do not store addresses outside our network
			if (fReachable)
			{
				vAddrOk.push_back(addr);
			}
		}
		
		addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
		
		if (vAddr.size() < 1000)
		{
			pfrom->fGetAddr = false;
		}
		
		if (pfrom->fOneShot)
		{
			pfrom->fDisconnect = true;
		}
	}
	else if (strCommand == "inv")
	{
		std::vector<CInv> vInv;
		
		vRecv >> vInv;
		
		if (vInv.size() > MAX_INV_SZ)
		{
			Misbehaving(pfrom->GetId(), 20);
			
			return error("message inv size() = %u", vInv.size());
		}

		LOCK(cs_main);
		
		CTxDB txdb("r");
		
		for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
		{
			const CInv &inv = vInv[nInv];

			boost::this_thread::interruption_point();
			pfrom->AddInventoryKnown(inv);

			bool fAlreadyHave = AlreadyHave(txdb, inv);
			
			LogPrint("net", "  got inventory: %s  %s\n", inv.ToString(), fAlreadyHave ? "have" : "new");

			if (!fAlreadyHave)
			{
				if (!fImporting && !fReindex)
				{
					if (inv.type == MSG_BLOCK)
					{
						AddBlockToQueue(pfrom->GetId(), inv.hash);
					}
					else
					{
						pfrom->AskFor(inv);
					}
				}
			}
			else if (inv.type == MSG_BLOCK && mapOrphanBlocks.count(inv.hash))
			{
				PushGetBlocks(pfrom, pindexBest, GetOrphanRoot(inv.hash));
			}

			// Track requests for our stuff
			g_signals.Inventory(inv.hash);
		}
	}
	else if (strCommand == "getdata")
	{
		std::vector<CInv> vInv;
		
		vRecv >> vInv;
		
		if (vInv.size() > MAX_INV_SZ)
		{
			Misbehaving(pfrom->GetId(), 20);
			
			return error("message getdata size() = %u", vInv.size());
		}

		if (fDebug || (vInv.size() != 1))
		{
			LogPrint("net", "received getdata (%u invsz)\n", vInv.size());
		}
		
		if ((fDebug && vInv.size() > 0) || (vInv.size() == 1))
		{
			LogPrint("net", "received getdata for: %s\n", vInv[0].ToString());
		}
		
		pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end());
		
		ProcessGetData(pfrom);
	}
	else if (strCommand == "getblocks")
	{
		CBlockLocator locator;
		uint256 hashStop;
		vRecv >> locator >> hashStop;

		LOCK(cs_main);

		// Find the last block the caller has in the main chain
		CBlockIndex* pindex = locator.GetBlockIndex();

		// Send the rest of the chain
		if (pindex)
		{
			pindex = pindex->pnext;
		}
		
		int nLimit = 5000;
		
		LogPrint("net", "getblocks %d to %s limit %d\n", (pindex ? pindex->nHeight : -1), hashStop.ToString(), nLimit);
		
		for (; pindex; pindex = pindex->pnext)
		{
			if (pindex->GetBlockHash() == hashStop)
			{
				LogPrint("net", "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
				
				break;
			}
			
			pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
			
			if (--nLimit <= 0)
			{
				// When this block is requested, we'll send an inv that'll make them
				// getblocks the next batch of inventory.
				LogPrint("net", "  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
				
				pfrom->hashContinue = pindex->GetBlockHash();
				
				break;
			}
		}
	}
	else if (strCommand == "getheaders")
	{
		CBlockLocator locator;
		uint256 hashStop;
		
		vRecv >> locator >> hashStop;

		LOCK(cs_main);

		if (IsInitialBlockDownload())
		{
			return true;
		}

		CBlockIndex* pindex = NULL;
		
		if (locator.IsNull())
		{
			// If locator is null, return the hashStop block
			std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashStop);
			
			if (mi == mapBlockIndex.end())
			{
				return true;
			}
			
			pindex = (*mi).second;
		}
		else
		{
			// Find the last block the caller has in the main chain
			pindex = locator.GetBlockIndex();
			
			if (pindex)
			{
				pindex = pindex->pnext;
			}
		}

		std::vector<CBlock> vHeaders;
		int nLimit = 2000;
		
		LogPrint("net", "getheaders %d to %s\n", (pindex ? pindex->nHeight : -1), hashStop.ToString());
		
		for (; pindex; pindex = pindex->pnext)
		{
			vHeaders.push_back(pindex->GetBlockHeader());
			
			if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
			{
				break;
			}
		}
		pfrom->PushMessage("headers", vHeaders);
	}
	else if (strCommand == "tx"|| strCommand == "dstx")
	{
		std::vector<uint256> vWorkQueue;
		std::vector<uint256> vEraseQueue;
		CTransaction tx;

		//masternode signed transaction
		bool ignoreFees = false;
		CTxIn vin;
		CInv inv;
		std::vector<unsigned char> vchSig;
		int64_t sigTime;
		CTxDB txdb("r");

		if(strCommand == "tx")
		{
			vRecv >> tx;
			
			inv = CInv(MSG_TX, tx.GetHash());
			
			// Check for recently rejected (and do other quick existence checks)
			if (AlreadyHave(txdb, inv))
			{
				return true;
			}
		}
		else if (strCommand == "dstx")
		{
			vRecv >> tx >> vin >> vchSig >> sigTime;
			
			inv = CInv(MSG_DSTX, tx.GetHash());
			
			// Check for recently rejected (and do other quick existence checks)
			if (AlreadyHave(txdb, inv))
			{
				return true;
			}
			
			//these allow masternodes to publish a limited amount of free transactions

			CMasternode* pmn = mnodeman.Find(vin);
			if(pmn != NULL)
			{
				if(!pmn->allowFreeTx)
				{
					//multiple peers can send us a valid masternode transaction
					if(fDebug)
					{
						LogPrintf("dstx: Masternode sending too many transactions %s\n", tx.GetHash().ToString().c_str());
					}
					
					return true;
				}

				std::string strMessage = tx.GetHash().ToString() + boost::lexical_cast<std::string>(sigTime);

				std::string errorMessage = "";
				if(!mnEngineSigner.VerifyMessage(pmn->pubkey2, vchSig, strMessage, errorMessage))
				{
					LogPrintf("dstx: Got bad masternode address signature %s \n", vin.ToString().c_str());
					
					//Misbehaving(pfrom->GetId(), 20);
					
					return false;
				}

				LogPrintf("dstx: Got Masternode transaction %s\n", tx.GetHash().ToString().c_str());

				ignoreFees = true;
				pmn->allowFreeTx = false;

				if(!mapMNengineBroadcastTxes.count(tx.GetHash()))
				{
					CMNengineBroadcastTx dstx;
					
					dstx.tx = tx;
					dstx.vin = vin;
					dstx.vchSig = vchSig;
					dstx.sigTime = sigTime;

					mapMNengineBroadcastTxes.insert(std::make_pair(tx.GetHash(), dstx));
				}
			}
		}

		pfrom->AddInventoryKnown(inv);

		LOCK(cs_main);

		bool fMissingInputs = false;

		pfrom->setAskFor.erase(inv.hash);
		mapAlreadyAskedFor.erase(inv);

		if (AcceptToMemoryPool(mempool, tx, true, &fMissingInputs, false, ignoreFees))
		{
			RelayTransaction(tx, inv.hash);
			vWorkQueue.push_back(inv.hash);

			// Recursively process any orphan transactions that depended on this one
			for (unsigned int i = 0; i < vWorkQueue.size(); i++)
			{
				std::map<uint256, std::set<uint256>>::iterator itByPrev = mapOrphanTransactionsByPrev.find(vWorkQueue[i]);
				
				if (itByPrev == mapOrphanTransactionsByPrev.end())
				{
					continue;
				}
			
				for (std::set<uint256>::iterator mi = itByPrev->second.begin(); mi != itByPrev->second.end(); ++mi)
				{
					const uint256& orphanTxHash = *mi;
					CTransaction& orphanTx = mapOrphanTransactions[orphanTxHash];
					bool fMissingInputs2 = false;

					if (AcceptToMemoryPool(mempool, orphanTx, true, &fMissingInputs2))
					{
						LogPrint("mempool", "   accepted orphan tx %s\n", orphanTxHash.ToString());
						
						RelayTransaction(orphanTx, orphanTxHash);
						vWorkQueue.push_back(orphanTxHash);
						vEraseQueue.push_back(orphanTxHash);
					}
					else if (!fMissingInputs2)
					{
						// Has inputs but not accepted to mempool
						// Probably non-standard or insufficient fee/priority
						vEraseQueue.push_back(orphanTxHash);
						
						LogPrint("mempool", "   removed orphan tx %s\n", orphanTxHash.ToString());
					}
				}
			}

			for(uint256 hash : vEraseQueue)
			{
				EraseOrphanTx(hash);
			}
		}
		else if (fMissingInputs)
		{
			AddOrphanTx(tx);

			// DoS prevention: do not allow mapOrphanTransactions to grow unbounded
			unsigned int nEvicted = LimitOrphanTxSize(MAX_ORPHAN_TRANSACTIONS);
			
			if (nEvicted > 0)
			{
				LogPrint("mempool", "mapOrphan overflow, removed %u tx\n", nEvicted);
			}
		}
		
		if(strCommand == "dstx")
		{
			inv = CInv(MSG_DSTX, tx.GetHash());
			RelayInventory(inv);
		}
		
		if (tx.nDoS)
		{
			Misbehaving(pfrom->GetId(), tx.nDoS);
		}
	}
	else if (strCommand == "block" && !fImporting && !fReindex) // Ignore blocks received while importing
	{
		CBlock block;
		vRecv >> block;
		uint256 hashBlock = block.GetHash();

		LogPrint("net", "received block %s\n", hashBlock.ToString());

		CInv inv(MSG_BLOCK, hashBlock);
		pfrom->AddInventoryKnown(inv);

		LOCK(cs_main);
		
		// Remember who we got this block from.
		mapBlockSource[inv.hash] = pfrom->GetId();
		MarkBlockAsReceived(inv.hash, pfrom->GetId());

		ProcessBlock(pfrom, &block);
		if (block.nDoS)
		{
			Misbehaving(pfrom->GetId(), block.nDoS);
		}
		
		if (DigitalNote::SMSG::ext_enabled)
		{
			DigitalNote::SMSG::ScanBlock(block);
		}
	}
	// This asymmetric behavior for inbound and outbound connections was introduced
	// to prevent a fingerprinting attack: an attacker can send specific fake addresses
	// to users' AddrMan and later request them by sending getaddr messages.
	// Making users (which are behind NAT and can only make outgoing connections) ignore
	// getaddr message mitigates the attack.
	else if ((strCommand == "getaddr") && (pfrom->fInbound))
	{
		// Don't return addresses older than nCutOff timestamp
		int64_t nCutOff = GetTime() - (nNodeLifespan * 24 * 60 * 60);
		pfrom->vAddrToSend.clear();
		std::vector<CAddress> vAddr = addrman.GetAddr();
		
		for(const CAddress &addr : vAddr)
		{
			if(addr.nTime > nCutOff)
			{
				pfrom->PushAddress(addr);
			}
		}
	}
	else if (strCommand == "mempool")
	{
		LOCK(cs_main);

		std::vector<uint256> vtxid;
		std::vector<CInv> vInv;
		CInv inv;
		
		mempool.queryHashes(vtxid);
		
		for (unsigned int i = 0; i < vtxid.size(); i++)
		{
			inv = CInv(MSG_TX, vtxid[i]);
			vInv.push_back(inv);
			
			if (i == (MAX_INV_SZ - 1))
			{
				break;
			}
		}
		
		if (vInv.size() > 0)
		{
			pfrom->PushMessage("inv", vInv);
		}
	}
	else if (strCommand == "ping")
	{
		if (pfrom->nVersion > BIP0031_VERSION)
		{
			uint64_t nonce = 0;
			
			vRecv >> nonce;
			
			// Echo the message back with the nonce. This allows for two useful features:
			//
			// 1) A remote node can quickly check if the connection is operational
			// 2) Remote nodes can measure the latency of the network thread. If this node
			//    is overloaded it won't respond to pings quickly and the remote node can
			//    avoid sending us more work, like chain download requests.
			//
			// The nonce stops the remote getting confused between different pings: without
			// it, if the remote node sends a ping once per second and this node takes 5
			// seconds to respond to each, the 5th ping the remote sends would appear to
			// return very quickly.
			pfrom->PushMessage("pong", nonce);
		}
	}
	else if (strCommand == "pong")
	{
		int64_t pingUsecEnd = GetTimeMicros();
		uint64_t nonce = 0;
		size_t nAvail = vRecv.in_avail();
		bool bPingFinished = false;
		std::string sProblem;

		if (nAvail >= sizeof(nonce))
		{
			vRecv >> nonce;

			// Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
			if (pfrom->nPingNonceSent != 0)
			{
				if (nonce == pfrom->nPingNonceSent)
				{
					// Matching pong received, this ping is no longer outstanding
					bPingFinished = true;
					int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
					
					if (pingUsecTime > 0)
					{
						// Successful ping time measurement, replace previous
						pfrom->nPingUsecTime = pingUsecTime;
					}
					else
					{
						// This should never happen
						sProblem = "Timing mishap";
					}
				}
				else
				{
					// Nonce mismatches are normal when pings are overlapping
					sProblem = "Nonce mismatch";
					
					if (nonce == 0)
					{
						// This is most likely a bug in another implementation somewhere, cancel this ping
						bPingFinished = true;
						sProblem = "Nonce zero";
					}
				}
			}
			else
			{
				sProblem = "Unsolicited pong without ping";
			}
		}
		else
		{
			// This is most likely a bug in another implementation somewhere, cancel this ping
			bPingFinished = true;
			sProblem = "Short payload";
		}

		if (!(sProblem.empty()))
		{
			LogPrint(
				"net",
				"pong %s %s: %s, %x expected, %x received, %zu bytes\n",
				pfrom->addr.ToString(),
				pfrom->strSubVer,
				sProblem,
				pfrom->nPingNonceSent,
				nonce,
				nAvail
			);
		}
		
		if (bPingFinished)
		{
			pfrom->nPingNonceSent = 0;
		}
	}
	else if (strCommand == "alert")
	{
		CAlert alert;
		
		vRecv >> alert;

		uint256 alertHash = alert.GetHash();
		if (pfrom->setKnown.count(alertHash) == 0)
		{
			if (alert.ProcessAlert())
			{
				// Relay
				pfrom->setKnown.insert(alertHash);
				{
					LOCK(cs_vNodes);
					
					for(CNode* pnode : vNodes)
					{
						alert.RelayTo(pnode);
					}
				}
			}
			else
			{
				// Small DoS penalty so peers that send us lots of
				// duplicate/expired/invalid-signature/whatever alerts
				// eventually get banned.
				// This isn't a Misbehaving(100) (immediate ban) because the
				// peer might be an older or different implementation with
				// a different signature key, etc.
				Misbehaving(pfrom->GetId(), 10);
			}
		}
	}
	else
	{
		if (DigitalNote::SMSG::ext_enabled)
		{
			DigitalNote::SMSG::ReceiveData(pfrom, strCommand, vRecv);
		}
		
		// Relay and Request MasterNode peer data
		//
		// We already have checks to ensure we relay
		// only the data that our peers can accept
		// so this has to be enabled for basic reference
		// relays such as masternode lists to occur.
		mnodeman.ProcessMessage(pfrom, strCommand, vRecv);
		ProcessMessageMasternodePayments(pfrom, strCommand, vRecv);
		ProcessMessageInstantX(pfrom, strCommand, vRecv);
		ProcessSpork(pfrom, strCommand, vRecv);
		// Ignore unknown commands for extensibility
	}

	// Update the last seen time for this node's address
	if (pfrom->fNetworkNode)
	{
		if (strCommand == "version" || strCommand == "addr" || strCommand == "inv" || strCommand == "getdata" || strCommand == "ping")
		{
			AddressCurrentlyConnected(pfrom->addr);
		}
	}

	return true;
}

// Adaptive block sizing depends on this
FILE* OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
{
	if (pos.IsNull())
	{
		return NULL;
	}

	boost::filesystem::path path = GetBlockPosFilename(pos, prefix);
	boost::filesystem::create_directories(path.parent_path());
	FILE* file = fopen(path.string().c_str(), "rb+");

	if (!file && !fReadOnly)
	{
		file = fopen(path.string().c_str(), "wb+");
	}

	if (!file)
	{
		LogPrintf("Unable to open file %s\n", path.string());
		
		return NULL;
	}

	if (pos.nPos)
	{
		if (fseek(file, pos.nPos, SEEK_SET))
		{
			LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
			fclose(file);
			
			return NULL;
		}
	}

	return file;
}

FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "rev", fReadOnly);
}

boost::filesystem::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix)
{
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

// requires LOCK(cs_vRecvMsg)
bool ProcessMessages(CNode* pfrom)
{
	//if (fDebug)
	//    LogPrintf("ProcessMessages(%zu messages)\n", pfrom->vRecvMsg.size());

	//
	// Message format
	//  (4) message start
	//  (12) command
	//  (4) size
	//  (4) checksum
	//  (x) data
	//
	bool fOk = true;

	if (!pfrom->vRecvGetData.empty())
	{
		ProcessGetData(pfrom);
	}

	// this maintains the order of responses
	if (!pfrom->vRecvGetData.empty())
	{
		return fOk;
	}

	std::deque<CNetMessage>::iterator it = pfrom->vRecvMsg.begin();

	while (!pfrom->fDisconnect && it != pfrom->vRecvMsg.end())
	{
		// Don't bother if send buffer is too full to respond anyway
		if (pfrom->nSendSize >= SendBufferSize())
		{
			break;
		}
		
		// get next message
		CNetMessage& msg = *it;

		//if (fDebug)
		//    LogPrintf("ProcessMessages(message %u msgsz, %zu bytes, complete:%s)\n",
		//            msg.hdr.nMessageSize, msg.vRecv.size(),
		//            msg.complete() ? "Y" : "N");

		// end, if an incomplete message is found
		if (!msg.complete())
		{
			break;
		}
		
		// at this point, any failure means we can delete the current message
		it++;

		// Scan for message start
		if (memcmp(msg.hdr.pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE) != 0)
		{
			LogPrintf("\n\nPROCESSMESSAGE: INVALID MESSAGESTART\n\n");
			
			fOk = false;
			
			break;
		}

		// Read header
		CMessageHeader& hdr = msg.hdr;
		
		if (!hdr.IsValid())
		{
			LogPrintf("\n\nPROCESSMESSAGE: ERRORS IN HEADER %s\n\n\n", hdr.GetCommand());
			
			continue;
		}
		
		std::string strCommand = hdr.GetCommand();

		// Message size
		unsigned int nMessageSize = hdr.nMessageSize;

		// Checksum
		CDataStream& vRecv = msg.vRecv;
		uint256 hash = Hash_bmw512(vRecv.begin(), vRecv.begin() + nMessageSize);
		unsigned int nChecksum = 0;
		
		memcpy(&nChecksum, &hash, sizeof(nChecksum));
		
		if (nChecksum != hdr.nChecksum)
		{
			LogPrintf(
				"ProcessMessages(%s, %u bytes) : CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n",
			   strCommand,
			   nMessageSize,
			   nChecksum,
			   hdr.nChecksum
			);
			
			continue;
		}

		// Process message
		bool fRet = false;
		
		try
		{
			fRet = ProcessMessage(pfrom, strCommand, vRecv);
			
			boost::this_thread::interruption_point();
		}
		catch (std::ios_base::failure& e)
		{
			if (strstr(e.what(), "end of data"))
			{
				// Allow exceptions from under-length message on vRecv
				LogPrintf("ProcessMessages(%s, %u bytes) : Exception '%s' caught, normally caused by a message being shorter than its stated length\n", strCommand, nMessageSize, e.what());
			}
			else if (strstr(e.what(), "size too large"))
			{
				// Allow exceptions from over-long size
				LogPrintf("ProcessMessages(%s, %u bytes) : Exception '%s' caught\n", strCommand, nMessageSize, e.what());
			}
			else
			{
				PrintExceptionContinue(&e, "ProcessMessages()");
			}
		}
		catch (boost::thread_interrupted)
		{
			throw;
		}
		catch (std::exception& e)
		{
			PrintExceptionContinue(&e, "ProcessMessages()");
		}
		catch (...)
		{
			PrintExceptionContinue(NULL, "ProcessMessages()");
		}

		if (!fRet)
		{
			LogPrintf("ProcessMessage(%s, %u bytes) FAILED\n", strCommand, nMessageSize);
		}
		
		break;
	}

	// In case the connection got shut down, its receive buffer was wiped
	if (!pfrom->fDisconnect)
	{
		pfrom->vRecvMsg.erase(pfrom->vRecvMsg.begin(), it);
	}

	return fOk;
}

bool SendMessages(CNode* pto, bool fSendTrickle)
{
	TRY_LOCK(cs_main, lockMain);

	if (lockMain)
	{
		// Don't send anything until we get their version message
		if (pto->nVersion == 0)
		{
			return true;
		}
		
		//
		// Message: ping
		//
		bool pingSend = false;
		
		if (pto->fPingQueued)
		{
			// RPC ping request by user
			pingSend = true;
		}
		
		if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros())
		{
			// Ping automatically sent as a latency probe & keepalive.
			pingSend = true;
		}
		
		if (pingSend)
		{
			uint64_t nonce = 0;
			
			while (nonce == 0)
			{
				GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
			}
			
			pto->fPingQueued = false;
			pto->nPingUsecStart = GetTimeMicros();
			
			if (pto->nVersion > BIP0031_VERSION)
			{
				pto->nPingNonceSent = nonce;
				pto->PushMessage("ping", nonce);
			}
			else
			{
				// Peer is too old to support ping command with nonce, pong will never arrive.
				pto->nPingNonceSent = 0;
				pto->PushMessage("ping");
			}
		}

		TRY_LOCK(cs_main, lockMain); // Acquire cs_main for IsInitialBlockDownload() and CNodeState()
		
		if (!lockMain)
		{
			return true;
		}
		
		// Start block sync
		if (pto->fStartSync && !fImporting && !fReindex)
		{
			pto->fStartSync = false;
			PushGetBlocks(pto, pindexBest, uint256(0));
		}

		// Resend wallet transactions that haven't gotten in a block yet
		// Except during reindex, importing and IBD, when old wallet
		// transactions become unconfirmed and spams other nodes.
		if (!fReindex && !fImporting && !IsInitialBlockDownload())
		{
			ResendWalletTransactions();
		}

		// Address refresh broadcast
		static int64_t nLastRebroadcast;
		if (!IsInitialBlockDownload() && (GetTime() - nLastRebroadcast > 24 * 60 * 60))
		{
			{
				LOCK(cs_vNodes);
				
				for(CNode* pnode : vNodes)
				{
					// Periodically clear setAddrKnown to allow refresh broadcasts
					if (nLastRebroadcast)
					{
						pnode->setAddrKnown.clear();
					}
					
					// Rebroadcast our address
					if (!fNoListen)
					{
						CAddress addr = GetLocalAddress(&pnode->addr);
						
						if (addr.IsRoutable())
						{
							pnode->PushAddress(addr);
						}
					}
				}
			}
			
			nLastRebroadcast = GetTime();
		}

		//
		// Message: addr
		//
		if (fSendTrickle)
		{
			std::vector<CAddress> vAddr;
			
			vAddr.reserve(pto->vAddrToSend.size());
			
			for(const CAddress& addr : pto->vAddrToSend)
			{
				// returns true if wasn't already contained in the set
				if (pto->setAddrKnown.insert(addr).second)
				{
					vAddr.push_back(addr);
					// receiver rejects addr messages larger than 1000
					if (vAddr.size() >= 1000)
					{
						pto->PushMessage("addr", vAddr);
						
						vAddr.clear();
					}
				}
			}
			
			pto->vAddrToSend.clear();
			
			if (!vAddr.empty())
			{
				pto->PushMessage("addr", vAddr);
			}
		}

		CNodeState &state = *State(pto->GetId());
		
		if (state.fShouldBan)
		{
			if (pto->addr.IsLocal())
			{
				LogPrintf("Warning: not banning local node %s!\n", pto->addr.ToString().c_str());
			}
			else
			{
				pto->fDisconnect = true;
				
				CNode::Ban(pto->addr, BanReasonNodeMisbehaving);
			}
			state.fShouldBan = false;
		}

		for(const CBlockReject& reject : state.rejects)
		{
			pto->PushMessage("reject", (std::string)"block", reject.chRejectCode, reject.strRejectReason, reject.hashBlock);
		}

		state.rejects.clear();

		//
		// Message: inventory
		//
		std::vector<CInv> vInv;
		std::vector<CInv> vInvWait;
		
		{
			LOCK(pto->cs_inventory);
			
			vInv.reserve(pto->vInventoryToSend.size());
			vInvWait.reserve(pto->vInventoryToSend.size());
			
			for(const CInv& inv : pto->vInventoryToSend)
			{
				if (pto->setInventoryKnown.count(inv))
				{
					continue;
				}
				
				// trickle out tx inv to protect privacy
				if (inv.type == MSG_TX && !fSendTrickle)
				{
					// 1/4 of tx invs blast to all immediately
					static uint256 hashSalt;
					
					if (hashSalt == 0)
					{
						hashSalt = GetRandHash();
					}
					
					uint256 hashRand = inv.hash ^ hashSalt;
					hashRand = Hash_bmw512(BEGIN(hashRand), END(hashRand));
					bool fTrickleWait = ((hashRand & 3) != 0);

					if (fTrickleWait)
					{
						vInvWait.push_back(inv);
						
						continue;
					}
				}

				// returns true if wasn't already contained in the set
				if (pto->setInventoryKnown.insert(inv).second)
				{
					vInv.push_back(inv);
					
					if (vInv.size() >= 1000)
					{
						pto->PushMessage("inv", vInv);
						
						vInv.clear();
					}
				}
			}
			
			pto->vInventoryToSend = vInvWait;
		}
		
		if (!vInv.empty())
		{
			pto->PushMessage("inv", vInv);
		}
		
		// Detect stalled peers. Require that blocks are in flight, we haven't
		// received a (requested) block in one minute, and that all blocks are
		// in flight for over two minutes, since we first had a chance to
		// process an incoming block.
		int64_t nNow = GetTimeMicros();
		
		if (
			!pto->fDisconnect &&
			state.nBlocksInFlight &&
			state.nLastBlockReceive < state.nLastBlockProcess - BLOCK_DOWNLOAD_TIMEOUT*1000000 &&
			state.vBlocksInFlight.front().nTime < state.nLastBlockProcess - 2*BLOCK_DOWNLOAD_TIMEOUT*1000000
		)
		{
			LogPrintf("Peer %s is stalling block download, disconnecting\n", state.name.c_str());
			
			pto->fDisconnect = true;
		}


		//
		// Message: getdata (blocks)
		//
		std::vector<CInv> vGetData;
		CTxDB txdb("r");
		
		while (!pto->fDisconnect && state.nBlocksToDownload && state.nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER)
		{
			uint256 hash = state.vBlocksToDownload.front();
			vGetData.push_back(CInv(MSG_BLOCK, hash));
			MarkBlockAsInFlight(pto->GetId(), hash);
			
			LogPrint("net", "Requesting block %s from %s\n", hash.ToString().c_str(), state.name.c_str());
			
			if (vGetData.size() >= 1000)
			{
				pto->PushMessage("getdata", vGetData);
				
				vGetData.clear();
			}
		}

		//
		// Message: getdata (non-blocks)
		//
		while (!pto->fDisconnect && !pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
		{
			const CInv& inv = (*pto->mapAskFor.begin()).second;
			
			if (!AlreadyHave(txdb, inv))
			{
				if (fDebug)
				{
					LogPrint("net", "sending getdata: %s\n", inv.ToString());
				}
				
				vGetData.push_back(inv);
				
				if (vGetData.size() >= 1000)
				{
					pto->PushMessage("getdata", vGetData);
					
					vGetData.clear();
				}
			}
			else
			{
				//If we're not going to ask, don't expect a response.
				pto->setAskFor.erase(inv.hash);
			}
			
			pto->mapAskFor.erase(pto->mapAskFor.begin());
		}
		
		if (!vGetData.empty())
		{
			pto->PushMessage("getdata", vGetData);
		}
		
		if (DigitalNote::SMSG::ext_enabled)
		{
			DigitalNote::SMSG::SendData(pto, fSendTrickle); // should be in cs_main?
		}
	}

	return true;
}

