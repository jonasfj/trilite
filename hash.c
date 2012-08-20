#include "hash.h"

#include <string.h>
#include <assert.h>

const sqlite3_api_routines *sqlite3_api;

/** Factor of extra slots to allocate, at least 1!
 * Notice that we only have one allocation factor, essentially, we don't deallocate,
 * If a lot of docs suddenly disappear from an entry, it's probably a popular entry anyway.
 * Yes, we'll deallocate when they are empty, yes, this could lead us to deallocate/reallocate
 * if a trigram is added/removed over and over again, but that's not likely to happen for many
 * trigrams unless it's the same document, but that's just stupid :)
 * Besides entries are sorted, to optimize for querying, thus, there's no nice amortized complexity anyway.
 */
#define ALLOCATION_FACTOR           (1.5f)
/** No point in allocating anything less than this */
#define MIN_ALLOCATION              4


#define HASH_TABLE_ENTRIES          32749

#define COMPUTE_HASH(trigram)       (trigram % HASH_TABLE_ENTRIES)

typedef struct hash_entry hash_entry;

/** A super simple hash table */
struct hash_table{
  /** Current memory usage by the hash table */
  int memory;
  /** Keys, each key contains a linked list of trigrams */
  hash_entry *keys[HASH_TABLE_ENTRIES];
};

/** Simple macro for getting the doclist pointer from hash_entry */
#define DOCLIST(pEntry)             ((sqlite3_int64*)(pEntry + 1))

/** Hash entry, for a given trigram, chained with all trigrams that have the same key */
struct hash_entry{
  /** Trigram stored in this entry */
  trilite_trigram trigram;
  
  /** Next hash entry with same key (NULL, if non) */
  hash_entry *next;
  
  /** Number of unused slots after nDocList */
  int nSizeAvail;
  
  /** Number of entries in doclist */
  int nDocList;

  /** doclist is stored at this location, you can get the doclist of a
   * hash_entry using the DOCLIST(pEntry) macro. */
};

static bool findEntry(hash_table*, trilite_trigram, hash_entry**, hash_entry**);

/** Allocate a new hash table, output it to ppTable */
int hashCreate(hash_table** ppTable){
  *ppTable = (hash_table*)sqlite3_malloc(sizeof(hash_table));
  if(!*ppTable) return SQLITE_NOMEM;
  memset(*ppTable, 0, sizeof(hash_table));
  (*ppTable)->memory = 0;
  return SQLITE_OK;
}

/** Release all resources held by the hash table */
void hashRelease(hash_table *pTable){
  int i;
  for(i = 0; i < HASH_TABLE_ENTRIES; i++){
    hash_entry *pEntry = pTable->keys[i];
    while(pEntry){
      hash_entry *pPrevEntry = pEntry;
      pEntry = pEntry->next;
      sqlite3_free(pPrevEntry);
    }
  }
  sqlite3_free(pTable);
}


/** Get an estimate of the hash_tables current memory usage */
int hashMemoryUsage(hash_table* pTable){
  return pTable->memory;
}


/** Find doclist for trigram in hash table 
 * Returns pointer to doclist, NULL if not found.
 * Number of entries in doclist is output as *pnDocList (0 if doclist == NULL).
 */
sqlite3_int64* hashFind(hash_table *pTable, trilite_trigram trigram, int *pnDocList){
  hash_entry *pEntry;
  hash_entry *pPrevEntry;
  if(findEntry(pTable, trigram, &pEntry, &pPrevEntry)){
    *pnDocList = pEntry->nDocList;
    return DOCLIST(pEntry);
  }
  *pnDocList = 0;
  return NULL;
}

/** Insert document id in hash */
bool hashInsert(hash_table *pTable, trilite_trigram trigram, sqlite3_int64 id){
  hash_entry *pEntry;
  hash_entry *pPrevEntry;
  if(findEntry(pTable, trigram, &pEntry, &pPrevEntry)){
    /* If there's no slots available, we have to reallocate and update next pointer on pPrevEntry */
    if(!pEntry->nSizeAvail){
      assert(ALLOCATION_FACTOR >= 1);
      int slots = pEntry->nDocList * ALLOCATION_FACTOR + 1;
      pEntry = (hash_entry*)sqlite3_realloc(pEntry, sizeof(hash_entry) + sizeof(sqlite3_int64) * slots);
      pEntry->nSizeAvail = slots - pEntry->nDocList;
      pTable->memory += pEntry->nSizeAvail * sizeof(sqlite3_int64);
    }
  }else{
    /* If there was not entry for this trigram let's allocate one */
    pEntry = (hash_entry*)sqlite3_malloc(sizeof(hash_entry) + sizeof(sqlite_int64) * MIN_ALLOCATION);
    pEntry->nDocList    = 0;
    pEntry->nSizeAvail  = MIN_ALLOCATION;
    pEntry->next        = NULL;
    pEntry->trigram     = trigram;
    pTable->memory += sizeof(hash_entry) + sizeof(sqlite_int64) * MIN_ALLOCATION;
  }
  /* Create/Update references */
  if(pPrevEntry)
    pPrevEntry->next = pEntry;
  else
    pTable->keys[COMPUTE_HASH(trigram)] = pEntry;
  
  /* Insert id into docList, move ids as necessary */
  int i;
  /* TODO A cute little binary search might be nice */
  sqlite3_int64 *docList = DOCLIST(pEntry);
  for(i = 0; i < pEntry->nDocList; i++)
    if(docList[i] >= id) break;
  /* Return false, if id was already here */
  if(i < pEntry->nDocList && docList[i] == id) return false;
  /* Move all ids > id */
  memmove(docList + i + 1, docList + i, (pEntry->nDocList - i) * sizeof(sqlite3_int64));
  /* Insert id */
  docList[i] = id;

  /* Update available size */
  pEntry->nDocList += 1;
  pEntry->nSizeAvail -= 1;
  
  return true;
}

/** Remove id from trigram, if present */
bool hashRemove(hash_table *pTable, trilite_trigram trigram, sqlite3_int64 id){
  assert(false);
  return false;
}


/** Find entry and previous entry for a trigram
 * Returns true, if an entry was found.
 * Entry is output as *ppEntry, previous entry is output as *ppPrevEntry,
 * if no entry is found *ppPrevEntry points to the last entry in list for
 * the key, or NULL if none.
 */
static bool findEntry(hash_table *pTable, trilite_trigram trigram, hash_entry **ppEntry, hash_entry **ppPrevEntry){
  assert(ppEntry && ppPrevEntry);
  *ppPrevEntry  = NULL;
  *ppEntry      = pTable->keys[COMPUTE_HASH(trigram)];
  while(*ppEntry){
    if((*ppEntry)->trigram == trigram)
      return true;
    *ppPrevEntry  = *ppEntry;
    *ppEntry      = (*ppEntry)->next;
  }
  return false;
}

/***************************** Hash Table Cursor *****************************/

/** Cursor for iterating over a hash table */
struct hash_table_cursor{
  int iOffset;
  hash_entry *pDelete;
  hash_table *pTable;
};

/** Allocate and open a hash table cursor */
int hashOpen(hash_table *pTable, hash_table_cursor **ppCur){
  *ppCur = (hash_table_cursor*)sqlite3_malloc(sizeof(hash_table_cursor));
  if(!*ppCur) return SQLITE_NOMEM;
  (*ppCur)->iOffset = 0;
  (*ppCur)->pDelete = NULL;
  (*ppCur)->pTable  = pTable;
  return SQLITE_OK;
}

/** Return and remove the next trigram and doclist
 * Returns false, if at end of hash table
 * The trigram is returned as *pTrigram, list of ids as *ids, and length of *pIds
 * as *nIds. Pointers will be deallocated on next call to either hashPop or
 * hashClose.
 */
bool hashPop(hash_table_cursor *pCur,
             trilite_trigram *pTrigram, sqlite3_int64 **pIds, int *nIds){
  /* Release anything waiting for release */
  if(pCur->pDelete){
    sqlite3_free(pCur->pDelete);
    pCur->pDelete = NULL;
  }

  /* Find next offset where there is something */
  while(pCur->iOffset < HASH_TABLE_ENTRIES && !pCur->pTable->keys[pCur->iOffset])
    pCur->iOffset++;

  /* If there was nothing return false */
  if(pCur->iOffset >= HASH_TABLE_ENTRIES)
    return false;

  /* Set the entry we wish to delete */
  pCur->pDelete = pCur->pTable->keys[pCur->iOffset];
  /* Remove it from the hash table */
  pCur->pTable->keys[pCur->iOffset] = pCur->pDelete->next;

  /* Update memory usage */
  pCur->pTable->memory -= sizeof(hash_entry) + (pCur->pDelete->nDocList + pCur->pDelete->nSizeAvail) * sizeof(sqlite3_int64);

  /* Set return values */
  *pTrigram = pCur->pDelete->trigram;
  *pIds     = DOCLIST(pCur->pDelete);
  *nIds     = pCur->pDelete->nDocList;
  return true; 
}

/** Release hash table cursor */
void hashClose(hash_table_cursor *pCur){
  /* Release anything waiting for release */
  if(pCur->pDelete){
    sqlite3_free(pCur->pDelete);
    pCur->pDelete = NULL;
  }
  /* Release the cursor */
  sqlite3_free(pCur);
  pCur = NULL;
}
