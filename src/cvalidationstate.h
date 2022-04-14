#ifndef CVALIDATIONSTATE_H
#define CVALIDATIONSTATE_H

#include <string>

/** Capture information about block/transaction validation */
class CValidationState
{
private:
	enum mode_state
	{
		MODE_VALID,   //! everything ok
		MODE_INVALID, //! network rule violation (DoS value may be set)
		MODE_ERROR,   //! run-time error
	} mode;

	int nDoS;
	std::string strRejectReason;
	unsigned char chRejectCode;
	bool corruptionPossible;

public:
	CValidationState();

	bool DoS(int level, bool ret = false, unsigned char chRejectCodeIn=0, std::string strRejectReasonIn="", bool corruptionIn=false);
	bool Invalid(bool ret = false, unsigned char _chRejectCode=0, std::string _strRejectReason="");
	bool Error(std::string strRejectReasonIn="");
	bool Abort(const std::string &msg);
	bool IsValid() const;
	bool IsInvalid() const;
	bool IsError() const;
	bool IsInvalid(int &nDoSOut) const;
	bool CorruptionPossible() const;
	unsigned char GetRejectCode() const;
	std::string GetRejectReason() const;
};

#endif // CVALIDATIONSTATE_H
