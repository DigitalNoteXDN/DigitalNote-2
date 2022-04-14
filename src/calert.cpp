//
// Alert system
//
#include "compat.h"

#include <stdint.h>
#include <algorithm>
#include <map>
#include <boost/thread.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "net/cnode.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "util.h"
#include "thread.h"
#include "enums/serialize_type.h"
#include "crypto/bmw/bmw512.h"
#include "cpubkey.h"
#include "ui_interface.h"
#include "serialize.h"

#include "calert.h"

std::map<uint256, CAlert> mapAlerts;
CCriticalSection cs_mapAlerts;

CAlert::CAlert()
{
	SetNull();
}

unsigned int CAlert::GetSerializeSize(int nType, int nVersion) const
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
	
	READWRITE(vchMsg);
	READWRITE(vchSig);
	
	return nSerSize;
}

template<typename Stream>
void CAlert::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(vchMsg);
	READWRITE(vchSig);
}

template<typename Stream>
void CAlert::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(vchMsg);
	READWRITE(vchSig);
}

template void CAlert::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CAlert::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

void CAlert::SetNull()
{
	CUnsignedAlert::SetNull();
	vchMsg.clear();
	vchSig.clear();
}

bool CAlert::IsNull() const
{
	return (nExpiration == 0);
}

uint256 CAlert::GetHash() const
{
	return Hash_bmw512(this->vchMsg.begin(), this->vchMsg.end());
}

bool CAlert::IsInEffect() const
{
	return (GetAdjustedTime() < nExpiration);
}

bool CAlert::Cancels(const CAlert& alert) const
{
	if (!IsInEffect())
	{
		return false; // this was a no-op before 31403
	}

	return (alert.nID <= nCancel || setCancel.count(alert.nID));
}

bool CAlert::AppliesTo(int nVersion, const std::string &strSubVerIn) const
{
	// TODO: rework for client-version-embedded-in-strSubVer ?
	return (IsInEffect() &&
			nMinVer <= nVersion && nVersion <= nMaxVer &&
			(setSubVer.empty() || setSubVer.count(strSubVerIn)));
}

bool CAlert::AppliesToMe() const
{
	return AppliesTo(PROTOCOL_VERSION, FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, std::vector<std::string>()));
}

bool CAlert::RelayTo(CNode* pnode) const
{
	if (!IsInEffect())
	{
		return false;
	}

	// don't relay to nodes which haven't sent their version message
	if (pnode->nVersion == 0)
	{
		return false;
	}

	// returns true if wasn't already contained in the set
	if (pnode->setKnown.insert(GetHash()).second)
	{
		if (AppliesTo(pnode->nVersion, pnode->strSubVer) ||
			AppliesToMe() ||
			GetAdjustedTime() < nRelayUntil)
		{
			pnode->PushMessage("alert", *this);
			
			return true;
		}
	}

	return false;
}

bool CAlert::CheckSignature() const
{
	CPubKey key(Params().AlertKey());

	if (!key.Verify(Hash_bmw512(vchMsg.begin(), vchMsg.end()), vchSig))
	{
		return error("CAlert::CheckSignature() : verify signature failed");
	}

	// Now unserialize the data
	CDataStream sMsg(vchMsg, SER_NETWORK, PROTOCOL_VERSION);

	sMsg >> *(CUnsignedAlert*)this;

	return true;
}

CAlert CAlert::getAlertByHash(const uint256 &hash)
{
	CAlert retval;

	LOCK(cs_mapAlerts);

	std::map<uint256, CAlert>::iterator mi = mapAlerts.find(hash);

	if(mi != mapAlerts.end())
	{
		retval = mi->second;
	}

	return retval;
}

bool CAlert::ProcessAlert(bool fThread)
{
	if (!CheckSignature())
	{
		return false;
	}

	if (!IsInEffect())
	{
		return false;
	}

	// alert.nID=max is reserved for if the alert key is
	// compromised. It must have a pre-defined message,
	// must never expire, must apply to all versions,
	// and must cancel all previous
	// alerts or it will be ignored (so an attacker can't
	// send an "everything is OK, don't panic" version that
	// cannot be overridden):
	int maxInt = std::numeric_limits<int>::max();

	if (nID == maxInt)
	{
		if (!(
				nExpiration != maxInt &&
				nCancel != (maxInt-1) &&
				nMinVer != 0 &&
				nMaxVer != maxInt &&
				setSubVer.empty() == false &&
				nPriority != maxInt &&
				strStatusBar != "URGENT: Alert key compromised, upgrade required"
				)
			)
		{
			return false;
		}
	}


	LOCK(cs_mapAlerts);

	// Cancel previous alerts
	for (std::map<uint256, CAlert>::iterator mi = mapAlerts.begin(); mi != mapAlerts.end();)
	{
		const CAlert& alert = (*mi).second;
		
		if (Cancels(alert))
		{
			LogPrint("alert", "cancelling alert %d\n", alert.nID);
			
			uiInterface.NotifyAlertChanged((*mi).first, CT_DELETED);
			mapAlerts.erase(mi++);
		}
		else if (!alert.IsInEffect())
		{
			LogPrint("alert", "expiring alert %d\n", alert.nID);
			
			uiInterface.NotifyAlertChanged((*mi).first, CT_DELETED);
			mapAlerts.erase(mi++);
		}
		else
		{
			mi++;
		}
	}

	// Check if this alert has been cancelled
	for(std::pair<const uint256, CAlert>& item : mapAlerts)
	{
		const CAlert& alert = item.second;
		
		if (alert.Cancels(*this))
		{
			LogPrint("alert", "alert already cancelled by %d\n", alert.nID);
			
			return false;
		}
	}

	// Add to mapAlerts
	mapAlerts.insert(std::make_pair(GetHash(), *this));

	// Notify UI and -alertnotify if it applies to me
	if(AppliesToMe())
	{
		uiInterface.NotifyAlertChanged(GetHash(), CT_NEW);
		std::string strCmd = GetArg("-alertnotify", "");
		
		if (!strCmd.empty())
		{
			// Alert text should be plain ascii coming from a trusted source, but to
			// be safe we first strip anything not in safeChars, then add single quotes around
			// the whole string before passing it to the shell:
			std::string singleQuote("'");
			std::string safeStatus = SanitizeString(strStatusBar);
			safeStatus = singleQuote+safeStatus+singleQuote;
			boost::replace_all(strCmd, "%s", safeStatus);

			if (fThread)
			{
				boost::thread t(runCommand, strCmd); // thread runs free
			}
			else
			{
				runCommand(strCmd);
			}
		}
	}

	LogPrint("alert", "accepted alert %d, AppliesToMe()=%d\n", nID, AppliesToMe());

	return true;
}

