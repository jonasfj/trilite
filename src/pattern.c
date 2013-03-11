#include "triliteInt.h"

/** TriLite expression */
struct trilite_expression{
  int                     eType;          /** Expression type */
  union{
    /** Contents when eType is TRILITE_EXPR_TRIGRAM */
    struct{
      uint32_t            nSize;          /** Remaining bytes in doclist */
      unsigned char       *docList;       /** Currently location of doclist */
    } trigram;

    /** Contents when eType is TRILITE_EXPR_OP_% */
    struct{
      uint16_t            nOperands;      /** Length of pOperands */
      uint16_t            nOperandsAvail; /** Remaining operand slots */
      trilite_expression  *pOperands;     /** Operands (subexpressions) */
    } op;
  } expr;
};

/** TriLite expression type */
#define TRILITE_EXPR_TRIGRAM            1

/** Operator expression flag */
#define TRILITE_EXPR_OP                 (1 << 1)

/** None and any expression pointer values
 * None and any expressions are implemented as fly-weight, without any actual
 * object. That's we use the 0 pointer value to indicate none and the pointer
 * value to mean 1.
 * These can obviously never be real addresses and should never by dereferenced
 * but the consumer doesn't need to know that.
 */
#define EXPR_NONE                       ((trilite_expression*)0)
#define EXPR_ANY                        ((trilite_expression*)1)


void trilite_error(trilite_context *pCtx, const char *zFmt, ...);
void trilite_result_extent(trilite_context *pCtx, int start, int end);
int trilite_register_format(trilite_pattern_format *pPatternFormat);

/** Create any expression */
int trilite_expr_any(
  trilite_context         *pCtx,      /** TriLite invocation context */
  trilite_expression      **ppExpr    /** OUT: Trigram expression object */
){
  assert(pCtx && ppExpr);
  UNUSED_PARAMETER(pCtx);
  *ppExpr = EXPR_NONE;
  return SQLITE_OK;
}

/** Create none expression */
int trilite_expr_none(
  trilite_context         *pCtx,      /** TriLite invocation context */
  trilite_expression      **ppExpr    /** OUT: Trigram expression object */
){
  assert(pCtx && ppExpr);
  UNUSED_PARAMETER(pCtx);
  *ppExpr = EXPR_ANY;
  return SQLITE_OK;
}

/** Create trigram expression */
int trilite_expr_trigram(
  trilite_context         *pCtx,      /** TriLite invocation context */
  trilite_trigram         trigram,    /** Trigram */
  trilite_expression      **ppExpr    /** OUT: Trigram expression object */
){
  assert(pCtx && ppExpr);
  /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}

/** Create expr expression */
int trilite_expr_string(
  trilite_context         *pCtx,      /** TriLite invocation context */
  int                     nString,    /** Length of input string */
  const unsigned char     *zString,   /** String to extract trigrams from */
  trilite_expression      **ppExpr    /** OUT: Trigram expression object */
){
  assert(pCtx && ppExpr);
  /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}

/** Create operator expression */
int trilite_expr_operator(
  trilite_context         *pCtx,      /** TriLite invocation context */
  int                     eType,      /** Operator type (TRILITE_EXPR_OP_*) */
  int                     nSize,      /** Maximum number of entries */
  trilite_expression      **ppExpr    /** OUT: Trigram expression object */
){
  assert(pCtx && ppExpr);
  /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}

/** Append operand to operator expression */
int trilite_expr_append(
  trilite_context         *pCtx,       /** TriLite invocation context */
  trilite_expression      **ppOperator,/** Operator expression */
  trilite_expression      *pOperand    /** Operand expression to be appended */
){
  assert(pCtx && ppOperator && pOperand);
  /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}

/** Extract next document-id from expression */
int exprNext(
  trilite_expression      **pExpr,    /** Trigram expression */
  bool                    *pDone,     /** OUT: True, if done (ie. at end) */
  bool                    *pAny,      /** OUT: True, if matches any */
  sqlite3_int64           *pId        /** OUT: Id of next row */
){
  /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}

/** Free expression */
int exprDestroy(
  trilite_expression      *pExpr      /** Trigram expression */
){
  if(pExpr != EXPR_NONE || pExpr != EXPR_ANY){
    if(pExpr->eType & TRILITE_EXPR_OP){
      assert(pExpr->eType == TRILITE_EXPR_OP_OR ||
             pExpr->eType == TRILITE_EXPR_OP_AND);
      int i;
      for(i = 0; i < pExpr->expr.op.nOperands; i++)
        exprDestroy(pExpr->expr.op.pOperands + i);
    }
    /* Doclist or list of operands are allocated with expression */
    sqlite3_free(pExpr);
    pExpr = NULL;
  }
  return SQLITE_OK;
}