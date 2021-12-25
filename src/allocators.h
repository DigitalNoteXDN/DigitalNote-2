#ifndef ALLOCATORS_H
#define ALLOCATORS_H

//
// Functions for directly locking/unlocking memory objects.
// Intended for non-dynamically allocated structures.
//
template<typename T>
void LockObject(const T &t);

template<typename T>
void UnlockObject(const T &t);

#endif // ALLOCATORS_H
