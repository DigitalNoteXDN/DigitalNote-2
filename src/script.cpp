#include "compat.h"

#include <openssl/ripemd.h>
#include <openssl/sha.h>

#include "crypto/common/ripemd160.h"
#include "crypto/common/sha256.h"
#include "crypto/bmw/bmw512.h"
#include "crypto/common/sha1.h"

#include "util.h"
#include "ctransaction.h"
#include "ckey.h"
#include "cpubkey.h"
#include "ckeystore.h"
#include "cscript.h"
#include "cscriptnum.h"
#include "ctxin.h"
#include "ctxout.h"
#include "chashwriter.h"
#include "chash160.h"
#include "chash256.h"
#include "hash.h"
#include "csignaturecache.h"
#include "signaturechecker.h"
#include "caffectedkeysvisitor.h"
#include "cscriptvisitor.h"
#include "cbignum_ctx.h"
#include "cbignum_const.h"
#include "uint/uint160.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "cdigitalnoteaddress.h"
#include "cblockindex.h"
#include "main_extern.h"
#include "fork.h"
#include "serialize.h"

#include "script.h"

#define stacktop(i)  (stack.at(stack.size()+(i)))
#define altstacktop(i)  (altstack.at(altstack.size()+(i)))

static const size_t nDefaultMaxNumSize = 4;

namespace {

inline bool set_success(ScriptError* ret)
{
	if (ret)
	{
		*ret = SCRIPT_ERR_OK;
	}

	return true;
}

inline bool set_error(ScriptError* ret, const ScriptError serror)
{
	if (ret)
	{
		*ret = serror;
	}

	return false;
	}

} // anon namespace

template <typename T>
std::vector<unsigned char> ToByteVector(const T& in)
{
	return std::vector<unsigned char>(in.begin(), in.end());
}

const char* ScriptErrorString(const ScriptError serror)
{
	switch (serror)
	{
		case SCRIPT_ERR_OK:
			return "No error";
		case SCRIPT_ERR_EVAL_FALSE:
			return "Script evaluated without error but finished with a false/empty top stack element";
		case SCRIPT_ERR_VERIFY:
			return "Script failed an OP_VERIFY operation";
		case SCRIPT_ERR_EQUALVERIFY:
			return "Script failed an OP_EQUALVERIFY operation";
		case SCRIPT_ERR_CHECKMULTISIGVERIFY:
			return "Script failed an OP_CHECKMULTISIGVERIFY operation";
		case SCRIPT_ERR_CHECKSIGVERIFY:
			return "Script failed an OP_CHECKSIGVERIFY operation";
		case SCRIPT_ERR_NUMEQUALVERIFY:
			return "Script failed an OP_NUMEQUALVERIFY operation";
		case SCRIPT_ERR_SCRIPT_SIZE:
			return "Script is too big";
		case SCRIPT_ERR_PUSH_SIZE:
			return "Push value size limit exceeded";
		case SCRIPT_ERR_OP_COUNT:
			return "Operation limit exceeded";
		case SCRIPT_ERR_STACK_SIZE:
			return "Stack size limit exceeded";
		case SCRIPT_ERR_SIG_COUNT:
			return "Signature count negative or greater than pubkey count";
		case SCRIPT_ERR_PUBKEY_COUNT:
			return "Pubkey count negative or limit exceeded";
		case SCRIPT_ERR_BAD_OPCODE:
			return "Opcode missing or not understood";
		case SCRIPT_ERR_DISABLED_OPCODE:
			return "Attempted to use a disabled opcode";
		case SCRIPT_ERR_INVALID_STACK_OPERATION:
			return "Operation not valid with the current stack size";
		case SCRIPT_ERR_INVALID_ALTSTACK_OPERATION:
			return "Operation not valid with the current altstack size";
		case SCRIPT_ERR_OP_RETURN:
			return "OP_RETURN was encountered";
		case SCRIPT_ERR_UNBALANCED_CONDITIONAL:
			return "Invalid OP_IF construction";
		case SCRIPT_ERR_SIG_HASHTYPE:
			return "Signature hash type missing or not understood";
		case SCRIPT_ERR_SIG_DER:
			return "Non-canonical DER signature";
		case SCRIPT_ERR_MINIMALDATA:
			return "Data push larger than necessary";
		case SCRIPT_ERR_SIG_PUSHONLY:
			return "Only non-push operators allowed in signatures";
		case SCRIPT_ERR_SIG_HIGH_S:
			return "Non-canonical signature: S value is unnecessarily high";
		case SCRIPT_ERR_SIG_NULLDUMMY:
			return "Dummy CHECKMULTISIG argument must be zero";
		case SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS:
			return "NOPx reserved for soft-fork upgrades";
		case SCRIPT_ERR_PUBKEYTYPE:
			return "Public key is neither compressed or uncompressed";
		case SCRIPT_ERR_UNKNOWN_ERROR:
		case SCRIPT_ERR_ERROR_COUNT:
		default:
			break;
	}

	return "unknown error";
}

CBigNum CastToBigNum(const valtype& vch, const size_t nMaxNumSize = nDefaultMaxNumSize)
{
	if(vch.size() > nMaxNumSize)
	{
		throw std::runtime_error("CastToBigNum() : overflow");
	}

	// Get rid of extra leading zeros
	return CBigNum(CBigNum(vch).getvch());
}

bool CastToBool(const valtype& vch)
{
	for (unsigned int i = 0; i < vch.size(); i++)
	{
		if (vch[i] != 0)
		{
			// Can be negative zero
			if (i == vch.size()-1 && vch[i] == 0x80)
			{
				return false;
			}
			
			return true;
		}
	}

	return false;
}

//
// WARNING: This does not work as expected for signed integers; the sign-bit
// is left in place as the integer is zero-extended. The correct behavior
// would be to move the most significant bit of the last byte during the
// resize process. MakeSameSize() is currently only used by the disabled
// opcodes OP_AND, OP_OR, and OP_XOR.
//
void MakeSameSize(valtype& vch1, valtype& vch2)
{
	// Lengthen the shorter one
	if (vch1.size() < vch2.size())
	{
		// PATCH:
		// +unsigned char msb = vch1[vch1.size()-1];
		// +vch1[vch1.size()-1] &= 0x7f;
		//  vch1.resize(vch2.size(), 0);
		// +vch1[vch1.size()-1] = msb;
		vch1.resize(vch2.size(), 0);
	}

	if (vch2.size() < vch1.size())
	{
		// PATCH:
		// +unsigned char msb = vch2[vch2.size()-1];
		// +vch2[vch2.size()-1] &= 0x7f;
		//  vch2.resize(vch1.size(), 0);
		// +vch2[vch2.size()-1] = msb;
		vch2.resize(vch1.size(), 0);
	}
}

//
// Script is a stack machine (like Forth) that evaluates a predicate
// returning a bool indicating valid or not.  There are no loops.
//
static inline void popstack(std::vector<valtype>& stack)
{
	if (stack.empty())
	{
		throw std::runtime_error("popstack() : stack empty");
	}

	stack.pop_back();
}

const char* GetTxnOutputType(txnouttype t)
{
	switch (t)
	{
		case TX_NONSTANDARD: return "nonstandard";
		case TX_PUBKEY: return "pubkey";
		case TX_PUBKEYHASH: return "pubkeyhash";
		case TX_SCRIPTHASH: return "scripthash";
		case TX_MULTISIG: return "multisig";
		case TX_NULL_DATA: return "nulldata";
	}

	return NULL;
}

const char* GetOpName(opcodetype opcode)
{
	switch (opcode)
	{
		// push value
		case OP_0                      : return "0";
		case OP_PUSHDATA1              : return "OP_PUSHDATA1";
		case OP_PUSHDATA2              : return "OP_PUSHDATA2";
		case OP_PUSHDATA4              : return "OP_PUSHDATA4";
		case OP_1NEGATE                : return "-1";
		case OP_RESERVED               : return "OP_RESERVED";
		case OP_1                      : return "1";
		case OP_2                      : return "2";
		case OP_3                      : return "3";
		case OP_4                      : return "4";
		case OP_5                      : return "5";
		case OP_6                      : return "6";
		case OP_7                      : return "7";
		case OP_8                      : return "8";
		case OP_9                      : return "9";
		case OP_10                     : return "10";
		case OP_11                     : return "11";
		case OP_12                     : return "12";
		case OP_13                     : return "13";
		case OP_14                     : return "14";
		case OP_15                     : return "15";
		case OP_16                     : return "16";

		// control
		case OP_NOP                    : return "OP_NOP";
		case OP_VER                    : return "OP_VER";
		case OP_IF                     : return "OP_IF";
		case OP_NOTIF                  : return "OP_NOTIF";
		case OP_VERIF                  : return "OP_VERIF";
		case OP_VERNOTIF               : return "OP_VERNOTIF";
		case OP_ELSE                   : return "OP_ELSE";
		case OP_ENDIF                  : return "OP_ENDIF";
		case OP_VERIFY                 : return "OP_VERIFY";
		case OP_RETURN                 : return "OP_RETURN";

		// stack ops
		case OP_TOALTSTACK             : return "OP_TOALTSTACK";
		case OP_FROMALTSTACK           : return "OP_FROMALTSTACK";
		case OP_2DROP                  : return "OP_2DROP";
		case OP_2DUP                   : return "OP_2DUP";
		case OP_3DUP                   : return "OP_3DUP";
		case OP_2OVER                  : return "OP_2OVER";
		case OP_2ROT                   : return "OP_2ROT";
		case OP_2SWAP                  : return "OP_2SWAP";
		case OP_IFDUP                  : return "OP_IFDUP";
		case OP_DEPTH                  : return "OP_DEPTH";
		case OP_DROP                   : return "OP_DROP";
		case OP_DUP                    : return "OP_DUP";
		case OP_NIP                    : return "OP_NIP";
		case OP_OVER                   : return "OP_OVER";
		case OP_PICK                   : return "OP_PICK";
		case OP_ROLL                   : return "OP_ROLL";
		case OP_ROT                    : return "OP_ROT";
		case OP_SWAP                   : return "OP_SWAP";
		case OP_TUCK                   : return "OP_TUCK";

		// splice ops
		case OP_CAT                    : return "OP_CAT";
		case OP_SUBSTR                 : return "OP_SUBSTR";
		case OP_LEFT                   : return "OP_LEFT";
		case OP_RIGHT                  : return "OP_RIGHT";
		case OP_SIZE                   : return "OP_SIZE";

		// bit logic
		case OP_INVERT                 : return "OP_INVERT";
		case OP_AND                    : return "OP_AND";
		case OP_OR                     : return "OP_OR";
		case OP_XOR                    : return "OP_XOR";
		case OP_EQUAL                  : return "OP_EQUAL";
		case OP_EQUALVERIFY            : return "OP_EQUALVERIFY";
		case OP_RESERVED1              : return "OP_RESERVED1";
		case OP_RESERVED2              : return "OP_RESERVED2";

		// numeric
		case OP_1ADD                   : return "OP_1ADD";
		case OP_1SUB                   : return "OP_1SUB";
		case OP_2MUL                   : return "OP_2MUL";
		case OP_2DIV                   : return "OP_2DIV";
		case OP_NEGATE                 : return "OP_NEGATE";
		case OP_ABS                    : return "OP_ABS";
		case OP_NOT                    : return "OP_NOT";
		case OP_0NOTEQUAL              : return "OP_0NOTEQUAL";
		case OP_ADD                    : return "OP_ADD";
		case OP_SUB                    : return "OP_SUB";
		case OP_MUL                    : return "OP_MUL";
		case OP_DIV                    : return "OP_DIV";
		case OP_MOD                    : return "OP_MOD";
		case OP_LSHIFT                 : return "OP_LSHIFT";
		case OP_RSHIFT                 : return "OP_RSHIFT";
		case OP_BOOLAND                : return "OP_BOOLAND";
		case OP_BOOLOR                 : return "OP_BOOLOR";
		case OP_NUMEQUAL               : return "OP_NUMEQUAL";
		case OP_NUMEQUALVERIFY         : return "OP_NUMEQUALVERIFY";
		case OP_NUMNOTEQUAL            : return "OP_NUMNOTEQUAL";
		case OP_LESSTHAN               : return "OP_LESSTHAN";
		case OP_GREATERTHAN            : return "OP_GREATERTHAN";
		case OP_LESSTHANOREQUAL        : return "OP_LESSTHANOREQUAL";
		case OP_GREATERTHANOREQUAL     : return "OP_GREATERTHANOREQUAL";
		case OP_MIN                    : return "OP_MIN";
		case OP_MAX                    : return "OP_MAX";
		case OP_WITHIN                 : return "OP_WITHIN";

		// crypto
		case OP_RIPEMD160              : return "OP_RIPEMD160";
		case OP_SHA1                   : return "OP_SHA1";
		case OP_SHA256                 : return "OP_SHA256";
		case OP_HASH160                : return "OP_HASH160";
		case OP_HASH256                : return "OP_HASH256";
		case OP_CODESEPARATOR          : return "OP_CODESEPARATOR";
		case OP_CHECKSIG               : return "OP_CHECKSIG";
		case OP_CHECKSIGVERIFY         : return "OP_CHECKSIGVERIFY";
		case OP_CHECKMULTISIG          : return "OP_CHECKMULTISIG";
		case OP_CHECKMULTISIGVERIFY    : return "OP_CHECKMULTISIGVERIFY";

		// expanson
		case OP_NOP1                   : return "OP_NOP1";
		case OP_NOP2                   : return "OP_NOP2";
		case OP_NOP3                   : return "OP_NOP3";
		case OP_NOP4                   : return "OP_NOP4";
		case OP_NOP5                   : return "OP_NOP5";
		case OP_NOP6                   : return "OP_NOP6";
		case OP_NOP7                   : return "OP_NOP7";
		case OP_NOP8                   : return "OP_NOP8";
		case OP_NOP9                   : return "OP_NOP9";
		case OP_NOP10                  : return "OP_NOP10";

		case OP_INVALIDOPCODE          : return "OP_INVALIDOPCODE";

		// Note:
		//  The template matching params OP_SMALLDATA/etc are defined in opcodetype enum
		//  as kind of implementation hack, they are *NOT* real opcodes.  If found in real
		//  Script, just let the default: case deal with them.

		default:
			return "OP_UNKNOWN";
	}
}

std::string ValueString(const std::vector<unsigned char>& vch)
{
	if (vch.size() <= 4)
	{
		return strprintf("%d", CBigNum(vch).getint());
	}
	else
	{
		return HexStr(vch);
	}
}

inline std::string StackString(const std::vector<std::vector<unsigned char> >& vStack)
{
	std::string str;

	for(const std::vector<unsigned char>& vch : vStack)
	{
		if (!str.empty())
		{
			str += " ";
		}
		
		str += ValueString(vch);
	}

	return str;
}

static bool IsCompressedOrUncompressedPubKey(const valtype &vchPubKey)
{
	if (vchPubKey.size() < 33)
	{
		return error("Non-canonical public key: too short");
	}

	if (vchPubKey[0] == 0x04)
	{
		if (vchPubKey.size() != 65)
		{
			return error("Non-canonical public key: invalid length for uncompressed key");
		}
	}
	else if (vchPubKey[0] == 0x02 || vchPubKey[0] == 0x03)
	{
		if (vchPubKey.size() != 33)
		{
			return error("Non-canonical public key: invalid length for compressed key");
		}
	}
	else
	{
		return error("Non-canonical public key: neither compressed nor uncompressed");
	}

	return true;
}

bool IsDERSignature(const valtype &vchSig, bool haveHashType)
{
	// See https://bitcointalk.org/index.php?topic=8392.msg127623#msg127623
	// A canonical signature exists of: <30> <total len> <02> <len R> <R> <02> <len S> <S> <hashtype>
	// Where R and S are not negative (their first byte has its highest bit not set), and not
	// excessively padded (do not start with a 0 byte, unless an otherwise negative number follows,
	// in which case a single 0 byte is necessary and even required).
	if(vchSig.size() < 9)
	{
		return error("Non-canonical signature: too short");
	}

	if(vchSig.size() > 73)
	{
		return error("Non-canonical signature: too long");
	}

	if(vchSig[0] != 0x30)
	{
		return error("Non-canonical signature: wrong type");
	}

	if(vchSig[1] != vchSig.size() - (haveHashType ? 3 : 2))
	{
		return error("Non-canonical signature: wrong length marker");
	}

	unsigned int nLenR = vchSig[3];

	if(5 + nLenR >= vchSig.size())
	{
		return error("Non-canonical signature: S length misplaced");
	}

	unsigned int nLenS = vchSig[5+nLenR];

	if((unsigned long)(nLenR + nLenS + (haveHashType ? 7 : 6)) != vchSig.size())
	{
		return error("Non-canonical signature: R+S length mismatch");
	}

	const unsigned char *R = &vchSig[4];

	if(R[-2] != 0x02)
	{
		return error("Non-canonical signature: R value type mismatch");
	}

	if (nLenR == 0)
	{
		return error("Non-canonical signature: R length is zero");
	}

	if(R[0] & 0x80)
	{
		return error("Non-canonical signature: R value negative");
	}

	if (nLenR > 1 && (R[0] == 0x00) && !(R[1] & 0x80))
	{
		return error("Non-canonical signature: R value excessively padded");
	}

	const unsigned char *S = &vchSig[6+nLenR];

	if (S[-2] != 0x02)
	{
		return error("Non-canonical signature: S value type mismatch");
	}

	if (nLenS == 0)
	{
		return error("Non-canonical signature: S length is zero");
	}

	if (S[0] & 0x80)
	{
		return error("Non-canonical signature: S value negative");
	}

	if (nLenS > 1 && (S[0] == 0x00) && !(S[1] & 0x80))
	{
		return error("Non-canonical signature: S value excessively padded");
	}

	return true;
}

bool static IsLowDERSignature(const valtype &vchSig)
{
	if (!IsDERSignature(vchSig))
	{
		return false;
	}

	std::vector<unsigned char> vchSigCopy(vchSig.begin(), vchSig.begin() + vchSig.size() - 1);

	return CPubKey::CheckLowS(vchSigCopy);
}

bool static IsDefinedHashtypeSignature(const valtype &vchSig)
{
	if (vchSig.size() == 0)
	{
		return false;
	}

	unsigned char nHashType = vchSig[vchSig.size() - 1] & (~(SIGHASH_ANYONECANPAY));

	if (nHashType < SIGHASH_ALL || nHashType > SIGHASH_SINGLE)
	{
		return error("Non-canonical signature: unknown hashtype byte");
	}

	return true;
}

bool static CheckSignatureEncoding(const valtype &vchSig)
{
	if (!IsLowDERSignature(vchSig))
	{
		return false;
	}
	else if (!IsDefinedHashtypeSignature(vchSig))
	{
		return false;
	}

	return true;
}

bool static CheckPubKeyEncoding(const valtype &vchSig)
{
	if (!IsCompressedOrUncompressedPubKey(vchSig))
	{
		return false;
	}

	return true;
}

bool EvalScript(std::vector<std::vector<unsigned char> >& stack, const CScript& script, const CTransaction& txTo, unsigned int nIn, unsigned int flags, int nHashType)
{
	CBigNum_CTX pctx;
	CScript::const_iterator pc = script.begin();
	CScript::const_iterator pend = script.end();
	CScript::const_iterator pbegincodehash = script.begin();
	opcodetype opcode;
	valtype vchPushValue;
	std::vector<bool> vfExec;
	std::vector<valtype> altstack;
	int nOpCount = 0;

	if (script.size() > 10000)
	{
		return false;
	}

	try
	{
		while (pc < pend)
		{
			bool fExec = !count(vfExec.begin(), vfExec.end(), false);

			//
			// Read instruction
			//
			if (!script.GetOp(pc, opcode, vchPushValue))
			{
				return false;
			}
			
			if (vchPushValue.size() > MAX_SCRIPT_ELEMENT_SIZE)
			{
				return false;
			}
			
			if (opcode > OP_16 && ++nOpCount > 201)
			{
				return false;
			}
			
			if (opcode == OP_CAT || opcode == OP_SUBSTR || opcode == OP_LEFT || opcode == OP_RIGHT  || opcode == OP_INVERT ||
				opcode == OP_AND || opcode == OP_OR     || opcode == OP_XOR  || opcode == OP_2MUL   || opcode == OP_2DIV   ||
				opcode == OP_MUL || opcode == OP_DIV    || opcode == OP_MOD  || opcode == OP_LSHIFT || opcode == OP_RSHIFT)
			{
				return false;
			}
			
			if (fExec && 0 <= opcode && opcode <= OP_PUSHDATA4)
			{
				stack.push_back(vchPushValue);
			}
			else if (fExec || (OP_IF <= opcode && opcode <= OP_ENDIF))
			{
				switch (opcode)
				{
					//
					// Push value
					//
					case OP_1NEGATE:
					case OP_1:
					case OP_2:
					case OP_3:
					case OP_4:
					case OP_5:
					case OP_6:
					case OP_7:
					case OP_8:
					case OP_9:
					case OP_10:
					case OP_11:
					case OP_12:
					case OP_13:
					case OP_14:
					case OP_15:
					case OP_16:
					{
						// ( -- value)
						CBigNum bn((int)opcode - (int)(OP_1 - 1));
						
						stack.push_back(bn.getvch());
					}
					break;
					
					//
					// Control
					//
					case OP_NOP:
					break;
					
					case OP_NOP1:
					case OP_NOP2:
					case OP_NOP3:
					case OP_NOP4:
					case OP_NOP5:
					case OP_NOP6:
					case OP_NOP7:
					case OP_NOP8:
					case OP_NOP9:
					case OP_NOP10:
					{
						if (flags & SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS)
						{
							return false;
						}
					}	
					break;

					case OP_IF:
					case OP_NOTIF:
					{
						// <expression> if [statements] [else [statements]] endif
						bool fValue = false;
						
						if (fExec)
						{
							if (stack.size() < 1)
							{
								return false;
							}
							
							valtype& vch = stacktop(-1);
							
							fValue = CastToBool(vch);
							
							if (opcode == OP_NOTIF)
							{
								fValue = !fValue;
							}
							
							popstack(stack);
						}
						
						vfExec.push_back(fValue);	
					}
					break;
					
					case OP_ELSE:
					{
						if (vfExec.empty())
						{
							return false;
						}
						
						vfExec.back() = !vfExec.back();	
					}
					break;
					
					case OP_ENDIF:
					{
						if (vfExec.empty())
						{
							return false;
						}
						
						vfExec.pop_back();
					}
					break;
					
					case OP_VERIFY:
					{
						// (true -- ) or
						// (false -- false) and return
						if (stack.size() < 1)
						{
							return false;
						}
						
						bool fValue = CastToBool(stacktop(-1));
						
						if (fValue)
						{
							popstack(stack);
						}
						else
						{
							return false;
						}	
					}
					break;
					
					case OP_RETURN:
					{
						return false;
					}
					
					//
					// Stack ops
					//
					case OP_TOALTSTACK:
					{
						if (stack.size() < 1)
						{
							return false;
						}
						
						altstack.push_back(stacktop(-1));
						popstack(stack);
					}
					break;
					
					case OP_FROMALTSTACK:
					{
						if (altstack.size() < 1)
						{
							return false;
						}
						
						stack.push_back(altstacktop(-1));
						popstack(altstack);
					}
					break;
					
					case OP_2DROP:
					{
						// (x1 x2 -- )
						if (stack.size() < 2)
						{
							return false;
						}
						
						popstack(stack);
						popstack(stack);	
					}
					break;
					
					case OP_2DUP:
					{
						// (x1 x2 -- x1 x2 x1 x2)
						if (stack.size() < 2)
						{
							return false;
						}
						
						valtype vch1 = stacktop(-2);
						valtype vch2 = stacktop(-1);
						
						stack.push_back(vch1);
						stack.push_back(vch2);	
					}
					break;
					
					case OP_3DUP:
					{
						// (x1 x2 x3 -- x1 x2 x3 x1 x2 x3)
						if (stack.size() < 3)
						{
							return false;
						}
						
						valtype vch1 = stacktop(-3);
						valtype vch2 = stacktop(-2);
						valtype vch3 = stacktop(-1);
						
						stack.push_back(vch1);
						stack.push_back(vch2);
						stack.push_back(vch3);
					}
					break;
					
					case OP_2OVER:
					{
						// (x1 x2 x3 x4 -- x1 x2 x3 x4 x1 x2)
						if (stack.size() < 4)
						{
							return false;
						}
						
						valtype vch1 = stacktop(-4);
						valtype vch2 = stacktop(-3);
						
						stack.push_back(vch1);
						stack.push_back(vch2);
						
						break;
					}
					
					case OP_2ROT:
					{
						// (x1 x2 x3 x4 x5 x6 -- x3 x4 x5 x6 x1 x2)
						if (stack.size() < 6)
						{
							return false;
						}
						
						valtype vch1 = stacktop(-6);
						valtype vch2 = stacktop(-5);
						
						stack.erase(stack.end()-6, stack.end()-4);
						stack.push_back(vch1);
						stack.push_back(vch2);
					}	
					break;

					case OP_2SWAP:
					{
						// (x1 x2 x3 x4 -- x3 x4 x1 x2)
						if (stack.size() < 4)
						{
							return false;
						}
						
						swap(stacktop(-4), stacktop(-2));
						swap(stacktop(-3), stacktop(-1));
					}	
					break;

					case OP_IFDUP:
					{
						// (x - 0 | x x)
						if (stack.size() < 1)
						{
							return false;
						}
						
						valtype vch = stacktop(-1);
						
						if (CastToBool(vch))
						{
							stack.push_back(vch);
						}
					}	
					break;

					case OP_DEPTH:
					{
						// -- stacksize
						CBigNum bn(stack.size());
						
						stack.push_back(bn.getvch());
					}	
					break;

					case OP_DROP:
					{
						// (x -- )
						if (stack.size() < 1)
						{
							return false;
						}
						
						popstack(stack);
					}	
					break;

					case OP_DUP:
					{
						// (x -- x x)
						if (stack.size() < 1)
						{
							return false;
						}
						
						valtype vch = stacktop(-1);
						
						stack.push_back(vch);
					}	
					break;

					case OP_NIP:
					{
						// (x1 x2 -- x2)
						if (stack.size() < 2)
						{
							return false;
						}
						
						stack.erase(stack.end() - 2);
					}	
					break;

					case OP_OVER:
					{
						// (x1 x2 -- x1 x2 x1)
						if (stack.size() < 2)
						{
							return false;
						}
						
						valtype vch = stacktop(-2);
						
						stack.push_back(vch);
					}	
					break;

					case OP_PICK:
					case OP_ROLL:
					{
						// (xn ... x2 x1 x0 n - xn ... x2 x1 x0 xn)
						// (xn ... x2 x1 x0 n - ... x2 x1 x0 xn)
						if (stack.size() < 2)
						{
							return false;
						}
						
						int n = CastToBigNum(stacktop(-1)).getint();
						
						popstack(stack);
						
						if (n < 0 || n >= (int)stack.size())
						{
							return false;
						}
						
						valtype vch = stacktop(-n-1);
						
						if (opcode == OP_ROLL)
						{
							stack.erase(stack.end()-n-1);
						}
						
						stack.push_back(vch);
					}
					break;

					case OP_ROT:
					{
						// (x1 x2 x3 -- x2 x3 x1)
						//  x2 x1 x3  after first swap
						//  x2 x3 x1  after second swap
						if (stack.size() < 3)
						{
							return false;
						}
						
						swap(stacktop(-3), stacktop(-2));
						swap(stacktop(-2), stacktop(-1));
					}	
					break;
					
					case OP_SWAP:
					{
						// (x1 x2 -- x2 x1)
						if (stack.size() < 2)
						{
							return false;
						}
						
						swap(stacktop(-2), stacktop(-1));
					}					
					break;
					
					case OP_TUCK:
					{
						// (x1 x2 -- x2 x1 x2)
						if (stack.size() < 2)
						{
							return false;
						}
						
						valtype vch = stacktop(-1);
						
						stack.insert(stack.end()-2, vch);
					}	
					break;
					
					//
					// Splice ops
					//
					case OP_CAT:
					{
						// (x1 x2 -- out)
						if (stack.size() < 2)
						{
							return false;
						}
						
						valtype& vch1 = stacktop(-2);
						valtype& vch2 = stacktop(-1);
						
						vch1.insert(vch1.end(), vch2.begin(), vch2.end());
						
						popstack(stack);
						
						if (stacktop(-1).size() > MAX_SCRIPT_ELEMENT_SIZE)
						{
							return false;
						}
					}	
					break;
					
					case OP_SUBSTR:
					{
						// (in begin size -- out)
						if (stack.size() < 3)
						{
							return false;
						}
						
						valtype& vch = stacktop(-3);
						
						int nBegin = CastToBigNum(stacktop(-2)).getint();
						int nEnd = nBegin + CastToBigNum(stacktop(-1)).getint();
						
						if (nBegin < 0 || nEnd < nBegin)
						{
							return false;
						}
						
						if (nBegin > (int)vch.size())
						{
							nBegin = vch.size();
						}
						
						if (nEnd > (int)vch.size())
						{
							nEnd = vch.size();
						}
						
						vch.erase(vch.begin() + nEnd, vch.end());
						vch.erase(vch.begin(), vch.begin() + nBegin);
						
						popstack(stack);
						popstack(stack);
					}	
					break;
					
					case OP_LEFT:
					case OP_RIGHT:
					{
						// (in size -- out)
						if (stack.size() < 2)
						{
							return false;
						}
						
						valtype& vch = stacktop(-2);
						
						int nSize = CastToBigNum(stacktop(-1)).getint();
						
						if (nSize < 0)
						{
							return false;
						}
						
						if (nSize > (int)vch.size())
						{
							nSize = vch.size();
						}
						
						if (opcode == OP_LEFT)
						{
							vch.erase(vch.begin() + nSize, vch.end());
						}
						else
						{
							vch.erase(vch.begin(), vch.end() - nSize);
						}
						
						popstack(stack);
					}	
					break;

					case OP_SIZE:
					{
						// (in -- in size)
						if (stack.size() < 1)
						{
							return false;
						}
						
						CBigNum bn(stacktop(-1).size());
						
						stack.push_back(bn.getvch());
					}
					break;


					//
					// Bitwise logic
					//
					case OP_INVERT:
					{
						// (in - out)
						if (stack.size() < 1)
						{
							return false;
						}
						
						valtype& vch = stacktop(-1);
						
						for (unsigned int i = 0; i < vch.size(); i++)
						{
							vch[i] = ~vch[i];
						}
					}
					break;

					//
					// WARNING: These disabled opcodes exhibit unexpected behavior
					// when used on signed integers due to a bug in MakeSameSize()
					// [see definition of MakeSameSize() above].
					//
					case OP_AND:
					case OP_OR:
					case OP_XOR:
					{
						// (x1 x2 - out)
						if (stack.size() < 2)
						{
							return false;
						}
						
						valtype& vch1 = stacktop(-2);
						valtype& vch2 = stacktop(-1);
						
						MakeSameSize(vch1, vch2); // <-- NOT SAFE FOR SIGNED VALUES
						
						if (opcode == OP_AND)
						{
							for (unsigned int i = 0; i < vch1.size(); i++)
							{
								vch1[i] &= vch2[i];
							}
						}
						else if (opcode == OP_OR)
						{
							for (unsigned int i = 0; i < vch1.size(); i++)
							{
								vch1[i] |= vch2[i];
							}
						}
						else if (opcode == OP_XOR)
						{
							for (unsigned int i = 0; i < vch1.size(); i++)
							{
								vch1[i] ^= vch2[i];
							}
						}
						
						popstack(stack);
					}
					break;

					case OP_EQUAL:
					case OP_EQUALVERIFY:
					//case OP_NOTEQUAL: // use OP_NUMNOTEQUAL
					{
						// (x1 x2 - bool)
						if (stack.size() < 2)
						{
							return false;
						}
						
						valtype& vch1 = stacktop(-2);
						valtype& vch2 = stacktop(-1);
						bool fEqual = (vch1 == vch2);
						
						// OP_NOTEQUAL is disabled because it would be too easy to say
						// something like n != 1 and have some wiseguy pass in 1 with extra
						// zero bytes after it (numerically, 0x01 == 0x0001 == 0x000001)
						//if (opcode == OP_NOTEQUAL)
						//	fEqual = !fEqual;
						popstack(stack);
						popstack(stack);
						
						stack.push_back(fEqual ? ValType_True : ValType_False);
						
						if (opcode == OP_EQUALVERIFY)
						{
							if (fEqual)
							{
								popstack(stack);
							}
							else
							{
								return false;
							}
						}
					}
					break;


					//
					// Numeric
					//
					case OP_1ADD:
					case OP_1SUB:
					case OP_2MUL:
					case OP_2DIV:
					case OP_NEGATE:
					case OP_ABS:
					case OP_NOT:
					case OP_0NOTEQUAL:
					{
						// (in -- out)
						if (stack.size() < 1)
						{
							return false;
						}
						
						CBigNum bn = CastToBigNum(stacktop(-1));
						
						switch (opcode)
						{
							case OP_1ADD:
							{
								bn += CBigNum_One;
							}
							break;
							
							case OP_1SUB:
							{
								bn -= CBigNum_One;
							}
							break;
							
							case OP_2MUL:
							{
								bn <<= 1;
							}
							break;
							
							case OP_2DIV:
							{
								bn >>= 1;
							}
							break;
							
							case OP_NEGATE:
							{
								bn = -bn;
							}
							break;
							
							case OP_ABS:
							{
								if (bn < CBigNum_Zero)
								{
									bn = -bn;
								}
							}
							break;
							
							case OP_NOT:
							{
								bn = (bn == CBigNum_Zero);
							}
							break;
							
							case OP_0NOTEQUAL:
							{
								bn = (bn != CBigNum_Zero);
							}
							break;
							
							default:
							{
								assert(!"invalid opcode");
							}
							break;
						}
						
						popstack(stack);
						
						stack.push_back(bn.getvch());
					}
					break;

					case OP_ADD:
					case OP_SUB:
					case OP_MUL:
					case OP_DIV:
					case OP_MOD:
					case OP_LSHIFT:
					case OP_RSHIFT:
					case OP_BOOLAND:
					case OP_BOOLOR:
					case OP_NUMEQUAL:
					case OP_NUMEQUALVERIFY:
					case OP_NUMNOTEQUAL:
					case OP_LESSTHAN:
					case OP_GREATERTHAN:
					case OP_LESSTHANOREQUAL:
					case OP_GREATERTHANOREQUAL:
					case OP_MIN:
					case OP_MAX:
					{
						// (x1 x2 -- out)
						if (stack.size() < 2)
						{
							return false;
						}
						
						CBigNum bn1 = CastToBigNum(stacktop(-2));
						CBigNum bn2 = CastToBigNum(stacktop(-1));
						CBigNum bn;
						
						switch (opcode)
						{
							case OP_ADD:
							{
								bn = bn1 + bn2;
							}
							break;

							case OP_SUB:
							{
								bn = bn1 - bn2;
							}
							break;

							case OP_MUL:
							{
								if (!BN_mul(bn.to_bignum(), bn1.to_bignum(), bn2.to_bignum(), pctx))
								{
									return false;
								}
							}
							break;

							case OP_DIV:
							{
								if (!BN_div(bn.to_bignum(), NULL, bn1.to_bignum(), bn2.to_bignum(), pctx))
								{
									return false;
								}
							}
							break;

							case OP_MOD:
							{
								if (!BN_mod(bn.to_bignum(), bn1.to_bignum(), bn2.to_bignum(), pctx))
								{
									return false;
								}
							}
							break;

							case OP_LSHIFT:
							{
								if (bn2 < CBigNum_Zero || bn2 > CBigNum(2048))
								{
									return false;
								}
								
								bn = bn1 << bn2.getulong();	
							}
							break;

							case OP_RSHIFT:
							{
								if (bn2 < CBigNum_Zero || bn2 > CBigNum(2048))
								{
									return false;
								}
								
								bn = bn1 >> bn2.getulong();
							}
							break;

							case OP_BOOLAND:
							{
								bn = (bn1 != CBigNum_Zero && bn2 != CBigNum_Zero);
							}
							break;
							
							case OP_BOOLOR:
							{
								bn = (bn1 != CBigNum_Zero || bn2 != CBigNum_Zero);
							}
							break;
							
							case OP_NUMEQUAL:
							{
								bn = (bn1 == bn2);
							}
							break;
							
							case OP_NUMEQUALVERIFY:
							{
								bn = (bn1 == bn2);
							}
							break;
							
							case OP_NUMNOTEQUAL:
							{
								bn = (bn1 != bn2);
							}
							break;
							
							case OP_LESSTHAN:
							{
								bn = (bn1 < bn2);
							}
							break;
							
							case OP_GREATERTHAN:
							{
								bn = (bn1 > bn2);
							}
							break;
							
							case OP_LESSTHANOREQUAL:
							{
								bn = (bn1 <= bn2);
							}
							break;
							
							case OP_GREATERTHANOREQUAL:
							{
								bn = (bn1 >= bn2);
							}
							break;
							
							case OP_MIN:
							{
								bn = (bn1 < bn2 ? bn1 : bn2);
							}
							break;
							
							case OP_MAX:
							{
								bn = (bn1 > bn2 ? bn1 : bn2);
							}
							break;
							
							default:
							{
								assert(!"invalid opcode");
							}
							break;
						}
						
						popstack(stack);
						popstack(stack);
						
						stack.push_back(bn.getvch());

						if (opcode == OP_NUMEQUALVERIFY)
						{
							if (CastToBool(stacktop(-1)))
							{
								popstack(stack);
							}
							else
							{
								return false;
							}
						}
					}
					break;

					case OP_WITHIN:
					{
						// (x min max -- out)
						if (stack.size() < 3)
						{
							return false;
						}
						
						CBigNum bn1 = CastToBigNum(stacktop(-3));
						CBigNum bn2 = CastToBigNum(stacktop(-2));
						CBigNum bn3 = CastToBigNum(stacktop(-1));
						
						bool fValue = (bn2 <= bn1 && bn1 < bn3);
						
						popstack(stack);
						popstack(stack);
						popstack(stack);
						
						stack.push_back(fValue ? ValType_True : ValType_False);
					}
					break;

					//
					// Crypto
					//
					case OP_RIPEMD160:
					case OP_SHA1:
					case OP_SHA256:
					case OP_HASH160:
					case OP_HASH256:
					{
						// (in -- hash)
						if (stack.size() < 1)
						{
							return false;
						}
						
						valtype& vch = stacktop(-1);
						valtype vchHash((opcode == OP_RIPEMD160 || opcode == OP_SHA1 || opcode == OP_HASH160) ? 20 : 32);
						
						if (opcode == OP_RIPEMD160)
						{
							RIPEMD160(&vch[0], vch.size(), &vchHash[0]);
						}
						else if (opcode == OP_SHA1)
						{
							SHA1(&vch[0], vch.size(), &vchHash[0]);
						}
						else if (opcode == OP_SHA256)
						{
							SHA256(&vch[0], vch.size(), &vchHash[0]);
						}
						else if (opcode == OP_HASH160)
						{
							uint160 hash160 = Hash160(vch);
							
							memcpy(&vchHash[0], &hash160, sizeof(hash160));
						}
						else if (opcode == OP_HASH256)
						{
							uint256 hash = Hash_bmw512(vch.begin(), vch.end());
							
							memcpy(&vchHash[0], &hash, sizeof(hash));
						}
						
						popstack(stack);
						
						stack.push_back(vchHash);
					}
					break;

					case OP_CODESEPARATOR:
					{
						// Hash starts after the code separator
						pbegincodehash = pc;
					}
					break;

					case OP_CHECKSIG:
					case OP_CHECKSIGVERIFY:
					{
						// (sig pubkey -- bool)
						if (stack.size() < 2)
						{
							return false;
						}
						
						valtype& vchSig    = stacktop(-2);
						valtype& vchPubKey = stacktop(-1);

						// Subset of script starting at the most recent codeseparator
						CScript scriptCode(pbegincodehash, pend);

						// Drop the signature, since there's no way for a signature to sign itself
						scriptCode.FindAndDelete(CScript(vchSig));

						if ((flags & SCRIPT_VERIFY_STRICTENC) && (!CheckSignatureEncoding(vchSig) || !CheckPubKeyEncoding(vchPubKey)))
						{
							return false;
						}
						
						bool fSuccess = CheckSignatureEncoding(vchSig) && CheckPubKeyEncoding(vchPubKey) &&
							CheckSig(vchSig, vchPubKey, scriptCode, txTo, nIn, nHashType, flags);

						popstack(stack);
						popstack(stack);
						
						stack.push_back(fSuccess ? ValType_True : ValType_False);
						
						if (opcode == OP_CHECKSIGVERIFY)
						{
							if (fSuccess)
							{
								popstack(stack);
							}
							else
							{
								return false;
							}
						}
					}
					break;

					case OP_CHECKMULTISIG:
					case OP_CHECKMULTISIGVERIFY:
					{
						// ([sig ...] num_of_signatures [pubkey ...] num_of_pubkeys -- bool)

						int i = 1;
						if ((int)stack.size() < i)
						{
							return false;
						}
						
						int nKeysCount = CastToBigNum(stacktop(-i)).getint();
						
						if (nKeysCount < 0 || nKeysCount > 20)
						{
							return false;
						}
						
						nOpCount += nKeysCount;
						
						if (nOpCount > 201)
						{
							return false;
						}
						
						int ikey = ++i;
						i += nKeysCount;
						
						if ((int)stack.size() < i)
						{
							return false;
						}
						
						int nSigsCount = CastToBigNum(stacktop(-i)).getint();
						
						if (nSigsCount < 0 || nSigsCount > nKeysCount)
						{
							return false;
						}
						
						int isig = ++i;
						i += nSigsCount;
						
						if ((int)stack.size() < i)
						{
							return false;
						}
						
						// Subset of script starting at the most recent codeseparator
						CScript scriptCode(pbegincodehash, pend);

						// Drop the signatures, since there's no way for a signature to sign itself
						for (int k = 0; k < nSigsCount; k++)
						{
							valtype& vchSig = stacktop(-isig-k);
							
							scriptCode.FindAndDelete(CScript(vchSig));
						}

						bool fSuccess = true;
						
						while (fSuccess && nSigsCount > 0)
						{
							valtype& vchSig = stacktop(-isig);
							valtype& vchPubKey = stacktop(-ikey);

							if ((flags & SCRIPT_VERIFY_STRICTENC) && (!CheckSignatureEncoding(vchSig) || !CheckPubKeyEncoding(vchPubKey)))
							{
								return false;
							}
							
							// Check signature
							bool fOk = CheckSignatureEncoding(vchSig) && CheckPubKeyEncoding(vchPubKey) &&
								CheckSig(vchSig, vchPubKey, scriptCode, txTo, nIn, nHashType, flags);

							if (fOk)
							{
								isig++;
								nSigsCount--;
							}
							
							ikey++;
							nKeysCount--;

							// If there are more signatures left than keys left,
							// then too many signatures have failed
							if (nSigsCount > nKeysCount)
							{
								fSuccess = false;
							}
						}

						// Clean up stack of actual arguments
						while (i-- > 1)
						{
							popstack(stack);
						}
						
						// A bug causes CHECKMULTISIG to consume one extra argument
						// whose contents were not checked in any way.
						//
						// Unfortunately this is a potential source of mutability,
						// so optionally verify it is exactly equal to zero prior
						// to removing it from the stack.
						if (stack.size() < 1)
						{
							return false;
						}
						
						if ((flags & SCRIPT_VERIFY_NULLDUMMY) && stacktop(-1).size())
						{
							return error("CHECKMULTISIG dummy argument not null");
						}
						
						popstack(stack);

						stack.push_back(fSuccess ? ValType_True : ValType_False);

						if (opcode == OP_CHECKMULTISIGVERIFY)
						{
							if (fSuccess)
							{
								popstack(stack);
							}
							else
							{
								return false;
							}
						}
					}
					break;

					default:
					{
						return false;
					}
					break;
				}
			}
			
			// Size limits
			if (stack.size() + altstack.size() > 1000)
			{
				return false;
			}
		}
	}
	catch (...)
	{
		return false;
	}

	if (!vfExec.empty())
	{
		return false;
	}

	return true;
}

bool static IsLowDERSignature(const valtype &vchSig, ScriptError* serror)
{
	if (!IsDERSignature(vchSig))
	{
		return set_error(serror, SCRIPT_ERR_SIG_DER);
	}

	std::vector<unsigned char> vchSigCopy(vchSig.begin(), vchSig.begin() + vchSig.size() - 1);

	return CPubKey::CheckLowS(vchSigCopy);
}

bool static CheckSignatureEncoding(const valtype &vchSig, unsigned int flags, ScriptError* serror)
{
	if ((flags & (SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC)) != 0 && !IsDERSignature(vchSig))
	{
		return set_error(serror, SCRIPT_ERR_SIG_DER);
	}
	else if ((flags & SCRIPT_VERIFY_LOW_S) != 0 && !IsLowDERSignature(vchSig, serror))
	{
		// serror is set
		return false;
	}
	else if ((flags & SCRIPT_VERIFY_STRICTENC) != 0 && !IsDefinedHashtypeSignature(vchSig))
	{
		return set_error(serror, SCRIPT_ERR_SIG_HASHTYPE);
	}

	return true;
}

bool static CheckPubKeyEncoding(const valtype &vchSig, unsigned int flags, ScriptError* serror)
{
	if ((flags & SCRIPT_VERIFY_STRICTENC) != 0 && !IsCompressedOrUncompressedPubKey(vchSig))
	{
		return set_error(serror, SCRIPT_ERR_PUBKEYTYPE);
	}

	return true;
}

bool static CheckMinimalPush(const valtype& data, opcodetype opcode)
{
	if (data.size() == 0)
	{
		// Could have used OP_0.
		return opcode == OP_0;
	}
	else if (data.size() == 1 && data[0] >= 1 && data[0] <= 16)
	{
		// Could have used OP_1 .. OP_16.
		return opcode == OP_1 + (data[0] - 1);
	}
	else if (data.size() == 1 && data[0] == 0x81)
	{
		// Could have used OP_1NEGATE.
		return opcode == OP_1NEGATE;
	}
	else if (data.size() <= 75)
	{
		// Could have used a direct push (opcode indicating number of bytes pushed + those bytes).
		return opcode == data.size();
	}
	else if (data.size() <= 255)
	{
		// Could have used OP_PUSHDATA.
		return opcode == OP_PUSHDATA1;
	}
	else if (data.size() <= 65535)
	{
		// Could have used OP_PUSHDATA2.
		return opcode == OP_PUSHDATA2;
	}

	return true;
}

bool EvalScript(std::vector<std::vector<unsigned char> >& stack, const CScript& script, unsigned int flags, const BaseSignatureChecker& checker, ScriptError* serror)
{
	CScript::const_iterator pc = script.begin();
	CScript::const_iterator pend = script.end();
	CScript::const_iterator pbegincodehash = script.begin();
	opcodetype opcode;
	valtype vchPushValue;
	std::vector<bool> vfExec;
	std::vector<valtype> altstack;
	set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);

	if (script.size() > 10000)
	{
		return set_error(serror, SCRIPT_ERR_SCRIPT_SIZE);
	}

	int nOpCount = 0;
	bool fRequireMinimal = (flags & SCRIPT_VERIFY_MINIMALDATA) != 0;

	try
	{
		while (pc < pend)
		{
			bool fExec = !count(vfExec.begin(), vfExec.end(), false);

			//
			// Read instruction
			//
			if (!script.GetOp(pc, opcode, vchPushValue))
			{
				return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
			}
			
			if (vchPushValue.size() > MAX_SCRIPT_ELEMENT_SIZE)
			{
				return set_error(serror, SCRIPT_ERR_PUSH_SIZE);
			}
			
			// Note how OP_RESERVED does not count towards the opcode limit.
			if (opcode > OP_16 && ++nOpCount > 201)
			{
				return set_error(serror, SCRIPT_ERR_OP_COUNT);
			}
			
			if (opcode == OP_CAT || opcode == OP_SUBSTR || opcode == OP_LEFT || opcode == OP_RIGHT  || opcode == OP_INVERT ||
				opcode == OP_AND || opcode == OP_OR     || opcode == OP_XOR  || opcode == OP_2MUL   || opcode == OP_2DIV ||
				opcode == OP_MUL || opcode == OP_DIV    || opcode == OP_MOD  || opcode == OP_LSHIFT || opcode == OP_RSHIFT)
			{
				return set_error(serror, SCRIPT_ERR_DISABLED_OPCODE); // Disabled opcodes.
			}
			
			if (fExec && 0 <= opcode && opcode <= OP_PUSHDATA4)
			{
				if (fRequireMinimal && !CheckMinimalPush(vchPushValue, opcode))
				{
					return set_error(serror, SCRIPT_ERR_MINIMALDATA);
				}
				
				stack.push_back(vchPushValue);
			}
			else if (fExec || (OP_IF <= opcode && opcode <= OP_ENDIF))
			{
				switch (opcode)
				{
					//
					// Push value
					//
					case OP_1NEGATE:
					case OP_1:
					case OP_2:
					case OP_3:
					case OP_4:
					case OP_5:
					case OP_6:
					case OP_7:
					case OP_8:
					case OP_9:
					case OP_10:
					case OP_11:
					case OP_12:
					case OP_13:
					case OP_14:
					case OP_15:
					case OP_16:
					{
						// ( -- value)
						CScriptNum bn((int)opcode - (int)(OP_1 - 1));
						
						stack.push_back(bn.getvch());
						// The result of these opcodes should always be the minimal way to push the data
						// they push, so no need for a CheckMinimalPush here.
					}
					break;


					//
					// Control
					//
					case OP_NOP:
					break;

					case OP_NOP1:
					case OP_NOP2:
					case OP_NOP3:
					case OP_NOP4:
					case OP_NOP5:
					case OP_NOP6:
					case OP_NOP7:
					case OP_NOP8:
					case OP_NOP9:
					case OP_NOP10:
					{
						if (flags & SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS)
						{
							return set_error(serror, SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS);
						}
					}
					break;

					case OP_IF:
					case OP_NOTIF:
					{
						// <expression> if [statements] [else [statements]] endif
						bool fValue = false;
						
						if (fExec)
						{
							if (stack.size() < 1)
							{
								return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
							}
							
							valtype& vch = stacktop(-1);
							
							fValue = CastToBool(vch);
							
							if (opcode == OP_NOTIF)
							{
								fValue = !fValue;
							}
							
							popstack(stack);
						}
						
						vfExec.push_back(fValue);
					}
					break;

					case OP_ELSE:
					{
						if (vfExec.empty())
						{
							return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
						}
						
						vfExec.back() = !vfExec.back();
					}
					break;

					case OP_ENDIF:
					{
						if (vfExec.empty())
						{
							return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
						}
						
						vfExec.pop_back();
					}
					break;

					case OP_VERIFY:
					{
						// (true -- ) or
						// (false -- false) and return
						if (stack.size() < 1)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						bool fValue = CastToBool(stacktop(-1));
						
						if (fValue)
						{
							popstack(stack);
						}
						else
						{
							return set_error(serror, SCRIPT_ERR_VERIFY);
						}
					}
					break;

					case OP_RETURN:
					{
						return set_error(serror, SCRIPT_ERR_OP_RETURN);
					}
					break;


					//
					// Stack ops
					//
					case OP_TOALTSTACK:
					{
						if (stack.size() < 1)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						altstack.push_back(stacktop(-1));
						
						popstack(stack);
					}
					break;

					case OP_FROMALTSTACK:
					{
						if (altstack.size() < 1)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_ALTSTACK_OPERATION);
						}
						
						stack.push_back(altstacktop(-1));
						
						popstack(altstack);
					}
					break;

					case OP_2DROP:
					{
						// (x1 x2 -- )
						if (stack.size() < 2)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						popstack(stack);
						popstack(stack);
					}
					break;

					case OP_2DUP:
					{
						// (x1 x2 -- x1 x2 x1 x2)
						if (stack.size() < 2)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype vch1 = stacktop(-2);
						valtype vch2 = stacktop(-1);
						
						stack.push_back(vch1);
						stack.push_back(vch2);
					}
					break;

					case OP_3DUP:
					{
						// (x1 x2 x3 -- x1 x2 x3 x1 x2 x3)
						if (stack.size() < 3)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype vch1 = stacktop(-3);
						valtype vch2 = stacktop(-2);
						valtype vch3 = stacktop(-1);
						
						stack.push_back(vch1);
						stack.push_back(vch2);
						stack.push_back(vch3);
					}
					break;

					case OP_2OVER:
					{
						// (x1 x2 x3 x4 -- x1 x2 x3 x4 x1 x2)
						if (stack.size() < 4)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype vch1 = stacktop(-4);
						valtype vch2 = stacktop(-3);
						
						stack.push_back(vch1);
						stack.push_back(vch2);
					}
					break;

					case OP_2ROT:
					{
						// (x1 x2 x3 x4 x5 x6 -- x3 x4 x5 x6 x1 x2)
						if (stack.size() < 6)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype vch1 = stacktop(-6);
						valtype vch2 = stacktop(-5);
						
						stack.erase(stack.end()-6, stack.end()-4);
						stack.push_back(vch1);
						stack.push_back(vch2);
					}
					break;

					case OP_2SWAP:
					{
						// (x1 x2 x3 x4 -- x3 x4 x1 x2)
						if (stack.size() < 4)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						swap(stacktop(-4), stacktop(-2));
						swap(stacktop(-3), stacktop(-1));
					}
					break;

					case OP_IFDUP:
					{
						// (x - 0 | x x)
						if (stack.size() < 1)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype vch = stacktop(-1);
						
						if (CastToBool(vch))
						{
							stack.push_back(vch);
						}
					}
					break;

					case OP_DEPTH:
					{
						// -- stacksize
						CScriptNum bn(stack.size());
						
						stack.push_back(bn.getvch());
					}
					break;

					case OP_DROP:
					{
						// (x -- )
						if (stack.size() < 1)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						popstack(stack);
					}
					break;

					case OP_DUP:
					{
						// (x -- x x)
						if (stack.size() < 1)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype vch = stacktop(-1);
						
						stack.push_back(vch);
					}
					break;

					case OP_NIP:
					{
						// (x1 x2 -- x2)
						if (stack.size() < 2)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						stack.erase(stack.end() - 2);
					}
					break;

					case OP_OVER:
					{
						// (x1 x2 -- x1 x2 x1)
						if (stack.size() < 2)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype vch = stacktop(-2);
						
						stack.push_back(vch);
					}
					break;

					case OP_PICK:
					case OP_ROLL:
					{
						// (xn ... x2 x1 x0 n - xn ... x2 x1 x0 xn)
						// (xn ... x2 x1 x0 n - ... x2 x1 x0 xn)
						if (stack.size() < 2)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						int n = CScriptNum(stacktop(-1), fRequireMinimal).getint();
						
						popstack(stack);
						
						if (n < 0 || n >= (int)stack.size())
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype vch = stacktop(-n-1);
						
						if (opcode == OP_ROLL)
						{
							stack.erase(stack.end()-n-1);
						}
						
						stack.push_back(vch);
					}
					break;

					case OP_ROT:
					{
						// (x1 x2 x3 -- x2 x3 x1)
						//  x2 x1 x3  after first swap
						//  x2 x3 x1  after second swap
						if (stack.size() < 3)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						swap(stacktop(-3), stacktop(-2));
						swap(stacktop(-2), stacktop(-1));
					}
					break;

					case OP_SWAP:
					{
						// (x1 x2 -- x2 x1)
						if (stack.size() < 2)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						swap(stacktop(-2), stacktop(-1));
					}
					break;

					case OP_TUCK:
					{
						// (x1 x2 -- x2 x1 x2)
						if (stack.size() < 2)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype vch = stacktop(-1);
						
						stack.insert(stack.end()-2, vch);
					}
					break;


					case OP_SIZE:
					{
						// (in -- in size)
						if (stack.size() < 1)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						CScriptNum bn(stacktop(-1).size());
						
						stack.push_back(bn.getvch());
					}
					break;


					//
					// Bitwise logic
					//
					case OP_EQUAL:
					case OP_EQUALVERIFY:
					//case OP_NOTEQUAL: // use OP_NUMNOTEQUAL
					{
						// (x1 x2 - bool)
						if (stack.size() < 2)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype& vch1 = stacktop(-2);
						valtype& vch2 = stacktop(-1);
						
						bool fEqual = (vch1 == vch2);
						
						// OP_NOTEQUAL is disabled because it would be too easy to say
						// something like n != 1 and have some wiseguy pass in 1 with extra
						// zero bytes after it (numerically, 0x01 == 0x0001 == 0x000001)
						//if (opcode == OP_NOTEQUAL)
						//	fEqual = !fEqual;
						popstack(stack);
						popstack(stack);
						
						stack.push_back(fEqual ? ValType_True : ValType_False);
						
						if (opcode == OP_EQUALVERIFY)
						{
							if (fEqual)
							{
								popstack(stack);
							}
							else
							{
								return set_error(serror, SCRIPT_ERR_EQUALVERIFY);
							}
						}
					}
					break;


					//
					// Numeric
					//
					case OP_1ADD:
					case OP_1SUB:
					case OP_NEGATE:
					case OP_ABS:
					case OP_NOT:
					case OP_0NOTEQUAL:
					{
						// (in -- out)
						if (stack.size() < 1)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						CScriptNum bn(stacktop(-1), fRequireMinimal);
						
						switch (opcode)
						{
							case OP_1ADD:
							{
								bn += ScriptNum_One;
							}
							break;
							
							case OP_1SUB:
							{
								bn -= ScriptNum_One;
							}
							break;
							
							case OP_NEGATE:
							{
								bn = -bn;
							}
							break;
							
							case OP_ABS:
							{
								if (bn < ScriptNum_Zero)
								{
									bn = -bn;
								}
							}
							break;
							
							case OP_NOT:
							{
								bn = (bn == ScriptNum_Zero);
							}
							break;
							
							case OP_0NOTEQUAL:
							{
								bn = (bn != ScriptNum_Zero);
							}
							break;
							
							default:
							{
								assert(!"invalid opcode");
							}
							break;
						}
						
						popstack(stack);
						
						stack.push_back(bn.getvch());
					}
					break;

					case OP_ADD:
					case OP_SUB:
					case OP_BOOLAND:
					case OP_BOOLOR:
					case OP_NUMEQUAL:
					case OP_NUMEQUALVERIFY:
					case OP_NUMNOTEQUAL:
					case OP_LESSTHAN:
					case OP_GREATERTHAN:
					case OP_LESSTHANOREQUAL:
					case OP_GREATERTHANOREQUAL:
					case OP_MIN:
					case OP_MAX:
					{
						// (x1 x2 -- out)
						if (stack.size() < 2)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						CScriptNum bn1(stacktop(-2), fRequireMinimal);
						CScriptNum bn2(stacktop(-1), fRequireMinimal);
						CScriptNum bn(0);
						
						switch (opcode)
						{
							case OP_ADD:
							{
								bn = bn1 + bn2;
							}
							break;

							case OP_SUB:
							{
								bn = bn1 - bn2;
							}
							break;

							case OP_BOOLAND:
							{
								bn = (bn1 != ScriptNum_Zero && bn2 != ScriptNum_Zero);
							}
							break;
							
							case OP_BOOLOR:
							{
								bn = (bn1 != ScriptNum_Zero || bn2 != ScriptNum_Zero);
							}
							break;
							
							case OP_NUMEQUAL:{
								bn = (bn1 == bn2);
							}
							break;
							
							case OP_NUMEQUALVERIFY:
							{
								bn = (bn1 == bn2);
							}
							break;
							
							case OP_NUMNOTEQUAL:
							{
								bn = (bn1 != bn2);
							}
							break;
							
							case OP_LESSTHAN:
							{
								bn = (bn1 < bn2);
							}
							break;
							
							case OP_GREATERTHAN:
							{
								bn = (bn1 > bn2);
							}
							break;
							
							case OP_LESSTHANOREQUAL:
							{
								bn = (bn1 <= bn2);
							}
							break;
							
							case OP_GREATERTHANOREQUAL:
							{
								bn = (bn1 >= bn2);
							}
							break;
							
							case OP_MIN:
							{
								bn = (bn1 < bn2 ? bn1 : bn2);
							}
							break;
							
							case OP_MAX:
							{
								bn = (bn1 > bn2 ? bn1 : bn2);
							}
							break;
							
							default:
							{
								assert(!"invalid opcode");
							}
							break;
						}
						
						popstack(stack);
						popstack(stack);
						
						stack.push_back(bn.getvch());

						if (opcode == OP_NUMEQUALVERIFY)
						{
							if (CastToBool(stacktop(-1)))
							{
								popstack(stack);
							}
							else
							{
								return set_error(serror, SCRIPT_ERR_NUMEQUALVERIFY);
							}
						}
					}
					break;

					case OP_WITHIN:
					{
						// (x min max -- out)
						if (stack.size() < 3)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						CScriptNum bn1(stacktop(-3), fRequireMinimal);
						CScriptNum bn2(stacktop(-2), fRequireMinimal);
						CScriptNum bn3(stacktop(-1), fRequireMinimal);
						
						bool fValue = (bn2 <= bn1 && bn1 < bn3);
						
						popstack(stack);
						popstack(stack);
						popstack(stack);
						
						stack.push_back(fValue ? ValType_True : ValType_False);
					}
					break;


					//
					// Crypto
					//
					case OP_RIPEMD160:
					case OP_SHA1:
					case OP_SHA256:
					case OP_HASH160:
					case OP_HASH256:
					{
						// (in -- hash)
						if (stack.size() < 1)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype& vch = stacktop(-1);
						valtype vchHash((opcode == OP_RIPEMD160 || opcode == OP_SHA1 || opcode == OP_HASH160) ? 20 : 32);
						
						if (opcode == OP_RIPEMD160)
						{
							CRIPEMD160().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
						}
						else if (opcode == OP_SHA1)
						{
							CSHA1().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
						}
						else if (opcode == OP_SHA256)
						{
							CSHA256().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
						}
						else if (opcode == OP_HASH160)
						{
							CHash160().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
						}
						else if (opcode == OP_HASH256)
						{
							CHash256().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
						}
						
						popstack(stack);
						
						stack.push_back(vchHash);
					}
					break;

					case OP_CODESEPARATOR:
					{
						// Hash starts after the code separator
						pbegincodehash = pc;
					}
					break;

					case OP_CHECKSIG:
					case OP_CHECKSIGVERIFY:
					{
						// (sig pubkey -- bool)
						if (stack.size() < 2)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						valtype& vchSig = stacktop(-2);
						valtype& vchPubKey = stacktop(-1);

						// Subset of script starting at the most recent codeseparator
						CScript scriptCode(pbegincodehash, pend);

						// Drop the signature, since there's no way for a signature to sign itself
						scriptCode.FindAndDelete(CScript(vchSig));

						if (!CheckSignatureEncoding(vchSig, flags, serror) || !CheckPubKeyEncoding(vchPubKey, flags, serror))
						{
							//serror is set
							return false;
						}
						bool fSuccess = checker.CheckSig(vchSig, vchPubKey, scriptCode);

						popstack(stack);
						popstack(stack);
						
						stack.push_back(fSuccess ? ValType_True : ValType_False);
						
						if (opcode == OP_CHECKSIGVERIFY)
						{
							if (fSuccess)
							{
								popstack(stack);
							}
							else
							{
								return set_error(serror, SCRIPT_ERR_CHECKSIGVERIFY);
							}
						}
					}
					break;

					case OP_CHECKMULTISIG:
					case OP_CHECKMULTISIGVERIFY:
					{
						// ([sig ...] num_of_signatures [pubkey ...] num_of_pubkeys -- bool)

						int i = 1;
						if ((int)stack.size() < i)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						int nKeysCount = CScriptNum(stacktop(-i), fRequireMinimal).getint();
						if (nKeysCount < 0 || nKeysCount > 20)
						{
							return set_error(serror, SCRIPT_ERR_PUBKEY_COUNT);
						}
						
						nOpCount += nKeysCount;
						
						if (nOpCount > 201)
						{
							return set_error(serror, SCRIPT_ERR_OP_COUNT);
						}
						
						int ikey = ++i;
						i += nKeysCount;
						
						if ((int)stack.size() < i)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						int nSigsCount = CScriptNum(stacktop(-i), fRequireMinimal).getint();
						
						if (nSigsCount < 0 || nSigsCount > nKeysCount)
						{
							return set_error(serror, SCRIPT_ERR_SIG_COUNT);
						}
						
						int isig = ++i;
						i += nSigsCount;
						
						if ((int)stack.size() < i)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}

						// Subset of script starting at the most recent codeseparator
						CScript scriptCode(pbegincodehash, pend);

						// Drop the signatures, since there's no way for a signature to sign itself
						for (int k = 0; k < nSigsCount; k++)
						{
							valtype& vchSig = stacktop(-isig-k);
							
							scriptCode.FindAndDelete(CScript(vchSig));
						}

						bool fSuccess = true;
						
						while (fSuccess && nSigsCount > 0)
						{
							valtype& vchSig = stacktop(-isig);
							valtype& vchPubKey = stacktop(-ikey);

							// Note how this makes the exact order of pubkey/signature evaluation
							// distinguishable by CHECKMULTISIG NOT if the STRICTENC flag is set.
							// See the script_(in)valid tests for details.
							if (!CheckSignatureEncoding(vchSig, flags, serror) || !CheckPubKeyEncoding(vchPubKey, flags, serror))
							{
								// serror is set
								return false;
							}

							// Check signature
							bool fOk = checker.CheckSig(vchSig, vchPubKey, scriptCode);

							if (fOk)
							{
								isig++;
								nSigsCount--;
							}
							
							ikey++;
							nKeysCount--;

							// If there are more signatures left than keys left,
							// then too many signatures have failed. Exit early,
							// without checking any further signatures.
							if (nSigsCount > nKeysCount)
							{
								fSuccess = false;
							}
						}

						// Clean up stack of actual arguments
						while (i-- > 1)
						{
							popstack(stack);
						}
						
						// A bug causes CHECKMULTISIG to consume one extra argument
						// whose contents were not checked in any way.
						//
						// Unfortunately this is a potential source of mutability,
						// so optionally verify it is exactly equal to zero prior
						// to removing it from the stack.
						if (stack.size() < 1)
						{
							return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
						}
						
						if ((flags & SCRIPT_VERIFY_NULLDUMMY) && stacktop(-1).size())
						{
							return set_error(serror, SCRIPT_ERR_SIG_NULLDUMMY);
						}
						
						popstack(stack);

						stack.push_back(fSuccess ? ValType_True : ValType_False);

						if (opcode == OP_CHECKMULTISIGVERIFY)
						{
							if (fSuccess)
							{
								popstack(stack);
							}
							else
							{
								return set_error(serror, SCRIPT_ERR_CHECKMULTISIGVERIFY);
							}
						}
					}
					break;

					default:
					{
						return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
					}
					break;
				}
			}
			
			// Size limits
			if (stack.size() + altstack.size() > 1000)
			{
				return set_error(serror, SCRIPT_ERR_STACK_SIZE);
			}
		}
	}
	catch (...)
	{
		return set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);
	}

	if (!vfExec.empty())
	{
		return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
	}

	return set_success(serror);
}

uint256 SignatureHash(CScript scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType)
{
	if (nIn >= txTo.vin.size())
	{
		LogPrintf("ERROR: SignatureHash() : nIn=%d out of range\n", nIn);
		
		return 1;
	}

	CTransaction txTmp(txTo);

	// In case concatenating two scripts ends up with two codeseparators,
	// or an extra one at the end, this prevents all those possible incompatibilities.
	scriptCode.FindAndDelete(CScript(OP_CODESEPARATOR));

	// Blank out other inputs' signatures
	for (unsigned int i = 0; i < txTmp.vin.size(); i++)
	{
		txTmp.vin[i].scriptSig = CScript();
	}

	txTmp.vin[nIn].scriptSig = scriptCode;

	// Blank out some of the outputs
	if ((nHashType & 0x1f) == SIGHASH_NONE)
	{
		// Wildcard payee
		txTmp.vout.clear();

		// Let the others update at will
		for (unsigned int i = 0; i < txTmp.vin.size(); i++)
		{
			if (i != nIn)
			{
				txTmp.vin[i].nSequence = 0;
			}
		}
	}
	else if ((nHashType & 0x1f) == SIGHASH_SINGLE)
	{
		// Only lock-in the txout payee at same index as txin
		unsigned int nOut = nIn;
		
		if (nOut >= txTmp.vout.size())
		{
			LogPrintf("ERROR: SignatureHash() : nOut=%d out of range\n", nOut);
			
			return 1;
		}
		
		txTmp.vout.resize(nOut+1);
		
		for (unsigned int i = 0; i < nOut; i++)
		{
			txTmp.vout[i].SetNull();
		}
		
		// Let the others update at will
		for (unsigned int i = 0; i < txTmp.vin.size(); i++)
		{
			if (i != nIn)
			{
				txTmp.vin[i].nSequence = 0;
			}
		}
	}

	// Blank out other inputs completely, not recommended for open transactions
	if (nHashType & SIGHASH_ANYONECANPAY)
	{
		txTmp.vin[0] = txTmp.vin[nIn];
		txTmp.vin.resize(1);
	}

	// Serialize and hash
	CHashWriter ss(SER_GETHASH, 0);

	ss << txTmp << nHashType;

	return ss.GetHash();
}

bool SignSignature(const CKeyStore &keystore, const CScript& fromPubKey, CTransaction& txTo, unsigned int nIn, int nHashType)
{
	assert(nIn < txTo.vin.size());

	CTxIn& txin = txTo.vin[nIn];

	// Leave out the signature from the hash, since a signature can't sign itself.
	// The checksig op will also drop the signatures from its hash.
	uint256 hash = SignatureHash(fromPubKey, txTo, nIn, nHashType);

	txnouttype whichType;

	if (!Solver(keystore, fromPubKey, hash, nHashType, txin.scriptSig, whichType))
	{
		return false;
	}

	if (whichType == TX_SCRIPTHASH)
	{
		// Solver returns the subscript that need to be evaluated;
		// the final scriptSig is the signatures from that
		// and then the serialized subscript:
		CScript subscript = txin.scriptSig;

		// Recompute txn hash using subscript in place of scriptPubKey:
		uint256 hash2 = SignatureHash(subscript, txTo, nIn, nHashType);

		txnouttype subType;
		bool fSolved = Solver(keystore, subscript, hash2, nHashType, txin.scriptSig, subType) && subType != TX_SCRIPTHASH;
		
		// Append serialized subscript whether or not it is completely signed:
		txin.scriptSig << static_cast<valtype>(subscript);
		
		if (!fSolved)
		{
			return false;
		}
	}

	// Test solution
	return VerifyScript(txin.scriptSig, fromPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, SignatureChecker(txTo, nIn));
}

bool SignSignature(const CKeyStore &keystore, const CTransaction& txFrom, CTransaction& txTo, unsigned int nIn, int nHashType)
{
	assert(nIn < txTo.vin.size());

	CTxIn& txin = txTo.vin[nIn];

	assert(txin.prevout.n < txFrom.vout.size());

	const CTxOut& txout = txFrom.vout[txin.prevout.n];

	return SignSignature(keystore, txout.scriptPubKey, txTo, nIn, nHashType);
}

bool CheckSig(std::vector<unsigned char> vchSig, const std::vector<unsigned char> &vchPubKey, const CScript &scriptCode,
		const CTransaction& txTo, unsigned int nIn, int nHashType, int flags)
{
	static CSignatureCache signatureCache;

	CPubKey pubkey(vchPubKey);

	if (!pubkey.IsValid())
	{
		return false;
	}

	// Hash type is one byte tacked on to the end of the signature
	if (vchSig.empty())
	{
		return false;
	}

	if (nHashType == 0)
	{
		nHashType = vchSig.back();
	}
	else if (nHashType != vchSig.back())
	{
		return false;
	}

	vchSig.pop_back();

	uint256 sighash = SignatureHash(scriptCode, txTo, nIn, nHashType);

	if (signatureCache.Get(sighash, vchSig, pubkey))
	{
		return true;
	}

	if (!pubkey.Verify(sighash, vchSig))
	{
		return false;
	}

	if (!(flags & SCRIPT_VERIFY_NOCACHE))
	{
		signatureCache.Set(sighash, vchSig, pubkey);
	}

	return true;
}

//
// Return public keys or hashes from scriptPubKey, for 'standard' transaction types.
//
bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet)
{
	// Templates
	static std::multimap<txnouttype, CScript> mTemplates;

	if (mTemplates.empty())
	{
		// Standard tx, sender provides pubkey, receiver adds signature
		mTemplates.insert(std::make_pair(TX_PUBKEY, CScript() << OP_PUBKEY << OP_CHECKSIG));

		// DigitalNote address tx, sender provides hash of pubkey, receiver provides signature and pubkey
		mTemplates.insert(std::make_pair(TX_PUBKEYHASH, CScript() << OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG));

		// Sender provides N pubkeys, receivers provides M signatures
		mTemplates.insert(std::make_pair(TX_MULTISIG, CScript() << OP_SMALLINTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_CHECKMULTISIG));

		// Empty, provably prunable, data-carrying output
		mTemplates.insert(std::make_pair(TX_NULL_DATA, CScript() << OP_RETURN << OP_SMALLDATA));
		mTemplates.insert(std::make_pair(TX_NULL_DATA, CScript() << OP_RETURN));
	}

	// Shortcut for pay-to-script-hash, which are more constrained than the other types:
	// it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
	if (scriptPubKey.IsPayToScriptHash())
	{
		typeRet = TX_SCRIPTHASH;
		
		std::vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
		
		vSolutionsRet.push_back(hashBytes);
		
		return true;
	}

	// Scan templates
	const CScript& script1 = scriptPubKey;

	for(const std::pair<txnouttype, CScript>& tplate : mTemplates)
	{
		const CScript& script2 = tplate.second;
		opcodetype opcode1, opcode2;
		std::vector<unsigned char> vch1, vch2;
		
		vSolutionsRet.clear();
		
		// Compare
		CScript::const_iterator pc1 = script1.begin();
		CScript::const_iterator pc2 = script2.begin();
		
		while (true)
		{
			if (pc1 == script1.end() && pc2 == script2.end())
			{
				// Found a match
				typeRet = tplate.first;
				
				if (typeRet == TX_MULTISIG)
				{
					// Additional checks for TX_MULTISIG:
					unsigned char m = vSolutionsRet.front()[0];
					unsigned char n = vSolutionsRet.back()[0];
					
					if (m < 1 || n < 1 || m > n || vSolutionsRet.size()-2 != n)
					{
						return false;
					}
				}
				
				return true;
			}
			
			if (!script1.GetOp(pc1, opcode1, vch1))
			{
				break;
			}
			
			if (!script2.GetOp(pc2, opcode2, vch2))
			{
				break;
			}
			
			// Template matching opcodes:
			if (opcode2 == OP_PUBKEYS)
			{
				while (vch1.size() >= 33 && vch1.size() <= 120)
				{
					vSolutionsRet.push_back(vch1);
					
					if (!script1.GetOp(pc1, opcode1, vch1))
					{
						break;
					}
				}
				
				if (!script2.GetOp(pc2, opcode2, vch2))
				{
					break;
				}
				
				// Normal situation is to fall through
				// to other if/else statements
			}

			if (opcode2 == OP_PUBKEY)
			{
				if (vch1.size() < 33 || vch1.size() > 120)
				{
					break;
				}
				
				vSolutionsRet.push_back(vch1);
			}
			else if (opcode2 == OP_PUBKEYHASH)
			{
				if (vch1.size() != sizeof(uint160))
				{
					break;
				}
				
				vSolutionsRet.push_back(vch1);
			}
			else if (opcode2 == OP_SMALLINTEGER)
			{   // Single-byte small integer pushed onto vSolutions
				if (opcode1 == OP_0 ||
					(opcode1 >= OP_1 && opcode1 <= OP_16))
				{
					char n = (char)CScript::DecodeOP_N(opcode1);
					vSolutionsRet.push_back(valtype(1, n));
				}
				else
				{
					break;
				}
			}
			else if (opcode2 == OP_SMALLDATA)
			{
				// small pushdata, <= MAX_OP_RETURN_RELAY bytes
				if (vch1.size() > MAX_OP_RETURN_RELAY)
				{
					break;
				}
			}
			else if (opcode1 != opcode2 || vch1 != vch2)
			{
				// Others must match exactly
				break;
			}
		}
	}

	vSolutionsRet.clear();
	typeRet = TX_NONSTANDARD;

	return false;
}

bool Sign1(const CKeyID& address, const CKeyStore& keystore, uint256 hash, int nHashType, CScript& scriptSigRet)
{
	CKey key;

	if (!keystore.GetKey(address, key))
	{
		return false;
	}

	std::vector<unsigned char> vchSig;
	bool test = -1;

	if (!key.Sign(hash, vchSig, test))
	{
		return false;
	}

	vchSig.push_back((unsigned char)nHashType);

	scriptSigRet << vchSig;

	return true;
}

bool SignN(const std::vector<valtype>& multisigdata, const CKeyStore& keystore, uint256 hash, int nHashType, CScript& scriptSigRet)
{
	int nSigned = 0;
	int nRequired = multisigdata.front()[0];

	for (unsigned int i = 1; i < multisigdata.size()-1 && nSigned < nRequired; i++)
	{
		const valtype& pubkey = multisigdata[i];
		CKeyID keyID = CPubKey(pubkey).GetID();
		
		if (Sign1(keyID, keystore, hash, nHashType, scriptSigRet))
		{
			++nSigned;
		}
	}

	return nSigned == nRequired;
}

//
// Sign scriptPubKey with private keys stored in keystore, given transaction hash and hash type.
// Signatures are returned in scriptSigRet (or returns false if scriptPubKey can't be signed),
// unless whichTypeRet is TX_SCRIPTHASH, in which case scriptSigRet is the redemption script.
// Returns false if scriptPubKey could not be completely satisfied.
//
bool Solver(const CKeyStore& keystore, const CScript& scriptPubKey, uint256 hash, int nHashType,
		CScript& scriptSigRet, txnouttype& whichTypeRet)
{
	std::vector<valtype> vSolutions;

	scriptSigRet.clear();

	if (!Solver(scriptPubKey, whichTypeRet, vSolutions))
	{
		return false;
	}

	CKeyID keyID;

	switch (whichTypeRet)
	{
		case TX_NONSTANDARD:
		case TX_NULL_DATA:
		{
			return false;
		}
		
		case TX_PUBKEY:
		{
			keyID = CPubKey(vSolutions[0]).GetID();
			
			return Sign1(keyID, keystore, hash, nHashType, scriptSigRet);
		}
		
		case TX_PUBKEYHASH:
		{
			keyID = CKeyID(uint160(vSolutions[0]));
			
			if (!Sign1(keyID, keystore, hash, nHashType, scriptSigRet))
			{
				return false;
			}
			else
			{
				CPubKey vch;
				
				keystore.GetPubKey(keyID, vch);
				
				scriptSigRet << vch;
			}
			return true;
		}
		
		case TX_SCRIPTHASH:
		{
			return keystore.GetCScript(uint160(vSolutions[0]), scriptSigRet);
		}
		
		case TX_MULTISIG:
		{
			scriptSigRet << OP_0; // workaround CHECKMULTISIG bug
			
			return (SignN(vSolutions, keystore, hash, nHashType, scriptSigRet));
		}
	}

	return false;
}

int ScriptSigArgsExpected(txnouttype t, const std::vector<std::vector<unsigned char> >& vSolutions)
{
	switch (t)
	{
		case TX_NONSTANDARD:
		case TX_NULL_DATA:
		{
			return -1;
		}
		
		case TX_PUBKEY:
		{
			return 1;
		}
		
		case TX_PUBKEYHASH:
		{
			return 2;
		}
		
		case TX_MULTISIG:
		{
			if (vSolutions.size() < 1 || vSolutions[0].size() < 1)
			{
				return -1;
			}
			
			return vSolutions[0][0] + 1;
		}
		
		case TX_SCRIPTHASH:
		{
			return 1; // doesn't include args needed by the script
		}
	}

	return -1;
}

bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType)
{
	std::vector<valtype> vSolutions;

	if (!Solver(scriptPubKey, whichType, vSolutions))
	{
		return false;
	}

	if (whichType == TX_MULTISIG)
	{
		unsigned char m = vSolutions.front()[0];
		unsigned char n = vSolutions.back()[0];
		
		// Support up to x-of-3 multisig txns as standard
		if (n < 1 || n > 3)
		{
			return false;
		}
		
		if (m < 1 || m > n)
		{
			return false;
		}
	}

	return whichType != TX_NONSTANDARD;
}

unsigned int HaveKeys(const std::vector<valtype>& pubkeys, const CKeyStore& keystore)
{
	unsigned int nResult = 0;

	for(const valtype& pubkey : pubkeys)
	{
		CKeyID keyID = CPubKey(pubkey).GetID();
		
		if (keystore.HaveKey(keyID))
		{
			++nResult;
		}
	}

	return nResult;
}

isminetype IsMine(const CKeyStore &keystore, const CTxDestination& dest)
{
	CScript script;

	script.SetDestination(dest);

	return IsMine(keystore, script);
}

isminetype IsMine(const CKeyStore &keystore, const CScript& scriptPubKey)
{
	std::vector<valtype> vSolutions;
	txnouttype whichType;

	if (!Solver(scriptPubKey, whichType, vSolutions))
	{
		if (keystore.HaveWatchOnly(scriptPubKey))
		{
			return ISMINE_WATCH_ONLY;
		}
		
		return ISMINE_NO;
	}

	CKeyID keyID;

	switch (whichType)
	{
		case TX_NONSTANDARD:
		case TX_NULL_DATA:
		{
		}
		break;
		
		case TX_PUBKEY:
		{
			keyID = CPubKey(vSolutions[0]).GetID();
			
			if (keystore.HaveKey(keyID))
			{
				return ISMINE_SPENDABLE;
			}
		}
		break;
		
		case TX_PUBKEYHASH:
		{
			keyID = CKeyID(uint160(vSolutions[0]));
			
			if (keystore.HaveKey(keyID))
			{
				return ISMINE_SPENDABLE;
			}
		}
		break;
		
		case TX_SCRIPTHASH:
		{
			CScriptID scriptID = CScriptID(uint160(vSolutions[0]));
			CScript subscript;
			
			if (keystore.GetCScript(scriptID, subscript))
			{
				isminetype ret = IsMine(keystore, subscript);
				
				if (ret == ISMINE_SPENDABLE)
				{
					return ret;
				}
			}
		}
		break;
		
		case TX_MULTISIG:
		{
			// Only consider transactions "mine" if we own ALL the
			// keys involved. multi-signature transactions that are
			// partially owned (somebody else has a key that can spend
			// them) enable spend-out-from-under-you attacks, especially
			// in shared-wallet situations.
			std::vector<valtype> keys(vSolutions.begin()+1, vSolutions.begin()+vSolutions.size()-1);
			
			if (HaveKeys(keys, keystore) == keys.size())
			{
				return ISMINE_SPENDABLE;
			}
		}
		break;
	}

	if (keystore.HaveWatchOnly(scriptPubKey))
	{
		return ISMINE_WATCH_ONLY;
	}

	return ISMINE_NO;
	}

	bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet)
	{
	std::vector<valtype> vSolutions;
	txnouttype whichType;

	if (!Solver(scriptPubKey, whichType, vSolutions))
	{
		return false;
	}

	if (whichType == TX_PUBKEY)
	{
		addressRet = CPubKey(vSolutions[0]).GetID();
		
		return true;
	}
	else if (whichType == TX_PUBKEYHASH)
	{
		addressRet = CKeyID(uint160(vSolutions[0]));
		
		return true;
	}
	else if (whichType == TX_SCRIPTHASH)
	{
		addressRet = CScriptID(uint160(vSolutions[0]));
		
		return true;
	}

	// Multisig txns have more than one address...
	return false;
}

void ExtractAffectedKeys(const CKeyStore &keystore, const CScript& scriptPubKey, std::vector<CKeyID> &vKeys)
{
	CAffectedKeysVisitor(keystore, vKeys).Process(scriptPubKey);
}

bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet)
{
	std::vector<valtype> vSolutions;

	addressRet.clear();

	typeRet = TX_NONSTANDARD;

	if (!Solver(scriptPubKey, typeRet, vSolutions))
	{
		return false;
	}

	if (typeRet == TX_NULL_DATA)
	{
		// This is data, not addresses
		return false;
	}

	if (typeRet == TX_MULTISIG)
	{
		nRequiredRet = vSolutions.front()[0];
		
		for (unsigned int i = 1; i < vSolutions.size()-1; i++)
		{
			CTxDestination address = CPubKey(vSolutions[i]).GetID();
			
			addressRet.push_back(address);
		}
	}
	else
	{
		CTxDestination address;
		
		nRequiredRet = 1;
		
		if (!ExtractDestination(scriptPubKey, address))
		{
		   return false;
		}
		
		addressRet.push_back(address);
	}

	return true;
}

/*
uint256 SignatureHash(const CScript& scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType)
{
	if (nIn >= txTo.vin.size())
	{
		//  nIn out of range
		return 1;
	}

	// Check for invalid use of SIGHASH_SINGLE
	if ((nHashType & 0x1f) == SIGHASH_SINGLE)
	{
		if (nIn >= txTo.vout.size())
		{
			//  nOut out of range
			return 1;
		}
	}

	// Wrapper to serialize only the necessary parts of the transaction being signed
	CTransactionSignatureSerializer txTmp(txTo, scriptCode, nIn, nHashType);

	// Serialize and hash
	CHashWriter ss(SER_GETHASH, 0);

	ss << txTmp << nHashType;

	return ss.GetHash();
}
*/

bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey, const CTransaction& txTo, unsigned int nIn,
		unsigned int flags, int nHashType)
{
	std::vector<std::vector<unsigned char> > stack, stackCopy;

	if (!EvalScript(stack, scriptSig, txTo, nIn, flags, nHashType))
	{
		return false;
	}

	stackCopy = stack;

	if (!EvalScript(stack, scriptPubKey, txTo, nIn, flags, nHashType))
	{
		return false;
	}

	if (stack.empty())
	{
		return false;
	}

	if (CastToBool(stack.back()) == false)
	{
		return false;
	}

	// Additional validation for spend-to-script-hash transactions:
	if (scriptPubKey.IsPayToScriptHash())
	{
		if (!scriptSig.IsPushOnly()) // scriptSig must be literals-only
		{
			return false; // or validation fails
		}
		
		const valtype& pubKeySerialized = stackCopy.back();
		CScript pubKey2(pubKeySerialized.begin(), pubKeySerialized.end());
		
		popstack(stackCopy);

		if (!EvalScript(stackCopy, pubKey2, txTo, nIn, flags, nHashType))
		{
			return false;
		}
		
		if (stackCopy.empty())
		{
			return false;
		}
		
		return CastToBool(stackCopy.back());
	}

	return true;
}

bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey, unsigned int flags,
		const BaseSignatureChecker& checker, ScriptError* serror)
{
	set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);

	if ((flags & SCRIPT_VERIFY_SIGPUSHONLY) != 0 && !scriptSig.IsPushOnly())
	{
		return set_error(serror, SCRIPT_ERR_SIG_PUSHONLY);
	}

	std::vector<std::vector<unsigned char> > stack, stackCopy;

	if (!EvalScript(stack, scriptSig, flags, checker, serror))
	{
		// serror is set
		return false;
	}

	if (flags & SCRIPT_VERIFY_P2SH)
	{
		stackCopy = stack;
	}

	if (!EvalScript(stack, scriptPubKey, flags, checker, serror))
	{
		// serror is set
		return false;
	}

	if (stack.empty())
	{
		return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
	}

	if (CastToBool(stack.back()) == false)
	{
		return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
	}

	// Additional validation for spend-to-script-hash transactions:
	if ((flags & SCRIPT_VERIFY_P2SH) && scriptPubKey.IsPayToScriptHash())
	{
		// scriptSig must be literals-only or validation fails
		if (!scriptSig.IsPushOnly())
		{
			return set_error(serror, SCRIPT_ERR_SIG_PUSHONLY);
		}
		
		// stackCopy cannot be empty here, because if it was the
		// P2SH  HASH <> EQUAL  scriptPubKey would be evaluated with
		// an empty stack and the EvalScript above would return false.
		assert(!stackCopy.empty());

		const valtype& pubKeySerialized = stackCopy.back();
		CScript pubKey2(pubKeySerialized.begin(), pubKeySerialized.end());
		
		popstack(stackCopy);

		if (!EvalScript(stackCopy, pubKey2, flags, checker, serror))
		{
			// serror is set
			return false;
		}
		
		if (stackCopy.empty())
		{
			return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
		}
		
		if (!CastToBool(stackCopy.back()))
		{
			return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
		}
		else
		{
			return set_success(serror);
		}
	}

	return set_success(serror);
}


/*
bool SignSignature(const CKeyStore &keystore, const CScript& fromPubKey, CTransaction& txTo, unsigned int nIn, int nHashType)
{
	assert(nIn < txTo.vin.size());

	CTxIn& txin = txTo.vin[nIn];

	// Leave out the signature from the hash, since a signature can't sign itself.
	// The checksig op will also drop the signatures from its hash.
	uint256 hash = SignatureHash(fromPubKey, txTo, nIn, nHashType);

	txnouttype whichType;

	if (!Solver(keystore, fromPubKey, hash, nHashType, txin.scriptSig, whichType))
	{
		return false;
	}

	if (whichType == TX_SCRIPTHASH)
	{
		// Solver returns the subscript that need to be evaluated;
		// the final scriptSig is the signatures from that
		// and then the serialized subscript:
		CScript subscript = txin.scriptSig;

		// Recompute txn hash using subscript in place of scriptPubKey:
		uint256 hash2 = SignatureHash(subscript, txTo, nIn, nHashType);

		txnouttype subType;
		bool fSolved = Solver(keystore, subscript, hash2, nHashType, txin.scriptSig, subType) && subType != TX_SCRIPTHASH;
		
		// Append serialized subscript whether or not it is completely signed:
		txin.scriptSig << static_cast<valtype>(subscript);
		
		if (!fSolved)
		{
			return false;
		}
	}

	// Test solution
	return VerifyScript(txin.scriptSig, fromPubKey, txTo, nIn, STANDARD_SCRIPT_VERIFY_FLAGS, 0);
}


bool SignSignature(const CKeyStore &keystore, const CTransaction& txFrom, CTransaction& txTo, unsigned int nIn, int nHashType)
{
	assert(nIn < txTo.vin.size());

	CTxIn& txin = txTo.vin[nIn];

	assert(txin.prevout.n < txFrom.vout.size());
	assert(txin.prevout.hash == txFrom.GetHash());

	const CTxOut& txout = txFrom.vout[txin.prevout.n];

	return SignSignature(keystore, txout.scriptPubKey, txTo, nIn, nHashType);
}*/

bool VerifySignature(const CTransaction& txFrom, const CTransaction& txTo, unsigned int nIn, unsigned int flags, int nHashType)
{
	std::string _txFrom, _txTo;

	_txFrom = txFrom.GetHash().ToString();
	_txTo = txTo.GetHash().ToString();

	// LogPrintf("VerifySignature from %s to %s\n", _txFrom.c_str(), _txTo.c_str());

	assert(nIn < txTo.vin.size());

	const CTxIn& txin = txTo.vin[nIn];

	if (txin.prevout.n >= txFrom.vout.size())
	{
		return false;
	}

	const CTxOut& txout = txFrom.vout[txin.prevout.n];

	if (txin.prevout.hash != txFrom.GetHash())
	{
		return false;
	}

	/*
		Exploit happpend on 31st Aug 2021 17:17:26
		
		Reference:
			https://xdn-explorer.com/block/00000000000371f620dba8ef1576407b558686d8b00ca275c3debbfaee6a3db8
	*/
	if(
		(
			(
				_txFrom == "81140f106083298143e0e0bd044705b83a891bf2072721dfa43f7237be5931fb" ||
				_txFrom == "164a0151731efc1536fd75e7d5c4a61e17ef67df1d0f4649b3689d604a41a955"
			) &&
			_txTo == "2a639be55df3d7789c73e05aab30edce8fc867d1aae76728e2d59dd2c19b39ab"
		) ||
		(
			_txFrom == "2a639be55df3d7789c73e05aab30edce8fc867d1aae76728e2d59dd2c19b39ab" &&
			_txTo == "adb24c4a4f50bf848ded522fad8de1546bcc16f1ae838a5b51888bcd753dd25b"
		)
	)
	{
		return true;
	}

	/*
		Prevent burn address to send any transactions
	*/
	CTxDestination ctxdest_address;
	ExtractDestination(txout.scriptPubKey, ctxdest_address);
	CDigitalNoteAddress cdigit_address(ctxdest_address);
	std::string str_address = cdigit_address.ToString();

	// Burn address must be filtered and
	// transactions with burn address that made in the past must be still valid
	if(
		(
			str_address == BURN_ADDRESS_A ||
			str_address == BURN_ADDRESS_B ||
			str_address == BURN_ADDRESS_C
		) && pindexBest->nHeight > 429972
	)
	{
		LogPrintf("[Error] Burn address %s trying to send coins!!! Run in circles and scream with your hands in the air!!!\n", str_address.c_str());
		
		return false;
	}

	return VerifyScript(txin.scriptSig, txout.scriptPubKey, txTo, nIn, flags, nHashType);
}

static CScript PushAll(const std::vector<valtype>& values)
{
	CScript result;

	for(const valtype& v : values)
	{
		result << v;
	}

	return result;
}

static CScript CombineMultisig(CScript scriptPubKey, const CTransaction& txTo, unsigned int nIn,
		const std::vector<valtype>& vSolutions, std::vector<valtype>& sigs1, std::vector<valtype>& sigs2)
{
	// Combine all the signatures we've got:
	std::set<valtype> allsigs;

	for(const valtype& v : sigs1)
	{
		if (!v.empty())
		{
			allsigs.insert(v);
		}
	}

	for(const valtype& v : sigs2)
	{
		if (!v.empty())
		{
			allsigs.insert(v);
		}
	}

	// Build a map of pubkey -> signature by matching sigs to pubkeys:
	assert(vSolutions.size() > 1);

	unsigned int nSigsRequired = vSolutions.front()[0];
	unsigned int nPubKeys = vSolutions.size()-2;

	std::map<valtype, valtype> sigs;

	for(const valtype& sig : allsigs)
	{
		for (unsigned int i = 0; i < nPubKeys; i++)
		{
			const valtype& pubkey = vSolutions[i+1];
			
			if (sigs.count(pubkey))
			{
				continue; // Already got a sig for this pubkey
			}
			
			if (CheckSig(sig, pubkey, scriptPubKey, txTo, nIn, 0, 0))
			{
				sigs[pubkey] = sig;
				
				break;
			}
		}
	}

	// Now build a merged CScript:
	unsigned int nSigsHave = 0;
	CScript result; result << OP_0; // pop-one-too-many workaround

	for (unsigned int i = 0; i < nPubKeys && nSigsHave < nSigsRequired; i++)
	{
		if (sigs.count(vSolutions[i+1]))
		{
			result << sigs[vSolutions[i+1]];
			
			++nSigsHave;
		}
	}

	// Fill any missing with OP_0:
	for (unsigned int i = nSigsHave; i < nSigsRequired; i++)
	{
		result << OP_0;
	}

	return result;
}

static CScript CombineSignatures(CScript scriptPubKey, const CTransaction& txTo, unsigned int nIn, const txnouttype txType,
		const std::vector<valtype>& vSolutions, std::vector<valtype>& sigs1, std::vector<valtype>& sigs2)
{
	switch (txType)
	{
		case TX_NONSTANDARD:
		case TX_NULL_DATA:
		{
			// Don't know anything about this, assume bigger one is correct:
			if (sigs1.size() >= sigs2.size())
			{
				return PushAll(sigs1);
			}
			
			return PushAll(sigs2);
		}
		
		case TX_PUBKEY:
		case TX_PUBKEYHASH:
		{
			// Signatures are bigger than placeholders or empty scripts:
			if (sigs1.empty() || sigs1[0].empty())
			{
				return PushAll(sigs2);
			}
			
			return PushAll(sigs1);
		}
		
		case TX_SCRIPTHASH:
		{
			if (sigs1.empty() || sigs1.back().empty())
			{
				return PushAll(sigs2);
			}
			else if (sigs2.empty() || sigs2.back().empty())
			{
				return PushAll(sigs1);
			}
			else
			{
				// Recur to combine:
				valtype spk = sigs1.back();
				CScript pubKey2(spk.begin(), spk.end());

				txnouttype txType2;
				std::vector<std::vector<unsigned char> > vSolutions2;
				
				Solver(pubKey2, txType2, vSolutions2);
				
				sigs1.pop_back();
				sigs2.pop_back();
				
				CScript result = CombineSignatures(pubKey2, txTo, nIn, txType2, vSolutions2, sigs1, sigs2);
				
				result << spk;
				
				return result;
			}
		}
		
		case TX_MULTISIG:
		{
			return CombineMultisig(scriptPubKey, txTo, nIn, vSolutions, sigs1, sigs2);
		}
	}

	return CScript();
}

CScript CombineSignatures(CScript scriptPubKey, const CTransaction& txTo, unsigned int nIn, const CScript& scriptSig1, const CScript& scriptSig2)
{
	txnouttype txType;
	std::vector<std::vector<unsigned char> > vSolutions;
	std::vector<valtype> stack1;
	std::vector<valtype> stack2;

	Solver(scriptPubKey, txType, vSolutions);

	EvalScript(stack1, scriptSig1, CTransaction(), 0, SCRIPT_VERIFY_NONE, 0);
	EvalScript(stack2, scriptSig2, CTransaction(), 0, SCRIPT_VERIFY_NONE, 0);

	return CombineSignatures(scriptPubKey, txTo, nIn, txType, vSolutions, stack1, stack2);
}

CScript GetScriptForDestination(const CTxDestination& dest)
{
	CScript script;

	boost::apply_visitor(CScriptVisitor(&script), dest);

	return script;
}

CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys)
{
	CScript script;

	script << CScript::EncodeOP_N(nRequired);

	for(const CPubKey& key : keys)
	{
		script << ToByteVector(key);
	}

	script << CScript::EncodeOP_N(keys.size()) << OP_CHECKMULTISIG;

	return script;
}

