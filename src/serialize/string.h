#ifndef SERIALIZE_STRING_H
#define SERIALIZE_STRING_H

#include <string>

template<typename C>
unsigned int GetSerializeSize(const std::basic_string<C>& str, int, int = 0);
template<typename Stream, typename C>
void Serialize(Stream& os, const std::basic_string<C>& str, int, int = 0);
template<typename Stream, typename C>
void Unserialize(Stream& is, std::basic_string<C>& str, int, int = 0);

#endif // SERIALIZE_STRING_H
