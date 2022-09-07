##
##	Ubuntu 22.04 doesn't automaticly install the developers version by default.
##	You have to manually execute:
##		sudo apt-get install -y libminiupnpc-dev
##

contains(USE_UBUNTU2204_MINIUPNPC, 1) {
	DEFINES += UBUNTU2204_MINIUPNPC
}