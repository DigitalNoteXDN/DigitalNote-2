#include "cinv.h"

#include "limitedmap.h"

template <typename K, typename V>
limitedmap<K, V>::limitedmap(limitedmap<K, V>::size_type nMaxSizeIn)
{
	nMaxSize = nMaxSizeIn;
}

template <typename K, typename V>
typename limitedmap<K, V>::const_iterator limitedmap<K, V>::begin() const
{
	return map.begin();
}

template <typename K, typename V>
typename limitedmap<K, V>::const_iterator limitedmap<K, V>::end() const
{
	return map.end();
}

template <typename K, typename V>
typename limitedmap<K, V>::size_type limitedmap<K, V>::size() const
{
	return map.size();
}

template <typename K, typename V>
bool limitedmap<K, V>::empty() const
{
	return map.empty();
}

template <typename K, typename V>
typename limitedmap<K, V>::const_iterator limitedmap<K, V>::find(const key_type& k) const
{
	return map.find(k);
}

template <typename K, typename V>
typename limitedmap<K, V>::size_type limitedmap<K, V>::count(const key_type& k) const
{
	return map.count(k);
}

template <typename K, typename V>
void limitedmap<K, V>::insert(const value_type& x)
{
	std::pair<iterator, bool> ret = map.insert(x);
	
	if (ret.second)
	{
		if (nMaxSize && map.size() == nMaxSize)
		{
			map.erase(rmap.begin()->second);
			rmap.erase(rmap.begin());
		}
		
		rmap.insert(std::make_pair(x.second, ret.first));
	}
	
	return;
}

template <typename K, typename V>
void limitedmap<K, V>::erase(const key_type& k)
{
	iterator itTarget = map.find(k);
	
	if (itTarget == map.end())
	{
		return;
	}
	
	std::pair<rmap_iterator, rmap_iterator> itPair = rmap.equal_range(itTarget->second);
	
	for (rmap_iterator it = itPair.first; it != itPair.second; ++it)
	{
		if (it->second == itTarget)
		{
			rmap.erase(it);
			map.erase(itTarget);
			
			return;
		}
	}
	
	// Shouldn't ever get here
	assert(0); //TODO remove me
	
	map.erase(itTarget);
}

template <typename K, typename V>
void limitedmap<K, V>::update(limitedmap<K, V>::const_iterator itIn, const mapped_type& v)
{
	//TODO: When we switch to C++11, use map.erase(itIn, itIn) to get the non-const iterator
	iterator itTarget = map.find(itIn->first);
	
	if (itTarget == map.end())
	{
		return;
	}
	
	std::pair<rmap_iterator, rmap_iterator> itPair = rmap.equal_range(itTarget->second);
	
	for (rmap_iterator it = itPair.first; it != itPair.second; ++it)
	{
		if (it->second == itTarget)
		{
			rmap.erase(it);
			itTarget->second = v;
			rmap.insert(std::make_pair(v, itTarget));
			return;
		}
	}
	
	// Shouldn't ever get here
	assert(0); //TODO remove me
	
	itTarget->second = v;
	rmap.insert(std::make_pair(v, itTarget));
}

template <typename K, typename V>
typename limitedmap<K, V>::size_type limitedmap<K, V>::max_size() const
{
	return nMaxSize;
}

template <typename K, typename V>
typename limitedmap<K, V>::size_type limitedmap<K, V>::max_size(limitedmap<K, V>::size_type s)
{
	if (s)
	{
		while (map.size() > s)
		{
			map.erase(rmap.begin()->second);
			rmap.erase(rmap.begin());
		}
	}

	nMaxSize = s;

	return nMaxSize;
}

template class limitedmap<CInv, int64_t>;

