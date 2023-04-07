#ifndef BANMAP_T_H
#define BANMAP_T_H

#include <map>

class CSubNet;
class CBanEntry;

typedef std::map<CSubNet, CBanEntry> banmap_t;

#endif // BANMAP_T_H