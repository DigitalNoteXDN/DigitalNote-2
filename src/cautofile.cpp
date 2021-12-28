#include <ios>

#include "serialize.h"
#include "cblock.h"
#include "ctransaction.h"
#include "uint/uint256.h"
#include "cdatastream.h"
#include "cflatdata.h"
#include "cvarint.h"

#include "cautofile.h"

CAutoFile::CAutoFile(FILE* filenew, int nTypeIn, int nVersionIn)
{
	file = filenew;
	nType = nTypeIn;
	nVersion = nVersionIn;
	state = 0;
	exceptmask = std::ios::badbit | std::ios::failbit;
}

CAutoFile::~CAutoFile()
{
	fclose();
}

void CAutoFile::fclose()
{
	if (file != NULL && file != stdin && file != stdout && file != stderr)
	{
		::fclose(file);
	}
	
	file = NULL;
}

FILE* CAutoFile::release()
{
	FILE* ret = file;
	
	file = NULL;
	
	return ret;
}

CAutoFile::operator FILE*()
{
	return file;
}

FILE* CAutoFile::operator->()
{
	return file;
}

FILE& CAutoFile::operator*()
{
	return *file;
}

FILE** CAutoFile::operator&()
{
	return &file;
}

FILE* CAutoFile::operator=(FILE* pnew)
{
	return file = pnew;
}

bool CAutoFile::operator!()
{
	return (file == NULL);
}

//
// Stream subset
//
void CAutoFile::setstate(short bits, const char* psz)
{
	state |= bits;
	
	if (state & exceptmask)
	{
		throw std::ios_base::failure(psz);
	}
}

bool CAutoFile::fail() const
{
	return state & (std::ios::badbit | std::ios::failbit);
}

bool CAutoFile::good() const
{
	return state == 0;
}

void CAutoFile::clear(short n)
{
	state = n;
}

short CAutoFile::exceptions()
{
	return exceptmask;
}

short CAutoFile::exceptions(short mask)
{
	short prev = exceptmask;
	
	exceptmask = mask; 
	
	setstate(0, "CAutoFile");
	
	return prev;
}

void CAutoFile::SetType(int n)
{
	nType = n;
}

int CAutoFile::GetType()
{
	return nType;
}

void CAutoFile::SetVersion(int n)
{
	nVersion = n;
}

int CAutoFile::GetVersion()
{
	return nVersion;
}

void CAutoFile::ReadVersion()
{
	*this >> nVersion;
}

void CAutoFile::WriteVersion()
{
	*this << nVersion;
}

CAutoFile& CAutoFile::read(char* pch, size_t nSize)
{
	if (!file)
	{
		throw std::ios_base::failure("CAutoFile::read : file handle is NULL");
	}
	
	if (fread(pch, 1, nSize, file) != nSize)
	{
		setstate(std::ios::failbit, feof(file) ? "CAutoFile::read : end of file" : "CAutoFile::read : fread failed");
	}
	
	return (*this);
}

CAutoFile& CAutoFile::write(const char* pch, size_t nSize)
{
	if (!file)
	{
		throw std::ios_base::failure("CAutoFile::write : file handle is NULL");
	}
	
	if (fwrite(pch, 1, nSize, file) != nSize)
	{
		setstate(std::ios::failbit, "CAutoFile::write : write failed");
	}
	
	return (*this);
}

template<typename T>
unsigned int CAutoFile::GetSerializeSize(const T& obj)
{
	// Tells the size of the object if serialized to this stream
	return ::GetSerializeSize(obj, nType, nVersion);
}

template unsigned int CAutoFile::GetSerializeSize<CBlock>(CBlock const&);

template<typename T>
CAutoFile& CAutoFile::operator<<(const T& obj)
{
	// Serialize to this stream
	if (!file)
	{
		throw std::ios_base::failure("CAutoFile::operator<< : file handle is NULL");
	}
	
	::Serialize(*this, obj, nType, nVersion);
	
	return (*this);
}

template CAutoFile& CAutoFile::operator<< <CDataStream>(CDataStream const&);
template CAutoFile& CAutoFile::operator<< <CFlatData>(CFlatData const&);
template CAutoFile& CAutoFile::operator<< <CVarInt<unsigned int> >(CVarInt<unsigned int> const&);
template CAutoFile& CAutoFile::operator<< <unsigned int>(unsigned int const&);
template CAutoFile& CAutoFile::operator<< <CBlock>(CBlock const&);

template<typename T>
CAutoFile& CAutoFile::operator>>(T& obj)
{
	// Unserialize from this stream
	if (!file)
	{
		throw std::ios_base::failure("CAutoFile::operator>> : file handle is NULL");
	}
	
	::Unserialize(*this, obj, nType, nVersion);
	
	return (*this);
}

template CAutoFile& CAutoFile::operator>><CFlatData>(CFlatData&);
template CAutoFile& CAutoFile::operator>><CVarInt<unsigned int> >(CVarInt<unsigned int>&);
template CAutoFile& CAutoFile::operator>><unsigned int>(unsigned int&);
template CAutoFile& CAutoFile::operator>><CBlock>(CBlock&);
template CAutoFile& CAutoFile::operator>><CTransaction>(CTransaction&);
template CAutoFile& CAutoFile::operator>><uint256>(uint256&);
