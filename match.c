#include "match.h"

const sqlite3_api_routines *sqlite3_api;

#include "kmp.h"
#include "scanstr.h"
#include "cursor.h"
#include "regexp.h"

#include <assert.h>
#include <string.h>

typedef enum pattern_type pattern_type;

/** Different pattern types */
enum pattern_type{
  /** Invalid pattern function */
  PATTERN_INVALID   = 0,
  /** Substring pattern */
  PATTERN_SUBSTR    = 1<<1,
  /** Regular expression pattern */
  PATTERN_REGEXP    = 1<<2,
  /** Flag set if extents should be computed */
  PATTERN_EXTENTS   = 1<<3
};

typedef struct aux_pattern_data aux_pattern_data;

/** Auxiliary data structure used to store compiled data for the match function */
struct aux_pattern_data{
  /** Match function type */
  pattern_type eType;

  /** Regular expression */
  regexp *pRegExp;

  /** Pattern */
  unsigned char *pattern;

  /** Length of pattern */
  int nPattern;
};

/** Release auxiliary data structure */
void matchAuxDataFree(aux_pattern_data *pAuxData){
  if(pAuxData->pRegExp)
    regexpRelease(pAuxData->pRegExp);
  pAuxData->pRegExp = NULL;
  sqlite3_free(pAuxData);
}

//TODO Implement regexpReportErrors...

/** Create auxiliary data for matchFunction, leaves ppAuxData NULL on pattern
 * failure */
void matchCreate(aux_pattern_data **ppAuxData, const unsigned char *pattern, int nPattern){
  // Allocate a structure
  *ppAuxData = (aux_pattern_data*)sqlite3_malloc(sizeof(aux_pattern_data) + nPattern + 1);

  // Copy the pattern, including the null char
  (*ppAuxData)->pattern = (unsigned char*)(*ppAuxData + 1);

  // Let's figure out what kind of pattern we have
  int offset = 0;
  if(strncmp((const char*)pattern, "substr:", 7) == 0){
    (*ppAuxData)->eType     = PATTERN_SUBSTR;
    (*ppAuxData)->pRegExp   = NULL;
    offset                  = 7;
  }else if(strncmp((const char*)pattern, "substr-extents:", 15) == 0){
    (*ppAuxData)->eType     = PATTERN_SUBSTR | PATTERN_EXTENTS;
    (*ppAuxData)->pRegExp   = NULL;
    offset                  = 15;
  }else if(strncmp((const char*)pattern, "regexp:", 7) == 0){
    (*ppAuxData)->eType = PATTERN_REGEXP;
    int rc              = regexpCompile(&(*ppAuxData)->pRegExp, pattern + 7, nPattern - 7);
    offset              = 7;
    assert(rc == SQLITE_OK);  //Errors should be handled else where
  }else if(strncmp((const char*)pattern, "regexp-extents:", 15) == 0){
    (*ppAuxData)->eType = PATTERN_REGEXP | PATTERN_EXTENTS;
    int rc              = regexpCompile(&(*ppAuxData)->pRegExp, pattern + 15, nPattern - 15);
    offset              = 15;
    assert(rc == SQLITE_OK);  //Errors should be handled else where
  //}else if(strncmp(pattern, "egrep:", 6) == 0){
  //  pAuxData->eType = PATTERN_REGEXP;
  //  pfxLen = 6;
  }else{
    sqlite3_free(*ppAuxData);
    *ppAuxData = NULL;
    return;
  }

  memcpy((*ppAuxData)->pattern, pattern + offset, nPattern - offset);
  (*ppAuxData)->pattern[nPattern - offset] = '\0';
  (*ppAuxData)->nPattern = nPattern - offset;
}

/** Custom match function that filters to exact matches
 * 
 * If no auxiliary data is stored, we decode the pattern, compile regular
 * expressions, etc. and store aux_pattern_data struct as auxiliary data for the
 * pattern.
 */
void matchFunction(sqlite3_context* pCtx, int argc, sqlite3_value** argv){
  // Validate the input
  if(argc != 2){
    sqlite3_result_error(pCtx, "The MATCH operator on a trigram index takes 2 arguments!", -1);
    return;
  }
  trilite_cursor *pTrgCur;
  if(triliteCursorFromBlob(&pTrgCur, argv[1]) != SQLITE_OK){
    sqlite3_result_error(pCtx, "The MATCH operator must have 'contents' as left hand side", -1);
    return;
  }

  // Get prepared auxiliary data, if any
  aux_pattern_data *pAuxData = (aux_pattern_data*)sqlite3_get_auxdata(pCtx, 0);
  // If we didn't get any auxiliary data, create it and store it!
  if(!pAuxData){
    // Check that we have correct datatype for the pattern
    if(sqlite3_value_type(argv[0]) != SQLITE_TEXT){
      sqlite3_result_error(pCtx, "The pattern for the MATCH operator on a trigram index must be a string", -1);
      return;
    }
    // Get the pattern
    const unsigned char *pattern = sqlite3_value_text(argv[0]);
    int nPattern                 = sqlite3_value_bytes(argv[0]);

    // Create some auxiliary data :)
    matchCreate(&pAuxData, pattern, nPattern);
    if(!pAuxData){
      // Die on errors, there shouldn't be any here if match is called correctly
      sqlite3_result_error(pCtx, "The match operator needs a valid pattern", -1);
      return;
    }

    // Save the auxiliary data for next time
    sqlite3_set_auxdata(pCtx, 0, pAuxData, (void(*)(void*))matchAuxDataFree);
  }
  

  // Get the text from the cursor
  const unsigned char *text;
  int nText;
  triliteText(pTrgCur, &text, &nText);

  bool retval = false;
  if(pAuxData->eType & PATTERN_SUBSTR){
    const unsigned char *start = scanstr(text, nText, pAuxData->pattern, pAuxData->nPattern);
    retval = start != NULL;
    // If output extents is requested
    if(pAuxData->eType & PATTERN_EXTENTS){
      while(start){
        const unsigned char *end = start + pAuxData->nPattern;
        triliteAddExtents(pTrgCur, start - text, end - text);
        start = scanstr(end, nText - (end - text), pAuxData->pattern, pAuxData->nPattern);
      }
    }
  } else if(pAuxData->eType == PATTERN_REGEXP){
    assert(pAuxData->pRegExp);
    retval = regexpMatch(pAuxData->pRegExp, text, nText);
  } else if(pAuxData->eType == (PATTERN_REGEXP | PATTERN_EXTENTS)){
    const unsigned char *start = text;
    const unsigned char *end   = text;
    while(regexpMatchExtents(pAuxData->pRegExp, &start, &end, end, nText - (end - text))){
      retval = true;
      triliteAddExtents(pTrgCur, start - text, end - text);
    }
  } else {
    assert(false);
    sqlite3_result_error(pCtx, "The pattern must be either a regular expression or substring pattern", -1);
    return;
  }

  // Return true (1) if pattern in a substring of text
  if(retval){
    sqlite3_result_int(pCtx, 1);
  }else{
    sqlite3_result_int(pCtx, 0);
  }
}



