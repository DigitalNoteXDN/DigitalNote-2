#ifndef CBANDB_H
#define CBANDB_H

#include <boost/filesystem.hpp>

#include "types/banmap_t.h"

/** Access to the banlist database (banlist.dat) */
class CBanDB
{
private:
	boost::filesystem::path pathBanlist;

public:
	CBanDB();
	bool Write(const banmap_t& banSet);
	bool Read(banmap_t& banSet);
};

#endif // CBANDB_H
