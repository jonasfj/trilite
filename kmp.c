#include "kmp.h"

#include <sqlite3ext.h>
const sqlite3_api_routines *sqlite3_api;

// This is an implementation of the Knuth–Morris–Pratt algorithm for string
// matching. The implementation with a few fixes is adopted from:
// http://www-igm.univ-mlv.fr/~lecroq/string/node8.html
// Who deserves credit for C'ifying this algorithm.
// Ideally, we should probably have an optimized implementation using SSE
// instructions or similar, consider looking at memmem from glibc.

/** Create a kmp context for matching substring using pattern */
void kmpCreate(kmp_context **ppContext, const unsigned char *pattern, int nPattern){
  *ppContext = (kmp_context*)sqlite3_malloc((nPattern + 1) * sizeof(int));
  
  int *kmp = *ppContext;
  int i = 0;
  int j = kmp[0] = -1;
  while(i < nPattern){
    while(j > -1 && pattern[i] != pattern[j])
      j = kmp[j];
    i++;
    j++;
    if(pattern[i] == pattern[j])
      kmp[i] = kmp[j];
    else
      kmp[i] = j;
  }
}

/** Test if a text has a substring using prebuilt context (built with associated pattern!) */
bool kmpTest(kmp_context *pContext, const unsigned char *text, int nText, const unsigned char *pattern, int nPattern){
  int i, j;
  int *kmp = pContext;
  i = j = 0;
  while(j < nText){
    while(i > -1 && pattern[i] != text[j])
      i = kmp[i];
    i++;
    j++;
    if(i >= nPattern){
      // Report match j - i as match offset
      return true;
      // We can get all matches if we continue here, but for the moment, we just
      // wan't the boolean answer
      // i = kmp[i];
    }
  }
  return false;
}

/** Release the kmp context */
void kmpRelease(kmp_context *pContext){
  sqlite3_free(pContext);
}


