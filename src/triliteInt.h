#ifndef TRILITE_INT_H
#define TRILITE_INT_H

/** TriLite Internal Interfaces
 * This file declares the internal trilite interface, that is all structures,
 * configuration settings and non-static functions used internally in trilite.
 * Pattern parsers should include pattern_parser.h. Programs using trilite
 * should include trilite.h. This file may only be included by files that make
 * up the trilite implementation.
 */


/***************************** Standard C library *****************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>


/********************************* SQLite API *********************************/
#include <sqlite3ext.h>
extern const sqlite3_api_routines *sqlite3_api;


/********************************* Public API *********************************/
#include "trilite.h"
#include "trilite_pattern.h"


/********************************** Typedefs **********************************/
typedef struct trilite_vtab trilite_vtab;
typedef struct trilite_column trilite_column;
typedef struct trilite_settings trilite_settings;
typedef struct trilite_cursor trilite_cursor;
typedef struct hash_table hash_table;
typedef struct hash_table_cursor hash_table_cursor;


/********************************* Structures *********************************/

/** Trilite virtual table settings */
struct trilite_settings{
  bool      bForbidFullMatchScan;       /** Forbid match with full table scan */
  int       iMaxRegExpMemory;           /** Max regular expression memory use */
};


/** Trilite column
 * Contains cached statements and hash tables for pending entries to be added
 * or removed.
 */
struct trilite_column{
  hash_table          *pPending;          /** Entries with pending changes */
  sqlite3_stmt        *pUpdateStmt;       /** Cached insert/replace statement */
};


/** Trilite virtual table
 * This is a subclass of sqlite3_vtab, and thus, this *must* be the first
 * attribute, to emphasize this relationship the attribute is called base.
 * There is an entry for each column in ppCols, the entry is NULL if the column
 * isn't indexed.
 */
struct trilite_vtab{
  sqlite3_vtab        base;               /** Base as required by SQLite */
  sqlite3             *db;                /** Database connection */
  const char          *zDb;               /** Database name */
  const char          *zName;             /** Virtual table name */
  trilite_settings    settings;           /** Table specific settings */
  int                 nCols;              /** Number of columns */
  trilite_column      **pCols;            /** Columns (See TRILITE_COLUMN_%) */
  sqlite3_stmt        *pInsertStmt;       /** Cached insert content statement */
  sqlite3_stmt        *pUpdateStmt;       /** Cached update content statement */
  sqlite3_stmt        *pDeleteStmt;       /** Cached delete content statement */
  int                 nPatternCache;      /** Number of cached patterns */
  int                 nPatternCacheAvail; /** Slots available for patterns */
  trilite_pattern     *pPatternCache;     /** Patterns currently cached */
};


/** Trilite cursor
 * This is a subclass of sqlite3_vtab_cursor, and thus, this *must* be the first
 * attribute, to emphasize this relationship the attribute is called base.
 */
struct trilite_cursor{
  sqlite3_vtab_cursor base;               /** Base as required by SQLite */
  int                 eof;                /** Is at end of file (query) */
  trilite_expression  *pExpr;             /** Expression filtering doc ids */
  int                 nPatterns;          /** Number of patterns */
  trilite_pattern     *pPatterns;         /** Patterns used in query */
  sqlite3_stmt        *pRowStmt;          /** Statement holding current row */
};


/** Pattern invocation context
 * This context passes important information to the functions used by the
 * pattern implementation. For instance pCtx and pTriVTab gives trilite_error
 * a place to write the error message (sqlite_context or pTriVTab->zErr).
 * Note that either pCtx or pTriVTab must not NULL.
 * nExtetns, nExtentAvail and pExtents gives trilite_result_extents a place to
 * store reported extents. It's allowed to set nExtentsAvail to -1, to indicate
 * that no extents are desired.
 * pCol (combined with pTriVTab) allows trilite_expr_trigram to fetch the
 * doclist and report errors. Note, that if pCol is not NULL, then pTriVTab must
 * be provided.
 */
struct trilite_context{
  sqlite3_context     *pCtx;            /** Scalar function context, if any */
  trilite_vtab        *pTriVTab;        /** vtable, if called from here */
  int                 nExtents;         /** Number of extents currently held */
  int                 nExtentsAvail;    /** Extents allocated, -1 if N/A */
  int                 *pExtents;        /** Extents reported */
  trilite_column      *pCol;            /** Target column, for trilite_expr_% */
};


/****************************** Constants/Macros ******************************/

/** Default values for trilite_settings */
#define DEFAULT_SETTINGS                                  \
  {                                                       \
    false,      /* Allow full match scan by default */    \
    8<<20       /* Default re2 memory usage */            \
  }


/** Keys in a hash tables with pending changes
 * The hash table has a fixed number of keys, entries for trigrams with key
 * collisions will be resolved using a linked list.
 * Note: When using modulo for to COMPUTE_HASH it makes sense to use a prime
 * number, powers of two are particularly bad as modulo these will return the
 * least significant bits of the trigram being hashed.
 */
#define HASH_TABLE_KEYS             9973
/* TODO: Consider implementing dynamic resizing for the hash table */


/** Hash function for hashing trigrams */
#define COMPUTE_HASH(trigram)       (trigram % HASH_TABLE_KEYS)


/** Over-allocation factor for hash table entries.
 * When allocating an entry in the hash table we allocate more slots than needed
 * this gives us a better amoritized complexity with respect to the number of
 * reallocations needed when added entries.
 * If we need to store N ids in an entry we allocate M - N extra slots, where
 * 
 *    M = MAX(HASH_ENTRY_ALLOCATION_FACTOR * N, HASH_ENTRY_MIN_ALLOCATION)
 * 
 * Note, that HASH_ENTRY_ALLOCATION_FACTOR must be greater than equal to 1.
 */
#define HASH_ENTRY_ALLOCATION_FACTOR           (1.5f)


/** Minimum number of extra ids to allocate when allocating an entry in the
 * hash table. Using a reasonble minimum value reduces the number of allocations
 * needed for small entries.
 */
#define HASH_ENTRY_MIN_ALLOCATION              4


/** Maximum size of a varint (used for dynamic memory allocation)
 * (If modified the code in varint.c must be updated to reflect this)
 */
#define MAX_VARINT_SIZE             9


/** Macro used to suppress compiler warnings for unused parameters */
#define UNUSED_PARAMETER(x)         (void)(x)


/** Macro for getting the maximum of two values */
#define MAX(a,b)    ((a) < (b) ? (b) : (a))


/** Macro for getting the minimum of two values */
#define MIN(a,b)    ((a) > (b) ? (b) : (a))


/*** Bits per byte */
#define BITSPERBYTE                 8


/** Super fly-weight implementation of key and blob columns
 * Entries in the pCols array on trilite_vtab can be either a pointer
 * to a trilite_column or a value from TRILITE_COLUMN_%, as defined below.
 */
#define TRILITE_COLUMN_BLOB         (trilite_column*)0
#define TRILITE_COLUMN_KEY          (trilite_column*)1

/**************************** Functions in vtable.c ***************************/
int triliteCreate(sqlite3*, void*, int, const char *const*,
                  sqlite3_vtab**, char**);
int triliteConnect(sqlite3*, void*, int, const char *const*,
                   sqlite3_vtab**, char**);
int triliteBestIndex(sqlite3_vtab*, sqlite3_index_info*);
int triliteDisconnect(sqlite3_vtab*);
int triliteDestroy(sqlite3_vtab*);
int triliteUpdate(sqlite3_vtab*, int, sqlite3_value**, sqlite_int64*);
int triliteRename(sqlite3_vtab*, const char*);
int triliteFindFunction(sqlite3_vtab*, int, const char*, 
                        void (**)(sqlite3_context*, int, sqlite3_value**),
                        void**);
int triliteBegin(sqlite3_vtab*);
int triliteSync(sqlite3_vtab*);
int triliteRollback(sqlite3_vtab*);
int triliteCommit(sqlite3_vtab*);
void triliteError(trilite_vtab*, const char*, ...);

/**************************** Functions in cursor.c ***************************/
int triliteOpen(sqlite3_vtab*, sqlite3_vtab_cursor**);
int triliteClose(sqlite3_vtab_cursor*);
int triliteFilter(sqlite3_vtab_cursor*, int, const char*, int, sqlite3_value**);
int triliteNext(sqlite3_vtab_cursor*);
int triliteEof(sqlite3_vtab_cursor*);
int triliteColumn(sqlite3_vtab_cursor*, sqlite3_context*, int);
int triliteRowid(sqlite3_vtab_cursor*, sqlite_int64*);


/**************************** Functions in varint.c ***************************/
int readVarInt(unsigned char*, sqlite3_int64*);
int writeVarInt(unsigned char*, sqlite3_int64);


/***************************** Functions in hash.c ****************************/
int hashCreate(hash_table**);
int hashFind(hash_table*, trilite_trigram, int*,
             const sqlite3_int64**, int*, const sqlite3_int64**);
int hashAdd(hash_table*, trilite_trigram, sqlite3_int64);
int hashRemove(hash_table*, trilite_trigram, sqlite3_int64);
int hashReset(hash_table*);
int hashIsEmpty(hash_table*, bool*);
int hashDestroy(hash_table*);
int hashOpen(hash_table*, hash_table_cursor**);
int hashTake(hash_table_cursor*, trilite_trigram*,
             int*, sqlite3_int64**, int*, sqlite3_int64**);
int hashClose(hash_table_cursor*);


/*************************** Functions in pattern.c ***************************/
int exprNext(trilite_expression**, bool*, bool*, sqlite3_int64*);
int exprDestroy(trilite_expression*);


/******************************** Debug Macros ********************************/

#ifndef NDEBUG

/** Functions to print log data from in debug-mode */
#define FUNCTIONS_TO_LOG  {                                                   \
    "triliteCreate",          /* Create TriLite table */                      \
    "triliteConnect",         /* Open database connection */                  \
    "triliteDisconnect",      /* Close database connection */                 \
    "triliteParse",           /* Virtual table declaration parser */          \
 /* __func__,                  * Any function */                              \
    NULL                      /* End of list */                               \
  }

/* Log messages, if current function is in the list of FUNCTIONS_TO_LOG */
#define log(...)                                                              \
  do{                                                                         \
    char* __TRILITE_INT_H__funcs[] = FUNCTIONS_TO_LOG;                        \
    int __TRILITE_INT_H__i = 0;                                               \
    while(__TRILITE_INT_H__funcs[__TRILITE_INT_H__i] != NULL){                \
      if(strcmp(__func__, __TRILITE_INT_H__funcs[__TRILITE_INT_H__i]) == 0){  \
        fprintf(stderr, "%s:%i ", __func__, __LINE__);                        \
        fprintf(stderr, __VA_ARGS__);                                         \
        fprintf(stderr, "\n");                                                \
        break;                                                                \
      }                                                                       \
      __TRILITE_INT_H__i++;                                                   \
    }                                                                         \
  }while(false)

#else  /* NDEBUG */

/* Ignore all calls to log, if NDEBUG is defined */
#define log(...)              ((void)0)

#endif /* NDEBUG */


#endif /* TRILITE_INT_H */