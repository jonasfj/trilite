#include "triliteInt.h"

/** Hash Table Implementation
 * This file defines the hash table used to stored pending changes, both
 * insertions and deletions. This way, when a pending change is deleted, we can
 * just remove the pending change.
 */

/** Internal declarations */
typedef struct hash_entry hash_entry;
static int hashFindEntry(hash_table*, trilite_trigram,
                         hash_entry**, hash_entry**);
static int hashReallocateEntry(hash_table*, hash_entry**,
                               trilite_trigram, int, hash_entry*);


/** A super simple hash table
 * Notice that the keys array is allocated in the block as the hash_table.
 */
struct hash_table{
  int                 iMemory;        /** Memory usage by the hash table */
  hash_entry          *keys[HASH_TABLE_KEYS]; /** Hash table keys */
};


/** Hash entry for a trigram
 * Chained with all trigrams that have the same hash key
 * Note, that pAdded and pRemoved will be allocated along with the hash_entry
 * that owns these arrays. This enables us to move memory between the two arrays
 * without the need to reallocate any of them.
 * Memory layout will be as follows:
 *
 *              Memory Layout of Block Allocated for Entries
 *           +--------------------+--------+--------+----------+
 *      Size | sizeof(hash_entry) | nAdded | nAvail | nRemoved |
 *           +--------------------+--------+--------+----------+
 *  Contents |     hash_entry     | pAdded |        | pRemoved |
 *           +--------------------+--------+--------+----------+
 *
 * As seen on the memory layout table we allocate one block for a hash_entry and
 * its pAdded and pRemoved arrays. We place the hash_entry in the front of the
 * block, followed by the pAdded array. At the end of the block we place the
 * pRemoved block. The space between the pAdded and pRemoved is not used and is
 * nAvail * sizeof(sqlite3_int64) bytes large. This is the over-alloocation used
 * to ensure a better amortized complexity with respect to the number of
 * reallocations.
 */
struct hash_entry{
  trilite_trigram     trigram;        /** Trigram stored in this entry */
  hash_entry          *pNext;         /** Next hash entry with same hash key */
  int                 nAvail;         /** Available slots for pAdded/pRemoved */
  int                 nAdded;         /** Number of entries in pAdded */
  int                 nRemoved;       /** Number of entries in pRemoved */
  sqlite3_int64       *pAdded;        /** Ids to be added for this trigram */
  sqlite3_int64       *pRemoved;      /** Ids to be removed for this trigram */
};


/** Cursor for iterating over a hash table */
struct hash_table_cursor{
  hash_table          *pTable;        /** Hash table from hashOpen */
  hash_entry          *pDelete;       /** Last entry returned (to be deleted) */
  int                 iCurrentKey;    /** Next key in the hash table */
}; 


/** Create hash table
 * Returns SQLITE_OK, if *ppTable point to a newly created hash table.
 */
int hashCreate(
  hash_table          **ppTable       /** OUT: Created hash table */
){
  assert(ppTable);

  /* Allocate block for hash_table and keys array */
  *ppTable = (hash_table*)sqlite3_malloc(sizeof(hash_table));
  if(!*ppTable)
    return SQLITE_NOMEM;
  
  /* Zero the entire allocated block */
  memset(*ppTable, 0, sizeof(hash_table));

  /* Initialize members */
  (*ppTable)->iMemory = 0;

  return SQLITE_OK;
}


/** Find changes to trigram entry
 * Returns number of added ids for trigram as *pnAdded and pointer to const
 * array of added ids as *ppAdded. *pnRemoved is the number of ids removed, and
 * *ppRemoved is a pointer to a const array of removed ids.
 * Notice that *ppAdded and *ppRemoved may be NULL, if *pnAdded and *pnRemoved
 * are zero, respectively.
 * Constant arrays *ppAdded and *ppRemoved are guaranteed to be valid until next
 * invokation of hashAdd, hashRemove, hashReset, hashTake, hashDestroy.
 */
int hashFind(
  hash_table          *pTable,        /** Hash table created with hashCreate */
  trilite_trigram     trigram,        /** Trigram to locate changes for */
  int                 *pnAdded,       /** OUT: Number of ids added */
  const sqlite3_int64 **ppAdded,      /** OUT: Ids added for given trigram */
  int                 *pnRemoved,     /** OUT: Number of ids removed */
  const sqlite3_int64 **ppRemoved     /** OUT: Ids removed for given trigram */
){
  assert(pTable && pnAdded && ppAdded && pnRemoved && ppRemoved);

  hash_entry *pEntry      = NULL;
  hash_entry *pPrevEntry  = NULL; /* Not used, but required by hashFindEntry */
  if(hashFindEntry(pTable, trigram, &pEntry, &pPrevEntry) != SQLITE_OK)
    return SQLITE_INTERNAL;

  /* If we found an entry, returm values from it, these arrays are supposed
   * to be valid until next time we change the hash table.
   * If we didn't find an entry, we return NULL and zero.
   */
  if(pEntry){
    *pnAdded    = pEntry->nAdded;
    *ppAdded    = pEntry->pAdded;
    *pnRemoved  = pEntry->nRemoved;
    *ppRemoved  = pEntry->pRemoved;
  }else{
    *pnAdded    = 0;
    *ppAdded    = NULL;
    *pnRemoved  = 0;
    *ppRemoved  = NULL;
  }

  return SQLITE_OK;
}


/** Add an id for a given trigram */
int hashAdd(
  hash_table          *pTable,        /** Hash table created with hashCreate */
  trilite_trigram     trigram,        /** Trigram to added id for */
  sqlite3_int64       id              /** Id to added for trigram */
){
  assert(pTable);

  /* Find hash_entry, if one is present */
  hash_entry *pEntry      = NULL;
  hash_entry *pPrevEntry  = NULL;
  if(hashFindEntry(pTable, trigram, &pEntry, &pPrevEntry) != SQLITE_OK)
    return SQLITE_INTERNAL;

  /* Check if id is in pRemoved */
  if(pEntry){
    int i;
    /* TODO: Consider implementing this using a binary search */
    for(i = 0; i < pEntry->nRemoved; i++){
      if(pEntry->pRemoved[i] >= id)
        break; /* ids are ordered low -> high */
    }
    /* If we found the id, remove it and we're done */
    if(i < pEntry->nRemoved && pEntry->pRemoved[i] == id){
      /* We remove index i by move everything in front of it one forward, thus
       * overwritting the i'th entry. Notice that pRemoved is allocated at the
       * end of the block that holds hash_entry, pAdded and pRemoved.
       * See comments on hash_entry for details on memory layout.
       */
      memmove(
        pEntry->pRemoved + 1,                     /* Move 1 slot forward */
        pEntry->pRemoved,                         /* Move from start */
        sizeof(sqlite3_int64) * i                 /* Move i slots forward */
      );
      pEntry->pRemoved += 1;
      pEntry->nRemoved -= 1;

      /* We've removed the pending entry for removal of id, which is equivalent
       * to inserting a pending opertion to insert id.
       */
      return SQLITE_OK;
    }
  }

  /* Reallocate the entry if necessary to insert id in pAdded */
  if(!pEntry || pEntry->nAvail == 0){
    int rc = hashReallocateEntry(pTable, &pEntry, trigram, 1, pPrevEntry);
    if(rc != SQLITE_OK)
      return rc;
  }

  /* At this point we should be ready to insert id into pAdded, and slots should
   * be available for this.
   */
  assert(pEntry->nAvail > 0);

  /* Find the index to insert id at in pAdded */
  /* TODO: Consider implementing this using a binary search */
  int i;
  for(i = 0; i < pEntry->nAdded; i++){
    if(pEntry->pAdded[i] >= id)
      break; /* ids are ordered low -> high */
  }

  /* We should never insert an id for the same trigram twice */
  if(pEntry->nAdded > 0 && pEntry->pAdded[i] == id){
    assert(false);
    return SQLITE_INTERNAL;
  }

  /* Move everything above the index we're inserting at */
  memmove(
    pEntry->pAdded + i + 1,                       /* Move to index i + 1 */
    pEntry->pAdded + i,                           /* Move from index i */
    sizeof(sqlite3_int64) * (pEntry->nAdded - i)  /* Move nAdded - i slots */
  );
  assert(pEntry->pAdded[i] == pEntry->pAdded[i+1]);

  /* Insert and update length of pAdded */
  pEntry->pAdded[i] = id;
  pEntry->nAdded++;
  pEntry->nAvail--;

  return SQLITE_OK;
}


/** Remove an id for a given trigram */
int hashRemove(
  hash_table          *pTable,        /** Hash table created with hashCreate */
  trilite_trigram     trigram,        /** Trigram to remove id for */
  sqlite3_int64       id              /** Id to remove for trigram */
){
  assert(pTable);

  /* Find hash_entry, if one is present */
  hash_entry *pEntry      = NULL;
  hash_entry *pPrevEntry  = NULL;
  if(hashFindEntry(pTable, trigram, &pEntry, &pPrevEntry) != SQLITE_OK)
    return SQLITE_INTERNAL;

  /* Check if id is in pAdded */
  if(pEntry){
    int i;
    /* TODO: Consider implementing this using a binary search */
    for(i = 0; i < pEntry->nAdded; i++){
      if(pEntry->pAdded[i] >= id)
        break; /* ids are ordered low -> high */
    }
    /* If we found the id, remove it and we're done */
    if(i < pEntry->nAdded && pEntry->pAdded[i] == id){
      /* We remove index i by move everything after it one slot forward */
      memmove(
        pEntry->pAdded + i,                               /* Move to i */
        pEntry->pAdded + i + 1,                           /* Move from i + 1 */
        sizeof(sqlite3_int64) * (pEntry->nAdded - i - 1)  /* n - i - 1 slots */
      );
      pEntry->nAdded--;
      
      /* We've removed the pending entry for insertion of id, which is
       * equivalent to inserting a pending opertion to remove id.
       */
      return SQLITE_OK;
    }
  }

  /* Reallocate the entry if necessary to insert id in pRemoved */
  if(!pEntry || pEntry->nAvail == 0){
    int rc = hashReallocateEntry(pTable, &pEntry, trigram, 1, pPrevEntry);
    if(rc != SQLITE_OK)
      return rc;
  }

  /* At this point we should be ready to insert id into pAdded, and slots should
   * be available for this.
   */
  assert(pEntry->nAvail > 0);

  /* Find the index to insert id at in pRemoved */
  /* TODO: Consider implementing this using a binary search */
  int i;
  for(i = 0; i < pEntry->nRemoved; i++){
    if(pEntry->pRemoved[i] >= id)
      break; /* ids are ordered low -> high */
  }

  /* We should never removed an id for the same trigram twice */
  if(pEntry->nRemoved > 0 && pEntry->pRemoved[i] == id){
    assert(false);
    return SQLITE_INTERNAL;
  }

  /* Move everything below the index 1 slot to the left */
  memmove(
    pEntry->pRemoved - 1,                         /* Move to pRemoved - 1 */
    pEntry->pRemoved,                             /* Move from pRemoved */
    sizeof(sqlite3_int64) * i                     /* Move i number of slots */
  );
  assert(pEntry->pRemoved[i - 1] == pEntry->pRemoved[i - 2]);

  /* Insert and update length of pAdded */
  pEntry->pRemoved[i] = id;
  pEntry->pRemoved--;
  pEntry->nRemoved++;
  pEntry->nAvail--;

  return SQLITE_OK;
}


/** Reset hash table
 * Removes all pending changes (added and removed ids)
 */
int hashReset(
  hash_table          *pTable         /** Hash table to reset */
){
  assert(pTable);

  int k;
  for(k = 0; k < HASH_TABLE_KEYS; k++){
    hash_entry *pEntry = pTable->keys[k];
    while(pEntry){
      hash_entry *pNext = pEntry->pNext;
      sqlite3_free(pEntry);
      pEntry = pNext;
    }
    pTable->keys[k] = NULL;
  }
  pTable->iMemory = 0;

  return SQLITE_OK;
}


/** Returns true if the hash table if empty
 * Determines if there is any pending changes.
 */
int hashIsEmpty(
  hash_table          *pTable,        /** Hash table to reset */
  bool                *pRetVal        /** OUT: true, if hash table is empty */
){
  assert(pTable && pRetVal);

  /* Iterate through all keys */
  int k;
  for(k = 0; k < HASH_TABLE_KEYS; k++){
    hash_entry *pEntry = pTable->keys[k];
    while(pEntry){
      /* Check if any entry has pending changes, return true */
      if(pEntry->nAdded + pEntry->nRemoved > 0){
        *pRetVal = true;
        return SQLITE_OK;
      }
      pEntry = pEntry->pNext;
    }
  }

  /* Return false if no changes was found */
  *pRetVal = false;

  return SQLITE_OK;
}


/** Destroy hash table (release all resources held) */
int hashDestroy(
  hash_table          *pTable         /** Hash table to free */
){
  assert(pTable);

  /* Release all hash entries */
  hashReset(pTable);

  /* Free memory used to hold the table */
  sqlite3_free(pTable);
  
  return SQLITE_OK;
}


/** Open as hash table cursor */
int hashOpen(
  hash_table          *pTable,        /** Hash table to open a cursor on */
  hash_table_cursor   **ppCursor      /** OUT: Hash table cursor */
){
  assert(pTable && ppCursor);

  /* Allocate cursor */
  *ppCursor = (hash_table_cursor*)sqlite3_malloc(sizeof(hash_table_cursor));
  if(!(*ppCursor))
    return SQLITE_NOMEM;

  /* Initialize members */
  (*ppCursor)->pTable       = pTable;
  (*ppCursor)->pDelete      = NULL;
  (*ppCursor)->iCurrentKey  = 0;

  return SQLITE_OK;
}


/** Returns and removes the next trigram and associated changes
 * The trigram is returned as *pTrigram, list of added ids as ppAdded, list of
 * removed ids as ppRemoved.
 * Pointers will be deallocated on next call to either hashPop or hashClose.
 * This function returns SQLITE_ROW when a trigram and associated changes is
 * returned, and SQLITE_DONE at end of hash table (ie. pointers are invalid!).
 */
int hashTake(
  hash_table_cursor   *pCursor,       /** Hash table cursor from hashOpen */
  trilite_trigram     *pTrigram,      /** OUT: Trigram being returned */
  int                 *pnAdded,       /** OUT: Number of ids added */
  sqlite3_int64       **ppAdded,      /** OUT: Ids added for trigram */
  int                 *pnRemoved,     /** OUT: Number of ids removed */
  sqlite3_int64       **ppRemoved     /** OUT: Ids removed for trigram */
){
  assert(pCursor && pnAdded && ppAdded && pnRemoved && ppRemoved);

  /* Get hash table pointer from cursor */
  hash_table  *pTable = pCursor->pTable;
  assert(pTable);

  /* Free hash entry to be deleted */
  if(pCursor->pDelete){
    sqlite3_free(pCursor->pDelete);
    pCursor->pDelete = NULL;
  }

  /* Increment the key until we find a non-empty entry */
  int key = pCursor->iCurrentKey;
  while(key < HASH_TABLE_KEYS && pTable->keys[key] == NULL)
    key++;
  pCursor->iCurrentKey = key;

  /* If there is no more keys */
  if(key >= HASH_TABLE_KEYS){
    *pnAdded    = 0;
    *ppAdded    = NULL;
    *pnRemoved  = 0;
    *ppRemoved  = NULL;
    return SQLITE_DONE;
  }

  /* Take entry from this key */
  hash_entry *pEntry  = pTable->keys[key];
  pTable->keys[key]   = pEntry->pNext;
  assert(pEntry);

  /* Return values from pEntry */
  *pTrigram           = pEntry->trigram;
  *pnAdded            = pEntry->nAdded;
  *ppAdded            = pEntry->pAdded;
  *pnRemoved          = pEntry->nRemoved;
  *ppRemoved          = pEntry->pRemoved;

  /* Next entry to delete is pEntry */
  pCursor->pDelete    = pEntry;

  return SQLITE_ROW;
}


/** Close and release resources held by hash table cursor */
int hashClose(
  hash_table_cursor   *pCursor        /** Hash table cursor from hashOpen */
){
  assert(pCursor);

  /* Free hash entry to be deleted */
  if(pCursor->pDelete){
    sqlite3_free(pCursor->pDelete);
    pCursor->pDelete = NULL;
  }

  /* Free the hash table */
  sqlite3_free(pCursor);

  return SQLITE_OK;
}


/** Find the hash table entry for a given trigram
 * The entry for trigram is returned as *ppEntry, if there is no entry for the
 * trigram, *ppEntry is set to NULL. *ppPrevEntry is set to entry that comes
 * right before the entry for trigram, NULL if trigram is the first entry.
 * If no entry for trigram is found, *ppPrevEntry is the entry that is a new
 * entry should be chained after.
 * (This is an internal auxiliary function only used in this file)
 */
static int hashFindEntry(
  hash_table          *pTable,        /** Hash table to search find entry in */
  trilite_trigram     trigram,        /** Trigram to find entry for */
  hash_entry          **ppEntry,      /** OUT: Entry for trigram or NULL */
  hash_entry          **ppPrevEntry   /** OUT: Entry before trigram or NULL */
){
  assert(pTable && ppEntry && ppPrevEntry);
  /* We start searching from the entry given by lookup in pTable->keys using the
   * hash function.
   * At this point the entry before it is NULL (ie. pTable->keys lookup).
   */
  *ppPrevEntry  = NULL;
  *ppEntry      = pTable->keys[COMPUTE_HASH(trigram)];
  /* As long as we have an entry, and this entry has a trigram with a value
   * lower than the trigram we're searching for we continue our search.
   * Notice, that hash entries are ordered by trigram value, in ascending order.
   */
  while(*ppEntry && (*ppEntry)->trigram < trigram){
    *ppPrevEntry  = *ppEntry;
    *ppEntry      = (*ppEntry)->pNext;
  }
  /* If at this point we have that the (*ppEntry)-trigram == trigram, then we
   * wish to output *ppEntry, as we found an entry for the trigram.
   * If this isn't the case we must, however, output NULL, indicating that the
   * hash table entry can be inserted after *ppPrevEntry
   */
  if(*ppEntry && (*ppEntry)->trigram != trigram)
    *ppEntry = NULL;
  return SQLITE_OK;
}


/** (Re)allocate hash table entry to make room for more slots
 * This function can only be used to increase the size of a hash_entry.
 * The hash_entry given as *ppEntry will be increased with nNewSlots new slots.
 * If *ppEntry is NULL it will be allocated from scratch. Notice that ppEntry is
 * both an input and an output variable as it may be relocated during
 * reallocation. pPrevEntry is the hash_entry *ppEntry should be chained after,
 * and given as NULL if *ppEntry should be the first entry for its key.
 */
static int hashReallocateEntry(
  hash_table          *pTable,        /** Hash table who owns this entry */
  hash_entry          **ppEntry,      /** IN/OUT: Entry to reallocate */
  trilite_trigram     trigram,        /** Trigram to *ppEntry */
  int                 nNewSlots,      /** Number of new slots to allocate for */
  hash_entry          *pPrevEntry     /** Entry to chaing *ppEntry after */
){
  assert(pTable && ppEntry && nNewSlots > 0);

  /* Find the number of slots required */
  int nSlots = nNewSlots;
  if(*ppEntry)
    nSlots += (*ppEntry)->nAdded + (*ppEntry)->nRemoved;

  /* Over-allocate using allocation factor and minimum allocation constant */
  nSlots = MIN(
    HASH_ENTRY_ALLOCATION_FACTOR * nSlots,
    HASH_ENTRY_MIN_ALLOCATION
  );

  /* Remember if we need to initialize *ppEntry, once reallocated */
  bool initialize = !(*ppEntry);

  /* Reallocate *ppEntry*/
  *ppEntry = (hash_entry*)sqlite3_realloc(
      *ppEntry,
      sizeof(hash_entry) + sizeof(sqlite3_int64) * nSlots
  );
  if(!(*ppEntry))
    return SQLITE_NOMEM;

  /* Initialize *ppEntry, if needed */
  if(initialize){
    (*ppEntry)->trigram   = trigram;
    (*ppEntry)->nAdded    = 0;
    (*ppEntry)->nRemoved  = 0;
    (*ppEntry)->nAvail    = 0;
  }
  assert((*ppEntry)->trigram == trigram);
  assert((*ppEntry)->nAdded >= 0);
  assert((*ppEntry)->nRemoved >= 0);
  assert((*ppEntry)->nAvail >= 0);

  /* Update pointers to potentially relocated memory */
  (*ppEntry)->pAdded    = (sqlite3_int64*)((*ppEntry) + 1);
  (*ppEntry)->pRemoved  = (*ppEntry)->pAdded + 
                          (*ppEntry)->nAdded +
                          (*ppEntry)->nAvail;

  /* Update pointers to potentially relocated *ppEntry */
  if(pPrevEntry){
    /* If there is a entry before *ppEntry we chain *ppEntry after pPrevEntry */
    (*ppEntry)->pNext   = pPrevEntry->pNext;
    pPrevEntry->pNext   = (*ppEntry);
  }else{
    /* If there's no entry before *ppEntry, we insert *ppEntry in the hash table
     * any entry in the hash table will be chained after *ppEntry.
     */
    int iHash           = COMPUTE_HASH(trigram);
    (*ppEntry)->pNext   = pTable->keys[iHash];
    pTable->keys[iHash] = *ppEntry;
  }

  /* Update number of available slots */
  nNewSlots = nSlots                  /* We allocated nSlots */
              - (*ppEntry)->nAdded    /* Subtract slots used for pAdded */
              - (*ppEntry)->nRemoved  /* Subtract slots used for pRemoved */
              - (*ppEntry)->nAvail;   /* Subtract slots already available */
  (*ppEntry)->nAvail += nNewSlots;

  /* Move pRemoved to the end of the allocated block */
  memmove(
    (*ppEntry)->pRemoved + nNewSlots,             /* Move nNewSlots forward */
    (*ppEntry)->pRemoved,                         /* Move from pRemoved */
    sizeof(sqlite3_int64) * (*ppEntry)->nRemoved  /* Move nRemoved slots */
  );
  (*ppEntry)->pRemoved += nNewSlots;

  return SQLITE_OK;
}