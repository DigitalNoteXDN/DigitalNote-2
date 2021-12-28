#ifndef SERIALIZE_BASE_H
#define SERIALIZE_BASE_H

#include <vector>
#include <cstdint>

template<typename T>
T& REF(const T& val);

template<typename T>
T* NCONST_PTR(const T* val);

template <class T, class TAl>
T* begin_ptr(std::vector<T, TAl>& v);

template <class T, class TAl>
const T* begin_ptr(const std::vector<T, TAl>& v);

template <class T, class TAl>
T* end_ptr(std::vector<T, TAl>& v);

template <class T, class TAl>
const T* end_ptr(const std::vector<T, TAl>& v);

unsigned int GetSizeOfCompactSize(uint64_t nSize);

#endif // SERIALIZE_BASE_H
