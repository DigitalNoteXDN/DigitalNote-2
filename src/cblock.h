#ifndef CBLOCK_H
#define CBLOCK_H

#include <vector>
#include <memory>

#include "uint/uint256.h"

class CBlockIndex;
class COutPoint;
class CTxDB;
class CWallet;
class CBlock;
class CTransaction;

typedef std::unique_ptr<CBlock> CBlockPtr;

//
// Legacy code
//
//#ifdef __GNUC__
//	#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
//	
//	/* Test for GCC < 6.3.0 */
//	#if GCC_VERSION > 60300
//		
//	#else
//		typedef std::auto_ptr<CBlock> CBlockPtr;
//	#endif
//#else
//	typedef std::unique_ptr<CBlock> CBlockPtr;
//#endif

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 *
 * Blocks are appended to blk0001.dat files on disk.  Their location on disk
 * is indexed by CBlockIndex objects in memory.
 */
class CBlock
{
public:
	// header
	static const int CURRENT_VERSION = 7;
	int nVersion;
	uint256 hashPrevBlock;
	uint256 hashMerkleRoot;
	unsigned int nTime;
	unsigned int nBits;
	unsigned int nNonce;

	// network and disk
	std::vector<CTransaction> vtx;

	// ppcoin: block signature - signed by one of the coin base txout[N]'s owner
	std::vector<unsigned char> vchBlockSig;

	// memory only
	mutable std::vector<uint256> vMerkleTree;

	// Denial-of-service detection:
	mutable int nDoS;
	bool DoS(int nDoSIn, bool fIn) const;

	CBlock();

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	void SetNull();
	bool IsNull() const;
	uint256 GetHash() const;
	uint256 GetPoWHash() const;
	int64_t GetBlockTime() const;
	void UpdateTime(const CBlockIndex* pindexPrev);

	// entropy bit for stake modifier if chosen by modifier
	unsigned int GetStakeEntropyBit() const;
	// ppcoin: two types of block: proof-of-work or proof-of-stake
	bool IsProofOfStake() const;
	bool IsProofOfWork() const;
	std::pair<COutPoint, unsigned int> GetProofOfStake() const;
	// ppcoin: get max transaction timestamp
	int64_t GetMaxTransactionTime() const;
	uint256 BuildMerkleTree() const;
	std::vector<uint256> GetMerkleBranch(int nIndex) const;
	static uint256 CheckMerkleBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex);

	bool WriteToDisk(unsigned int& nFileRet, unsigned int& nBlockPosRet);
	bool ReadFromDisk(unsigned int nFile, unsigned int nBlockPos, bool fReadTransactions=true);
	std::string ToString() const;

	bool DisconnectBlock(CTxDB& txdb, CBlockIndex* pindex);
	bool ConnectBlock(CTxDB& txdb, CBlockIndex* pindex, bool fJustCheck=false);
	bool ReadFromDisk(const CBlockIndex* pindex, bool fReadTransactions=true);
	bool SetBestChain(CTxDB& txdb, CBlockIndex* pindexNew);
	bool AddToBlockIndex(unsigned int nFile, unsigned int nBlockPos, const uint256& hashProof);
	bool CheckBlock(bool fCheckPOW=true, bool fCheckMerkleRoot=true, bool fCheckSig=true) const;
	bool AcceptBlock();
	bool SignBlock(CWallet& keystore, int64_t nFees);
	bool CheckBlockSignature() const;
	void RebuildAddressIndex(CTxDB& txdb);

private:
	bool SetBestChainInner(CTxDB& txdb, CBlockIndex *pindexNew);
};

#endif // CBLOCK_H
