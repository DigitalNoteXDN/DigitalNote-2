#ifndef COMPAREVALUEONLY_H
#define COMPAREVALUEONLY_H

#include <utility>
#include <cstdint>

template <typename T>
struct CompareValueOnly
{
	bool operator() (const std::pair<int64_t, T>& t1, const std::pair<int64_t, T>& t2) const
	{
		return t1.first < t2.first;
	}
};

#endif // COMPAREVALUEONLY_H
