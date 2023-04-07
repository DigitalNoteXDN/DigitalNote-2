#ifndef SMESSAGE_H
#define SMESSAGE_H

#include <cstdint>
#include <string>
#include <vector>

#include "enums/changetype.h"

class CNode;
class CDataStream;
class CBlock;
class CBlockIndex;
class CKeyID;
class CPubKey;

#define SMSG_MASK_UNREAD		(1 << 0)

namespace DigitalNote
{
	namespace SMSG
	{
		class Token;
		class Message;
		class SecureMessage;
		
		int BuildBucketSet();
		int AddWalletAddresses();
		int ReadIni();
		int WriteIni();
		bool Start(bool fDontStart, bool fScanChain);
		bool Shutdown();
		bool Enable();
		bool Disable();
		bool ReceiveData(CNode* pfrom, const std::string &strCommand, CDataStream& vRecv);
		bool SendData(CNode* pto, bool fSendTrickle);
		bool ScanBlock(CBlock& block);
		bool ScanChainForPublicKeys(CBlockIndex* pindexStart);
		bool ScanBlockChain();
		bool ScanBuckets();
		int WalletUnlocked();
		int WalletKeyChanged(std::string sAddress, std::string sLabel, ChangeType mode);
		int ScanMessage(uint8_t *pHeader, uint8_t *pPayload, uint32_t nPayload, bool reportToGui);
		int GetStoredKey(CKeyID& ckid, CPubKey& cpkOut);
		int GetLocalKey(CKeyID& ckid, CPubKey& cpkOut);
		int GetLocalPublicKey(std::string& strAddress, std::string& strPublicKey);
		int AddAddress(std::string& address, std::string& publicKey);
		int Retrieve(DigitalNote::SMSG::Token &token, std::vector<uint8_t>& vchData);
		int Receive(CNode* pfrom, std::vector<uint8_t>& vchData);
		int StoreUnscanned(uint8_t *pHeader, uint8_t *pPayload, uint32_t nPayload);
		int Store(uint8_t *pHeader, uint8_t *pPayload, uint32_t nPayload, bool fUpdateBucket);
		int Store(DigitalNote::SMSG::SecureMessage& smsg, bool fUpdateBucket);
		int Send(std::string &addressFrom, std::string &addressTo, std::string &message, std::string &sError);
		int Validate(uint8_t *pHeader, uint8_t *pPayload, uint32_t nPayload);
		int SetHash(uint8_t *pHeader, uint8_t *pPayload, uint32_t nPayload);
		int Encrypt(DigitalNote::SMSG::SecureMessage &smsg, const std::string &addressFrom, const std::string &addressTo,
				const std::string &message);
		int Decrypt(bool fTestOnly, std::string &address, uint8_t *pHeader, uint8_t *pPayload, uint32_t nPayload,
				DigitalNote::SMSG::Message &msg);
		int Decrypt(bool fTestOnly, std::string &address, DigitalNote::SMSG::SecureMessage &smsg, DigitalNote::SMSG::Message &msg);
	}
}

#endif // SEC_MESSAGE_H
