#ifndef SERIALIZE_WRITE_H
#define SERIALIZE_WRITE_H

#include <cstdint>

// Serialize
template<typename Stream>
void Serialize(Stream& s, char a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, signed char a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, unsigned char a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, signed short a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, unsigned short a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, signed int a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, unsigned int a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, signed long a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, unsigned long a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, signed long long a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, unsigned long long a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, float a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, double a, int, int = 0);
template<typename Stream>
void Serialize(Stream& s, bool a, int, int = 0);
template<typename Stream, typename T>
void Serialize(Stream& os, const T& a, long nType, int nVersion);

template<typename Stream>
void WriteCompactSize(Stream& os, uint64_t nSize);

#endif // SERIALIZE_WRITE_H
