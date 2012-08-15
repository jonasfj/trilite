#include "vtable.h"

#include "trilite.h"
#include "cursor.h"
#include "config.h"

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

/** Trigram module */
static const sqlite3_module triliteModule = {
  /* iVersion      */ 1,
  /* xCreate       */ triliteCreate,
  /* xConnect      */ triliteConnect,
  /* xBestIndex    */ triliteBestIndex,
  /* xDisconnect   */ triliteDisconnect,
  /* xDestroy      */ triliteDestroy,
  /* xOpen         */ triliteOpen,
  /* xClose        */ triliteClose,
  /* xFilter       */ triliteFilter,
  /* xNext         */ triliteNext,
  /* xEof          */ triliteEof,
  /* xColumn       */ triliteColumn,
  /* xRowid        */ triliteRowid,
  /* xUpdate       */ triliteUpdate,
  /* xBegin        */ triliteBegin,
  /* xSync         */ triliteSync,
  /* xCommit       */ triliteCommit,
  /* xRollback     */ 0,
  /* xFindFunction */ triliteFindFunction,
  /* xRename */       triliteRename,
  
  /* Change iVersion before using these fields */
  /* xSavepoint    */ 0,
  /* xRelease      */ 0,
  /* xRollbackTo   */ 0
};

/** Initialize extension, called this from sqlite
 * See http://www.sqlite.org/lang_corefunc.html#load_extension */
int sqlite3_extension_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi){
  int rc = SQLITE_OK;

  // Initialize the extension API
  SQLITE_EXTENSION_INIT2(pApi);

  // Create module
  rc = sqlite3_create_module(db, "trilite", &triliteModule, 0);

  return rc;
}

/* Entry point for applications loading this module,
 * This will load the module */
void load_trilite_extension(){
  sqlite3_auto_extension((void(*)(void))sqlite3_extension_init);
}

