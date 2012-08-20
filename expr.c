#include "expr.h"
#include "vtable.h"
#include "cursor.h"
#include "varint.h"
#include "regexp.h"

const sqlite3_api_routines *sqlite3_api;

#include <string.h>
#include <assert.h>

#define MAX(a,b)    ((a) < (b) ? (b) : (a))
#define MIN(a,b)    ((a) > (b) ? (b) : (a))

/** Expression structure */
struct expr{ 
  /** Type of this expression */
  expr_type eType;
 
  /** Contents, depending on eType */
  union{

    /** Trigram Expression, valid when eType == EXPR_TRIGRAM */
    struct{
      /** Number of bytes left in doclist */
      int nSize;

      /** Pointer to current offset in doclist
       * Please be advised docList is allocated with the expression */
      unsigned char *docList;

      /** Last id read from docList */
      sqlite3_int64 curId;
    } trigram;

    /** Operator expression, when eType & EXPR_OP */
    struct {
      /** Expression 1 */
      expr *expr1;
      /** Expression 2 */
      expr *expr2;
    } op;
  } expr;
};

/** Parse a sequence of patterns that must hold into a single expression
 * Return *ppExpr = NULL and *pAll = true, if a full table scan is required
 * if *ppExpr = NULL and *pAll = false, implies that nothing matches the
 * patterns, ie. resultset is empty! */
int exprParsePatterns(expr **ppExpr, bool *pAll, trilite_vtab *pTrgVtab, int argc, sqlite3_value **argv){
  int rc = SQLITE_OK;
  *ppExpr = NULL;
  /* For each pattern add it to the others with a AND */
  int i;
  for(i = 0; i < argc; i++){
    const unsigned char* pattern  = sqlite3_value_text(argv[i]);
    int                  nPattern = sqlite3_value_bytes(argv[i]);
    expr *pExpr;
    rc = exprParse(&pExpr, pAll, pTrgVtab, pattern, nPattern);
    /* Release and return on error, error message is already set */
    if(rc != SQLITE_OK) goto abort;
    /* If one of the and conditions fails, we're done */
    if(!pExpr && !pAll) goto abort;
    /* If matches all continue */
    if(!pExpr && pAll) continue;
    /* Combine with an AND */
    if(*ppExpr)
      rc = exprOperator(ppExpr, *ppExpr, pExpr, EXPR_AND);
    else
      *ppExpr = pExpr;
    if(rc != SQLITE_OK) goto abort;
  }
  /* We didn't get anything, we would have aborted before this */
  /* unless, everything is accepted! */
  if(!*ppExpr) *pAll = true;
  return rc;
abort:
  *pAll = false;
  exprRelease(*ppExpr);
  *ppExpr = NULL;
  return rc;
}

/** Parse expression, load doclists and output it to ppExpr */
int exprParse(expr **ppExpr, bool *pAll, trilite_vtab *pTrgVtab, const unsigned char *pattern, int nPattern){
  if(strncmp((const char*)pattern, "substr:", 7) == 0){
    return exprSubstring(ppExpr, pAll, pTrgVtab, pattern + 7, nPattern - 7);
  }else if(strncmp((const char*)pattern, "substr-extents:", 15) == 0){
    return exprSubstring(ppExpr, pAll, pTrgVtab, pattern + 15, nPattern - 15);
  }else if(strncmp((const char*)pattern, "regexp:", 7) == 0){
    return regexpPreFilter(ppExpr, pAll, pTrgVtab, pattern + 7, nPattern - 7);
  }else if(strncmp((const char*)pattern, "regexp-extents:", 15) == 0){
    return regexpPreFilter(ppExpr, pAll, pTrgVtab, pattern + 15, nPattern - 15);
  }else{
    triliteError(pTrgVtab, "MATCH pattern must be a regular expression or a substring pattern!");
    return SQLITE_ERROR;
  }
}


/** Release resources held by expression */
void exprRelease(expr *pExpr){
  if(!pExpr) return;
  if(pExpr->eType & EXPR_OP){
    exprRelease(pExpr->expr.op.expr1);
    pExpr->expr.op.expr1 = NULL;
    exprRelease(pExpr->expr.op.expr2);
    pExpr->expr.op.expr2 = NULL;
  }
  sqlite3_free(pExpr);
}

/** Next candidate id */
static sqlite3_int64 exprNextCandidate(expr *pExpr){
  assert(pExpr);
  if(pExpr->eType & EXPR_OP){
    sqlite3_int64 v1 = exprNextCandidate(pExpr->expr.op.expr1);
    sqlite3_int64 v2 = exprNextCandidate(pExpr->expr.op.expr2);
    if(pExpr->eType == EXPR_AND)
      return MAX(v1, v2);
    assert(pExpr->eType == EXPR_OR);
    return MIN(v1, v2);
  }else{
    assert(pExpr->eType == EXPR_TRIGRAM);
    return pExpr->expr.trigram.curId;
  }
}

/** Check if id is a result and move to next id
 * Set ppExpr = NULL, if at end */
static bool exprCheckAndMove(expr **ppExpr, sqlite3_int64 id){
  assert(ppExpr && *ppExpr);
  if((*ppExpr)->eType & EXPR_OP){
    bool r1 = exprCheckAndMove(&(*ppExpr)->expr.op.expr1, id);
    bool r2 = exprCheckAndMove(&(*ppExpr)->expr.op.expr2, id);
    if((*ppExpr)->eType == EXPR_AND){
      /* If one of them is at end, we're done */
      if(!(*ppExpr)->expr.op.expr1 || !(*ppExpr)->expr.op.expr2){
        exprRelease((*ppExpr)->expr.op.expr1);
        exprRelease((*ppExpr)->expr.op.expr2);
        sqlite3_free(*ppExpr);
        *ppExpr = NULL;
      }
      return r1 && r2;
    }
    assert((*ppExpr)->eType == EXPR_OR);
    if(!(*ppExpr)->expr.op.expr1 && !(*ppExpr)->expr.op.expr2){
      sqlite3_free(*ppExpr);
      *ppExpr = NULL;
    }else if(!(*ppExpr)->expr.op.expr1){
      expr* expr2 = (*ppExpr)->expr.op.expr2;
      sqlite3_free(*ppExpr);
      *ppExpr = expr2;
    }else if(!(*ppExpr)->expr.op.expr2){
      expr* expr1 = (*ppExpr)->expr.op.expr1;
      sqlite3_free(*ppExpr);
      *ppExpr = expr1;
    }
    return r1 || r2;
  }else{
    assert((*ppExpr)->eType == EXPR_TRIGRAM);
    while((*ppExpr)->expr.trigram.nSize > 0 && (*ppExpr)->expr.trigram.curId < id){
      sqlite3_int64 prev = (*ppExpr)->expr.trigram.curId;
      int read = readVarInt((*ppExpr)->expr.trigram.docList, &(*ppExpr)->expr.trigram.curId);
      (*ppExpr)->expr.trigram.curId   += prev;
      (*ppExpr)->expr.trigram.nSize   -= read;
      (*ppExpr)->expr.trigram.docList += read;
    }
    bool retval = (*ppExpr)->expr.trigram.curId == id;
    /* Check if we have to move forward */
    if((*ppExpr)->expr.trigram.curId <= id){
      /* If we can't we're at the end and done */
      if((*ppExpr)->expr.trigram.nSize == 0){
        sqlite3_free(*ppExpr);
        *ppExpr = NULL;
      }else{
        sqlite3_int64 prev = (*ppExpr)->expr.trigram.curId;
        int read = readVarInt((*ppExpr)->expr.trigram.docList, &(*ppExpr)->expr.trigram.curId);
        (*ppExpr)->expr.trigram.curId   += prev;
        (*ppExpr)->expr.trigram.nSize   -= read;
        (*ppExpr)->expr.trigram.docList += read;
      }
    }
    return retval;
  }
}

/** Get the next result id
 * Returns true, if *pId is a result, sets ppExpr NULL there's nothing more */
bool exprNextResult(expr **ppExpr, sqlite3_int64 *pId){
  while(*ppExpr){
    *pId = exprNextCandidate(*ppExpr);
    if(exprCheckAndMove(ppExpr, *pId))
      return true;
  }
  assert(!*ppExpr);
  return false;
}


/** Create an expression for matching substrings */
int exprSubstring(expr **ppExpr, bool *pAll, trilite_vtab *pTrgVtab, const unsigned char *string, int nString){
  int rc = SQLITE_OK;
  *ppExpr = NULL;

  /* There should be trigrams here, these special cases should be handled elsewhere */
  if(nString < 3){
    *pAll = true;
    return SQLITE_OK;
  }

  int i;
  for(i = 0; i < nString - 2; i++){
    trilite_trigram trigram = HASH_TRIGRAM(string + i);
    /* Get a trigram expression for the trigram */
    expr *pTrgExpr;
    rc = exprTrigram(&pTrgExpr, pTrgVtab, trigram);
    /* If there's no trigramExpr that satisfy our conditions */
    /* we're done here as the substring can't be matched! */
    if(!pTrgExpr){
      exprRelease(*ppExpr);
      *pAll = false;
      *ppExpr = NULL; /* Can't satisfy this tree */
      return rc;
    }

    /* Combine with an AND expression */
    if(*ppExpr){
      rc = exprOperator(ppExpr, *ppExpr, pTrgExpr, EXPR_AND);
      assert(rc == SQLITE_OK);
    }else
      *ppExpr = pTrgExpr;
  }

  return rc;
}


/** Create a trigram expression for matching against a single trigram */
int exprTrigram(expr **ppExpr, trilite_vtab *pTrgVtab, trilite_trigram trigram){
  int rc = SQLITE_OK;

  sqlite3_blob *pBlob;
  char *zTable = sqlite3_mprintf("%s_index", pTrgVtab->zName);
  /* Open the blob */
  rc = sqlite3_blob_open(pTrgVtab->db, pTrgVtab->zDb, zTable, "doclist", trigram, 0, &pBlob);
  sqlite3_free(zTable);

  /* If we didn't get a blob */
  if(rc != SQLITE_OK){
    *ppExpr = NULL;
    return SQLITE_OK;
  }
  /* Get size of blob */
  int nSize = sqlite3_blob_bytes(pBlob);

  /* Allocate space for expr and doclist at the same time */
  *ppExpr = (expr*)sqlite3_malloc(sizeof(expr) + nSize);

  /* Set the expr */
  (*ppExpr)->eType                 = EXPR_TRIGRAM;
  (*ppExpr)->expr.trigram.docList  = ((unsigned char*)(*ppExpr)) + sizeof(expr);
  (*ppExpr)->expr.trigram.nSize    = nSize;

  /* Read doclist into memory */
  sqlite3_blob_read(pBlob, (*ppExpr)->expr.trigram.docList, nSize, 0);

  /* Release blob */
  sqlite3_blob_close(pBlob);

  /* Read first id */
  int read = readVarInt((*ppExpr)->expr.trigram.docList, &(*ppExpr)->expr.trigram.curId);
  (*ppExpr)->expr.trigram.curId   += DELTA_LIST_OFFSET;
  (*ppExpr)->expr.trigram.nSize   -= read;
  (*ppExpr)->expr.trigram.docList += read;

  return rc;
}

/** Create an operator expression */
int exprOperator(expr** ppExpr, expr* pExpr1, expr* pExpr2, expr_type eType){
  assert(eType & EXPR_OP);
  *ppExpr = (expr*)sqlite3_malloc(sizeof(expr));
  if(!*ppExpr) return SQLITE_NOMEM;
  (*ppExpr)->eType = eType;
  (*ppExpr)->expr.op.expr1 = pExpr1;
  (*ppExpr)->expr.op.expr2 = pExpr2;
  return SQLITE_OK;
}

