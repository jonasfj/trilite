#include "triliteInt.h"

/* Static functions */
static int triliteParse(sqlite3*, int, const char* const*, bool,
                        trilite_vtab**, char**);

/** Column types used by the parser, used in triliteParse */
typedef enum column_type{
  COLUMN_TYPE_BLOB,                   /** Blob column */
  COLUMN_TYPE_TEXT,                   /** Indexed text column */
  COLUMN_TYPE_KEY                     /** Integer primary key column */
} column_type;


/** Parse trilite virtual table arguments
 * Allocates and fills out the vtable structure.
 * Returns SQLITE_OK, if successful. On failure *pzErr contains an error
 * message allocated with sqlite3_malloc or sqlite3_mprintf
 */
static int triliteParse(
  sqlite3             *db,            /** Database connection */
  int                 iArgc,          /** Number of arguments */
  const char * const  *pArgv,         /** Arguments given to create statement */
  bool                bCreate,        /** Create underlying tables */
  trilite_vtab        **ppTVTab,      /** OUT: Virtual table */
  char                **pzErr         /** OUT: Error message */
){
  int i, rc = SQLITE_OK;
  char* zSql = NULL;

  const char * zDb    = pArgv[1];
  const char * zName  = pArgv[2];
  int nDb             = strlen(pArgv[1]);
  int nName           = strlen(pArgv[2]);

  /* Variables to store results from parse in */
  bool             hasKey    = false;            /* Ensure primary key column */
  trilite_settings settings  = DEFAULT_SETTINGS; /* Read default settings */
  int              nCols     = 0;                /* Number of columns */
  column_type      eColTypes[iArgc - 2];         /* Column types */
  const char *     zColNames[iArgc - 2];         /* Column names */
  int              nColNames[iArgc - 2];         /* Length of column name */
  int              nTextCols = 0;                /* Number of text columns */

  /***************************** Parse Arguments ******************************/
  LOG("Arguments for %s:", zName);
  for(i = 3; i < iArgc; i++){
    LOG("  pArgv[%i] = '%s'", i, pArgv[i]);
    /* pArgv is a list of arguments on the form:
     * Argument ::= Column | Setting
     * Column   ::= <name> <type>
     * Setting  ::= <key> = <option>
     *
     * We parse this as <term1> '='? <term2>, where
     *   zTerm1:  <term1> (not zero terminated)
     *   nTerm1:  length of <term1>
     *   zTerm2:  <term2> (not zero terminated)
     *   nTerm2:  length of <term2>
     *   bAssign: true, if '=' was the separator.
     */
    const char *input = pArgv[i];         /* Parser input */
    const char *zTerm1 = input;           /* Start of term 1 */
    input += strcspn(input, " =");        /* Consume until ' ' or '=' */
    int nTerm1 = input - zTerm1;          /* Length of term 1 */
    input += strspn(input, " ");          /* Consume ' ' */
    bool bAssign = (input[0] == '=');     /* Check for assignment */
    input += strspn(input, " =");         /* Consume ' ' and '=' */
    const char *zTerm2 = input;           /* Start of term 2 */
    input += strcspn(input, " ");         /* Consume until ' ' */
    int nTerm2 = input - zTerm2;          /* Length of term 2 */
    /* Check that we're at the end of the expression */
    if(input[0] != '\0'){
      *pzErr = sqlite3_mprintf(
        "Argument %i, \"%s\" has more than 2 terms",
        i - 2, pArgv[i]);
      return SQLITE_ERROR;
    }
    /* Check that first term is non-empty */
    if(nTerm1 == 0){
      *pzErr = sqlite3_mprintf(
        "Argument %i, \"%s\" has no first term",
        i - 2, pArgv[i]);
      return SQLITE_ERROR;
    }
    /* Check that second term is non-empty */
    if(nTerm2 == 0){
      *pzErr = sqlite3_mprintf(
        "Argument %i, \"%s\" has no second term",
        i - 2, pArgv[i]);
      return SQLITE_ERROR;
    }

    /* If this is a settings assignment read it as such */
    if(bAssign){
      if(sqlite3_strnicmp("MaxRegExpMemory", zTerm1, 15) == 0){
        int value = atoi(zTerm2);
        /* We require memory greater than zero
         * zero also indicates parse error
         */
        if(value <= 0){
          *pzErr = sqlite3_mprintf(
            "Cannot parse positive integer value \"%s\" in "
            "argument %i, \"%s\".",
            zTerm2, i - 2, pArgv[i]);
          return SQLITE_ERROR;
        }
        settings.iMaxRegExpMemory = value;
      }else if(sqlite3_strnicmp("ForbidFullMatchScan", zTerm1, 19) == 0){
        if(sqlite3_strnicmp("true", zTerm2, 4) == 0){
          settings.bForbidFullMatchScan = true;
        }else if(sqlite3_strnicmp("false", zTerm2, 5) == 0){
          settings.bForbidFullMatchScan = false;
        }else{
          *pzErr = sqlite3_mprintf(
            "Cannot intepret boolean value \"%s\" in argument %i, \"%s\".",
            zTerm2, i - 2, pArgv[i]);
          return SQLITE_ERROR;
        }
      }
    }else if(sqlite3_strnicmp("text", zTerm2, 4) == 0){
      /* Store column type */
      eColTypes[nCols] = COLUMN_TYPE_TEXT;
      zColNames[nCols] = zTerm1;
      nColNames[nCols] = nTerm1;
      nCols++;
      nTextCols++;
    }else if(sqlite3_strnicmp("blob", zTerm2, 4) == 0){
      /* Store column type */
      eColTypes[nCols] = COLUMN_TYPE_BLOB;
      zColNames[nCols] = zTerm1;
      nColNames[nCols] = nTerm1;
      nCols++;
    }else if(sqlite3_strnicmp("key", zTerm2, 3) == 0){
      /* Case if it's the key column */
      if(hasKey){
        *pzErr = sqlite3_mprintf("Table cannot have two key columns");
        return SQLITE_ERROR;
      }
      /* Note that we've now have a key */
      hasKey = true;
      /* Store column type */
      eColTypes[nCols] = COLUMN_TYPE_KEY;
      zColNames[nCols] = zTerm1;
      nColNames[nCols] = nTerm1;
      nCols++;
    }else{
      /* Unknown datatype error */
      *pzErr = sqlite3_mprintf(
        "Unknown datatype \"%s\" in argument %i, \"%s\".",
        zTerm2, i - 3 + 1, pArgv[i]);
      return SQLITE_ERROR;
    }
  }

  /* Check that there's a key column, this is a strict requirement */
  if(!hasKey){
    *pzErr = sqlite3_mprintf("TriLite tables must have a key column");
    return SQLITE_ERROR;
  }

  /****************************** Create Tables *******************************/

  /* If we're asked to create the underlying tables */
  if(bCreate){

    /* Create column string for %_data table */
    char* zCols = sqlite3_mprintf("");
    for(i = 0; i < nCols; i++){
      /* Separator, if applicable */
      char*     zSep = "";
      if(i > 0) zSep = ", ";

      /* Columns on the form: c<i> <type> */
      if(eColTypes[i] == COLUMN_TYPE_TEXT){
        zCols = sqlite3_mprintf("%z%sc%i TEXT", zCols, zSep, i);
      }else if(eColTypes[i] == COLUMN_TYPE_BLOB){
        zCols = sqlite3_mprintf("%z%sc%i BLOB", zCols, zSep, i);
      }else if(eColTypes[i] == COLUMN_TYPE_KEY){
        zCols = sqlite3_mprintf("%z%sc%i INTEGER PRIMARY KEY", zCols, zSep, i);
      }
    }

    /* Construct and execute CREATE TABLE statement */
    zSql = sqlite3_mprintf(
      "CREATE TABLE %Q.'%q_data' (%z);",
      zDb, zName, zCols);
    
    /* Execute statment */
    LOG("Create _data table: \"%s\"", zSql);
    rc = sqlite3_exec(db, zSql, NULL, NULL, pzErr);
    sqlite3_free(zSql);
    zSql = NULL;

    /* Handle errors */
    if(rc != SQLITE_OK){
      *pzErr = sqlite3_mprintf(
        "TriLite failed to create data table: %z",
        *pzErr);
      return rc;
    }

    /* Create index tables */
    for(i = 0; i < nCols; i++){
      /* Skip non-text columns */
      if(eColTypes[i] != COLUMN_TYPE_TEXT)
        continue;

      /* Create idx table statement */
      zSql = sqlite3_mprintf(
        "CREATE TABLE %Q.'%q_idx%i' (trg INTEGER PRIMARY KEY, ids BLOB);",
        zDb, zName, i);

      /* Execute statment */
      LOG("Create _idx table: \"%s\"", zSql);
      rc = sqlite3_exec(db, zSql, NULL, NULL, pzErr);
      sqlite3_free(zSql);
      zSql = NULL;

      /* Handle errors */
      if(rc != SQLITE_OK){
        *pzErr = sqlite3_mprintf(
          "TriLite failed to create idx table: %z",
          *pzErr);
        return rc;
      }
    }
  }

  /****************************** Declare Table *******************************/

  /* Construct table declaration statement */
  zSql = sqlite3_mprintf("CREATE TABLE x (");
  for(i = 0; i < nCols; i++){
    /* Separator, if applicable */
    char*     zSep = "";
    if(i > 0) zSep = ", ";

    /* Get column name as \0 terminated string */
    char zColName[nColNames[i] + 1];
    strncpy(zColName, zColNames[i], nColNames[i]);
    zColName[nColNames[i]] = '\0';

    if(eColTypes[i] == COLUMN_TYPE_TEXT){
      zSql = sqlite3_mprintf("%z%s%s TEXT", zSql, zSep, zColName);
    }else if(eColTypes[i] == COLUMN_TYPE_BLOB){
      zSql = sqlite3_mprintf("%z%s%s BLOB", zSql, zSep, zColName);
    }else if(eColTypes[i] == COLUMN_TYPE_KEY){
      zSql = sqlite3_mprintf(
        "%z%s%s INTEGER PRIMARY KEY",
        zSql, zSep, zColName);
    }
  }
  zSql = sqlite3_mprintf("%z);", zSql);

  /* Declare virtual table interface */
  LOG("Declare virtual table: \"%s\"", zSql);
  rc = sqlite3_declare_vtab(db, zSql);
  sqlite3_free(zSql);
  zSql = NULL;
  if(rc != SQLITE_OK){
    return rc;
  }

  /***************************** Construct VTable *****************************/

  /* Take memory for vtab */
  trilite_vtab *pTVTab = (trilite_vtab*)sqlite3_malloc(sizeof(trilite_vtab));

  /* Fill out trilite_vtab */
  pTVTab->db                  = db;
  pTVTab->zDb                 = (const char*)strcpy(
                                              (char*)sqlite3_malloc(nDb + 1),
                                              pArgv[1]);
  pTVTab->zName               = (const char*)strcpy(
                                              (char*)sqlite3_malloc(nName + 1),
                                              pArgv[2]);
  pTVTab->settings            = settings;
  pTVTab->nCols               = nCols;
  pTVTab->pCols               = (trilite_column**)sqlite3_malloc(
                                              sizeof(trilite_column*) * nCols);
  pTVTab->pInsertStmt         = NULL;
  pTVTab->pUpdateStmt         = NULL;
  pTVTab->pDeleteStmt         = NULL;
  pTVTab->nPatternCache       = 0;
  pTVTab->nPatternCacheAvail  = 0;
  pTVTab->pPatternCache       = NULL;

  /* Create trilite_column structures as needed */
  for(i = 0; i < nCols; i++){
    if(eColTypes[i] == COLUMN_TYPE_TEXT){
      pTVTab->pCols[i] = (trilite_column*)sqlite3_malloc(
                                                    sizeof(trilite_column));
      pTVTab->pCols[i]->pPending    = NULL;
      pTVTab->pCols[i]->pUpdateStmt = NULL;
    }else if(eColTypes[i] == COLUMN_TYPE_BLOB){
      pTVTab->pCols[i] = TRILITE_COLUMN_BLOB;
    }else if(eColTypes[i] == COLUMN_TYPE_KEY){
      pTVTab->pCols[i] = TRILITE_COLUMN_KEY;
    }
  }

  /* Output the result */
  *ppTVTab = pTVTab;

  return SQLITE_OK;
}

/** Create trilite virtual table 
 * The arguments given to the CREATE VIRTUAL TABLE ... USING trilite ... are
 * given to this function and parsed in triliteConnect.
 * This function returns SQLITE_OK on success and SQLITE_ERROR on failure, where
 * it may choose to set *pzErr to an sqlite3_malloc or sqlite3_mprintf allocated
 * error message.
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteCreate(
  sqlite3             *db,            /** SQLite database handle */
  void                *pAux,          /** Auxiliary data (not used here) */
  int                 iArgc,          /** Number of arguments */
  const char * const  *pArgv,         /** Arguments given to create statement */
  sqlite3_vtab        **ppVTab,       /** OUT: Virtual table */
  char                **pzErr         /** OUT: Error messages */
){
  UNUSED_PARAMETER(pAux);
  LOG("Creating trilite table");
  return triliteParse(db, iArgc, pArgv, true, (trilite_vtab**)ppVTab, pzErr);
}


/** Connect to an existing TriLite table
 * When connecting to and existing trilite virtual table the connect function is
 * given the arguments as given in the original CREATE VIRTUAL TABLE statement.
 * This function returns SQLITE_OK on success and SQLITE_ERROR on failure, where
 * it may choose to set *pzErr to an sqlite3_malloc or sqlite3_mprintf allocated
 * error message.
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteConnect(
  sqlite3             *db,            /** SQLite database handle */
  void                *pAux,          /** Auxiliary data (not used here) */
  int                 iArgc,          /** Number of arguments */
  const char * const  *pArgv,         /** Arguments given to create statement */
  sqlite3_vtab        **ppVTab,       /** OUT: Virtual table */
  char                **pzErr         /** OUT: Error messages */
){
  UNUSED_PARAMETER(pAux);
  LOG("Connecting to trilite table");
  return triliteParse(db, iArgc, pArgv, false, (trilite_vtab**)ppVTab, pzErr);
}


/** Disconnect from TriLite table
 * Essentially flushes changes and destroys the virtual table object.
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteDisconnect(
  sqlite3_vtab        *pVTab          /** Virtual table */
){
  LOG("Disconnecting trilite table");
 /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Destroy TriLite table
 * Delete virtual table and virtual table object.
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteDestroy(
  sqlite3_vtab        *pVTab          /** Virtual table */
){
 /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Determine index strategy for query
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteBestIndex(
  sqlite3_vtab        *pVTab,         /** Virtual table */
  sqlite3_index_info  *pInfo          /** IN/OUT: Query and index information */
){
  /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Update/Insert row in TriLite table
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteUpdate(
  sqlite3_vtab        *pVTab,         /** Virtual table */
  int                 iArgc,          /** Number of arguments */
  sqlite3_value       **pArgv,        /** Arguments */
  sqlite_int64        *pRowId         /** OUT: Row id of updated row */
){
  /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Rename TriLite table
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteRename(
  sqlite3_vtab        *pVTab,         /** Virtual table */
  const char          *zNewName       /** New name of TriLite table */
){
  char* zSql;
  int rc = SQLITE_OK;
  trilite_vtab *pTVTab = (trilite_vtab*)pVTab;

  char* zErr = NULL;

  /* Rename the %_data table */
  zSql = sqlite3_mprintf(
    "ALTER TABLE %Q.'%q_data' RENAME TO '%q_data';",
    pTVTab->zDb, pTVTab->zName, zNewName);
  /* Execute rename */
  rc = sqlite3_exec(pTVTab->db, zSql, NULL, NULL, &zErr);
  sqlite3_free(zSql);
  /* Handle errors */
  if(rc != SQLITE_OK){
    if(!zErr)
      zErr = sqlite3_mprintf("SQL statment failed!");
    triliteError(pTVTab,
      "TriLite failed to rename %q_data table to %q_data: %z",
      pTVTab->zName, zNewName, zErr);
    return rc;
  }

  /* Rename all the %_idx% tables */
  int i;
  for(i = 0; i < pTVTab->nCols; i++){
    /* Skip key and blob columns, these aren't indexed */
    if(pTVTab->pCols[i] == TRILITE_COLUMN_BLOB ||
       pTVTab->pCols[i] == TRILITE_COLUMN_KEY){
      continue;
    }
    /* Statement to rename an %_idx% table */
    zSql = sqlite3_mprintf(
        "ALTER TABLE %Q.'%q_idx%i' RENAME TO '%q_idx%i';",
        pTVTab->zDb, pTVTab->zName, i, zNewName, i);
    /* Execute statement */
    rc = sqlite3_exec(pTVTab->db, zSql, NULL, NULL, &zErr);
    sqlite3_free(zSql);
    /* Handle errors */
    if(rc != SQLITE_OK){
      if(!zErr)
        zErr = sqlite3_mprintf("SQL statment failed!");
      triliteError(pTVTab,
        "TriLite failed to rename %q_idx%i table to %q_idx%i: %z",
        pTVTab->zName, i, zNewName, i, zErr);
      return rc;
    }
  }

  /* Update the name on pTVTab */
  sqlite3_free((char*)pTVTab->zName);
  pTVTab->zName = (const char*)strcpy(
                                (char*)sqlite3_malloc(strlen(zNewName) + 1),
                                zNewName);

  return SQLITE_OK;
}


/** Lookup TriLite virtual table specific SQL functions
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteFindFunction(
  sqlite3_vtab        *pVTab,         /** Virtual table */
  int                 nArg,           /** Number of arguments for function */
  const char          *zName,         /** Name of function to lookup */
  void (**pFunc)(sqlite3_context*, int, sqlite3_value**), /** OUT: Function */
  void                **ppArg         /** OUT: Auxiliary data */
){
  /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Begin transaction
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteBegin(
  sqlite3_vtab        *pVTab          /** Virtual table */
){
  /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Acknowledge that transaction can be committed
 * To ensure that we can commit the current changes, we must flush the hash-
 * table with pending changes in this function.
 * If this function is successful, then an immediate invokation of triliteCommit
 * must also be successful.
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteSync(
  sqlite3_vtab        *pVTab          /** Virtual table */
){
  /* TODO: Implement this */
  assert(false);
  return SQLITE_INTERNAL;
}


/** Rollback transaction
 * Discard all entries from the hash-table of pending entries. Notice that
 * sqlite will take care to rollback the underlying data tables, so we don't
 * have to worry about that.
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteRollback(
  sqlite3_vtab        *pVTab          /** Virtual table */
){
  int i, rc = SQLITE_OK;
  trilite_vtab *pTVTab = (trilite_vtab*)pVTab;
  /* For each column in this virtual table */
  for(i = 0; i < pTVTab->nCols; i++){
    /* Skip BLOB and KEY columns */
    if(pTVTab->pCols[i] == TRILITE_COLUMN_BLOB)
      continue;
    if(pTVTab->pCols[i] == TRILITE_COLUMN_KEY)
      continue;
    /* Reset pending hash table, as we must have TEXT column */
    trilite_column *pCol = pTVTab->pCols[i];
    if(pCol->pPending == NULL)
      continue;
    if(hashReset(pCol->pPending) != SQLITE_OK){
      /* If hashReset wasn't successful, we have an arbitrary internal error
       * We can't do much about this, behaviour undefined!
       */
      rc = SQLITE_INTERNAL;
    }
  }
  return rc;
}


/** Commit transaction
 * This is essentially cheching that there is no pending changes. This must be
 * the case as triliteSync should be called before triliteCommit.
 * Notice, this function must not fail if triliteSync was successful!
 * (This function is a part of the SQLite virtual table interface)
 */
int triliteCommit(
  sqlite3_vtab        *pVTab          /** Virtual table */
){
  int i, rc = SQLITE_OK;
  trilite_vtab *pTVTab = (trilite_vtab*)pVTab;
  bool empty = true;
  /* For each column in this virtual table */
  for(i = 0; i < pTVTab->nCols; i++){
    /* Skip BLOB and KEY columns */
    if(pTVTab->pCols[i] == TRILITE_COLUMN_BLOB)
      continue;
    if(pTVTab->pCols[i] == TRILITE_COLUMN_KEY)
      continue;
    /* Check if the hash table of pending changes is empty */
    trilite_column *pCol = pTVTab->pCols[i];
    bool result = false;
    if(hashIsEmpty(pCol->pPending, &result) != SQLITE_OK){
      /* If hashIsEmpty wasn't successful, we have an arbitrary internal error
       * We can't do much about this, behaviour undefined!
       */
      rc = SQLITE_INTERNAL;
    }
    assert(result);
    empty = result || empty;
  }
  /* Return SQLITE_ERROR, if we have pending changes */
  if(!empty && rc == SQLITE_OK)
    return SQLITE_ERROR;
  return rc;
}

/** Report error to the vtable structure
 * This function must be called with an error message whenever a non SQLITE_OK
 * value is returned from any public function. Otherwise, any previously
 * generated error message, which is still located in zErrMsg on the virtual
 * table will be read by sqlite.
 * So it's better to just call this method with an empty error message, if you
 * don't have one!
 */
void triliteError(
  trilite_vtab        *pTVTab,        /** Trilite virtual table */
  const char          *zFormat,       /** Error formating string */
  ...                                 /** Error arguments */
){
  /* Release error message if there's already one */
  if(pTVTab->base.zErrMsg)
    sqlite3_free(pTVTab->base.zErrMsg);
  pTVTab->base.zErrMsg = NULL;

  /** Pass argument list to vmprintf and set it as error message */
  va_list args;
  va_start(args, zFormat);
  pTVTab->base.zErrMsg = sqlite3_vmprintf(zFormat, args);
  va_end(args);
}