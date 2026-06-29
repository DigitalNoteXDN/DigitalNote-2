#ifndef UTIL_H
#define UTIL_H

#include <string>

template<typename T>
std::string HexStr(const T itbegin, const T itend, bool fSpaces=false);

template<typename T>
std::string HexStr(const T& vch, bool fSpaces=false);

#endif // UTIL_H
