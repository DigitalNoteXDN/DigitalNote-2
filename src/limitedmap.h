#ifndef LIMITEDMAP_H
#define LIMITEDMAP_H

#include <assert.h> // TODO: remove
#include <map>

/** STL-like map container that only keeps the N elements with the highest value. */
template <typename K, typename V>
class limitedmap
{
public:
	typedef K key_type;
	typedef V mapped_type;
	typedef std::pair<const key_type, mapped_type> value_type;
	typedef typename std::map<K, V>::const_iterator const_iterator;
	typedef typename std::map<K, V>::size_type size_type;

protected:
	std::map<K, V> map;
	typedef typename std::map<K, V>::iterator iterator;
	std::multimap<V, iterator> rmap;
	typedef typename std::multimap<V, iterator>::iterator rmap_iterator;
	size_type nMaxSize;

public:
	limitedmap(size_type nMaxSizeIn = 0);

	const_iterator begin() const;
	const_iterator end() const;
	size_type size() const;
	bool empty() const;
	const_iterator find(const key_type& k) const;
	size_type count(const key_type& k) const;
	void insert(const value_type& x);
	void erase(const key_type& k);
	void update(const_iterator itIn, const mapped_type& v);
	size_type max_size() const;
	size_type max_size(size_type s);
};

#endif // LIMITEDMAP_H
