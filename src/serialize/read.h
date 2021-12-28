#ifndef SERIALIZE_READ_H
#define SERIALIZE_READ_H

#include <cstdint>

// GetSerializeSize
unsigned int GetSerializeSize(char a, int, int = 0);
unsigned int GetSerializeSize(signed char a, int, int = 0);
unsigned int GetSerializeSize(unsigned char a, int, int = 0);
unsigned int GetSerializeSize(signed short a, int, int = 0);
unsigned int GetSerializeSize(unsigned short a, int, int = 0);
unsigned int GetSerializeSize(signed int a, int, int = 0);
unsigned int GetSerializeSize(unsigned int a, int, int = 0);
unsigned int GetSerializeSize(signed long a, int, int = 0);
unsigned int GetSerializeSize(unsigned long a, int, int = 0);
unsigned int GetSerializeSize(signed long long a, int, int = 0);
unsigned int GetSerializeSize(unsigned long long a, int, int = 0);
unsigned int GetSerializeSize(float a, int, int = 0);
unsigned int GetSerializeSize(double a, int, int = 0);
unsigned int GetSerializeSize(bool a, int, int = 0);
template<typename T>
unsigned int GetSerializeSize(const T& a, long nType, int nVersion);


// Unserialize
template<typename Stream>
void Unserialize(Stream& s, char& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, signed char& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, unsigned char& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, signed short& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, unsigned short& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, signed int& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, unsigned int& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, signed long& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, unsigned long& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, signed long long& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, unsigned long long& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, float& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, double& a, int, int = 0);
template<typename Stream>
void Unserialize(Stream& s, bool& a, int, int = 0);
template<typename Stream, typename T>
void Unserialize(Stream& is, T& a, long nType, int nVersion);

template<typename Stream>
uint64_t ReadCompactSize(Stream& is);

#endif // SERIALIZE_READ_H