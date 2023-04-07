#ifndef CADDRDB_H
#define CADDRDB_H

#include <boost/filesystem.hpp>

class CAddrMan;

/** Access to the (IP) address database (peers.dat) */
class CAddrDB
{
private:
	boost::filesystem::path pathAddr;

public:
	CAddrDB();
	bool Write(const CAddrMan& addr);
	bool Read(CAddrMan& addr);
};

#endif // CADDRDB_H
