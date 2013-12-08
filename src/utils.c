#include "triliteInt.h"

/** Extract unqiue trigrams from a text 
 * This function will output an array of unique trigrams to *ppTrigrams, this
 * array must be freed using sqlite3_free when it's no longer needed.
 */
void extractTrigrams(
  const char          *zText,         /** Text to extract trigrams from */
  int                 nText,          /** Lengths of zText */
  trilite_trigram     **ppTrigrams,   /** OUT: Array of unqiue trigrams */
  int                 *nTrigrams      /** OUT: Length of ppTrigrams */
){
  assert(false);
}

/** Quicksort with 3-way partitioning of trigrams
 * Sorts an array of trigrams using a 3-way quicksort. This quicksort adaption
 * should perform fairly well for arrays with duplicate keys.
 * See: http://www.sorting-algorithms.com/static/QuicksortIsOptimal.pdf
 */
void sortTrigrams(
  trilite_trigram     *pTrigrams,     /** Array of trigrams to sort */
  int                 iStart,         /** Start index to sort from */
  int                 iEnd            /** End index to sort to */
){
  assert(false);
}



#ifndef NDEBUG

/** Determine if zFunction matches a pattern in FUNCTIONS_TO_LOG
 * This is a utility function only present when NDEBUG is defined.
 */
bool isFunctionToLog(
  const char          *zFunction      /** Function name to check */
){
  /* List of functions to log, as given in triliteInt.h */
  const char* pzFunctions[] = FUNCTIONS_TO_LOG;
  int i = 0;
  while(pzFunctions[i] != NULL){
    int j = 0;
    while(pzFunctions[i][j] == zFunction[j] && pzFunctions[i][j] != '\0')
      j++;
    if(pzFunctions[i][j] == zFunction[j] || pzFunctions[i][j] == '*')
      return true;
  }
  return false;
}

#endif /* NDEBUG */