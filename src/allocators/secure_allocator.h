#ifndef SECURE_ALLOCATOR_H
#define SECURE_ALLOCATOR_H

#include <memory>

//
// Allocator that locks its contents from being paged
// out of memory and clears its contents before deletion.
//
template<typename T>
struct secure_allocator : public std::allocator<T>
{
	// MSVC8 default copy constructor is broken
	typedef std::allocator<T> base;
	typedef typename base::size_type size_type;
	typedef typename base::difference_type  difference_type;
	typedef typename base::pointer pointer;
	typedef typename base::const_pointer const_pointer;
	typedef typename base::reference reference;
	typedef typename base::const_reference const_reference;
	typedef typename base::value_type value_type;

	template<typename _Other>
	struct rebind
	{
		typedef secure_allocator<_Other> other;
	};

	secure_allocator() throw();

	secure_allocator(const secure_allocator& a) throw() : base(a)
	{
		
	}

	template <typename U>
	secure_allocator(const secure_allocator<U>& a) throw() : base(a)
	{
		
	}

	~secure_allocator() throw();

	T* allocate(std::size_t n, const void *hint = 0);
	void deallocate(T* p, std::size_t n);
};

#endif // SECURE_ALLOCATOR_H
