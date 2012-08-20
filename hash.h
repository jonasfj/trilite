#ifndef TRILITE_HASH_H
#define TRILITE_HASH_H

#include "config.h"

#include <sqlite3ext.h>

#include <stdbool.h>

int hashCreate(hash_table**);
void hashRelease(hash_table*);
int hashMemoryUsage(hash_table*);
sqlite3_int64* hashFind(hash_table*, trilite_trigram, int*);
bool hashInsert(hash_table*, trilite_trigram, sqlite3_int64);
bool hashRemove(hash_table*, trilite_trigram, sqlite3_int64);

int hashOpen(hash_table*, hash_table_cursor**);
bool hashPop(hash_table_cursor*, trilite_trigram*, sqlite3_int64**, int*);
void hashClose(hash_table_cursor*);

#endif /* TRILITE_HASH_H */
