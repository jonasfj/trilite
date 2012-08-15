#include "cursor.h"
#include "vtable.h"
#include "config.h"
#include "varint.h"
#include "expr.h"

const sqlite3_api_routines *sqlite3_api;

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>


typedef struct doclist doclist;

static int resetCursor(trilite_cursor *pTrgCur);


/** Document list as stored in the trilite_cursor */
struct doclist{
  /** Number of bytes in ids */
  int nSize;
  
  /** Offset in ids */
  int iOffset;
  
  /** Previously read id (for delta list decompression) */
  sqlite_int64 prevId;
  
  /** List of ids */
  unsigned char *ids;
};

/** Trigram cursor */
struct trilite_cursor{
  /** Base class */
  sqlite3_vtab_cursor base;

  /** End of cursor */
  int eof;

  /** Number of doclists currently held */
  int nDocLists;
  
  /** Index number used by this cursor */
  int idxNum;
  
  /** Expression begin evaluated */
  expr *pExpr;

  /** Fetch a row, holds the current row */
  sqlite3_stmt *stmt_fetch_content;
};


/** Create a new trilite cursor */
int triliteOpen(sqlite3_vtab *pVtab, sqlite3_vtab_cursor **ppCur){
  trilite_cursor* pTrgCur = (trilite_cursor*)sqlite3_malloc(sizeof(trilite_cursor));
  if(!pTrgCur) return SQLITE_NOMEM;
  
  // This shouldn't have anything effects
  pTrgCur->eof = 1;     // Start at end, wait for xFilter
  pTrgCur->idxNum = 0;  // Set an invalid strategy

  // Provide pVtab
  pTrgCur->base.pVtab = pVtab;

  // Set statements NULL, so we can call finalize without problems
  pTrgCur->stmt_fetch_content = NULL;

  // Set expr NULL
  pTrgCur->pExpr = NULL;

  //TODO Decide if we should register this cursor with renameTable,
  // to ensure that prepared statements works after rename.
  // For now we just assume that cursors as supposed to be invalidated :)
  
  *ppCur = (sqlite3_vtab_cursor*)pTrgCur;
  return SQLITE_OK;
}

/** Close the trilite cursor (releases all resources held) */
int triliteClose(sqlite3_vtab_cursor *pCur){
  trilite_cursor* pTrgCur = (trilite_cursor*)pCur;
  
  // Release resources held
  resetCursor(pTrgCur);
  
  // Release cursor
  sqlite3_free(pTrgCur);
  pTrgCur = 0;
  
  return SQLITE_OK;
}


/** Start an index scan, ie. filter for results */
int triliteFilter(sqlite3_vtab_cursor *pCur, int idxNum, const char* zIdx, int argc, sqlite3_value **argv){
  trilite_cursor* pTrgCur = (trilite_cursor*)pCur;
  trilite_vtab* pTrgVtab = (trilite_vtab*)pTrgCur->base.pVtab;
  int rc = SQLITE_OK;
  
  // Reset the cursor
  resetCursor(pTrgCur);
  pTrgCur->eof = 0;
  
  // Store the index strategy
  pTrgCur->idxNum = idxNum;
  assert(pTrgCur->idxNum);
  
  char *zSql;
  // Some sort of intelligent trigram matching
  if(idxNum & IDX_MATCH_SCAN){
    assert(argc > 0);
    // Get the pattern
    //TODO What happens if this is not a text value?

    // Parse query
    rc = exprParsePatterns(&pTrgCur->pExpr, pTrgVtab, argc, argv);
    // Appropriate error should be reported by exprParse and friends
    if(rc != SQLITE_OK) return rc;

    // Prepare statement for triliteNext
    zSql = sqlite3_mprintf("SELECT id, text FROM %Q.'%q_content' WHERE id = ?", pTrgVtab->zDb, pTrgVtab->zName);
    if(!zSql) return SQLITE_NOMEM;
    rc = sqlite3_prepare_v2(pTrgVtab->db, zSql, -1, &pTrgCur->stmt_fetch_content, 0);
    sqlite3_free(zSql);
    assert(rc == SQLITE_OK);

  
  // Full table scan
  }else if(idxNum & IDX_FULL_SCAN){
    assert(argc == 0);
    // Statement for descending ordering
    if(idxNum & ORDER_BY_DESC){
      zSql = "SELECT id, text FROM %Q.'%q_content' order by id DESC";
    // Statement for ascending ordering
    }else if(idxNum & ORDER_BY_ASC){
      zSql = "SELECT id, text FROM %Q.'%q_content' order by id ASC";
    // Statement without ordering (if there's no requirement, let's not pass any along)
    }else{
      zSql = "SELECT id, text FROM %Q.'%q_content'";
    }
    
    log("IDX_FULL_SCAN with '%s'", zSql);
    
    // Add database and table prefix
    zSql = sqlite3_mprintf(zSql, pTrgVtab->zDb, pTrgVtab->zName);
    if(!zSql) return SQLITE_NOMEM;
    // Prepare statement
    rc = sqlite3_prepare_v2(pTrgVtab->db, zSql, -1, &pTrgCur->stmt_fetch_content, 0);
    sqlite3_free(zSql);
    assert(rc == SQLITE_OK);
    // Notice that next will always be called before this function exists
    // triliteNext will advanced this to
    
  // Simple rowid lookup
  }else if(idxNum & IDX_ROW_LOOKUP){
    assert(argc == 1);
    // Select row from %_content
    zSql = sqlite3_mprintf("SELECT id, text FROM %Q.'%q_content' WHERE id = ?", pTrgVtab->zDb, pTrgVtab->zName);
    if(!zSql) return SQLITE_NOMEM;
    // Prepare statement
    rc = sqlite3_prepare_v2(pTrgVtab->db, zSql, -1, &pTrgCur->stmt_fetch_content, 0);
    sqlite3_free(zSql);
    assert(rc == SQLITE_OK);
    // Bind value to it
    rc = sqlite3_bind_value(pTrgCur->stmt_fetch_content, 1, argv[0]);
    assert(rc == SQLITE_OK);
    // Notice that next will always be called before this function exists
    // triliteNext will advanced this to
  }
  
  // Advanced next
  rc = triliteNext(pCur);
  
  return rc;
}


/** Reset this cursor */
static int resetCursor(trilite_cursor *pTrgCur){
  int rc = SQLITE_OK;

  // Release expr if any
  if(pTrgCur->pExpr)
    exprRelease(pTrgCur->pExpr);
  pTrgCur->pExpr = NULL;
  
  // Select row from %_content
  if(pTrgCur->stmt_fetch_content){
    rc = sqlite3_finalize(pTrgCur->stmt_fetch_content);
    pTrgCur->stmt_fetch_content = NULL;
    assert(rc == SQLITE_OK);
  }

  // Set at end and forget idx  
  pTrgCur->eof = 1;     // Start at end, wait for xFilter
  pTrgCur->idxNum = 0;  // Set an invalid strategy
  
  return rc;
}


/** Move to next row, or set eof = true (non-zero) */
int triliteNext(sqlite3_vtab_cursor *pCur){
  trilite_cursor* pTrgCur = (trilite_cursor*)pCur;
  assert(pTrgCur->idxNum);
  int rc = SQLITE_OK;
  
  assert(!pTrgCur->eof);
  
  // Move next if in a full table scan or point query
  if(pTrgCur->idxNum & (IDX_FULL_SCAN | IDX_ROW_LOOKUP)){
    // Move content to next row
    rc = sqlite3_step(pTrgCur->stmt_fetch_content);
    // Set eof, if at end
    if(rc != SQLITE_ROW)
      pTrgCur->eof = 1;
    // If we're done or got a row, we're good :)
    if(rc == SQLITE_ROW || rc == SQLITE_DONE)
      rc = SQLITE_OK;
   
  // Move next in a match scan
  }else if(pTrgCur->idxNum & IDX_MATCH_SCAN){
    //TODO Implement support for ascending ordering
    assert(!(pTrgCur->idxNum & ORDER_BY_ASC));
   
    // Okay, we're looking for an id and is a result
    sqlite3_int64 id = - 1;
    if(!exprNextResult(&pTrgCur->pExpr, &id))
      pTrgCur->eof = 1;

    // Reset statement from previous row
    // Even if we don't have a result, we should release resources
    // preferably as soon as possible.
    sqlite3_reset(pTrgCur->stmt_fetch_content);
    
    // If we're not at the end, and we have result (implied by invariant)
    if(!pTrgCur->eof){
      assert(rc == SQLITE_OK);
      // Bind the result id we have
      rc = sqlite3_bind_int64(pTrgCur->stmt_fetch_content, 1, id);
      assert(rc == SQLITE_OK);
      // Step
      rc = sqlite3_step(pTrgCur->stmt_fetch_content);
      assert(rc == SQLITE_ROW);
      if(rc != SQLITE_ROW){
        return SQLITE_INTERNAL;
      }
      rc = SQLITE_OK;
    }
  }
  
  return rc;
}


/** Return true (non-zero) if we're at end */
int triliteEof(sqlite3_vtab_cursor *pCur){
  trilite_cursor* pTrgCur = (trilite_cursor*)pCur;
  assert(pTrgCur->idxNum);
  return pTrgCur->eof;  // Must be set in triliteNext
}

/** Return a value from the current row */
int triliteColumn(sqlite3_vtab_cursor *pCur, sqlite3_context *pCon, int iCol){
  trilite_cursor* pTrgCur = (trilite_cursor*)pCur;
  assert(pTrgCur->idxNum);
  assert(iCol < 2); // We only have two cols
  
  // Fetch result from current row
  sqlite3_value *pVal = sqlite3_column_value(pTrgCur->stmt_fetch_content, iCol);

  // Return result
  sqlite3_result_value(pCon, pVal);

  return SQLITE_OK;
}

/** Return the current rowid/id */
int triliteRowid(sqlite3_vtab_cursor *pCur, sqlite_int64 *id){
  trilite_cursor* pTrgCur = (trilite_cursor*)pCur;
  assert(pTrgCur->idxNum);
  // Output the current rowid/id
  *id = sqlite3_column_int64(pTrgCur->stmt_fetch_content, 0);
  return SQLITE_OK;
}

