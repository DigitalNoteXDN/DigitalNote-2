#ifndef DB_H
#define DB_H

#include <string>

class CDBEnv;

extern unsigned int nWalletDBUpdated;
extern CDBEnv bitdb;

void ThreadFlushWalletDB(const std::string& strWalletFile);

#endif // DB_H
