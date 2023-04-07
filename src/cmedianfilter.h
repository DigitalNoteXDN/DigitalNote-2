#ifndef CMEDIANFILTER_H
#define CMEDIANFILTER_H

#include <vector>

/** Median filter over a stream of values.
 * Returns the median of the last N numbers
 */
template <typename T>
class CMedianFilter
{
private:
	std::vector<T> vValues;
	std::vector<T> vSorted;
	unsigned int nSize;

public:
	CMedianFilter(unsigned int size, T initial_value);

	void input(T value);
	T median() const;
	int size() const;
	std::vector<T> sorted() const;
};

#endif // CMEDIANFILTER_H
