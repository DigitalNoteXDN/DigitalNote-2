#ifndef SERIALIZE_PAIR_H
#define SERIALIZE_PAIR_H

#include <utility>

template<typename K, typename T>
unsigned int GetSerializeSize(const std::pair<K, T>& item, int nType, int nVersion);
template<typename Stream, typename K, typename T>
void Serialize(Stream& os, const std::pair<K, T>& item, int nType, int nVersion);
template<typename Stream, typename K, typename T>
void Unserialize(Stream& is, std::pair<K, T>& item, int nType, int nVersion);

#endif // SERIALIZE_PAIR_H
