#ifndef VALTYPE_H
#define VALTYPE_H

#include <vector>

typedef std::vector<unsigned char> valtype;

static const valtype ValType_False(0);
static const valtype ValType_Zero(0);
static const valtype ValType_True(1, 1);

#endif // VALTYPE_H
