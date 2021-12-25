#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include "rpcclient.h"
#include "init.h"
#include "util.h"
#include "main_extern.h"
#include "chainparams.h"
#include "noui.h"
#include "ui_translate.h"
#include "fork.h"

void WaitForShutdown(boost::thread_group* threadGroup)
{
    bool fShutdown = ShutdownRequested();
	
    // Tell the main threads to shutdown.
    while (!fShutdown)
    {
        MilliSleep(200);
        fShutdown = ShutdownRequested();
    }
	
    if (threadGroup)
    {
        threadGroup->interrupt_all();
        threadGroup->join_all();
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
bool AppInit(int argc, char* argv[])
{
	boost::thread_group threadGroup;
	bool fRet = false;

	fHaveGUI = false;

	try
	{
		//
		// Parameters
		//
		// If Qt is used, parameters/bitcoin.conf are parsed in qt/bitcoin.cpp's main()
		ParseParameters(argc, argv);
		
		if (!boost::filesystem::is_directory(GetDataDir(false)))
		{
			fprintf(stderr, "Error: Specified directory does not exist\n");
			
			Shutdown();
		}
		
		ReadConfigFile(mapArgs, mapMultiArgs);

		if (mapArgs.count("-?") || mapArgs.count("--help"))
		{
			// First part of help message is specific to bitcoind / RPC client
			std::string strUsage = ui_translate("DigitalNote version") + " " + FormatFullVersion() + "\n\n" +
				ui_translate("Usage:") + "\n" +
				  "  DigitalNoted [options]                     " + "\n" +
				  "  DigitalNoted [options] <command> [params]  " + ui_translate("Send command to -server or DigitalNoted") + "\n" +
				  "  DigitalNoted [options] help                " + ui_translate("List commands") + "\n" +
				  "  DigitalNoted [options] help <command>      " + ui_translate("Get help for a command") + "\n";

			strUsage += "\n" + HelpMessage();

			fprintf(stdout, "%s", strUsage.c_str());
			
			return false;
		}

		// Command-line RPC
		for (int i = 1; i < argc; i++)
		{
			if (!IsSwitchChar(argv[i][0]) && !boost::algorithm::istarts_with(argv[i], "DigitalNote:"))
			{
				fCommandLine = true;
			}
		}
		
		if (fCommandLine)
		{
			if (!SelectParamsFromCommandLine())
			{
				fprintf(stderr, "Error: invalid combination of -regtest and -testnet.\n");
				
				return false;
			}
			
			int ret = CommandLineRPC(argc, argv);
			
			exit(ret);
		}
		
	#if !WIN32
		fDaemon = GetBoolArg("-daemon", false);
		
		if (fDaemon)
		{
			// Daemonize
			pid_t pid = fork();
			
			if (pid < 0)
			{
				fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
				
				return false;
			}
			
			if (pid > 0) // Parent process, pid is child process id
			{
				CreatePidFile(GetPidFile(), pid);
				
				return true;
			}
			
			// Child process falls through to rest of initialization
			pid_t sid = setsid();
			
			if (sid < 0)
			{
				fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
			}
		}
	#endif

		fRet = AppInit2(threadGroup);
	}
	catch (std::exception& e)
	{
		PrintException(&e, "AppInit()");
	}
	catch (...)
	{
		PrintException(NULL, "AppInit()");
	}

	if (!fRet)
	{
		threadGroup.interrupt_all();
		// threadGroup.join_all(); was left out intentionally here, because we didn't re-test all of
		// the startup-failure cases to make sure they don't result in a hang due to some
		// thread-blocking-waiting-for-another-thread-during-startup case
	}
	else
	{
		WaitForShutdown(&threadGroup);
	}

	Shutdown();

	return fRet;
}

int main(int argc, char* argv[])
{
	bool fRet = false;
	
	// Connect bitcoind signal handlers
	noui_connect();

	fRet = AppInit(argc, argv);

	if (fRet && fDaemon)
	{
		return 0;
	}

	return (fRet ? 0 : 1);
}

