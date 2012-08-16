#ifndef TRILITE_CURSOR_H
#define TRILITE_CURSOR_H

#include <sqlite3ext.h>
#include <stdint.h>

#include "config.h"

typedef struct trilite_cursor trilite_cursor;

int triliteOpen(sqlite3_vtab*, sqlite3_vtab_cursor**);
int triliteClose(sqlite3_vtab_cursor*);
int triliteFilter(sqlite3_vtab_cursor*, int, const char*, int, sqlite3_value**);
int triliteNext(sqlite3_vtab_cursor*);
int triliteEof(sqlite3_vtab_cursor*);
int triliteColumn(sqlite3_vtab_cursor*, sqlite3_context*, int);
int triliteRowid(sqlite3_vtab_cursor*, sqlite_int64*);
int triliteCursorFromBlob(trilite_cursor**, sqlite3_value*);
int triliteText(trilite_cursor*, const unsigned char**, int*);
int triliteAddExtents(trilite_cursor*, uint32_t, uint32_t);
void extentsFunction(sqlite3_context*, int, sqlite3_value**);

#endif /* TRILITE_CURSOR_H */
