#ifndef TRILITE_EXPR_H
#define TRILITE_EXPR_H

#include "config.h"

#include <sqlite3ext.h>

#include <stdbool.h>

/** Expression types */
enum expr_type{
  EXPR_TRIGRAM  = 1,
  EXPR_OP       = 1 << 1,
  EXPR_AND      = EXPR_OP | (1 << 2),
  EXPR_OR       = EXPR_OP | (1 << 3)
};

typedef enum expr_type expr_type;

int exprParsePatterns(expr**, bool*, trilite_vtab*, int, sqlite3_value**);
int  exprParse(expr**, bool*, trilite_vtab*, const unsigned char*, int);
void exprRelease(expr*);
bool exprNextResult(expr**, sqlite3_int64*);

int exprSubstring(expr**, bool*, trilite_vtab*, const unsigned char*, int);
int exprTrigram(expr**, trilite_vtab*, trilite_trigram);
int exprOperator(expr**, expr*, expr*, expr_type);

#endif /* TRILITE_EXPR_H */
