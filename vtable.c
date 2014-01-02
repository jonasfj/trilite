#include "vtable.h"
#include "config.h"
#include "varint.h"
#include "hash.h"
#include "match.h"
#include "cursor.h"

const sqlite3_api_routines *sqlite3_api;

#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>


/** Cost of a full table scan
 * Please enjoy the fact that this prime estimate
 */
#define COST_FULL_SCAN      499979.0

/** Cost of match scan */
#define COST_MATCH_SCAN     19

/** Cost of a row lookup */
#define COST_ROW_LOOKUP     1


static int saveDocList(trilite_vtab*, trilite_trigram, sqlite3_int64*, int);
static int indexAddText(trilite_vtab*, sqlite3_int64, sqlite3_value*);
static int indexRemoveText(trilite_vtab*, sqlite3_int64);
static int prepareSql(trilite_vtab*);
static int finalizeSql(trilite_vtab*);



/** Create virtual table */
int triliteCreate(sqlite3 *db, void *pAux, int argc, const char *const *argv, sqlite3_vtab **ppVtab, char **pzErr){
  char* zSql;
  int rc = SQLITE_OK;

  /* Create tables */
  zSql = sqlite3_mprintf(
    "CREATE TABLE %Q.'%q_content' (id INTEGER PRIMARY KEY, text TEXT);"
    "CREATE TABLE %Q.'%q_index' (trigram INTEGER PRIMARY KEY, doclist BLOB);",
    argv[1], argv[2],
    argv[1], argv[2]);
  rc = sqlite3_exec(db, zSql, NULL, NULL, pzErr);
  sqlite3_free(zSql);

  if(rc != SQLITE_OK)
    return rc;

  /* Create the virtual table */
  rc = triliteConnect(db, pAux, argc, argv, ppVtab, pzErr);
  
  return rc;
}


/** Connect to virtual table */
int triliteConnect(sqlite3 *db, void *pAux, int argc, const char *const *argv, sqlite3_vtab **ppVtab, char **pzErr){
  trilite_vtab *pTrgVtab;
  int rc = SQLITE_OK;
  
  /* Allocate new virtual table structure */
  int nDb = strlen(argv[1]);
  int nName = strlen(argv[2]);     /* Space for storing db and table name  */
  pTrgVtab = (trilite_vtab*)sqlite3_malloc(sizeof(trilite_vtab) + nDb + nName + 2);
  if(!pTrgVtab) return SQLITE_NOMEM;
  
  /* Space allocated with table structure, for storing stirngs */
  memset(pTrgVtab, 0, sizeof(trilite_vtab) + nDb + nName + 2);
  pTrgVtab->zDb   = (char*)(pTrgVtab + 1);
  pTrgVtab->zName = (char*)(pTrgVtab + 1) + nDb + 1;
  memcpy(pTrgVtab->zDb, argv[1], nDb);
  memcpy(pTrgVtab->zName, argv[2], nName);
 
  /* Allocate hash table */
  hashCreate(&pTrgVtab->pAdded);

  /* Set database connection */
  pTrgVtab->db = db;

  /* Default values for various settings */
  pTrgVtab->forbidFullMatchScan = false;
  pTrgVtab->maxRegExpMemory     = 8<<20;  /* About 8 MiB */
  
  /* Prepare sql statements */
  rc = prepareSql(pTrgVtab);
  if(rc != SQLITE_OK)
    return rc;
  
  /* Declare virtual table */
  rc = sqlite3_declare_vtab(db, "CREATE TABLE x(id INTEGER PRIMARY KEY, text TEXT, contents HIDDEN)");
  if(rc != SQLITE_OK)
    return rc;
  
  /* Return the virtual table */
  *ppVtab = (sqlite3_vtab*)pTrgVtab;
  return rc;
}


/** Rename virtual table */
int triliteRename(sqlite3_vtab *pVtab, const char* zNewName){
  trilite_vtab* pTrgVtab = (trilite_vtab*)pVtab;
  char* zSql;
  int rc = SQLITE_OK;
  
  /* Allocate space for new name */
  char* zName = (char*)sqlite3_malloc(strlen(zNewName) + 1);
  if(!zName) return SQLITE_NOMEM;
  strcpy(zName, zNewName);
  
  /* Update database tables */
  zSql = sqlite3_mprintf(
    "ALTER TABLE %Q.'%q_content' RENAME TO '%q_content';"
    "ALTER TABLE %Q.'%q_index'   RENAME TO '%q_index';",
    pTrgVtab->zDb, pTrgVtab->zName, zNewName,
    pTrgVtab->zDb, pTrgVtab->zName, zNewName);
  rc = sqlite3_exec(pTrgVtab->db, zSql, NULL, NULL, NULL);
  sqlite3_free(zSql);
  if(rc != SQLITE_OK) return rc;
  
  /* Delete all the current sql statements */
  rc = finalizeSql(pTrgVtab);
  if(rc != SQLITE_OK) return rc;
  
  /* Check if pTrgVtab->zName was allocated with pTrgVtab, if not release it */
  if(pTrgVtab->zName != (char*)(pTrgVtab + 1) + strlen(pTrgVtab->zDb) + 1)
    sqlite3_free(pTrgVtab->zName);
  
  /* Assign the new name */
  pTrgVtab->zName = zName;
  
  /* Prepare new sql statements */
  rc = prepareSql(pTrgVtab);
  if(rc != SQLITE_OK) return rc;
  
  return rc;
}


/** Update the virtual table */
int triliteUpdate(sqlite3_vtab* pVtab, int argc, sqlite3_value **argv, sqlite_int64 *pRowid){
  trilite_vtab* pTrgVtab = (trilite_vtab*)pVtab;
  int rc = SQLITE_OK;
  int type = sqlite3_value_type(argv[0]);
  
  /* TODO Handle errors better, ie. not missing values and stuff, currently NOT */
  /* handled at all */
  
  /* With respect to the HIDDEN contents column we simply ignore whatever value */
  /* is provided for it, maybe it would be better to raise an error on insert of */
  /* a none NULL value in this column. */

  /* Delete row argv[0] */
  if(argc == 1){
    log("Deleting row: %lli", sqlite3_value_int64(argv[0]));
  
    /* Remove text from index */
    rc = indexRemoveText(pTrgVtab, sqlite3_value_int64(argv[0]));
    if(rc != SQLITE_OK) return rc;
    
    /* Execute delete statement */
    sqlite3_bind_value(pTrgVtab->stmt_delete_content, 1, argv[0]);
    sqlite3_step(pTrgVtab->stmt_delete_content);
    rc = sqlite3_reset(pTrgVtab->stmt_delete_content);
    sqlite3_clear_bindings(pTrgVtab->stmt_delete_content);
    
    return rc;
  }
  
  
  /* Insert new row */
  if(argc > 1 && type == SQLITE_NULL){
    /* Notice that we get the desired rowid as argv[1] and argv[2] because we */
    /* have id as alias of rowid in our table definition, anyways, */
    /* noworries this makes argv[3] the text. */
    /* Insert argv[3] as text with argv[1] as rowid (argv[1] may be NULL) */
    sqlite3_bind_value(pTrgVtab->stmt_insert_content, 1, argv[1]);
    sqlite3_bind_value(pTrgVtab->stmt_insert_content, 2, argv[3]);
    
    sqlite3_step(pTrgVtab->stmt_insert_content);
    rc = sqlite3_reset(pTrgVtab->stmt_insert_content);
    sqlite3_clear_bindings(pTrgVtab->stmt_insert_content);
    if(rc != SQLITE_OK) return rc;
    
    /* Output rowid */
    *pRowid = sqlite3_last_insert_rowid(pTrgVtab->db);
    
    log("Inserted row, got id: %lli", *pRowid);
    
    /* Add to text index */
    rc = indexAddText(pTrgVtab, *pRowid, argv[3]);
    
    return rc;
  }
  
  /* Update existing row */
  if(argc > 1 && type != SQLITE_NULL){
    /* Remove text from index */
    rc = indexRemoveText(pTrgVtab, sqlite3_value_int64(argv[0]));
    if(rc != SQLITE_OK) return rc;
    
    /* Notice that we get the desired rowid as argv[1] and argv[2] because we */
    /* have id as alias of rowid in our table definition, anyways, */
    /* noworries this makes argv[3] the text. */
    /* Update set text = argv[3], id = argv[1]  where id = argv[0] */
    sqlite3_bind_value(pTrgVtab->stmt_update_content, 1, argv[1]);
    sqlite3_bind_value(pTrgVtab->stmt_update_content, 2, argv[3]);
    sqlite3_bind_value(pTrgVtab->stmt_update_content, 3, argv[0]);
    
    sqlite3_step(pTrgVtab->stmt_update_content);
    rc = sqlite3_reset(pTrgVtab->stmt_update_content);
    sqlite3_clear_bindings(pTrgVtab->stmt_update_content);
    if(rc != SQLITE_OK) return rc;
    
    log("Updated row: %lli (new id: %lli)", sqlite3_value_int64(argv[0]), sqlite3_value_int64(argv[1]));
    
    /* Add to text index */
    rc = indexAddText(pTrgVtab, sqlite3_value_int64(argv[1]), argv[2]);
    
    return rc;
  }
  assert(false);
  return SQLITE_INTERNAL;
}


/** Begin transaction, necessary to ensure that triliteCommit is called */
int triliteBegin(sqlite3_vtab *pVtab){
  log(" -- BEGIN TRANSACTION -- ");
  return SQLITE_OK;
}

/** Sync things prior to commit or rollback
 * This flushes pending changes to doclists. This flush is not needed if we're
 * doing a rollback afterwards, ie. only required when committing.
 * But we can't really do it in commit as this creates nested commit.
 * If followed by rollback, underlying tables will be rollbacked too,
 * so no harm, in fact this will ensure that the set of pending changes is empty
 * after rollback */
int triliteSync(sqlite3_vtab *pVtab){
  trilite_vtab* pTrgVtab = (trilite_vtab*)pVtab;
  int rc = SQLITE_OK;

  /* Open hash cursor, for popping of doclists */
  hash_table_cursor *pCur;
  hashOpen(pTrgVtab->pAdded, &pCur);

  log(" -- SYNC TRANSACTION -- ");

  trilite_trigram trigram;
  sqlite_int64 *ids;
  int nIds;
  while(hashPop(pCur, &trigram, &ids, &nIds)){
    rc = saveDocList(pTrgVtab, trigram, ids, nIds);
    log("save: %i, nids: %i", trigram, nIds);
    assert(rc == SQLITE_OK);
  }

  /* Release hash cursor again */
  hashClose(pCur);
  pCur = NULL;

  return rc;
}

/** Commit pending changes to doclists */
int triliteCommit(sqlite3_vtab *pVtab){
  log(" -- END TRANSACTION -- ");
  return SQLITE_OK;
}



/** Select index
 * We offer a full table scan by default, however, if there's a EQ on rowid,
 * or a MATCH on text column we will choose a indexing strategy take advantage
 * of this.
 * Also note that ORDER BY rowid is consumed, but that is the ONLY ordering we
 * offer.
 */
int triliteBestIndex(sqlite3_vtab *pVtab, sqlite3_index_info *pInfo){
  /* By default we do a full table scan */
  pInfo->idxNum         = IDX_FULL_SCAN;
  pInfo->estimatedCost  = COST_FULL_SCAN;

  log("Computing best index:");

  int i;
  for(i = 0; i < pInfo->nConstraint; i++){
    /* Log the constraint for debugging */
    log("------- Constraint:");
    log("Column: %i", pInfo->aConstraint[i].iColumn);
    log("Op:     %i", pInfo->aConstraint[i].op);
    log("Usable: %i", pInfo->aConstraint[i].usable);
  
    /* Skip constraints we can't use */
    if(!pInfo->aConstraint[i].usable) continue;
    
    /* Check if there's a match we can use */
    if(pInfo->aConstraint[i].iColumn == 2 &&
       pInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_MATCH &&
       pInfo->estimatedCost > COST_MATCH_SCAN){
      pInfo->idxNum         = IDX_MATCH_SCAN;
      pInfo->estimatedCost  = COST_MATCH_SCAN;
    }
    
    /* Check if there's a rowid lookup */
    if(pInfo->aConstraint[i].iColumn < 1 &&
       pInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_EQ &&
       pInfo->estimatedCost > COST_ROW_LOOKUP){
      pInfo->idxNum         = IDX_ROW_LOOKUP;
      pInfo->estimatedCost  = COST_ROW_LOOKUP;
      /* Best index available, take it and break */
      pInfo->aConstraintUsage[i].argvIndex = 1;
      pInfo->aConstraintUsage[i].omit = 1;
      break;
    }
    
    /* Yes, technically we could support GT, LE, LT and GE on rowid/id, but */
    /* that is not the goal of this virtual table. If you want it, implement */
    /* it yourself. */
  }

  /* If we doing a match scan, take all the match arguments we can get */
  if(pInfo->idxNum == IDX_MATCH_SCAN){
    int argvIndex = 1;
    for(i = 0; i < pInfo->nConstraint; i++){
      /* Skip constraints we can't use */
      if(!pInfo->aConstraint[i].usable) continue;

      /* If this was a match constraint use it! */
      if(pInfo->aConstraint[i].iColumn == 2 &&
       pInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_MATCH){
        pInfo->aConstraintUsage[i].argvIndex = argvIndex++;
        pInfo->aConstraintUsage[i].omit = 0;  /* Can't omit */
      }
    }
  }
  
  /* Try to consume order by */
  /* Do this reverse, as we want the outer most DESC/ASC value */
  /* in case some idiot thinks it makes sense to order by both */
  /* id and rowid, although they are the same */
  for(i = pInfo->nOrderBy - 1; i >= 0; i--){
    log("------- Order By:");
    log("Column: %i", pInfo->aOrderBy[i].iColumn);
    log("DESC:   %i", pInfo->aOrderBy[i].desc);
    /* Anything on id we can consume */
    if(pInfo->aOrderBy[i].iColumn < 1){
      pInfo->orderByConsumed = 1;
      if(pInfo->aOrderBy[i].desc){
        pInfo->idxNum |=  ORDER_BY_DESC;  /* Add flag */
        pInfo->idxNum &= ~ORDER_BY_ASC;   /* Remove flag */
      }else{
        pInfo->idxNum |=  ORDER_BY_ASC;   /* Add flag */
        pInfo->idxNum &= ~ORDER_BY_DESC;  /* Remove flag */
      }
    }else{
      assert(pInfo->aOrderBy[i].iColumn == 1);
      /* No ordering on the text */
      pInfo->orderByConsumed = 0;
      /* Forget all about ordering */
      pInfo->idxNum &= ~(ORDER_BY_ASC | ORDER_BY_DESC);
      break; /* don't look any further we know the answer */
    }
  }
  
  /* Enforce invariants */
  /* Only one of the ORDER_BY bits may be set */
  assert(!(pInfo->idxNum & ORDER_BY_DESC && pInfo->idxNum & ORDER_BY_ASC));
  /* We should always have an index strategy bit set */
  assert(pInfo->idxNum & (IDX_FULL_SCAN | IDX_ROW_LOOKUP | IDX_MATCH_SCAN));
  /* We should ONLY have one index strategy bit set */
  assert(
    IMPLIES(
          pInfo->idxNum & IDX_FULL_SCAN,
          !(pInfo->idxNum & (IDX_ROW_LOOKUP | IDX_MATCH_SCAN))
    )
  );
  assert(
    IMPLIES(
          pInfo->idxNum & IDX_MATCH_SCAN,
          !(pInfo->idxNum & (IDX_ROW_LOOKUP | IDX_FULL_SCAN))
    )
  );
  assert(
    IMPLIES(
          pInfo->idxNum & IDX_ROW_LOOKUP,
          !(pInfo->idxNum & (IDX_MATCH_SCAN | IDX_FULL_SCAN))
    )
  );
  
  return SQLITE_OK;
}

/** Overload the match function
 *
 * Arguments to the MATCH operator is available when filtering the results,
 * we could easily do exact matching in the triliteNext function for the cursor
 * however, we wish to postpone this operation to the last possible moment,
 * hoping that sqlite might skip some of the results as it joins tables.
 * In order to ensure that other operators are applied before the MATCH operator
 * place the MATCH operator as the right-most term in your WHERE clause.
 * 
 * At the moment sqlite does not feature any strategies for communicating the
 * cost of respective scalar functions, so it's the responsibility of the
 * developer to order the scalar functions in order from cheap to expensive.
 * (Note, that selectivity might also be relevant in such considerations).
 */
int triliteFindFunction(sqlite3_vtab* pVtab, int nArg, const char* zName,
                               void (**pxFunc)(sqlite3_context*, int, sqlite3_value**), void **ppUserData){
  /* Our match function overload is called match and takes two parameters */
  /* Notice, that even though the sqlite documentation is silent on this issue, zName is ALWAYS in lower case. */
  /* ppUserData can be obtained inside the function using sqlite3_user_data, however, I see no way in which we */
  /* can release this structure again. In any event, we'll just compile the regular expression twice, once in */
  /* filter to get substrings we can filter for, and once in the match function to extract exact matches. */
  if(strcmp("match", zName) == 0 && nArg == 2){
    *pxFunc = matchFunction;    /* See match.c */
    return 1;
  }
  if(strcmp("extents", zName) == 0 && nArg == 1){
    *pxFunc = extentsFunction;  /* See cursor.c */
    return 1;
  }
  return 0;
}


/** Disconnect from virtual table */
int triliteDisconnect(sqlite3_vtab *pVtab){
  trilite_vtab* pTrgVtab = (trilite_vtab*)pVtab;
  int rc = SQLITE_OK;
  
  /* Finalize sql statements (release memory) */
  rc = finalizeSql(pTrgVtab);
  if(rc != SQLITE_OK) return rc;

  /* Check if pTrgVtab->zName was allocated with pTrgVtab, if not release it */
  if(pTrgVtab->zName != (char*)(pTrgVtab + 1) + strlen(pTrgVtab->zDb) + 1)
    sqlite3_free(pTrgVtab->zName);

  /* Release hash table */
  hashRelease(pTrgVtab->pAdded);

  /* Release virtual table */
  sqlite3_free(pVtab);
  
  return rc;
}

/** Drop the virtual table */
int triliteDestroy(sqlite3_vtab *pVtab){
  trilite_vtab* pTrgVtab = (trilite_vtab*)pVtab;
  char* zSql;
  int rc;
  
  /* Drop tables */
  zSql = sqlite3_mprintf(
    "DROP TABLE '%q'.'%q_content';"
    "DROP TABLE '%q'.'%q_index';",
    pTrgVtab->zDb, pTrgVtab->zName,
    pTrgVtab->zDb, pTrgVtab->zName);
  rc = sqlite3_exec(pTrgVtab->db, zSql, NULL, NULL, NULL);
  sqlite3_free(zSql);
  if(rc != SQLITE_OK) return rc;
  
  /* Disconnect and done */
  rc = triliteDisconnect(pVtab);
  
  return rc;
}


/*************************** Auxiliary Functions *****************************/


/** Report an error message */
void triliteError(trilite_vtab *pTrgVtab, const char *format, ...){
  /* Release error message if there's already one */
  if(pTrgVtab->base.zErrMsg)
    sqlite3_free(pTrgVtab->base.zErrMsg);

  /* Use argument list as given */
  va_list args;
  va_start(args, format);
  /* Pass it to sqlite3_mprintf */
  pTrgVtab->base.zErrMsg = sqlite3_vmprintf(format, args);
  /* Release it */
  va_end(args);
}


/** Add a text to the trigram index */
static int indexAddText(trilite_vtab *pTrgVtab, sqlite3_int64 id, sqlite3_value *vText){
  /* Get the text */
  const unsigned char *zText = sqlite3_value_text(vText);
  int nText = sqlite3_value_bytes(vText);
  
  /* Don't insert anything for empty texts */
  if(nText == 0) return SQLITE_OK;
  
  log("Adding docid: %lli to index with '%s'", id, zText);
  
  /* List of trigrams seen so far (Just allocate plenty of memory) */
  uint32_t *trigrams = (uint32_t*)sqlite3_malloc(sizeof(uint32_t) * nText);
  int nTrigrams = 0;
  if(!trigrams) return SQLITE_NOMEM;
  
  int pos;
  for(pos = 0; pos <= nText - 3; pos++){
    /* Convert byte values of trigram to int as use as trigram */
    uint32_t trigram = HASH_TRIGRAM(zText + pos);

    /* Continue if we've seen this trigram before */
    int i;
    for(i = 0; i < nTrigrams; i++)
      if(trigrams[i] == trigram) break;
    if(i < nTrigrams) continue;
    
    log("Found new trigram '%c%c%c'", zText[pos], zText[pos + 1], zText[pos + 2]);
    
    /* Add trigram to list of trigrams */
    trigrams[nTrigrams++] = trigram;

    /* Insert id for trigram in hash table for added doclists */
    hashInsert(pTrgVtab->pAdded, trigram, id);
  }
  
  /* Release list of trigrams */
  sqlite3_free(trigrams);
  
  if(hashMemoryUsage(pTrgVtab->pAdded) > MAX_PENDING_BYTES)
    triliteSync((sqlite3_vtab*)pTrgVtab);

  return SQLITE_OK;
}

/** Save docList to database
 * We assume the docList is sorted in ascending order of ids */
static int saveDocList(trilite_vtab *pTrgVtab, trilite_trigram trigram, sqlite3_int64 *ids, int nIds){
  int rc = SQLITE_OK;

  /* doclist and size */
  int nSize;
  unsigned char *docList;


  /* Find list for this trigram, if any */
  sqlite3_stmt *pStmt = pTrgVtab->stmt_fetch_doclist;
  sqlite3_bind_int64(pStmt, 1, (sqlite_int64)trigram);
  if((rc = sqlite3_step(pStmt)) == SQLITE_ROW){
      /* Get old list from query */
      unsigned char *oldList = (unsigned char*)sqlite3_column_blob(pStmt, 0);
      int nOldSize           = sqlite3_column_bytes(pStmt, 0);
      /* Allocate new list */
      docList = (unsigned char*)sqlite3_malloc(nOldSize + MAX_VARINT_SIZE * nIds);
      nSize = 0;
      /* Merge both lists */
      sqlite3_int64 prevWritten = DELTA_LIST_OFFSET;
      int i = 0;  /* Offset in ids */
      int j = 0;  /* Byte offset in oldList */
      /* Last value read */
      sqlite3_int64 read = SQLITE3_INT64_MIN;
      bool read_valid = false;  /* True, if read is valid */
      /* Read a value from oldList if there's any */
      if(j < nOldSize){
        read_valid = true;
        j += readVarInt(oldList + j, &read);
        read += DELTA_LIST_OFFSET;
      }
      /* Continue looping while there's data */
      while(i < nIds || read_valid){
        /* Write everything in ids smaller than read */
        while(i < nIds && (ids[i] < read || !read_valid)){
          nSize += writeVarInt(docList + nSize, ids[i] - prevWritten);
          prevWritten = ids[i];
          i += 1;
        }
        /* Write while read is valid and smaller than next ids, or ids is at end */
        while(read_valid && (i >= nIds || read < ids[i])){
          nSize += writeVarInt(docList + nSize, read - prevWritten);
          prevWritten = read;
          read_valid = false;
          if(j < nOldSize){
            read_valid = true;
            sqlite3_int64 prevRead = read;
            j += readVarInt(oldList + j, &read);
            read += prevRead;
          }
        }
        /* If we have two of the same id */
        if(read_valid && i < nIds && ids[i] == read){
          /* Write once */
          nSize += writeVarInt(docList + nSize, read - prevWritten);
          prevWritten = read;
          /* Advance oldList */
          read_valid = false;
          if(j < nOldSize){
            read_valid = true;
            sqlite3_int64 prevRead = read;
            j += readVarInt(oldList + j, &read);
            read += prevRead;
          }
          /* Advanced ids */
          i += 1;
        }
      }
  }else{
    /* Allocate memory for new docList as we didn't get any from query */
    assert(rc == SQLITE_DONE);    
    docList = (unsigned char*)sqlite3_malloc(MAX_VARINT_SIZE * nIds);
    nSize = 0;
    sqlite3_int64 prev = DELTA_LIST_OFFSET;
    int i;
    for(i = 0; i < nIds; i++){
      nSize += writeVarInt(docList + nSize, ids[i] - prev);
      prev = ids[i];
    }
  }
  /* Reset stmt */
  rc = sqlite3_reset(pTrgVtab->stmt_fetch_doclist);
  assert(rc == SQLITE_OK);

  /*Insert docList */
  rc = sqlite3_bind_int64(pTrgVtab->stmt_update_doclist, 1, (sqlite_int64)trigram);
  assert(rc == SQLITE_OK);
  rc = sqlite3_bind_blob(pTrgVtab->stmt_update_doclist, 2, docList, nSize, sqlite3_free);
  assert(rc == SQLITE_OK);
  /* Evaluate */
  rc = sqlite3_step(pTrgVtab->stmt_update_doclist);
  assert(rc == SQLITE_DONE);
  rc = sqlite3_reset(pTrgVtab->stmt_update_doclist);
  assert(rc == SQLITE_OK);

  return rc;
}

/** Remove text from trigram index */
static int indexRemoveText(trilite_vtab *pTrgVtab, sqlite3_int64 id){
  /*TODO: Implement this function */
  assert(0);
  return SQLITE_INTERNAL;
}

/****************************** Sql Statements *******************************/

/** Prepare sql statements for use */
static int prepareSql(trilite_vtab *pTrgVtab){
  char *zSql;
  char *zDb = pTrgVtab->zDb;
  char *zName = pTrgVtab->zName;
  int rc = SQLITE_OK;
  
  /* Delete from %_content */
  zSql = sqlite3_mprintf("DELETE FROM %Q.'%q_content' WHERE id = ?", zDb, zName);
  rc = sqlite3_prepare_v2(pTrgVtab->db, zSql, -1, &pTrgVtab->stmt_delete_content, 0);
  sqlite3_free(zSql);
  assert(rc == SQLITE_OK);
  
  /* Insert into %_content */
  zSql = sqlite3_mprintf("INSERT INTO %Q.'%q_content' (id, text) VALUES (?, ?)", zDb, zName);
  rc = sqlite3_prepare_v2(pTrgVtab->db, zSql, -1, &pTrgVtab->stmt_insert_content, 0);
  sqlite3_free(zSql);
  assert(rc == SQLITE_OK);

  /* Update row in %_content */
  zSql = sqlite3_mprintf("UPDATE %Q.'%q_content' SET id = ?, text = ? WHERE id = ?", zDb, zName);
  rc = sqlite3_prepare_v2(pTrgVtab->db, zSql, -1, &pTrgVtab->stmt_update_content, 0);
  sqlite3_free(zSql);
  assert(rc == SQLITE_OK);

  /* Select row from %_index */
  zSql = sqlite3_mprintf("SELECT doclist FROM %Q.'%q_index' WHERE trigram = ?", zDb, zName);
  rc = sqlite3_prepare_v2(pTrgVtab->db, zSql, -1, &pTrgVtab->stmt_fetch_doclist, 0);
  sqlite3_free(zSql);
  assert(rc == SQLITE_OK);

  /* Update (insert) row into %_index */
  zSql = sqlite3_mprintf("INSERT OR REPLACE INTO %Q.'%q_index' (trigram, doclist) VALUES (?, ?)", zDb, zName);
  rc = sqlite3_prepare_v2(pTrgVtab->db, zSql, -1, &pTrgVtab->stmt_update_doclist, 0);
  sqlite3_free(zSql);
  assert(rc == SQLITE_OK);
  
  return rc;
}

/** Finalize sql statement, release memory */
static int finalizeSql(trilite_vtab *pTrgVtab){
  int rc = SQLITE_OK;
  log("Releasing statements");

  /* Delete from %_content */
  rc = sqlite3_finalize(pTrgVtab->stmt_delete_content);
  pTrgVtab->stmt_delete_content = NULL;
  assert(rc == SQLITE_OK);
  
  /* Insert into %_content */
  rc = sqlite3_finalize(pTrgVtab->stmt_insert_content);
  pTrgVtab->stmt_insert_content = NULL;
  assert(rc == SQLITE_OK);

  /* Update row in %_content */
  rc = sqlite3_finalize(pTrgVtab->stmt_update_content);
  pTrgVtab->stmt_update_content = NULL;
  assert(rc == SQLITE_OK);

  /* Select row in %_index */
  rc = sqlite3_finalize(pTrgVtab->stmt_fetch_doclist);
  pTrgVtab->stmt_fetch_doclist = NULL;
  assert(rc == SQLITE_OK);
  
  /* Update (insert) row in %_index */
  rc = sqlite3_finalize(pTrgVtab->stmt_update_doclist);
  pTrgVtab->stmt_update_doclist = NULL;    
  assert(rc == SQLITE_OK);
  
  /* It's too late to care about errors where, maybe an assert than none occur would be appropriate */
  return rc;
}

