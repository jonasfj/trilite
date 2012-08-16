#ifndef TRILITE_REGEXP_H
#define TRILITE_REGEXP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct expr expr;
typedef struct trilite_vtab trilite_vtab;

typedef struct regexp regexp;

int regexpPreFilter(expr**, bool*, trilite_vtab*, const unsigned char*, int);

//TODO Add refernece counting to regular expressions
// and reuse previously compiled expressions, when loading from cursor
int regexpCompile(regexp**, const unsigned char*, int);
bool regexpMatch(regexp*, const unsigned char*, int);
bool regexpMatchExtents(regexp*, const unsigned char**, const unsigned char**, const unsigned char*, int);
void regexpRelease(regexp*);

#endif /* TRILITE_REGEXP_H */
