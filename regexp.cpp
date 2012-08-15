// All of our headers should be declared external C in C++
// Since this is the ONLY C++ file we'll needs, let's only
// do that here.
extern "C" {

#include "regexp.h"
#include "vtable.h"
#include "expr.h"

}

// DON'T USE SQLITE API FROM THIS FILE!
// Notice that it is *not* possible to forward declare global variables in C++
// and as sqlite3 extension API relies on sqlite3_api* being forward declared in
// every file (and defined in trilite.c) we can't access this variable from C++
// Fortunately we're mainly interested in generating error codes here, as all
// other logic should be handled elsewhere, and luckily it makes a lot of sense
// to make a simple wrapper function for outputting error messages, so this is
// not an issue.
// But for future reference don't use the sqlite3 API from this function!

#include <assert.h>
#include <stdbool.h>

#include <string>


#include <re2/re2.h>
#include <re2/prefilter.h>

static int exprFromPreFilter(expr**, trilite_vtab*, re2::Prefilter*, bool*);

/** Construct a filter expression from a regular expression
 * Returns SQLITE_ERROR and sets user relevant error message on error */
int regexpPreFilter(expr **ppExpr, trilite_vtab *pTrgVtab, const unsigned char *expr, int nExpr){
  int rc = SQLITE_OK;

  *ppExpr = NULL;
  
  // Create regular expression from string
  re2::RE2 re(re2::StringPiece((const char*)expr, nExpr), re2::RE2::Quiet);

  // TODO if re.ProgramSize() > SOME_THRESHOLD return and error message 
  // Ideally this threshold should be a runtime setting

  // Provide error message if regular expression compilation failed
  if(!re.ok()){
    // Error codes from re2
    std::string msg;
    switch(re.error_code()){
      case re2::RE2::ErrorBadEscape:
        msg = "Bad escape sequence at '%s'";
        break;
      case re2::RE2::ErrorBadCharClass:
        msg = "Bad character class at '%s'";
        break;
      case re2::RE2::ErrorBadCharRange:
        msg = "Bad character range at '%s'";
        break;
      case re2::RE2::ErrorMissingBracket:
        msg = "Missing bracket in '%s'";
        break;
      case re2::RE2::ErrorMissingParen:
        msg = "Missing paranthesis in '%s'";
        break;
      case re2::RE2::ErrorTrailingBackslash:
        msg = "Trailing backslash in '%s'";
        break;
      case re2::RE2::ErrorRepeatArgument:
        msg = "Repeat argument missing in '%s'";
        break;
      case re2::RE2::ErrorRepeatSize:
        msg = "Bad repeat argument at '%s'";
        break;
      case re2::RE2::ErrorRepeatOp:
        msg = "Bad repear operator at '%s'";
        break;
      case re2::RE2::ErrorBadPerlOp:
        msg = "Bad perl operator at '%s'";
        break;
      case re2::RE2::ErrorBadUTF8:
        msg = "Invalid UTF-8 at '%s'";
        break;
      case re2::RE2::ErrorBadNamedCapture:
        msg = "Bad named capture group at '%s'";
        break;
      case re2::RE2::ErrorPatternTooLarge:
        msg = "Pattern '%s' is too large";
        break;
      case re2::RE2::ErrorInternal:
      default:
        msg = "Unknown internal error at '%s'";
        break;
    }
    // Now set the error message
    triliteError(pTrgVtab, ("REGEXP: " + msg).c_str(), re.error_arg().c_str());

    // Return error
    return SQLITE_ERROR;
  }

  // Compute a prefilter
  re2::Prefilter* pf = re2::Prefilter::FromRE2(&re);

  // Provide error message if a filter couldn't be devised, as we shall not
  // permit full table scans.
  if(!pf){
    triliteError(pTrgVtab, "REGEXP: Failed to build a filter for the regular expression");
    return SQLITE_ERROR;
  }

  bool all;
  rc = exprFromPreFilter(ppExpr, pTrgVtab, pf, &all);
  assert(rc == SQLITE_OK);

  // if no expr and all, it matches everything and we raise an error
  // Notice if all is false, we have that nothing matches and we just leave
  // ppExpr NULL, and everything is fine...
  if(!*ppExpr && all){
    triliteError(pTrgVtab, "REGEXP: Failed to build a filter for the regular expression");
    return SQLITE_ERROR;
  }

  // Release the prefilter
  delete pf;

  return rc;
}

/** Construct an expr from a prefilter
 * Returns SQLITE_OK on success, outputs expression as *ppExpr, if NULL, *all
 * determines if it's because everything matches the expr or nothing matches the
 * expression, ie. *all == true, implies everything matches the expression */
static int exprFromPreFilter(expr **ppExpr, trilite_vtab *pTrgVtab, re2::Prefilter* pf, bool *pAll){
  int rc = SQLITE_OK;
  assert(pf && pAll);
  *ppExpr = NULL;
  if(pf->op() == re2::Prefilter::ALL){
    *pAll = true;
    return SQLITE_OK;
  }
  if(pf->op() == re2::Prefilter::NONE){
    *pAll = false;
    return SQLITE_OK;
  }

  // If we have an atom it's a substring
  if(pf->op() == re2::Prefilter::ATOM){
    // If there's no trigrams in the atom, it matches everything
    if(pf->atom().size() < 3){
      *pAll = true;
      return SQLITE_OK;
    }
    // Construct expr from substring
    rc = exprSubstring(ppExpr, pTrgVtab, (const unsigned char*)pf->atom().c_str(), pf->atom().size());
    return rc;
  }

  // Get the operator type
  expr_type eType = EXPR_OR;
  if(pf->op() == re2::Prefilter::AND)
    eType = EXPR_AND;
  assert(pf->op() == re2::Prefilter::OR);

  // Get sub expressions
  std::vector<re2::Prefilter*>* subs = pf->subs();

  size_t i;
  for(i = 0; i < subs->size(); i++){
    bool all;
    expr *pExpr = NULL;
    rc = exprFromPreFilter(&pExpr, pTrgVtab, (*subs)[i], &all);
    assert(rc == SQLITE_OK);
    // Abort if we get an error
    if(rc != SQLITE_OK){
      exprRelease(pExpr);
      exprRelease(*ppExpr);
      *ppExpr = NULL;
      return SQLITE_ERROR;
    }
    // if we didn't get an expr
    if(!pExpr){
      // if all and operator is OR, we're done
      if(all && eType == EXPR_OR){
        *pAll = true;
        exprRelease(*ppExpr);
        *ppExpr = NULL;
        return SQLITE_OK;
      }
      // if !all and operator is AND, we're done
      if(!all && eType == EXPR_AND){
        *pAll = false;
        exprRelease(*ppExpr);
        *ppExpr = NULL;
        return SQLITE_OK;
      }
      // If !all and OR, or all and AND, we simply ignore this term
      continue;
    }

    // Add pExpr to ppExpr
    if(*ppExpr){
      exprOperator(ppExpr, *ppExpr, pExpr, eType);
    }else
      *ppExpr = pExpr;
  }

  return SQLITE_OK;
}



/************************ Regular Expression Wrapper *************************/

/** Wrapping the regular expression in a struct
 * This is less ugly than casting the regexp pointer to an re2::RE2 pointer,
 * and technically this shouldn't add any overhead */
struct regexp{
  /** The cached and compiled regular expression */
  re2::RE2 re;

  /** Stupid constructor for re */
  regexp(re2::StringPiece pattern) : re(pattern, re2::RE2::Quiet) {}
};

/** Compile a regular expression */
int regexpCompile(regexp **ppRegExp, const unsigned char *pattern, int nPattern){
  *ppRegExp = new regexp(re2::StringPiece((const char*)pattern, nPattern));
  if(!*ppRegExp) return SQLITE_NOMEM;
  if(!(*ppRegExp)->re.ok()){
    delete *ppRegExp;
    *ppRegExp = NULL;
    return SQLITE_ERROR;
  }
  return SQLITE_OK;
}

/** Match with a regular expression */
bool regexpMatch(regexp *pRegExp, const unsigned char *text, int nText){
  return re2::RE2::PartialMatch(re2::StringPiece((const char*)text, nText), pRegExp->re);
}

/** Release regular expression */
void regexpRelease(regexp *pRegExp){
  delete pRegExp;
}



