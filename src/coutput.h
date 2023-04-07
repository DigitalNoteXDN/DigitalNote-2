#ifndef COUTPUT_H
#define COUTPUT_H

#include <string>

class CWalletTx;

class COutput
{
public:
	const CWalletTx* tx;
	int i;
	int nDepth;
	bool fSpendable;

	COutput(const CWalletTx *txIn, int iIn, int nDepthIn, bool fSpendableIn);

	std::string ToString() const;
	//Used with MNengine. Will return fees, then everything else, then very small inputs that aren't fees
	int Priority() const;
	void print() const;
};

#endif // COUTPUT_H
