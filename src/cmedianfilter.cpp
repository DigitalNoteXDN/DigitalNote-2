#include <algorithm>
#include <cassert>
#include <cstdint>

#include "cmedianfilter.h"

template <typename T>
CMedianFilter<T>::CMedianFilter(unsigned int size, T initial_value) : nSize(size)
{
	vValues.reserve(size);
	vValues.push_back(initial_value);
	vSorted = vValues;
}

template <typename T>
void CMedianFilter<T>::input(T value)
{
	if(vValues.size() == nSize)
	{
		vValues.erase(vValues.begin());
	}
	
	vValues.push_back(value);

	vSorted.resize(vValues.size());
	std::copy(vValues.begin(), vValues.end(), vSorted.begin());
	std::sort(vSorted.begin(), vSorted.end());
}

template <typename T>
T CMedianFilter<T>::median() const
{
	int size = vSorted.size();
	
	assert(size>0);
	
	if(size & 1) // Odd number of elements
	{
		return vSorted[size/2];
	}
	else // Even number of elements
	{
		return (vSorted[size/2-1] + vSorted[size/2]) / 2;
	}
}

template <typename T>
int CMedianFilter<T>::size() const
{
	return vValues.size();
}

template <typename T>
std::vector<T> CMedianFilter<T>::sorted () const
{
	return vSorted;
}

// Template generation
template class CMedianFilter<int64_t>;
