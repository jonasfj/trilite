#ifndef TRILITE_VTABLE_H
#define TRILITE_VTABLE_H

#include <sqlite3ext.h>
#include <stdbool.h>

#include "config.h"

/** Trilite virtual table */
struct trilite_vtab {
  /** Base as required by sqlite */
  sqlite3_vtab base;
  
  /** Database connection */
  sqlite3 *db;
  
  /** Database name */
  char *zDb;

  /** Virtual table name */
  char *zName;
  
  /** Delete row from %_content */
  sqlite3_stmt *stmt_delete_content;
  
  /** Insert row into %_content */
  sqlite3_stmt *stmt_insert_content;
  
  /** Update row in %_content */
  sqlite3_stmt *stmt_update_content;
  
  /** Select row from %_index */
  sqlite3_stmt *stmt_fetch_doclist;

  /** Update/insert row in %_index */ 
  sqlite3_stmt *stmt_update_doclist;

  /** Hash table of new trigrams and their doclists */
  hash_table *pAdded;

  /** Raise error when evaluating a match scan as full table scan */
  bool forbidFullMatchScan;

  /** Max regexp memory */
  int maxRegExpMemory;
};

int triliteCreate(sqlite3*, void*, int, const char *const*, sqlite3_vtab**, char**);
int triliteConnect(sqlite3*, void*, int, const char *const*, sqlite3_vtab**, char**);
int triliteBestIndex(sqlite3_vtab*, sqlite3_index_info*);
int triliteDisconnect(sqlite3_vtab*);
int triliteDestroy(sqlite3_vtab*);
int triliteUpdate(sqlite3_vtab*, int, sqlite3_value**, sqlite_int64*);
int triliteRename(sqlite3_vtab*, const char*);
int triliteFindFunction(sqlite3_vtab*, int, const char*, void (**)(sqlite3_context*, int, sqlite3_value**), void**);
int triliteBegin(sqlite3_vtab*);
int triliteSync(sqlite3_vtab*);
int triliteCommit(sqlite3_vtab*);
void triliteError(trilite_vtab*, const char*, ...);

#endif /* TRILITE_VTABLE_H */
