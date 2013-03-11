#ifndef TRILITE_TRILITE_H
#define TRILITE_TRILITE_H

#include <sqlite3ext.h>

/** Load the TriLite extension from SQLite
 * See load_extension() in SQLite documentation
 */
int sqlite3_extension_init(sqlite3*, char**, const sqlite3_api_routines*);

/** Load trigram extension
 * Uses sqlite3_auto_extension() to call sqlite3_extension_init, you can invoke
 * this function to load TriLite when linking with trilite.
 */
void trilite_load_extension();

#endif /* TRILITE_TRILITE_H */