##
##	Old version contains version number inside include name
##
##	Ubuntu 22.04 doesn't automaticly install the developers version by default.
##	You have to manually execute:
##		sudo apt-get install -y libminiupnpc-dev
##

contains(USE_MINIUPNPC_VERSION, 1) {
	DEFINES += USE_MINIUPNPC_VERSION
}