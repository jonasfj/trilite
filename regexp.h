#ifndef TRILITE_REGEXP_H
#define TRILITE_REGEXP_H

#include <stdbool.h>

typedef struct expr expr;
typedef struct trilite_vtab trilite_vtab;

typedef struct regexp regexp;

int regexpPreFilter(expr**, trilite_vtab*, const unsigned char*, int);

int regexpCompile(regexp**, const unsigned char*, int);
bool regexpMatch(regexp*, const unsigned char*, int);
void regexpRelease(regexp*);

#endif /* TRILITE_REGEXP_H */
