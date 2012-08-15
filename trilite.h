#ifndef TRILITE_TRILITE_H
#define TRILITE_TRILITE_H

#include <sqlite3ext.h>

int sqlite3_extension_init(sqlite3*, char**, const sqlite3_api_routines*);

void load_trigram_extension();

#endif /* TRILITE_TRILITE_H */
