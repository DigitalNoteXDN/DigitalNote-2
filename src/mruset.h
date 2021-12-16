#ifndef MRUSET_H
#define MRUSET_H

#include <set>
#include <deque>

/** STL-like set container that only keeps the most recent N elements. */
template <typename T>
class mruset
{
public:
    typedef T key_type;
    typedef T value_type;
    typedef typename std::set<T>::iterator iterator;
    typedef typename std::set<T>::const_iterator const_iterator;
    typedef typename std::set<T>::size_type size_type;

protected:
    std::set<T> set;
    std::deque<T> queue;
    size_type nMaxSize;

public:
	mruset(size_type nMaxSizeIn = 0);

	iterator begin() const;
	iterator end() const;
	size_type size() const;
	bool empty() const;
	iterator find(const key_type& k) const;
	size_type count(const key_type& k) const;
	void clear();
	
	friend inline bool operator==(const mruset<T>& a, const mruset<T>& b)
	{
		return a.set == b.set;
	}
	
	friend inline bool operator==(const mruset<T>& a, const std::set<T>& b)
	{
		return a.set == b;
	}
	
	friend inline bool operator<(const mruset<T>& a, const mruset<T>& b)
	{
		return a.set < b.set;
	}
	
	std::pair<iterator, bool> insert(const key_type& x);
	size_type max_size() const;
	size_type max_size(size_type s);
};

#endif // MRUSET_H
