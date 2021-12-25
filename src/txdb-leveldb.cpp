#include "compat.h"

#include <map>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/thread.hpp>
#include <leveldb/db.h>
#include <leveldb/cache.h>
#include <leveldb/filter_policy.h>
#include <leveldb/write_batch.h>

#include "txdb-leveldb.h"
#include "main_extern.h"
#include "ctransaction.h"
#include "cbignum.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "cblock.h"
#include "ctxin.h"
#include "ctxout.h"
#include "uint/uint160.h"
#include "ctxindex.h"
#include "cdiskblockindex.h"
#include "util.h"
#include "enums/serialize_type.h"
#include "cdatastream.h"

leveldb::DB *txdb; // global pointer for LevelDB object instance

class CBatchScanner : public leveldb::WriteBatch::Handler
{
public:
	std::string needle;
	bool *deleted;
	std::string *foundValue;
	bool foundEntry;

	CBatchScanner() : foundEntry(false)
	{
		
	}

	virtual void Put(const leveldb::Slice& key, const leveldb::Slice& value)
	{
		if (key.ToString() == needle)
		{
			foundEntry = true;
			*deleted = false;
			*foundValue = value.ToString();
		}
	}

	virtual void Delete(const leveldb::Slice& key)
	{
		if (key.ToString() == needle) {
			foundEntry = true;
			*deleted = true;
		}
	}
};

static leveldb::Options GetOptions()
{
	leveldb::Options options;
	int nCacheSizeMB = GetArg("-dbcache", 100);

	options.block_cache = leveldb::NewLRUCache(nCacheSizeMB * 1048576);
	options.filter_policy = leveldb::NewBloomFilterPolicy(10);

	return options;
}

void init_blockindex(leveldb::Options& options, bool fRemoveOld = false)
{
	// First time init.
	boost::filesystem::path directory = GetDataDir() / "txleveldb";

	if (fRemoveOld)
	{
		boost::filesystem::remove_all(directory); // remove directory
		unsigned int nFile = 1;

		while (true)
		{
			boost::filesystem::path strBlockFile = GetDataDir() / strprintf("blk%04u.dat", nFile);

			// Break if no such file
			if( !boost::filesystem::exists( strBlockFile ) )
			{
				break;
			}
			
			boost::filesystem::remove(strBlockFile);

			nFile++;
		}
	}

	boost::filesystem::create_directory(directory);
	leveldb::Status status = leveldb::DB::Open(options, directory.string(), &txdb);

	LogPrintf("Opening LevelDB in %s\n", directory.string());

	if (!status.ok())
	{
		throw std::runtime_error(strprintf("init_blockindex(): error opening database environment %s", status.ToString()));
	}
}

static CBlockIndex* InsertBlockIndex(uint256 hash)
{
	if (hash == 0)
	{
		return NULL;
	}

	// Return existing
	std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hash);
	if (mi != mapBlockIndex.end())
	{
		return (*mi).second;
	}
	
	// Create new
	CBlockIndex* pindexNew = new CBlockIndex();
	if (!pindexNew)
	{
		throw std::runtime_error("LoadBlockIndex() : new CBlockIndex failed");
	}
	
	mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
	pindexNew->phashBlock = &((*mi).first);

	return pindexNew;
}

// CDB subclasses are created and destroyed VERY OFTEN. That's why
// we shouldn't treat this as a free operations.
CTxDB::CTxDB(const char* pszMode)
{
	assert(pszMode);

	activeBatch = NULL;
	fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));

	if (txdb)
	{
		pdb = txdb;
		
		return;
	}

	bool fCreate = strchr(pszMode, 'c');

	options = GetOptions();
	options.create_if_missing = true;
	options.filter_policy = leveldb::NewBloomFilterPolicy(10);

	init_blockindex(options); // Init directory
	pdb = txdb;

	if (Exists(std::string("version")))
	{
		ReadVersion(nVersion);
		
		LogPrintf("Transaction index version is %d\n", nVersion);

		if (nVersion < DATABASE_VERSION)
		{
			LogPrintf("Required index version is %d, removing old database\n", DATABASE_VERSION);

			// Leveldb instance destruction
			delete txdb;
			txdb = pdb = NULL;
			delete activeBatch;
			activeBatch = NULL;

			init_blockindex(options, true); // Remove directory and create new database
			pdb = txdb;

			bool fTmp = fReadOnly;
			fReadOnly = false;
			WriteVersion(DATABASE_VERSION); // Save transaction index version
			fReadOnly = fTmp;
		}
	}
	else if (fCreate)
	{
		bool fTmp = fReadOnly;
		fReadOnly = false;
		WriteVersion(DATABASE_VERSION);
		fReadOnly = fTmp;
	}

	LogPrintf("Opened LevelDB successfully\n");
}

CTxDB::~CTxDB()
{
	// Note that this is not the same as Close() because it deletes only
	// data scoped to this TxDB object.
	delete activeBatch;
}

void CTxDB::Close()
{
    delete txdb;
    txdb = pdb = NULL;
    delete options.filter_policy;
    options.filter_policy = NULL;
    delete options.block_cache;
    options.block_cache = NULL;
    delete activeBatch;
    activeBatch = NULL;
}

// When performing a read, if we have an active batch we need to check it first
// before reading from the database, as the rest of the code assumes that once
// a database transaction begins reads are consistent with it. It would be good
// to change that assumption in future and avoid the performance hit, though in
// practice it does not appear to be large.
bool CTxDB::ScanBatch(const CDataStream &key, std::string *value, bool *deleted) const
{
    assert(activeBatch);
    
	*deleted = false;
    CBatchScanner scanner;
    scanner.needle = key.str();
    scanner.deleted = deleted;
    scanner.foundValue = value;
    
	leveldb::Status status = activeBatch->Iterate(&scanner);
    if (!status.ok())
	{
        throw std::runtime_error(status.ToString());
    }
	
    return scanner.foundEntry;
}

template<typename K, typename T>
bool CTxDB::Read(const K& key, T& value)
{
	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	ssKey.reserve(1000);
	ssKey << key;
	std::string strValue;

	bool readFromDb = true;
	if (activeBatch) {
		// First we must search for it in the currently pending set of
		// changes to the db. If not found in the batch, go on to read disk.
		bool deleted = false;
		readFromDb = ScanBatch(ssKey, &strValue, &deleted) == false;
		if (deleted) {
			return false;
		}
	}
	if (readFromDb) {
		leveldb::Status status = pdb->Get(leveldb::ReadOptions(),
										  ssKey.str(), &strValue);
		if (!status.ok()) {
			if (status.IsNotFound())
				return false;
			// Some unexpected error.
			LogPrintf("LevelDB read failure: %s\n", status.ToString());
			return false;
		}
	}
	// Unserialize value
	try {
		CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(),
							SER_DISK, CLIENT_VERSION);
		ssValue >> value;
	}
	catch (std::exception &e) {
		return false;
	}
	return true;
}

template<typename K, typename T>
bool CTxDB::Write(const K& key, const T& value)
{
	if (fReadOnly)
		assert(!"Write called on database in read-only mode");

	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	ssKey.reserve(1000);
	ssKey << key;
	CDataStream ssValue(SER_DISK, CLIENT_VERSION);
	ssValue.reserve(10000);
	ssValue << value;

	if (activeBatch) {
		activeBatch->Put(ssKey.str(), ssValue.str());
		return true;
	}
	leveldb::Status status = pdb->Put(leveldb::WriteOptions(), ssKey.str(), ssValue.str());
	if (!status.ok()) {
		LogPrintf("LevelDB write failure: %s\n", status.ToString());
		return false;
	}
	return true;
}

template<typename K>
bool CTxDB::Erase(const K& key)
{
	if (!pdb)
		return false;
	if (fReadOnly)
		assert(!"Erase called on database in read-only mode");

	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	ssKey.reserve(1000);
	ssKey << key;
	if (activeBatch) {
		activeBatch->Delete(ssKey.str());
		return true;
	}
	leveldb::Status status = pdb->Delete(leveldb::WriteOptions(), ssKey.str());
	return (status.ok() || status.IsNotFound());
}

template<typename K>
bool CTxDB::Exists(const K& key)
{
	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	ssKey.reserve(1000);
	ssKey << key;
	std::string unused;

	if (activeBatch) {
		bool deleted;
		if (ScanBatch(ssKey, &unused, &deleted) && !deleted) {
			return true;
		}
	}


	leveldb::Status status = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &unused);
	return status.IsNotFound() == false;
}

bool CTxDB::TxnBegin()
{
	assert(!activeBatch);

	activeBatch = new leveldb::WriteBatch();

	return true;
}

bool CTxDB::TxnCommit()
{
	assert(activeBatch);

	leveldb::Status status = pdb->Write(leveldb::WriteOptions(), activeBatch);
	
	delete activeBatch;
	
	activeBatch = NULL;

	if (!status.ok())
	{
		LogPrintf("LevelDB batch commit failure: %s\n", status.ToString());
		
		return false;
	}

	return true;
}

bool CTxDB::TxnAbort()
{
	delete activeBatch;

	activeBatch = NULL;

	return true;
}

bool CTxDB::ReadVersion(int& nVersion)
{
	nVersion = 0;
	
	return Read(std::string("version"), nVersion);
}

bool CTxDB::WriteVersion(int nVersion)
{
	return Write(std::string("version"), nVersion);
}

bool CTxDB::WriteAddrIndex(uint160 addrHash, uint256 txHash)
{
	std::vector<uint256> txHashes;
	if(!ReadAddrIndex(addrHash, txHashes))
	{
		txHashes.push_back(txHash);
		
		return Write(std::make_pair(std::string("adr"), addrHash), txHashes);
	}
	else
	{
		if(std::find(txHashes.begin(), txHashes.end(), txHash) == txHashes.end()) 
		{
			txHashes.push_back(txHash);
			return Write(std::make_pair(std::string("adr"), addrHash), txHashes);
		}
		else
		{
			return true; // already have this tx hash
		}
	}
}

bool CTxDB::ReadAddrIndex(uint160 addrHash, std::vector<uint256>& txHashes)
{
	return Read(std::make_pair(std::string("adr"), addrHash), txHashes);
}

bool CTxDB::ReadTxIndex(uint256 hash, CTxIndex& txindex)
{
	txindex.SetNull();
	
	return Read(std::make_pair(std::string("tx"), hash), txindex);
}

bool CTxDB::UpdateTxIndex(uint256 hash, const CTxIndex& txindex)
{
	return Write(std::make_pair(std::string("tx"), hash), txindex);
}

bool CTxDB::AddTxIndex(const CTransaction& tx, const CDiskTxPos& pos, int nHeight)
{
	// Add to tx index
	uint256 hash = tx.GetHash();

	CTxIndex txindex(pos, tx.vout.size());

	return Write(make_pair(std::string("tx"), hash), txindex);
}

bool CTxDB::EraseTxIndex(const CTransaction& tx)
{
	uint256 hash = tx.GetHash();

	return Erase(std::make_pair(std::string("tx"), hash));
}

bool CTxDB::ContainsTx(uint256 hash)
{
	return Exists(std::make_pair(std::string("tx"), hash));
}

bool CTxDB::ReadDiskTx(uint256 hash, CTransaction& tx, CTxIndex& txindex)
{
	tx.SetNull();
	
	if (!ReadTxIndex(hash, txindex))
	{
		return false;
	}
	
	return (tx.ReadFromDisk(txindex.pos));
}

bool CTxDB::ReadDiskTx(uint256 hash, CTransaction& tx)
{
	CTxIndex txindex;
	
	return ReadDiskTx(hash, tx, txindex);
}

bool CTxDB::ReadDiskTx(COutPoint outpoint, CTransaction& tx, CTxIndex& txindex)
{
	return ReadDiskTx(outpoint.hash, tx, txindex);
}

bool CTxDB::ReadDiskTx(COutPoint outpoint, CTransaction& tx)
{
	CTxIndex txindex;
	
	return ReadDiskTx(outpoint.hash, tx, txindex);
}

bool CTxDB::WriteBlockIndex(const CDiskBlockIndex& blockindex)
{
	return Write(std::make_pair(std::string("blockindex"), blockindex.GetBlockHash()), blockindex);
}

bool CTxDB::ReadHashBestChain(uint256& hashBestChain)
{
	return Read(std::string("hashBestChain"), hashBestChain);
}

bool CTxDB::WriteHashBestChain(uint256 hashBestChain)
{
	return Write(std::string("hashBestChain"), hashBestChain);
}

bool CTxDB::ReadBestInvalidTrust(CBigNum& bnBestInvalidTrust)
{
	return Read(std::string("bnBestInvalidTrust"), bnBestInvalidTrust);
}

bool CTxDB::WriteBestInvalidTrust(CBigNum bnBestInvalidTrust)
{
	return Write(std::string("bnBestInvalidTrust"), bnBestInvalidTrust);
}

bool CTxDB::LoadBlockIndex()
{
	if (mapBlockIndex.size() > 0)
	{
		// Already loaded once in this session. It can happen during migration
		// from BDB.
		return true;
	}
	
	// The block index is an in-memory structure that maps hashes to on-disk
	// locations where the contents of the block can be found. Here, we scan it
	// out of the DB and into mapBlockIndex.
	leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
	// Seek to start key.
	CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
	
	ssStartKey << std::make_pair(std::string("blockindex"), uint256(0));
	iterator->Seek(ssStartKey.str());
	
	// Now read each entry.
	while (iterator->Valid())
	{
		boost::this_thread::interruption_point();
		
		// Unpack keys and values.
		CDataStream ssKey(SER_DISK, CLIENT_VERSION);
		ssKey.write(iterator->key().data(), iterator->key().size());
		
		CDataStream ssValue(SER_DISK, CLIENT_VERSION);
		ssValue.write(iterator->value().data(), iterator->value().size());
		
		std::string strType;
		ssKey >> strType;
		
		// Did we reach the end of the data to read?
		if (strType != "blockindex")
		{
			break;
		}
		
		CDiskBlockIndex diskindex;
		ssValue >> diskindex;

		uint256 blockHash = diskindex.GetBlockHash();

		// Construct block index object
		CBlockIndex* pindexNew    = InsertBlockIndex(blockHash);
		pindexNew->pprev          = InsertBlockIndex(diskindex.hashPrev);
		pindexNew->pnext          = InsertBlockIndex(diskindex.hashNext);
		pindexNew->nFile          = diskindex.nFile;
		pindexNew->nBlockPos      = diskindex.nBlockPos;
		pindexNew->nHeight        = diskindex.nHeight;
		pindexNew->nMint          = diskindex.nMint;
		pindexNew->nMoneySupply   = diskindex.nMoneySupply;
		pindexNew->nFlags         = diskindex.nFlags;
		pindexNew->nStakeModifier = diskindex.nStakeModifier;
		pindexNew->bnStakeModifierV2 = diskindex.bnStakeModifierV2;
		pindexNew->prevoutStake   = diskindex.prevoutStake;
		pindexNew->nStakeTime     = diskindex.nStakeTime;
		pindexNew->hashProof      = diskindex.hashProof;
		pindexNew->nVersion       = diskindex.nVersion;
		pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
		pindexNew->nTime          = diskindex.nTime;
		pindexNew->nBits          = diskindex.nBits;
		pindexNew->nNonce         = diskindex.nNonce;

		// Watch for genesis block
		if (pindexGenesisBlock == NULL && blockHash == Params().HashGenesisBlock())
		{
			pindexGenesisBlock = pindexNew;
		}
		
		if (!pindexNew->CheckIndex())
		{
			delete iterator;
			
			return error("LoadBlockIndex() : CheckIndex failed at %d", pindexNew->nHeight);
		}

		// NovaCoin: build setStakeSeen
		if (pindexNew->IsProofOfStake())
		{
			setStakeSeen.insert(std::make_pair(pindexNew->prevoutStake, pindexNew->nStakeTime));
		}
		
		iterator->Next();
	}
	
	delete iterator;

	boost::this_thread::interruption_point();

	// Calculate nChainTrust
	std::vector<std::pair<int, CBlockIndex*> > vSortedByHeight;
	vSortedByHeight.reserve(mapBlockIndex.size());
	
	for(const std::pair<uint256, CBlockIndex*>& item : mapBlockIndex)
	{
		CBlockIndex* pindex = item.second;
		vSortedByHeight.push_back(std::make_pair(pindex->nHeight, pindex));
	}
	
	sort(vSortedByHeight.begin(), vSortedByHeight.end());
	
	for(const std::pair<int, CBlockIndex*>& item : vSortedByHeight)
	{
		CBlockIndex* pindex = item.second;
		pindex->nChainTrust = (pindex->pprev ? pindex->pprev->nChainTrust : 0) + pindex->GetBlockTrust();
	}

	// Load hashBestChain pointer to end of best chain
	if (!ReadHashBestChain(hashBestChain))
	{
		if (pindexGenesisBlock == NULL)
		{
			return true;
		}
		
		return error("CTxDB::LoadBlockIndex() : hashBestChain not loaded");
	}
	
	if (!mapBlockIndex.count(hashBestChain))
	{
		return error("CTxDB::LoadBlockIndex() : hashBestChain not found in the block index");
	}
	
	pindexBest = mapBlockIndex[hashBestChain];
	nBestHeight = pindexBest->nHeight;
	nBestChainTrust = pindexBest->nChainTrust;

	LogPrintf("LoadBlockIndex(): hashBestChain=%s  height=%d  trust=%s  date=%s\n",
		hashBestChain.ToString(), nBestHeight, CBigNum(nBestChainTrust).ToString(),
		DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime())
	);

	// Load bnBestInvalidTrust, OK if it doesn't exist
	CBigNum bnBestInvalidTrust;
	ReadBestInvalidTrust(bnBestInvalidTrust);
	nBestInvalidTrust = bnBestInvalidTrust.getuint256();

	// Verify blocks in the best chain
	int nCheckLevel = GetArg("-checklevel", 1);
	int nCheckDepth = GetArg( "-checkblocks", 500);
	
	if (nCheckDepth == 0)
	{
		nCheckDepth = 1000000000; // suffices until the year 19000
	}
	
	if (nCheckDepth > nBestHeight)
	{
		nCheckDepth = nBestHeight;
	}
	
	LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
	
	CBlockIndex* pindexFork = NULL;
	std::map<std::pair<unsigned int, unsigned int>, CBlockIndex*> mapBlockPos;
	
	for (CBlockIndex* pindex = pindexBest; pindex && pindex->pprev; pindex = pindex->pprev)
	{
		boost::this_thread::interruption_point();
		
		if (pindex->nHeight < nBestHeight-nCheckDepth)
		{
			break;
		}
		
		CBlock block;
		if (!block.ReadFromDisk(pindex))
		{
			return error("LoadBlockIndex() : block.ReadFromDisk failed");
		}
		
		// check level 1: verify block validity
		// check level 7: verify block signature too
		if (nCheckLevel>0 && !block.CheckBlock(true, true, (nCheckLevel>6)))
		{
			LogPrintf("LoadBlockIndex() : *** found bad block at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
			pindexFork = pindex->pprev;
		}
		
		// check level 2: verify transaction index validity
		if (nCheckLevel>1)
		{
			std::pair<unsigned int, unsigned int> pos = std::make_pair(pindex->nFile, pindex->nBlockPos);
			mapBlockPos[pos] = pindex;
			
			for(const CTransaction &tx : block.vtx)
			{
				uint256 hashTx = tx.GetHash();
				CTxIndex txindex;
				
				if (ReadTxIndex(hashTx, txindex))
				{
					// check level 3: checker transaction hashes
					if (nCheckLevel>2 || pindex->nFile != txindex.pos.nFile || pindex->nBlockPos != txindex.pos.nBlockPos)
					{
						// either an error or a duplicate transaction
						CTransaction txFound;
						
						if (!txFound.ReadFromDisk(txindex.pos))
						{
							LogPrintf("LoadBlockIndex() : *** cannot read mislocated transaction %s\n", hashTx.ToString());
							
							pindexFork = pindex->pprev;
						}
						else
						{
							if (txFound.GetHash() != hashTx) // not a duplicate tx
							{
								LogPrintf("LoadBlockIndex(): *** invalid tx position for %s\n", hashTx.ToString());
								
								pindexFork = pindex->pprev;
							}
						}
					}
					
					// check level 4: check whether spent txouts were spent within the main chain
					unsigned int nOutput = 0;
					if (nCheckLevel>3)
					{
						for(const CDiskTxPos &txpos : txindex.vSpent)
						{
							if (!txpos.IsNull())
							{
								std::pair<unsigned int, unsigned int> posFind = std::make_pair(txpos.nFile, txpos.nBlockPos);
								
								if (!mapBlockPos.count(posFind))
								{
									LogPrintf("LoadBlockIndex(): *** found bad spend at %d, hashBlock=%s, hashTx=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString(), hashTx.ToString());
									
									pindexFork = pindex->pprev;
								}
								
								// check level 6: check whether spent txouts were spent by a valid transaction that consume them
								if (nCheckLevel>5)
								{
									CTransaction txSpend;
									
									if (!txSpend.ReadFromDisk(txpos))
									{
										LogPrintf("LoadBlockIndex(): *** cannot read spending transaction of %s:%i from disk\n", hashTx.ToString(), nOutput);
										
										pindexFork = pindex->pprev;
									}
									else if (!txSpend.CheckTransaction())
									{
										LogPrintf("LoadBlockIndex(): *** spending transaction of %s:%i is invalid\n", hashTx.ToString(), nOutput);
										
										pindexFork = pindex->pprev;
									}
									else
									{
										bool fFound = false;
										
										for(const CTxIn &txin : txSpend.vin)
										{
											if (txin.prevout.hash == hashTx && txin.prevout.n == nOutput)
											{
												fFound = true;
											}
										}
										
										if (!fFound)
										{
											LogPrintf("LoadBlockIndex(): *** spending transaction of %s:%i does not spend it\n", hashTx.ToString(), nOutput);
											
											pindexFork = pindex->pprev;
										}
									}
								}
							}
							
							nOutput++;
						}
					}
				}
				
				// check level 5: check whether all prevouts are marked spent
				if (nCheckLevel>4)
				{
					for(const CTxIn &txin : tx.vin)
					{
						CTxIndex txindex;

						if (ReadTxIndex(txin.prevout.hash, txindex))
						{
							if (txindex.vSpent.size()-1 < txin.prevout.n || txindex.vSpent[txin.prevout.n].IsNull())
							{
								LogPrintf("LoadBlockIndex(): *** found unspent prevout %s:%i in %s\n", txin.prevout.hash.ToString(), txin.prevout.n, hashTx.ToString());
								
								pindexFork = pindex->pprev;
							}
						}
					}
				}
			}
		}
	}
	
	if (pindexFork)
	{
		boost::this_thread::interruption_point();
		
		// Reorg back to the fork
		LogPrintf("LoadBlockIndex() : *** moving best chain pointer back to block %d\n", pindexFork->nHeight);
		
		CBlock block;
		if (!block.ReadFromDisk(pindexFork))
		{
			return error("LoadBlockIndex() : block.ReadFromDisk failed");
		}
		
		CTxDB txdb;
		block.SetBestChain(txdb, pindexFork);
	}

	return true;
}

