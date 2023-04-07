#ifndef CTXDSOUT_H
#define CTXDSOUT_H

#include "ctxout.h"

/** Holds an MNengine output
 */
class CTxDSOut : public CTxOut
{
public:
	int nSentTimes;								//times we've sent this anonymously

	CTxDSOut(const CTxOut& out);
};

#endif // CTXDSOUT_H
