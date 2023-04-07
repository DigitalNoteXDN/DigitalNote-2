#ifndef NET_BAN_REASON_H
#define NET_BAN_REASON_H

typedef enum BanReason
{
	BanReasonUnknown = 0,
	BanReasonNodeMisbehaving = 1,
	BanReasonManuallyAdded = 2
} BanReason;

#endif // NET_BAN_REASON_H
