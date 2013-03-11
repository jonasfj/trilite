#include "triliteInt.h"

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

/** TriLite module */
static const sqlite3_module triliteModule = {
  1,                    /* iVersion */
  triliteCreate,        /* xCreate */
  triliteConnect,       /* xConnect */
  triliteBestIndex,     /* xBestIndex */
  triliteDisconnect,    /* xDisconnect */
  triliteDestroy,       /* xDestroy */
  triliteOpen,          /* xOpen */
  triliteClose,         /* xClose */
  triliteFilter,        /* xFilter */
  triliteNext,          /* xNext */
  triliteEof,           /* xEof */
  triliteColumn,        /* xColumn */
  triliteRowid,         /* xRowid */
  triliteUpdate,        /* xUpdate */
  triliteBegin,         /* xBegin */
  triliteSync,          /* xSync */
  triliteCommit,        /* xCommit */
  triliteRollback,      /* xRollback */
  triliteFindFunction,  /* xFindFunction */
  triliteRename,        /* xRename */
  /* Change iVersion before using these fields */
  NULL,                 /* xSavepoint */
  NULL,                 /* xRelease */
  NULL                  /* xRollbackTo */
};

/** Load extension, called this from SQLite
 * See http://www.sqlite.org/lang_corefunc.html#load_extension
 */
int sqlite3_extension_init(sqlite3 *db, char **pzErrMsg,
                           const sqlite3_api_routines *pApi){
  int rc = SQLITE_OK;

  /* Initialize the extension API */
  SQLITE_EXTENSION_INIT2(pApi);

  /* Register place holder for the extents function */
  rc = sqlite3_overload_function(db, "extents", 1);
  assert(rc == SQLITE_OK);
  
  /* Create module */
  rc = sqlite3_create_module(db, "trilite", &triliteModule, 0);
  assert(rc == SQLITE_OK);

  return rc;
}

/* Entry point for applications loading this module,
 * This will load the module
 */
void trilite_load_extension(){
  sqlite3_auto_extension((void(*)(void))sqlite3_extension_init);
}
