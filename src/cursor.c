#include "triliteInt.h"

/** Open cursor on virtual table
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteOpen(
  sqlite3_vtab        *pVTab,         /** Table to open cursor on */
  sqlite3_vtab_cursor **ppCursor      /** OUT: Virtual table cursor */
){
  assert(pVTab && ppCursor);

  /** TODO: Implementation missing */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Close virtual table cursor
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteClose(
  sqlite3_vtab_cursor *pCursor        /** Virtual table cursor to close */
){
  assert(pCursor);

  /** TODO: Implementation missing */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Apply filter options
 * Arguments given to this functions are determined by triliteBestIndex.
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteFilter(
  sqlite3_vtab_cursor *pCursor,       /** Virtual table cursor to filter */
  int                 nIdxNum,        /** Index number from triliteBestIndex */
  const char          *zIdxStr,       /** Index string from triliteBestIndex */
  int                 nArgc,          /** Number of arguments */
  sqlite3_value       **ppArgv        /** Arguments to index strategy */
){
  assert(pCursor);

  /** TODO: Implementation missing */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Move to next row
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteNext(
  sqlite3_vtab_cursor *pCursor        /** Virtual table cursor to move */
){
  assert(pCursor);

  /** TODO: Implementation missing */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Check if at end of query
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteEof(
  sqlite3_vtab_cursor *pCursor        /** Virtual table cursor to test */
){
  assert(pCursor);

  /** TODO: Implementation missing */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Return value stored in column of current row
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteColumn(
  sqlite3_vtab_cursor *pCursor,       /** Virtal table cursor */
  sqlite3_context     *pCtx,          /** SQLite context */
  int                 iCol            /** Column to return value for */
){
  assert(pCursor);

  /** TODO: Implementation missing */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Return current row id
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteRowid(
  sqlite3_vtab_cursor *pCursor,       /** Virtual table cursor */
  sqlite_int64        *pId            /** OUT: Id of current row */
){
  assert(pCursor);

  /** TODO: Implementation missing */
  assert(false);
  return SQLITE_INTERNAL;
}
