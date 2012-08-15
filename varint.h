#ifndef TRILITE_VARINT_H
#define TRILITE_VARINT_H

#include <sqlite3ext.h>

#define MAX_VARINT_SIZE             9

int readVarInt(unsigned char*, sqlite3_int64*);
int writeVarInt(unsigned char*, sqlite3_int64);

#endif /* TRILITE_VARINT_H */
