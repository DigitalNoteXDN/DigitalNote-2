#ifndef CSCRIPTNUM_H
#define CSCRIPTNUM_H

#include <vector>
#include <cstdint>

/**
 * Numeric opcodes (OP_1ADD, etc) are restricted to operating on 4-byte integers.
 * The semantics are subtle, though: operands must be in the range [-2^31 +1...2^31 -1],
 * but results may overflow (and are valid as long as they are not used in a subsequent
 * numeric operation). CScriptNum enforces those semantics by storing results as
 * an int64 and allowing out-of-range values to be returned as a vector of bytes but
 * throwing an exception if arithmetic is done or the result is interpreted as an integer.
 */
class CScriptNum
{
private:
	int64_t m_value;

	int64_t set_vch(const std::vector<unsigned char>& vch);

public:
	static const size_t nMaxNumSize = 4;

	explicit CScriptNum(const int64_t& n);
	explicit CScriptNum(const std::vector<unsigned char>& vch, bool fRequireMinimal);

	bool operator==(const int64_t& rhs) const;
	bool operator!=(const int64_t& rhs) const;
	bool operator<=(const int64_t& rhs) const;
	bool operator< (const int64_t& rhs) const;
	bool operator>=(const int64_t& rhs) const;
	bool operator> (const int64_t& rhs) const;

	bool operator==(const CScriptNum& rhs) const;
	bool operator!=(const CScriptNum& rhs) const;
	bool operator<=(const CScriptNum& rhs) const;
	bool operator< (const CScriptNum& rhs) const;
	bool operator>=(const CScriptNum& rhs) const;
	bool operator> (const CScriptNum& rhs) const;

	CScriptNum operator+(const int64_t& rhs) const;
	CScriptNum operator-(const int64_t& rhs) const;
	CScriptNum operator+(const CScriptNum& rhs) const;
	CScriptNum operator-(const CScriptNum& rhs) const;

	CScriptNum& operator+=(const CScriptNum& rhs);
	CScriptNum& operator-=(const CScriptNum& rhs);
	CScriptNum operator-() const;
	CScriptNum& operator=(const int64_t& rhs);
	CScriptNum& operator+=(const int64_t& rhs);
	CScriptNum& operator-=(const int64_t& rhs);
	int getint() const;
	std::vector<unsigned char> getvch() const;
	static std::vector<unsigned char> serialize(const int64_t& value);
};

static const CScriptNum ScriptNum_Zero(0);
static const CScriptNum ScriptNum_One(1);
static const CScriptNum ScriptNum_False(0);
static const CScriptNum ScriptNum_True(1);

#endif // CSCRIPTNUM_H
