#include "compat.h"

#include "init.h"
#include "util.h"
#include "ui_interface.h"
#include "ui_translate.h"

#include <string>

static int noui_ThreadSafeMessageBox(const std::string& message, const std::string& caption, unsigned int style)
{
	std::string strCaption;
	
	// Check for usage of predefined caption
	switch (style)
	{
		case CClientUIInterface::MSG_ERROR:
			strCaption += ui_translate("Error");
			break;
		case CClientUIInterface::MSG_WARNING:
			strCaption += ui_translate("Warning");
			break;
		case CClientUIInterface::MSG_INFORMATION:
			strCaption += ui_translate("Information");
			break;
		default:
			strCaption += caption; // Use supplied caption
	}

	LogPrintf("%s: %s\n", caption, message);
	fprintf(stderr, "%s: %s\n", strCaption.c_str(), message.c_str());
	
	return 4;
}

static bool noui_ThreadSafeAskFee(int64_t nFeeRequired, const std::string& strCaption)
{
	return true;
}

static void noui_InitMessage(const std::string &message)
{
	LogPrintf("init message: %s\n", message);
}

void noui_connect()
{
	// Connect bitcoind signal handlers
	uiInterface.ThreadSafeMessageBox.connect(noui_ThreadSafeMessageBox);
	uiInterface.ThreadSafeAskFee.connect(noui_ThreadSafeAskFee);
	uiInterface.InitMessage.connect(noui_InitMessage);
}
