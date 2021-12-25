#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>

#include "rpcserver.h"
#include "init.h" // pwalletMain
#include "cwallet.h"
#include "script.h"
#include "smsg.h"
#include "smsg_const.h"
#include "smsg_extern.h"
#include "smsg/bucket.h"
#include "smsg/options.h"
#include "smsg/address.h"
#include "smsg/db.h"
#include "smsg/stored.h"
#include "smsg/messagedata.h"
#include "thread.h"
#include "util.h"
#include "base58.h"
#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"

json_spirit::Value smsgenable(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0)
	{
		throw std::runtime_error("smsgenable \nEnable secure messaging.");
	}

	if (DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is already enabled.");
	}

	json_spirit::Object result;

	if (!DigitalNote::SMSG::Enable())
	{
		result.push_back(json_spirit::Pair("result", "Failed to enable secure messaging."));
	}
	else
	{
		result.push_back(json_spirit::Pair("result", "Enabled secure messaging."));
	}

	return result;
}

json_spirit::Value smsgdisable(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0)
	{
		throw std::runtime_error("smsgdisable \nDisable secure messaging.");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is already disabled.");
	}

	json_spirit::Object result;

	if (!DigitalNote::SMSG::Disable())
	{
		result.push_back(json_spirit::Pair("result", "Failed to disable secure messaging."));
	}
	else
	{
		result.push_back(json_spirit::Pair("result", "Disabled secure messaging."));
	}

	return result;
}

json_spirit::Value smsgoptions(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() > 3)
	{
		throw std::runtime_error("smsgoptions [list|set <optname> <value>]\nList and manage options.");
	}

	std::string mode = "list";

	if (params.size() > 0)
	{
		mode = params[0].get_str();
	}

	json_spirit::Object result;

	if (mode == "list")
	{
		result.push_back(json_spirit::Pair("option", std::string("newAddressRecv = ") + (DigitalNote::SMSG::ext_options.fNewAddressRecv ? "true" : "false")));
		result.push_back(json_spirit::Pair("option", std::string("newAddressAnon = ") + (DigitalNote::SMSG::ext_options.fNewAddressAnon ? "true" : "false")));

		result.push_back(json_spirit::Pair("result", "Success."));
	}
	else if (mode == "set")
	{
		if (params.size() < 3)
		{
			result.push_back(json_spirit::Pair("result", "Too few parameters."));
			result.push_back(json_spirit::Pair("expected", "set <optname> <value>"));
			
			return result;
		}
		
		std::string optname = params[1].get_str();
		std::string value   = params[2].get_str();
		
		if (optname == "newAddressRecv")
		{
			if (value == "+" || value == "on"  || value == "true"  || value == "1")
			{
				DigitalNote::SMSG::ext_options.fNewAddressRecv = true;
			}
			else if (value == "-" || value == "off" || value == "false" || value == "0")
			{
				DigitalNote::SMSG::ext_options.fNewAddressRecv = false;
			}
			else
			{
				result.push_back(json_spirit::Pair("result", "Unknown value."));
				
				return result;
			}
			
			result.push_back(json_spirit::Pair("set option", std::string("newAddressRecv = ") + (DigitalNote::SMSG::ext_options.fNewAddressRecv ? "true" : "false")));
		}
		else if (optname == "newAddressAnon")
		{
			if (value == "+" || value == "on"  || value == "true"  || value == "1")
			{
				DigitalNote::SMSG::ext_options.fNewAddressAnon = true;
			}
			else if (value == "-" || value == "off" || value == "false" || value == "0")
			{
				DigitalNote::SMSG::ext_options.fNewAddressAnon = false;
			}
			else
			{
				result.push_back(json_spirit::Pair("result", "Unknown value."));
				
				return result;
			}
			
			result.push_back(json_spirit::Pair("set option", std::string("newAddressAnon = ") + (DigitalNote::SMSG::ext_options.fNewAddressAnon ? "true" : "false")));
		}
		else
		{
			result.push_back(json_spirit::Pair("result", "Option not found."));
			return result;
		}
	}
	else
	{
		result.push_back(json_spirit::Pair("result", "Unknown Mode."));
		result.push_back(json_spirit::Pair("expected", "smsgoption [list|set <optname> <value>]"));
	}

	return result;
}

json_spirit::Value smsglocalkeys(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() > 3)
	{
		throw std::runtime_error("smsglocalkeys [whitelist|all|wallet|recv <+/-> <address>|anon <+/-> <address>]\nList and manage keys.");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is disabled.");
	}

	json_spirit::Object result;
	std::string mode = "whitelist";

	if (params.size() > 0)
	{
		mode = params[0].get_str();
	}

	char cbuf[256];

	if (mode == "whitelist" || mode == "all")
	{
		uint32_t nKeys = 0;
		int all = mode == "all" ? 1 : 0;
		
		for (std::vector<DigitalNote::SMSG::Address>::iterator it = DigitalNote::SMSG::ext_addresses.begin(); it != DigitalNote::SMSG::ext_addresses.end(); ++it)
		{
			if (!all && !it->fReceiveEnabled)
			{
				continue;
			}
			
			CDigitalNoteAddress coinAddress(it->sAddress);
			
			if (!coinAddress.IsValid())
			{
				continue;
			}
			
			std::string sPublicKey;
			CKeyID keyID;
			
			if (!coinAddress.GetKeyID(keyID))
			{
				continue;
			}
			
			CPubKey pubKey;
			if (!pwalletMain->GetPubKey(keyID, pubKey))
			{
				continue;
			}
			
			if (!pubKey.IsValid() || !pubKey.IsCompressed())
			{
				continue;
			}
			
			sPublicKey = EncodeBase58(pubKey.Raw());
			
			std::string sLabel = pwalletMain->mapAddressBook[keyID];
			std::string sInfo;
			
			if (all)
			{
				sInfo = std::string("Receive ") + (it->fReceiveEnabled ? "on,  " : "off, ");
			}
			
			sInfo += std::string("Anon ") + (it->fReceiveAnon ? "on" : "off");
			result.push_back(json_spirit::Pair("key", it->sAddress + " - " + sPublicKey + " " + sInfo + " - " + sLabel));
			
			nKeys++;
		}
		
		
		snprintf(cbuf, sizeof(cbuf), "%u keys listed.", nKeys);
		result.push_back(json_spirit::Pair("result", std::string(cbuf)));
	}
	else if (mode == "recv")
	{
		if (params.size() < 3)
		{
			result.push_back(json_spirit::Pair("result", "Too few parameters."));
			result.push_back(json_spirit::Pair("expected", "recv <+/-> <address>"));
			
			return result;
		}
		
		std::string op      = params[1].get_str();
		std::string addr    = params[2].get_str();
		
		std::vector<DigitalNote::SMSG::Address>::iterator it;
		
		for (it = DigitalNote::SMSG::ext_addresses.begin(); it != DigitalNote::SMSG::ext_addresses.end(); ++it)
		{
			if (addr != it->sAddress)
			{
				continue;
			}
		
			break;
		}
		
		if (it == DigitalNote::SMSG::ext_addresses.end())
		{
			result.push_back(json_spirit::Pair("result", "Address not found."));
			
			return result;
		};
		
		if (op == "+" || op == "on"  || op == "add" || op == "a")
		{
			it->fReceiveEnabled = true;
		}
		else if (op == "-" || op == "off" || op == "rem" || op == "r")
		{
			it->fReceiveEnabled = false;
		}
		else
		{
			result.push_back(json_spirit::Pair("result", "Unknown operation."));
			
			return result;
		}
		
		std::string sInfo;
		
		sInfo = std::string("Receive ") + (it->fReceiveEnabled ? "on, " : "off,");
		sInfo += std::string("Anon ") + (it->fReceiveAnon ? "on" : "off");
		
		result.push_back(json_spirit::Pair("result", "Success."));
		result.push_back(json_spirit::Pair("key", it->sAddress + " " + sInfo));
		
		return result;
	}
	else if (mode == "anon")
	{
		if (params.size() < 3)
		{
			result.push_back(json_spirit::Pair("result", "Too few parameters."));
			result.push_back(json_spirit::Pair("expected", "anon <+/-> <address>"));
			
			return result;
		}
		
		std::string op      = params[1].get_str();
		std::string addr    = params[2].get_str();
		
		std::vector<DigitalNote::SMSG::Address>::iterator it;
		
		for (it = DigitalNote::SMSG::ext_addresses.begin(); it != DigitalNote::SMSG::ext_addresses.end(); ++it)
		{
			if (addr != it->sAddress)
			{
				continue;
			}
			
			break;
		}
		
		if (it == DigitalNote::SMSG::ext_addresses.end())
		{
			result.push_back(json_spirit::Pair("result", "Address not found."));
			return result;
		}
		
		if (op == "+" || op == "on"  || op == "add" || op == "a")
		{
			it->fReceiveAnon = true;
		}
		else if (op == "-" || op == "off" || op == "rem" || op == "r")
		{
			it->fReceiveAnon = false;
		}
		else
		{
			result.push_back(json_spirit::Pair("result", "Unknown operation."));
			
			return result;
		}
		
		std::string sInfo;
		
		sInfo = std::string("Receive ") + (it->fReceiveEnabled ? "on, " : "off,");
		sInfo += std::string("Anon ") + (it->fReceiveAnon ? "on" : "off");
		
		result.push_back(json_spirit::Pair("result", "Success."));
		result.push_back(json_spirit::Pair("key", it->sAddress + " " + sInfo));
		
		return result;
		
	}
	else if (mode == "wallet")
	{
		uint32_t nKeys = 0;
		
		for(const std::pair<CTxDestination, std::string>& entry : pwalletMain->mapAddressBook)
		{
			if (!IsMine(*pwalletMain, entry.first))
			{
				continue;
			}
			
			CDigitalNoteAddress coinAddress(entry.first);
			
			if (!coinAddress.IsValid())
			{
				continue;
			}
			
			std::string address;
			std::string sPublicKey;
			address = coinAddress.ToString();
			
			CKeyID keyID;
			if (!coinAddress.GetKeyID(keyID))
			{
				continue;
			}
			
			CPubKey pubKey;
			if (!pwalletMain->GetPubKey(keyID, pubKey))
			{
				continue;
			}
			
			if (!pubKey.IsValid() || !pubKey.IsCompressed())
			{
				continue;
			}
			
			sPublicKey = EncodeBase58(pubKey.Raw());
			
			result.push_back(json_spirit::Pair("key", address + " - " + sPublicKey + " - " + entry.second));
			nKeys++;
		}
		
		snprintf(cbuf, sizeof(cbuf), "%u keys listed from wallet.", nKeys);
		
		result.push_back(json_spirit::Pair("result", std::string(cbuf)));
	}
	else
	{
		result.push_back(json_spirit::Pair("result", "Unknown Mode."));
		result.push_back(json_spirit::Pair("expected", "smsglocalkeys [whitelist|all|wallet|recv <+/-> <address>|anon <+/-> <address>]"));
	}

	return result;
}

json_spirit::Value smsgscanchain(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0)
	{
		throw std::runtime_error("smsgscanchain \nLook for public keys in the block chain.");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is disabled.");
	}

	json_spirit::Object result;

	if (!DigitalNote::SMSG::ScanBlockChain())
	{
		result.push_back(json_spirit::Pair("result", "Scan Chain Failed."));
	}
	else
	{
		result.push_back(json_spirit::Pair("result", "Scan Chain Completed."));
	}

	return result;
}

json_spirit::Value smsgscanbuckets(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0)
	{
		throw std::runtime_error("smsgscanbuckets \nForce rescan of all messages in the bucket store.");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is disabled.");
	}

	if (pwalletMain->IsLocked())
	{
		throw std::runtime_error("Wallet is locked.");
	}

	json_spirit::Object result;
	if (!DigitalNote::SMSG::ScanBuckets())
	{
		result.push_back(json_spirit::Pair("result", "Scan Buckets Failed."));
	}
	else
	{
		result.push_back(json_spirit::Pair("result", "Scan Buckets Completed."));
	}

	return result;
}

json_spirit::Value smsgaddkey(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 2)
	{
		throw std::runtime_error("smsgaddkey <address> <pubkey>\nAdd address, pubkey pair to database.");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is disabled.");
	}

	std::string addr = params[0].get_str();
	std::string pubk = params[1].get_str();

	json_spirit::Object result;
	int rv = DigitalNote::SMSG::AddAddress(addr, pubk);

	if (rv != 0)
	{
		result.push_back(json_spirit::Pair("result", "Public key not added to db."));
		
		switch (rv)
		{
			case 2:
				result.push_back(json_spirit::Pair("reason", "publicKey is invalid."));
				break;
			
			case 3:
				result.push_back(json_spirit::Pair("reason", "publicKey does not match address."));
				break;
			
			case 4:
				result.push_back(json_spirit::Pair("reason", "address is already in db."));
				break;
			
			case 5:
				result.push_back(json_spirit::Pair("reason", "address is invalid."));
				break;
			
			default:
				result.push_back(json_spirit::Pair("reason", "error."));
				break;
		}
	}
	else
	{
		result.push_back(json_spirit::Pair("result", "Added public key to db."));
	}

	return result;
}

json_spirit::Value smsggetpubkey(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 1)
	{
		throw std::runtime_error(
			"smsggetpubkey <address>\n"
			"Return the base58 encoded compressed public key for an address.\n"
			"Tests localkeys first, then looks in public key db.\n");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is disabled.");
	}

	std::string address   = params[0].get_str();
	std::string publicKey;

	json_spirit::Object result;
	int rv = DigitalNote::SMSG::GetLocalPublicKey(address, publicKey);

	switch (rv)
	{
		case 0:
			result.push_back(json_spirit::Pair("result", "Success."));
			result.push_back(json_spirit::Pair("address in wallet", address));
			result.push_back(json_spirit::Pair("compressed public key", publicKey));
			
			return result; // success, don't check db
		
		case 2:
		case 3:
			result.push_back(json_spirit::Pair("result", "Failed."));
			result.push_back(json_spirit::Pair("message", "Invalid address."));
			
			return result;
		
		case 4:
			break; // check db
		
		//case 1:
		default:
			result.push_back(json_spirit::Pair("result", "Failed."));
			result.push_back(json_spirit::Pair("message", "Error."));
			
			return result;
	}

	CDigitalNoteAddress coinAddress(address);
	CKeyID keyID;

	if (!coinAddress.GetKeyID(keyID))
	{
		result.push_back(json_spirit::Pair("result", "Failed."));
		result.push_back(json_spirit::Pair("message", "Invalid address."));
		
		return result;
	}

	CPubKey cpkFromDB;
	rv = DigitalNote::SMSG::GetStoredKey(keyID, cpkFromDB);

	switch (rv)
	{
		case 0:
			if (!cpkFromDB.IsValid() || !cpkFromDB.IsCompressed())
			{
				result.push_back(json_spirit::Pair("result", "Failed."));
				result.push_back(json_spirit::Pair("message", "Invalid address."));
			}
			else
			{
				//cpkFromDB.SetCompressedPubKey(); // make sure key is compressed
				publicKey = EncodeBase58(cpkFromDB.Raw());
				
				result.push_back(json_spirit::Pair("result", "Success."));
				result.push_back(json_spirit::Pair("peer address in DB", address));
				result.push_back(json_spirit::Pair("compressed public key", publicKey));
			}
			
			break;
		
		case 2:
			result.push_back(json_spirit::Pair("result", "Failed."));
			result.push_back(json_spirit::Pair("message", "Address not found in wallet or db."));
			
			return result;
		
		//case 1:
		default:
			result.push_back(json_spirit::Pair("result", "Failed."));
			result.push_back(json_spirit::Pair("message", "Error, GetStoredKey()."));
			
			return result;
	}

	return result;
}

json_spirit::Value smsgsend(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 3)
	{
		throw std::runtime_error(
			"smsgsend <addrFrom> <addrTo> <message>\n"
			"Send an encrypted message from addrFrom to addrTo.");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is disabled.");
	}

	std::string addrFrom  = params[0].get_str();
	std::string addrTo    = params[1].get_str();
	std::string msg       = params[2].get_str();

	json_spirit::Object result;
	std::string sError;

	if (DigitalNote::SMSG::Send(addrFrom, addrTo, msg, sError) != 0)
	{
		result.push_back(json_spirit::Pair("result", "Send failed."));
		result.push_back(json_spirit::Pair("error", sError));
	}
	else
	{
		result.push_back(json_spirit::Pair("result", "Sent."));
	}

	return result;
}

json_spirit::Value smsgsendanon(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 2)
	{
		throw std::runtime_error(
			"smsgsendanon <addrTo> <message>\n"
			"Send an anonymous encrypted message to addrTo.");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is disabled.");
	}

	std::string addrFrom  = "anon";
	std::string addrTo    = params[0].get_str();
	std::string msg       = params[1].get_str();

	json_spirit::Object result;
	std::string sError;

	if (DigitalNote::SMSG::Send(addrFrom, addrTo, msg, sError) != 0)
	{
		result.push_back(json_spirit::Pair("result", "Send failed."));
		result.push_back(json_spirit::Pair("error", sError));
	}
	else
	{
		result.push_back(json_spirit::Pair("result", "Sent."));
	}

	return result;
}

json_spirit::Value smsginbox(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() > 1) // defaults to read
	{
		throw std::runtime_error(
			"smsginbox [all|unread|clear]\n" 
			"Decrypt and display all received messages.\n"
			"Warning: clear will delete all messages.");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is disabled.");
	}

	if (pwalletMain->IsLocked())
	{
		throw std::runtime_error("Wallet is locked.");
	}

	std::string mode = "unread";
	json_spirit::Object result;

	if (params.size() > 0)
	{
		mode = params[0].get_str();
	}

	{
		LOCK(DigitalNote::SMSG::ext_cs_db);
		
		DigitalNote::SMSG::DB dbInbox;
		
		if (!dbInbox.Open("cr+"))
		{
			throw std::runtime_error("Could not open DB.");
		}
		
		uint32_t nMessages = 0;
		char cbuf[256];
		std::string sPrefix("im");
		unsigned char chKey[18];
		
		if (mode == "clear")
		{
			dbInbox.TxnBegin();
			
			leveldb::Iterator* it = dbInbox.pdb->NewIterator(leveldb::ReadOptions());
			
			while (dbInbox.NextSmesgKey(it, sPrefix, chKey))
			{
				dbInbox.EraseSmesg(chKey);
				nMessages++;
			}
			
			delete it;
			
			dbInbox.TxnCommit();
			
			snprintf(cbuf, sizeof(cbuf), "Deleted %u messages.", nMessages);
			result.push_back(json_spirit::Pair("result", std::string(cbuf)));
		}
		else if (mode == "all" || mode == "unread")
		{
			int fCheckReadStatus = mode == "unread" ? 1 : 0;
			
			DigitalNote::SMSG::Stored smsgStored;
			DigitalNote::SMSG::Message msg;
			
			dbInbox.TxnBegin();
			
			leveldb::Iterator* it = dbInbox.pdb->NewIterator(leveldb::ReadOptions());
			
			while (dbInbox.NextSmesg(it, sPrefix, chKey, smsgStored))
			{
				if (fCheckReadStatus && !(smsgStored.status & SMSG_MASK_UNREAD))
				{
					continue;
				}
				
				uint32_t nPayload = smsgStored.vchMessage.size() - SMSG_HDR_LEN;
				
				if (DigitalNote::SMSG::Decrypt(false, smsgStored.sAddrTo, &smsgStored.vchMessage[0],
						&smsgStored.vchMessage[SMSG_HDR_LEN], nPayload, msg) == 0)
				{
					json_spirit::Object objM;
					
					objM.push_back(json_spirit::Pair("received", getTimeString(smsgStored.timeReceived, cbuf, sizeof(cbuf))));
					objM.push_back(json_spirit::Pair("sent", getTimeString(msg.timestamp, cbuf, sizeof(cbuf))));
					objM.push_back(json_spirit::Pair("from", msg.sFromAddress));
					objM.push_back(json_spirit::Pair("to", smsgStored.sAddrTo));
					objM.push_back(json_spirit::Pair("text", std::string((char*)&msg.vchMessage[0]))); // ugh
					
					result.push_back(json_spirit::Pair("message", objM));
				}
				else
				{
					result.push_back(json_spirit::Pair("message", "Could not decrypt."));
				}
				
				if (fCheckReadStatus)
				{
					smsgStored.status &= ~SMSG_MASK_UNREAD;
					dbInbox.WriteSmesg(chKey, smsgStored);
				}
				
				nMessages++;
			}
			
			delete it;
			
			dbInbox.TxnCommit();
			
			snprintf(cbuf, sizeof(cbuf), "%u messages shown.", nMessages);
			result.push_back(json_spirit::Pair("result", std::string(cbuf)));
		}
		else
		{
			result.push_back(json_spirit::Pair("result", "Unknown Mode."));
			result.push_back(json_spirit::Pair("expected", "[all|unread|clear]."));
		}
	}

	return result;
}

json_spirit::Value smsgoutbox(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() > 1) // defaults to read
	{
		throw std::runtime_error(
			"smsgoutbox [all|clear]\n" 
			"Decrypt and display all sent messages.\n"
			"Warning: clear will delete all sent messages.");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is disabled.");
	}

	if (pwalletMain->IsLocked())
	{
		throw std::runtime_error("Wallet is locked.");
	}

	std::string mode = "all";

	if (params.size() > 0)
	{
		mode = params[0].get_str();
	}

	json_spirit::Object result;
	std::string sPrefix("sm");
	unsigned char chKey[18];

	memset(&chKey[0], 0, 18);

	{
		LOCK(DigitalNote::SMSG::ext_cs_db);
		
		DigitalNote::SMSG::DB dbOutbox;
		
		if (!dbOutbox.Open("cr+"))
		{
			throw std::runtime_error("Could not open DB.");
		}
		
		uint32_t nMessages = 0;
		char cbuf[256];
		
		if (mode == "clear")
		{
			dbOutbox.TxnBegin();
			
			leveldb::Iterator* it = dbOutbox.pdb->NewIterator(leveldb::ReadOptions());
			
			while (dbOutbox.NextSmesgKey(it, sPrefix, chKey))
			{
				dbOutbox.EraseSmesg(chKey);
				nMessages++;
			}
			
			delete it;
			
			dbOutbox.TxnCommit();
			
			snprintf(cbuf, sizeof(cbuf), "Deleted %u messages.", nMessages);
			
			result.push_back(json_spirit::Pair("result", std::string(cbuf)));
		}
		else if (mode == "all")
		{
			DigitalNote::SMSG::Stored smsgStored;
			DigitalNote::SMSG::Message msg;
			
			leveldb::Iterator* it = dbOutbox.pdb->NewIterator(leveldb::ReadOptions());
			
			while (dbOutbox.NextSmesg(it, sPrefix, chKey, smsgStored))
			{
				uint32_t nPayload = smsgStored.vchMessage.size() - SMSG_HDR_LEN;
				
				if (DigitalNote::SMSG::Decrypt(false, smsgStored.sAddrOutbox, &smsgStored.vchMessage[0],
						&smsgStored.vchMessage[SMSG_HDR_LEN], nPayload, msg) == 0)
				{
					json_spirit::Object objM;
					
					objM.push_back(json_spirit::Pair("sent", getTimeString(msg.timestamp, cbuf, sizeof(cbuf))));
					objM.push_back(json_spirit::Pair("from", msg.sFromAddress));
					objM.push_back(json_spirit::Pair("to", smsgStored.sAddrTo));
					objM.push_back(json_spirit::Pair("text", std::string((char*)&msg.vchMessage[0]))); // ugh
					
					result.push_back(json_spirit::Pair("message", objM));
				}
				else
				{
					result.push_back(json_spirit::Pair("message", "Could not decrypt."));
				}
				
				nMessages++;
			};
			delete it;
			
			snprintf(cbuf, sizeof(cbuf), "%u sent messages shown.", nMessages);
			
			result.push_back(json_spirit::Pair("result", std::string(cbuf)));
		}
		else
		{
			result.push_back(json_spirit::Pair("result", "Unknown Mode."));
			result.push_back(json_spirit::Pair("expected", "[all|clear]."));
		}
	}

	return result;
}

json_spirit::Value smsgbuckets(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() > 1)
	{
		throw std::runtime_error(
			"smsgbuckets [stats|dump]\n"
			"Display some statistics.");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is disabled.");
	}

	std::string mode = "stats";
	json_spirit::Object result;
	char cbuf[256];

	if (params.size() > 0)
	{
		mode = params[0].get_str();
	}

	if (mode == "stats")
	{
		uint32_t nBuckets = 0;
		uint32_t nMessages = 0;
		uint64_t nBytes = 0;
		
		{
			LOCK(DigitalNote::SMSG::ext_cs);
			
			std::map<int64_t, DigitalNote::SMSG::Bucket>::iterator it;
			
			for (it = DigitalNote::SMSG::ext_buckets.begin(); it != DigitalNote::SMSG::ext_buckets.end(); ++it)
			{
				std::set<DigitalNote::SMSG::Token>& tokenSet = it->second.setTokens;
				
				std::string sBucket = boost::lexical_cast<std::string>(it->first);
				std::string sFile = sBucket + "_01.dat";
				
				snprintf(cbuf, sizeof(cbuf), "%" PRIszu, tokenSet.size());
				std::string snContents(cbuf);
				
				std::string sHash = boost::lexical_cast<std::string>(it->second.hash);
				
				nBuckets++;
				nMessages += tokenSet.size();
				
				json_spirit::Object objM;
				
				objM.push_back(json_spirit::Pair("bucket", sBucket));
				objM.push_back(json_spirit::Pair("time", getTimeString(it->first, cbuf, sizeof(cbuf))));
				objM.push_back(json_spirit::Pair("no. messages", snContents));
				objM.push_back(json_spirit::Pair("hash", sHash));
				objM.push_back(json_spirit::Pair("last changed", getTimeString(it->second.timeChanged, cbuf, sizeof(cbuf))));
				
				boost::filesystem::path fullPath = GetDataDir() / "smsgStore" / sFile;


				if (!boost::filesystem::exists(fullPath))
				{
					// -- If there is a file for an empty bucket something is wrong.
					if (tokenSet.size() == 0)
					{
						objM.push_back(json_spirit::Pair("file size", "Empty bucket."));
					}
					else
					{
						objM.push_back(json_spirit::Pair("file size, error", "File not found."));
					}
				}
				else
				{
					try
					{    
						uint64_t nFBytes = 0;
						nFBytes = boost::filesystem::file_size(fullPath);
						nBytes += nFBytes;
						objM.push_back(json_spirit::Pair("file size", bytesReadable(nFBytes)));
					}
					catch (const boost::filesystem::filesystem_error& ex)
					{
						objM.push_back(json_spirit::Pair("file size, error", ex.what()));
					}
				}
				
				result.push_back(json_spirit::Pair("bucket", objM));
			}
		}
		
		std::string snBuckets = boost::lexical_cast<std::string>(nBuckets);
		std::string snMessages = boost::lexical_cast<std::string>(nMessages);
		
		json_spirit::Object objM;
		
		objM.push_back(json_spirit::Pair("buckets", snBuckets));
		objM.push_back(json_spirit::Pair("messages", snMessages));
		objM.push_back(json_spirit::Pair("size", bytesReadable(nBytes)));
		
		result.push_back(json_spirit::Pair("total", objM));
	}
	else if (mode == "dump")
	{
		{
			LOCK(DigitalNote::SMSG::ext_cs);
			
			std::map<int64_t, DigitalNote::SMSG::Bucket>::iterator it;
			
			for (it = DigitalNote::SMSG::ext_buckets.begin(); it != DigitalNote::SMSG::ext_buckets.end(); ++it)
			{
				std::string sFile = boost::lexical_cast<std::string>(it->first) + "_01.dat";
				
				try
				{
					boost::filesystem::path fullPath = GetDataDir() / "smsgStore" / sFile;
					boost::filesystem::remove(fullPath);
				}
				catch (const boost::filesystem::filesystem_error& ex)
				{
					//objM.push_back(json_spirit::Pair("file size, error", ex.what()));
					printf("Error removing bucket file %s.\n", ex.what());
				}
			}
			
			DigitalNote::SMSG::ext_buckets.clear();
		}
		
		result.push_back(json_spirit::Pair("result", "Removed all buckets."));	
	}
	else
	{
		result.push_back(json_spirit::Pair("result", "Unknown Mode."));
		result.push_back(json_spirit::Pair("expected", "[stats|dump]."));
	}

	return result;
}

json_spirit::Value smsggetmessagesforaccount(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() > 1)
	{
		throw std::runtime_error(
				"smsggetmessagesforaccount \"account\" [all|unread]\n"
				"Decrypt and display all messages for an account."
				"smsggetmessagesforaccount \"account\"\n"
				"\nReturns decrypted and display all messages for an account.\n"
				"\nArguments:\n"
				"1. \"account\"  (string, required) The account name.\n");
	}

	if (!DigitalNote::SMSG::ext_enabled)
	{
		throw std::runtime_error("Secure messaging is disabled.");
	}

	if (pwalletMain->IsLocked())
	{
		throw std::runtime_error("Wallet is locked.");
	}

	std::string strAccount;

	if (params.size() > 0)
	{
		strAccount = params[0].get_str();
	}
	else
	{
		throw std::runtime_error("Account is required.");
	}

	json_spirit::Object result;
	uint32_t nMessages = 0;
	json_spirit::Array resMessagesIn;
	json_spirit::Array resMessagesOut;
	std::vector<std::string> accountAddresses;
	char cbuf[256];

	// Find all addresses that have the given account
	for(const std::pair<CDigitalNoteAddress, std::string>& item : pwalletMain->mapAddressBook)
	{
		const CDigitalNoteAddress& address = item.first;
		const std::string& strName = item.second;
		
		if (strName == strAccount)
		{
			accountAddresses.push_back(address.ToString());
		}
	}

	// get 'in' messages
	{
		LOCK(DigitalNote::SMSG::ext_cs_db);

		DigitalNote::SMSG::DB dbInbox;

		if (!dbInbox.Open("cr+"))
		{
			throw std::runtime_error("Could not open DB.");
		}
		
		std::string sPrefix("im");
		unsigned char chKey[18];
		DigitalNote::SMSG::Stored smsgStored;
		DigitalNote::SMSG::Message msg;

		dbInbox.TxnBegin();

		leveldb::Iterator *it = dbInbox.pdb->NewIterator(leveldb::ReadOptions());
		
		while(dbInbox.NextSmesg(it, sPrefix, chKey, smsgStored))
		{
			uint32_t nPayload = smsgStored.vchMessage.size() - SMSG_HDR_LEN;
			
			if (DigitalNote::SMSG::Decrypt(false, smsgStored.sAddrTo, &smsgStored.vchMessage[0],
					&smsgStored.vchMessage[SMSG_HDR_LEN], nPayload, msg) == 0)
			{
				bool oneOfTheAccountMessages = false;
				
				for (std::string accountAddress : accountAddresses)
				{
					if (smsgStored.sAddrTo == accountAddress)
					{
						oneOfTheAccountMessages = true;
						
						break;
					}
				}

				if (oneOfTheAccountMessages == false)
				{
					continue;
				}

				json_spirit::Object objM;
				
				objM.push_back(json_spirit::Pair("received", getTimeString(smsgStored.timeReceived, cbuf, sizeof(cbuf))));
				objM.push_back(json_spirit::Pair("sent", getTimeString(msg.timestamp, cbuf, sizeof(cbuf))));
				objM.push_back(json_spirit::Pair("from", msg.sFromAddress));
				objM.push_back(json_spirit::Pair("to", smsgStored.sAddrTo));
				objM.push_back(json_spirit::Pair("text", std::string((char *) &msg.vchMessage[0]))); // ugh

				resMessagesIn.push_back(objM);
			}
			else
			{
				json_spirit::Object objM;
				
				objM.push_back(json_spirit::Pair("message", "Could not decrypt."));
				
				resMessagesIn.push_back(objM);
			}

			nMessages++;
		}
		
		delete it;
		
		dbInbox.TxnCommit();
	}

	// get 'out' messages
	{
		LOCK(DigitalNote::SMSG::ext_cs_db);

		DigitalNote::SMSG::DB dbOutbox;
		std::string sPrefix("sm");
		unsigned char chKey[18];
		
		memset(&chKey[0], 0, 18);

		if (!dbOutbox.Open("cr+"))
		{
			throw std::runtime_error("Could not open DB.");
		}
		
		DigitalNote::SMSG::Stored smsgStored;
		DigitalNote::SMSG::Message msg;
		
		leveldb::Iterator *it = dbOutbox.pdb->NewIterator(leveldb::ReadOptions());
		
		while (dbOutbox.NextSmesg(it, sPrefix, chKey, smsgStored))
		{
			uint32_t nPayload = smsgStored.vchMessage.size() - SMSG_HDR_LEN;
			
			if (DigitalNote::SMSG::Decrypt(false, smsgStored.sAddrOutbox, &smsgStored.vchMessage[0],
					&smsgStored.vchMessage[SMSG_HDR_LEN], nPayload, msg) == 0)
			{
				bool oneOfTheAccountMessages = false;
				
				for (std::string accountAddress : accountAddresses)
				{
					if (fDebugSmsg)
					{
						LogPrint("smsg", "Comparing from address with account addresses %s vs %s \n", accountAddress, msg.sFromAddress);
					}
					
					if (msg.sFromAddress == accountAddress)
					{
						oneOfTheAccountMessages = true;
						
						break;
					}
				}
				
				if (oneOfTheAccountMessages == false)
				{
					continue;
				}

				json_spirit::Object objM;
				
				objM.push_back(json_spirit::Pair("sent", getTimeString(msg.timestamp, cbuf, sizeof(cbuf))));
				objM.push_back(json_spirit::Pair("from", msg.sFromAddress));
				objM.push_back(json_spirit::Pair("to", smsgStored.sAddrTo));
				objM.push_back(json_spirit::Pair("text", std::string((char *) &msg.vchMessage[0])));

				resMessagesOut.push_back(objM);
			}
			else
			{
				json_spirit::Object objM;
				
				objM.push_back(json_spirit::Pair("message", "Could not decrypt."));
				
				resMessagesOut.push_back(objM);
			}
			
			nMessages++;
		}
		
		delete it;
	}

	result.push_back(json_spirit::Pair("messagesIn", resMessagesIn));
	result.push_back(json_spirit::Pair("messagesOut", resMessagesOut));

	snprintf(cbuf, sizeof(cbuf), "%u messages shown.", nMessages);
	result.push_back(json_spirit::Pair("result", std::string(cbuf)));

	return result;
}

