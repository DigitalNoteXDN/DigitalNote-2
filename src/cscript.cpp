#include <boost/variant/static_visitor.hpp>

#include "cbignum.h"
#include "cscriptvisitor.h"
#include "script.h"
#include "uint/uint160.h"
#include "uint/uint256.h"
#include "cpubkey.h"
#include "hash.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"

#include "cscript.h"

/**
	Protected
*/
CScript& CScript::push_int64(int64_t n)
{
	if (n == -1 || (n >= 1 && n <= 16))
	{
		push_back(n + (OP_1 - 1));
	}
	else
	{
		CBigNum bn(n);
		
		*this << bn.getvch();
	}
	
	return *this;
}

CScript& CScript::push_uint64(uint64_t n)
{
	if (n >= 1 && n <= 16)
	{
		push_back(n + (OP_1 - 1));
	}
	else
	{
		CBigNum bn(n);
		*this << bn.getvch();
	}
	
	return *this;
}

/**
	Public
*/
CScript::CScript()
{
	
}

CScript::CScript(const CScript& b) : std::vector<unsigned char>(b.begin(), b.end())
{
	
}

CScript::CScript(const_iterator pbegin, const_iterator pend) : std::vector<unsigned char>(pbegin, pend)
{
	
}

#ifndef _MSC_VER
CScript::CScript(const unsigned char* pbegin, const unsigned char* pend) : std::vector<unsigned char>(pbegin, pend)
{
	
}
#endif

CScript& CScript::operator+=(const CScript& b)
{
	insert(end(), b.begin(), b.end());
	
	return *this;
}

CScript operator+(const CScript& a, const CScript& b)
{
	CScript ret = a;
	
	ret += b;
	
	return ret;
}

//explicit CScript(char b) is not portable.  Use 'signed char' or 'unsigned char'.
CScript::CScript(signed char b)
{
	operator<<(b);
}

CScript::CScript(short b)
{
	operator<<(b);
}

CScript::CScript(int b)
{
	operator<<(b);
}

CScript::CScript(long b)				{ operator<<(b); }
CScript::CScript(long long b)			{ operator<<(b); }
CScript::CScript(unsigned char b)		{ operator<<(b); }
CScript::CScript(unsigned int b)		{ operator<<(b); }
CScript::CScript(unsigned short b)		{ operator<<(b); }
CScript::CScript(unsigned long b)		{ operator<<(b); }
CScript::CScript(unsigned long long b)	{ operator<<(b); }

CScript::CScript(opcodetype b)							{ operator<<(b); }
CScript::CScript(const uint256& b)						{ operator<<(b); }
CScript::CScript(const CBigNum& b)						{ operator<<(b); }
CScript::CScript(const std::vector<unsigned char>& b)	{ operator<<(b); }

//CScript& operator<<(char b) is not portable.  Use 'signed char' or 'unsigned char'.
CScript& CScript::operator<<(signed char b)			{ return push_int64(b); }
CScript& CScript::operator<<(short b)				{ return push_int64(b); }
CScript& CScript::operator<<(int b)					{ return push_int64(b); }
CScript& CScript::operator<<(long b)				{ return push_int64(b); }
CScript& CScript::operator<<(long long b)			{ return push_int64(b); }
CScript& CScript::operator<<(unsigned char b)		{ return push_uint64(b); }
CScript& CScript::operator<<(unsigned int b)		{ return push_uint64(b); }
CScript& CScript::operator<<(unsigned short b)		{ return push_uint64(b); }
CScript& CScript::operator<<(unsigned long b)		{ return push_uint64(b); }
CScript& CScript::operator<<(unsigned long long b)	{ return push_uint64(b); }

CScript& CScript::operator<<(opcodetype opcode)
{
	if (opcode < 0 || opcode > 0xff)
	{
		throw std::runtime_error("CScript::operator<<() : invalid opcode");
	}

	insert(end(), (unsigned char)opcode);
	
	return *this;
}

CScript& CScript::operator<<(const uint160& b)
{
	insert(end(), sizeof(b));
	insert(end(), (unsigned char*)&b, (unsigned char*)&b + sizeof(b));
	
	return *this;
}

CScript& CScript::operator<<(const uint256& b)
{
	insert(end(), sizeof(b));
	insert(end(), (unsigned char*)&b, (unsigned char*)&b + sizeof(b));
	
	return *this;
}

CScript& CScript::operator<<(const CPubKey& key)
{
	assert(key.size() < OP_PUSHDATA1);
	
	insert(end(), (unsigned char)key.size());
	insert(end(), key.begin(), key.end());
	
	return *this;
}

CScript& CScript::operator<<(const CBigNum& b)
{
	*this << b.getvch();
	
	return *this;
}

CScript& CScript::operator<<(const std::vector<unsigned char>& b)
{
	if (b.size() < OP_PUSHDATA1)
	{
		insert(end(), (unsigned char)b.size());
	}
	else if (b.size() <= 0xff)
	{
		insert(end(), OP_PUSHDATA1);
		insert(end(), (unsigned char)b.size());
	}
	else if (b.size() <= 0xffff)
	{
		insert(end(), OP_PUSHDATA2);
		unsigned short nSize = b.size();
		insert(end(), (unsigned char*)&nSize, (unsigned char*)&nSize + sizeof(nSize));
	}
	else
	{
		insert(end(), OP_PUSHDATA4);
		unsigned int nSize = b.size();
		insert(end(), (unsigned char*)&nSize, (unsigned char*)&nSize + sizeof(nSize));
	}
	
	insert(end(), b.begin(), b.end());
	
	return *this;
}

CScript& CScript::operator<<(const CScript& b)
{
	// I'm not sure if this should push the script or concatenate scripts.
	// If there's ever a use for pushing a script onto a script, delete this member fn
	assert(!"Warning: Pushing a CScript onto a CScript with << is probably not intended, use + to concatenate!");
	
	return *this;
}

bool CScript::GetOp(iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>& vchRet)
{
	// Wrapper so it can be called with either iterator or const_iterator
	const_iterator pc2 = pc;
	bool fRet = GetOp2(pc2, opcodeRet, &vchRet);

	pc = begin() + (pc2 - begin());

	return fRet;
}

bool CScript::GetOp(iterator& pc, opcodetype& opcodeRet)
{
	const_iterator pc2 = pc;
	bool fRet = GetOp2(pc2, opcodeRet, NULL);

	pc = begin() + (pc2 - begin());

	return fRet;
}

bool CScript::GetOp(const_iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>& vchRet) const
{
	return GetOp2(pc, opcodeRet, &vchRet);
}

bool CScript::GetOp(const_iterator& pc, opcodetype& opcodeRet) const
{
	return GetOp2(pc, opcodeRet, NULL);
}

bool CScript::GetOp2(const_iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>* pvchRet) const
{
	opcodeRet = OP_INVALIDOPCODE;
	
	if (pvchRet)
	{
		pvchRet->clear();
	}
	
	if (pc >= end())
	{
		return false;
	}
	
	// Read instruction
	if (end() - pc < 1)
	{
		return false;
	}
	
	unsigned int opcode = *pc++;

	// Immediate operand
	if (opcode <= OP_PUSHDATA4)
	{
		unsigned int nSize;
		
		if (opcode < OP_PUSHDATA1)
		{
			nSize = opcode;
		}
		else if (opcode == OP_PUSHDATA1)
		{
			if (end() - pc < 1)
			{
				return false;
			}
			
			nSize = *pc++;
		}
		else if (opcode == OP_PUSHDATA2)
		{
			if (end() - pc < 2)
			{
				return false;
			}
			
			nSize = 0;
			memcpy(&nSize, &pc[0], 2);
			pc += 2;
		}
		else if (opcode == OP_PUSHDATA4)
		{
			if (end() - pc < 4)
			{
				return false;
			}
			
			memcpy(&nSize, &pc[0], 4);
			pc += 4;
		}
		
		if (end() - pc < 0 || (unsigned int)(end() - pc) < nSize)
		{
			return false;
		}
		
		if (pvchRet)
		{
			pvchRet->assign(pc, pc + nSize);
		}
		
		pc += nSize;
	}

	opcodeRet = (opcodetype)opcode;
	
	return true;
}

// Encode/decode small integers:
int CScript::DecodeOP_N(opcodetype opcode)
{
	if (opcode == OP_0)
	{
		return 0;
	}
	
	assert(opcode >= OP_1 && opcode <= OP_16);
	
	return (int)opcode - (int)(OP_1 - 1);
}

opcodetype CScript::EncodeOP_N(int n)
{
	assert(n >= 0 && n <= 16);
	
	if (n == 0)
	{
		return OP_0;
	}
	
	return (opcodetype)(OP_1+n-1);
}

int CScript::FindAndDelete(const CScript& b)
{
	int nFound = 0;
	
	if (b.empty())
	{
		return nFound;
	}
	
	iterator pc = begin();
	opcodetype opcode;
	
	do
	{
		while (end() - pc >= (long)b.size() && memcmp(&pc[0], &b[0], b.size()) == 0)
		{
			erase(pc, pc + b.size());
			++nFound;
		}
	}
	while (GetOp(pc, opcode));
	
	return nFound;
}

int CScript::Find(opcodetype op) const
{
	int nFound = 0;
	opcodetype opcode;

	for (const_iterator pc = begin(); pc != end() && GetOp(pc, opcode);)
	{
		if (opcode == op)
		{
			++nFound;
		}
	}

	return nFound;
}

// Pre-version-0.6, Bitcoin always counted CHECKMULTISIGs
// as 20 sigops. With pay-to-script-hash, that changed:
// CHECKMULTISIGs serialized in scriptSigs are
// counted more accurately, assuming they are of the form
//  ... OP_N CHECKMULTISIG ...
unsigned int CScript::GetSigOpCount(bool fAccurate) const
{
	unsigned int n = 0;
	const_iterator pc = begin();
	opcodetype lastOpcode = OP_INVALIDOPCODE;

	while (pc < end())
	{
		opcodetype opcode;
		
		if (!GetOp(pc, opcode))
		{
			break;
		}
		
		if (opcode == OP_CHECKSIG || opcode == OP_CHECKSIGVERIFY)
		{
			n++;
		}
		else if (opcode == OP_CHECKMULTISIG || opcode == OP_CHECKMULTISIGVERIFY)
		{
			if (fAccurate && lastOpcode >= OP_1 && lastOpcode <= OP_16)
			{
				n += DecodeOP_N(lastOpcode);
			}
			else
			{
				n += 20;
			}
		}
		
		lastOpcode = opcode;
	}

	return n;
}

// Accurately count sigOps, including sigOps in
// pay-to-script-hash transactions:
unsigned int CScript::GetSigOpCount(const CScript& scriptSig) const
{
	if (!IsPayToScriptHash())
	{
		return GetSigOpCount(true);
	}

	// This is a pay-to-script-hash scriptPubKey;
	// get the last item that the scriptSig
	// pushes onto the stack:
	const_iterator pc = scriptSig.begin();
	std::vector<unsigned char> data;

	while (pc < scriptSig.end())
	{
		opcodetype opcode;
		
		if (!scriptSig.GetOp(pc, opcode, data))
		{
			return 0;
		}
		
		if (opcode > OP_16)
		{
			return 0;
		}
	}

	/// ... and return its opcount:
	CScript subscript(data.begin(), data.end());

	return subscript.GetSigOpCount(true);
}

bool CScript::IsNormalPaymentScript() const
{
	if(this->size() != 25)
	{
		return false;
	}

	std::string str;
	opcodetype opcode;
	const_iterator pc = begin();
	int i = 0;

	while (pc < end())
	{
		GetOp(pc, opcode);

		if(i == 0 && opcode != OP_DUP)
		{
			return false;
		}
		else if(i == 1 && opcode != OP_HASH160)
		{
			return false;
		}
		else if(i == 3 && opcode != OP_EQUALVERIFY)
		{
			return false;
		}
		else if(i == 4 && opcode != OP_CHECKSIG)
		{
			return false;
		}
		else if(i == 5)
		{
			return false;
		}
		
		i++;
	}

	return true;
}

bool CScript::IsPayToScriptHash() const
{
	// Extra-fast test for pay-to-script-hash CScripts:
	return (this->size() == 23 &&
			this->at(0) == OP_HASH160 &&
			this->at(1) == 0x14 &&
			this->at(22) == OP_EQUAL);
}

// Called by IsStandardTx and P2SH VerifyScript (which makes it consensus-critical).
bool CScript::IsPushOnly() const
{
	const_iterator pc = begin();
	
	while (pc < end())
	{
		opcodetype opcode;
		
		if (!GetOp(pc, opcode))
		{
			return false;
		}
		
		if (opcode > OP_16)
		{
			return false;
		}
	}
	
	return true;
}

/**
* Returns whether the script is guaranteed to fail at execution,
* regardless of the initial stack. This allows outputs to be pruned
* instantly when entering the UTXO set.
*/
bool CScript::IsUnspendable() const
{
	return (size() > 0 && *begin() == OP_RETURN);
}

// Called by IsStandardTx.
bool CScript::HasCanonicalPushes() const
{
	const_iterator pc = begin();
	
	while (pc < end())
	{
		opcodetype opcode;
		std::vector<unsigned char> data;
		
		if (!GetOp(pc, opcode, data))
		{
			return false;
		}
		
		if (opcode > OP_16)
		{
			continue;
		}
		
		if (opcode < OP_PUSHDATA1 && opcode > OP_0 && (data.size() == 1 && data[0] <= 16))
		{
			// Could have used an OP_n code, rather than a 1-byte push.
			return false;
		}
		
		if (opcode == OP_PUSHDATA1 && data.size() < OP_PUSHDATA1)
		{
			// Could have used a normal n-byte push, rather than OP_PUSHDATA1.
			return false;
		}
		
		if (opcode == OP_PUSHDATA2 && data.size() <= 0xFF)
		{
			// Could have used an OP_PUSHDATA1.
			return false;
		}
		
		if (opcode == OP_PUSHDATA4 && data.size() <= 0xFFFF)
		{
			// Could have used an OP_PUSHDATA2.
			return false;
		}
	}

	return true;
}

void CScript::SetDestination(const CTxDestination& dest)
{
	boost::apply_visitor(CScriptVisitor(this), dest);
}

void CScript::SetMultisig(int nRequired, const std::vector<CPubKey>& keys)
{
	this->clear();

	*this << EncodeOP_N(nRequired);

	for(const CPubKey& key : keys)
	{
		*this << key;
	}

	*this << EncodeOP_N(keys.size()) << OP_CHECKMULTISIG;
}

std::string CScript::ToString(bool fShort) const
{
	std::string str;
	opcodetype opcode;
	std::vector<unsigned char> vch;
	const_iterator pc = begin();

	while (pc < end())
	{
		if (!str.empty())
		{
			str += " ";
		}
		
		if (!GetOp(pc, opcode, vch))
		{
			str += "[error]";
			
			return str;
		}
		
		if (0 <= opcode && opcode <= OP_PUSHDATA4)
		{
			str += fShort? ValueString(vch).substr(0, 10) : ValueString(vch);
		}
		else
		{
			str += GetOpName(opcode);
		}
	}

	return str;
}

CScriptID CScript::GetID() const
{
	return CScriptID(Hash160(*this));
}

void CScript::clear()
{
	// The default std::vector::clear() does not release memory.
	std::vector<unsigned char>().swap(*this);
}

