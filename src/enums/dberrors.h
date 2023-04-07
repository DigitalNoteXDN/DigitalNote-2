#ifndef DBERRORS_H
#define DBERRORS_H

/** Error statuses for the wallet database */
enum DBErrors
{
	DB_LOAD_OK,
	DB_CORRUPT,
	DB_NONCRITICAL_ERROR,
	DB_TOO_NEW,
	DB_LOAD_FAIL,
	DB_NEED_REWRITE
};

#endif // DBERRORS_H
