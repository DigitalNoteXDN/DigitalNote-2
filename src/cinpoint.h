#ifndef CINPOINT_H
#define CINPOINT_H

class CTransaction;

/** An inpoint - a combination of a transaction and an index n into its vin */
class CInPoint
{
public:
	CTransaction* ptx;
	unsigned int n;

	CInPoint();
	CInPoint(CTransaction* ptxIn, unsigned int nIn);

	void SetNull();
	bool IsNull() const;
};

#endif // CINPOINT_H
