#ifndef SMSG_EXTERN_SIGNAL_H
#define SMSG_EXTERN_SIGNAL_H

#include <boost/signals2/signal.hpp>

#include "json/json_spirit_value.h"

namespace DigitalNote
{
	namespace SMSG
	{
		class Stored;
		
		// Inbox db changed, called with lock DigitalNote::SMSG::ext_cs_db held.
		extern boost::signals2::signal<void (DigitalNote::SMSG::Stored& inboxHdr)>	ext_signal_NotifyInboxChanged;
		extern boost::signals2::signal<void (json_spirit::Object& inboxHdr)>		ext_signal_NotifyInboxChangedJson;
		// Outbox db changed, called with lock DigitalNote::SMSG::ext_cs_db held.
		extern boost::signals2::signal<void (DigitalNote::SMSG::Stored& outboxHdr)>	ext_signal_NotifyOutboxChanged;
		extern boost::signals2::signal<void (json_spirit::Object& outboxHdr)>		ext_signal_NotifyOutboxChangedJson;
		// Wallet Unlocked, called after all messages received while locked have been processed.
		extern boost::signals2::signal<void ()>										ext_signal_NotifyWalletUnlocked;
		
	} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_EXTERN_SIGNAL_H
