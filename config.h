#ifndef TRILITE_CONFIG_H
#define TRILITE_CONFIG_H

#include <stdio.h>
#include <stdint.h>

// Simple log macro for easy debuggin
#ifndef NDEBUG
#define log(...)   do{                                                        \
                            fprintf(stderr, "%s:%i: ", __FILE__, __LINE__);   \
                            fprintf(stderr, __VA_ARGS__);                     \
                            fprintf(stderr, "\n");                            \
                          }while(0)
#else
#define log(...)   ((void)0)
#endif


/** Number of bits per byte */
#define BITSPERBYTE       8

/** Type for usage as trigram type */
typedef uint32_t trilite_trigram;

// Maximum number of bytes pending before flush hash table to database
#define  MAX_PENDING_BYTES          (1 * 1024 * 1024)
// Notes
// 200MiB         6 mins

/** Compute a unique 32 bit hash of a trigram
 * We use this hash as rowid for the document lists.
 */
#define HASH_TRIGRAM(str)   ( (((trilite_trigram)(str)[0]))                    \
                            | (((trilite_trigram)(str)[1]) << BITSPERBYTE)     \
                            | (((trilite_trigram)(str)[2]) << (BITSPERBYTE*2)) )


/** Maximum value of an sqlite3_int64 */
#define SQLITE3_INT64_MAX                   (  (sqlite3_int64)0x7FFFFFFFFFFFFFFF)
#define SQLITE3_INT64_MIN                   (- (sqlite3_int64)0x7FFFFFFFFFFFFFFF)

#define DELTA_LIST_OFFSET                   0

/** Use scanstr over KMP for substring matching
 * scanstr is better on PCs with a modern CPU, KMP is probably only relevant for
 * embedded system with non-pipelined CPUs. */
#define ENABLE_SCANSTR                      1

/** Index strateties
 * Please note that 0, is an invalid index strategy, this invariant is assumed in
 * the cursor implementation. Also note that IDX flag are exclusive!
 * However, they may be combined with ONE ORDER_BY flag.
 */
#define IDX_FULL_SCAN       1<<0
#define IDX_MATCH_SCAN      1<<1
#define IDX_ROW_LOOKUP      1<<2

/** Flag we can raise on idxNum */
#define ORDER_BY_DESC       1<<3
#define ORDER_BY_ASC        1<<4

/* Macro used to suppress compiler warnings for unused parameters
 * (Heartlessly stolen from fts4)*/
#define UNUSED_PARAMETER(x)   (void)(x)

/** Sweet macro for logical implication */
#define IMPLIES(A,B)    (!(A) || ((A) && (B)))

#endif /* TRILITE_CONFIG_H */
