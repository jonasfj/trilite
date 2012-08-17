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

#define MIN(A,B)            ((A) < (B) ? (A) : (B))

typedef struct doclist doclist;

static int resetCursor(trilite_cursor *pTrgCur);


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

  /** Extents recorded by triliteAddExtents */
  uint32_t* extents;

  /** Number slots available in extents */
  int nExtentsAvail;

  /** Number of extents recorded */
  int nExtents;

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

  // Set extents NULL
  pTrgCur->extents = NULL;
  pTrgCur->nExtents = 0;
  pTrgCur->nExtentsAvail = 0;

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
    log("Starting a match index scan");
    assert(argc > 0);
    // Get the pattern
    //TODO What happens if this is not a text value?

    // Parse query
    bool all;
    rc = exprParsePatterns(&pTrgCur->pExpr, &all, pTrgVtab, argc, argv);
    // Appropriate error should be reported by exprParse and friends
    if(rc != SQLITE_OK) return rc;

    // We didn't get any expression, because it matches all (ie. no filtering)
    if(!pTrgCur->pExpr && all){
      // TODO Have a table setting as to raise error instead!!!
      // TODO When doing this added setting for regexp size and runtime memory
      log("Switching to full table scan");
      // Change to a full table scan
      pTrgCur->idxNum = (idxNum & ~IDX_MATCH_SCAN) | IDX_FULL_SCAN;
      idxNum = pTrgCur->idxNum;
    }else{
      // Prepare statement for triliteNext
      zSql = sqlite3_mprintf("SELECT id, text FROM %Q.'%q_content' WHERE id = ?", pTrgVtab->zDb, pTrgVtab->zName);
      if(!zSql) return SQLITE_NOMEM;
      rc = sqlite3_prepare_v2(pTrgVtab->db, zSql, -1, &pTrgCur->stmt_fetch_content, 0);
      sqlite3_free(zSql);
      assert(rc == SQLITE_OK);
    }
    log("Expr and sql ready!");
  }
  
  // Full table scan
  if(idxNum & IDX_FULL_SCAN){
    log("Starting a full index scan");
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
  }

  // Simple rowid lookup
  if(idxNum & IDX_ROW_LOOKUP){
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

  // Free the extents
  if(pTrgCur->extents)
    sqlite3_free(pTrgCur->extents);
  pTrgCur->extents = NULL;
  pTrgCur->nExtents = 0;
  pTrgCur->nExtentsAvail = 0;

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
    
    // Reset the extents
    if(pTrgCur->extents)
      sqlite3_free(pTrgCur->extents);
    pTrgCur->extents = NULL;
    pTrgCur->nExtents = 0;
    pTrgCur->nExtentsAvail = 0;

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
int triliteColumn(sqlite3_vtab_cursor *pCur, sqlite3_context *pCtx, int iCol){
  trilite_cursor* pTrgCur = (trilite_cursor*)pCur;
  assert(pTrgCur->idxNum);
  assert(iCol < 3); // We only have three cols

  // Take special care of the contents column
  if(iCol == 2){
    sqlite3_result_blob(pCtx, &pTrgCur, sizeof(trilite_cursor*), SQLITE_TRANSIENT);
    return SQLITE_OK;
  }
  
  // Fetch result from current row
  assert(iCol < 2); // We only have 2 actual columns
  sqlite3_value *pVal = sqlite3_column_value(pTrgCur->stmt_fetch_content, iCol);

  // Return result
  sqlite3_result_value(pCtx, pVal);

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

/** Get cursor pointer from blob, returns SQLITE_OK on success */
int triliteCursorFromBlob(trilite_cursor **ppTrgCur, sqlite3_value *pBlob){
  if(sqlite3_value_type(pBlob) != SQLITE_BLOB || sqlite3_value_bytes(pBlob) != sizeof(trilite_cursor*))
    return SQLITE_ERROR;
  memcpy(ppTrgCur, sqlite3_value_blob(pBlob), sizeof(trilite_cursor*));
  return SQLITE_OK;
}

/** Get current text held by cursor */
int triliteText(trilite_cursor *pTrgCur, const unsigned char **pText, int *pnText){
  *pText  = sqlite3_column_text(pTrgCur->stmt_fetch_content, 1);
  *pnText = sqlite3_column_bytes(pTrgCur->stmt_fetch_content, 1);
  return SQLITE_OK;
}

/** Record the start and end of a extent that constitutes a match against the
 * current row, these values are returned by extents(contents) function in SQL.
 * Note, that the extents is reset at each call to triliteNext, and there's no
 * requirement for extents to be sorted */
int triliteAddExtents(trilite_cursor *pTrgCur, uint32_t start, uint32_t end){
  // Reallocate if there's no more memory
  if(pTrgCur->nExtentsAvail == 0){
    int nSlots = MIN((pTrgCur->nExtents + 1) * OFFSETS_REALLOC_FACTOR, MIN_OFFSETS_ALLOCATION);
    pTrgCur->extents = (uint32_t*)sqlite3_realloc(pTrgCur->extents, nSlots * sizeof(uint32_t) * 2);
    if(!pTrgCur->extents) return SQLITE_NOMEM;
    pTrgCur->nExtentsAvail = nSlots - pTrgCur->nExtents;
  }
  // Add the extents to the list
  pTrgCur->extents[pTrgCur->nExtents * 2]     = start;
  pTrgCur->extents[pTrgCur->nExtents * 2 + 1] = end;
  pTrgCur->nExtents++;
  pTrgCur->nExtentsAvail--;
  return SQLITE_OK;
}

/** Sql function to get extents from cursor */
void extentsFunction(sqlite3_context* pCtx, int argc, sqlite3_value** argv){
  // Validate the input
  if(argc != 1){
    sqlite3_result_error(pCtx, "The extents function takes exactly 1 paramter", -1);
    return;
  }
  // Get holds for the cursor object
  trilite_cursor *pTrgCur;
  if(triliteCursorFromBlob(&pTrgCur, argv[0]) != SQLITE_OK){
    sqlite3_result_error(pCtx, "The extents function only operates on the 'contents' column", -1);
    return;
  }
  // TODO Make a reference counted extents object, s.t. we don't need to copy
  // everything as is the case when using SQLITE_TRANSIENT
  // Then freeze it, so we can't realloc it, ie. don't added more extents to the
  // object after the call to this function.
  // In fact report an error if something like that happens...
  sqlite3_result_blob(pCtx, pTrgCur->extents, pTrgCur->nExtents * 2 * sizeof(uint32_t), SQLITE_TRANSIENT);
}


