#include "compat.h"

#include <cstring>

#include "base58.h"
#include "cpubkey.h"
#include "serialize.h"
#include "chashwriter.h"
#include "util.h"
#include "cautofile.h"
#include "cdatastream.h"

#include "cstealthaddress.h"

CStealthAddress::CStealthAddress()
{
	options = 0;
}

unsigned int CStealthAddress::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;

	s.nType = nType;
	s.nVersion = nVersion;
	
	READWRITE(this->options);
	READWRITE(this->scan_pubkey);
	READWRITE(this->spend_pubkey);
	READWRITE(this->label);
	READWRITE(this->scan_secret);
	READWRITE(this->spend_secret);
	
	return nSerSize;
}

template<typename Stream>
void CStealthAddress::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(this->options);
	READWRITE(this->scan_pubkey);
	READWRITE(this->spend_pubkey);
	READWRITE(this->label);
	READWRITE(this->scan_secret);
	READWRITE(this->spend_secret);
}

template<typename Stream>
void CStealthAddress::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(this->options);
	READWRITE(this->scan_pubkey);
	READWRITE(this->spend_pubkey);
	READWRITE(this->label);
	READWRITE(this->scan_secret);
	READWRITE(this->spend_secret);
}

template void CStealthAddress::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CStealthAddress::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
template void CStealthAddress::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
template void CStealthAddress::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);
template void CStealthAddress::Serialize<CHashWriter>(CHashWriter& s, int nType, int nVersion) const;

bool CStealthAddress::SetEncoded(const std::string& encodedAddress)
{
	data_chunk raw;

	if (!DecodeBase58(encodedAddress, raw))
	{
		if (fDebug)
		{
			LogPrintf("CStealthAddress::SetEncoded DecodeBase58 falied.\n");
		}
		
		return false;
	}

	if (!VerifyChecksum(raw))
	{
		if (fDebug)
		{
			LogPrintf("CStealthAddress::SetEncoded verify_checksum falied.\n");
		}
		
		return false;
	}

	if (raw.size() < 1 + 1 + 33 + 1 + 33 + 1 + 1 + 4)
	{
		if (fDebug)
		{
			LogPrintf("CStealthAddress::SetEncoded() too few bytes provided.\n");
		}

		return false;
	}
	
	uint8_t* p = &raw[0];
	uint8_t version = *p++;

	if (version != stealth_version_byte)
	{
		LogPrintf("CStealthAddress::SetEncoded version mismatch 0x%x != 0x%x.\n", version, stealth_version_byte);
		
		return false;
	}

	options = *p++;

	scan_pubkey.resize(33);
	memcpy(&scan_pubkey[0], p, 33);
	p += 33;
	//uint8_t spend_pubkeys = *p++;
	p++;

	spend_pubkey.resize(33);
	memcpy(&spend_pubkey[0], p, 33);

	return true;
};

std::string CStealthAddress::Encoded() const
{
	// https://wiki.unsystem.net/index.php/DarkWallet/Stealth#Address_format
	// [version] [options] [scan_key] [N] ... [Nsigs] [prefix_length] ...

	data_chunk raw;
	raw.push_back(stealth_version_byte);

	raw.push_back(options);

	raw.insert(raw.end(), scan_pubkey.begin(), scan_pubkey.end());
	raw.push_back(1); // number of spend pubkeys
	raw.insert(raw.end(), spend_pubkey.begin(), spend_pubkey.end());
	raw.push_back(0); // number of signatures
	raw.push_back(0); // ?

	AppendChecksum(raw);

	return EncodeBase58(raw);
};

int CStealthAddress::SetScanPubKey(CPubKey pk)
{
	scan_pubkey.resize(pk.size());

	memcpy(&scan_pubkey[0], pk.begin(), pk.size());

	return 0;
}

bool CStealthAddress::operator<(const CStealthAddress& y) const
{
	return memcmp(&scan_pubkey[0], &y.scan_pubkey[0], ec_compressed_size) < 0;
}

