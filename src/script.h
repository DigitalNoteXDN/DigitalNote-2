#ifndef SCRIPT_H
#define SCRIPT_H

#include <string>
#include <vector>
#include <stdint.h>

#include "types/ctxdestination.h"
#include "types/valtype.h"
#include "enums/isminetype.h"
#include "enums/script_error.h"
#include "enums/opcodetype.h"
#include "enums/sighash.h"
#include "enums/txnouttype.h"

class CKeyStore;
class CTransaction;
class BaseSignatureChecker;
class CKeyID;
class CScript;
class uint256;
class CPubKey;

template <typename T>
std::vector<unsigned char> ToByteVector(const T& in);

const char* ScriptErrorString(const ScriptError error);
const char* GetTxnOutputType(txnouttype t);
const char* GetOpName(opcodetype opcode);
std::string ValueString(const std::vector<unsigned char>& vch);
std::string StackString(const std::vector<std::vector<unsigned char> >& vStack);

bool CheckSig(std::vector<unsigned char> vchSig, const std::vector<unsigned char> &vchPubKey, const CScript &scriptCode,
	const CTransaction& txTo, unsigned int nIn, int nHashType, int flags);
bool IsDERSignature(const valtype &vchSig, bool haveHashType = true);
bool EvalScript(std::vector<std::vector<unsigned char> >& stack, const CScript& script, const CTransaction& txTo,
		unsigned int nIn, unsigned int flags, int nHashType);
bool EvalScript(std::vector<std::vector<unsigned char> >& stack, const CScript& script, unsigned int flags,
		const BaseSignatureChecker& checker, ScriptError* error = NULL);
bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet);
int ScriptSigArgsExpected(txnouttype t, const std::vector<std::vector<unsigned char> >& vSolutions);
bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType);
isminetype IsMine(const CKeyStore& keystore, const CScript& scriptPubKey);
isminetype IsMine(const CKeyStore& keystore, const CTxDestination& dest);
void ExtractAffectedKeys(const CKeyStore &keystore, const CScript& scriptPubKey, std::vector<CKeyID> &vKeys);
bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet);
bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet);
bool SignSignature(const CKeyStore& keystore, const CScript& fromPubKey, CTransaction& txTo, unsigned int nIn, int nHashType=SIGHASH_ALL);
bool SignSignature(const CKeyStore& keystore, const CTransaction& txFrom, CTransaction& txTo, unsigned int nIn, int nHashType=SIGHASH_ALL);
bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey, const CTransaction& txTo, unsigned int nIn,
		unsigned int flags, int nHashType);
bool VerifySignature(const CTransaction& txFrom, const CTransaction& txTo, unsigned int nIn, unsigned int flags, int nHashType);

bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey, unsigned int flags, const BaseSignatureChecker& checker,
		ScriptError* error = NULL);

// Given two sets of signatures for scriptPubKey, possibly with OP_0 placeholders,
// combine them intelligently and return the result.
CScript CombineSignatures(CScript scriptPubKey, const CTransaction& txTo, unsigned int nIn, const CScript& scriptSig1,
		const CScript& scriptSig2);

CScript GetScriptForDestination(const CTxDestination& dest);
CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys);

bool Solver(const CKeyStore& keystore, const CScript& scriptPubKey, uint256 hash, int nHashType,
                  CScript& scriptSigRet, txnouttype& whichTypeRet);
uint256 SignatureHash(CScript scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType);

#endif // SCRIPT_H
