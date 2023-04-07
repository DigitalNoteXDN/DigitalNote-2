#ifndef BACKWARDS_H
#define BACKWARDS_H

template <class T>
class backwards
{
	T& _obj;

public:
	backwards(T &obj) : _obj(obj)
	{
		
	}

	auto begin()
	{
		return _obj.rbegin();
	}

	auto end()
	{
		return _obj.rend();
	}
};

#endif // BACKWARDS_H
