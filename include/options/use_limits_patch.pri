##
## missing 
## https://stackoverflow.com/questions/4798936/numeric-limits-was-not-declared-in-this-scope-no-matching-function-for-call-t
##

contains(USE_LIMITS_PATCH, 1) {
	QMAKE_CXXFLAGS_WARN_ON += -include /usr/include/c++/11*/limits
}