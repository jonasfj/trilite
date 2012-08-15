#ifndef TRILITE_KMP_H
#define TRILITE_KMP_H

#include <stdbool.h>

typedef int kmp_context;

void kmpCreate(kmp_context**, const unsigned char*, int);
bool kmpTest(kmp_context*, const unsigned char*, int, const unsigned char*, int);
void kmpRelease(kmp_context*);

#endif /* TRILITE_KMP_H */
