#include "varint.h"
#include "config.h"

const sqlite3_api_routines *sqlite3_api;

#include <stdint.h>

#define VARINT_END_BITMASK          (1 << (BITSPERBYTE - 1))

/** Read integer in varint encoding from pBuf, return it as *out
 * Returns number of bytes read.
 */
int readVarInt(unsigned char *pBuf, sqlite3_int64 *out){
  // Yes, it's ugly to unroll a loop manually, I didn't test to see if it's
  // faster, it was just easier to write it this way without having unecessary
  // checks all over the place. Also I don't see any reason to waste time
  // designing a smart loop, when this works, and probably is faster :)
  *out = 0;
  // Read byte 0
  *out |= (sqlite3_int64)(pBuf[0] & ~VARINT_END_BITMASK);
  if(pBuf[0] & VARINT_END_BITMASK) return 1;
  // Read byte 1
  *out |= ((sqlite3_int64)(pBuf[1] & ~VARINT_END_BITMASK)) << 7;
  if(pBuf[1] & VARINT_END_BITMASK) return 2;
  // Read byte 2
  *out |= ((sqlite3_int64)(pBuf[2] & ~VARINT_END_BITMASK)) << (7 * 2);
  if(pBuf[2] & VARINT_END_BITMASK) return 3;
  // Read byte 3
  *out |= ((sqlite3_int64)(pBuf[3] & ~VARINT_END_BITMASK)) << (7 * 3);
  if(pBuf[3] & VARINT_END_BITMASK) return 4;
  // Read byte 4
  *out |= ((sqlite3_int64)(pBuf[4] & ~VARINT_END_BITMASK)) << (7 * 4);
  if(pBuf[4] & VARINT_END_BITMASK) return 5;
  // Read byte 5
  *out |= ((sqlite3_int64)(pBuf[5] & ~VARINT_END_BITMASK)) << (7 * 5);
  if(pBuf[5] & VARINT_END_BITMASK) return 6;
  // Read byte 6
  *out |= ((sqlite3_int64)(pBuf[6] & ~VARINT_END_BITMASK)) << (7 * 6);
  if(pBuf[6] & VARINT_END_BITMASK) return 7;
  // Read byte 7
  *out |= ((sqlite3_int64)(pBuf[7] & ~VARINT_END_BITMASK)) << (7 * 7);
  if(pBuf[7] & VARINT_END_BITMASK) return 8;
  // Read byte 8 (starting from 0)
  *out |= ((sqlite3_int64)pBuf[8]) << (7 * 8);
  return 9;
}

/** Write an integer in varint encoding to pBuf
 * Returns number of bytes written */
int writeVarInt(unsigned char *pBuf, sqlite3_int64 input){
  // Write byte 0
  pBuf[0] = ((unsigned char)input) & ~VARINT_END_BITMASK;
  input = input >> 7;
  if(input == 0){ pBuf[0] |= VARINT_END_BITMASK; return 1;}
  // Write byte 1
  pBuf[1] = ((unsigned char)input) & ~VARINT_END_BITMASK;
  input = input >> 7;
  if(input == 0){ pBuf[1] |= VARINT_END_BITMASK; return 2;}
  // Write byte 2
  pBuf[2] = ((unsigned char)input) & ~VARINT_END_BITMASK;
  input = input >> 7;
  if(input == 0){ pBuf[2] |= VARINT_END_BITMASK; return 3;}
  // Write byte 3
  pBuf[3] = ((unsigned char)input) & ~VARINT_END_BITMASK;
  input = input >> 7;
  if(input == 0){ pBuf[3] |= VARINT_END_BITMASK; return 4;}
  // Write byte 4
  pBuf[4] = ((unsigned char)input) & ~VARINT_END_BITMASK;
  input = input >> 7;
  if(input == 0){ pBuf[4] |= VARINT_END_BITMASK; return 5;}
  // Write byte 5
  pBuf[5] = ((unsigned char)input) & ~VARINT_END_BITMASK;
  input = input >> 7;
  if(input == 0){ pBuf[5] |= VARINT_END_BITMASK; return 6;}
  // Write byte 6
  pBuf[6] = ((unsigned char)input) & ~VARINT_END_BITMASK;
  input = input >> 7;
  if(input == 0){ pBuf[6] |= VARINT_END_BITMASK; return 7;}
  // Write byte 7
  pBuf[7] = ((unsigned char)input) & ~VARINT_END_BITMASK;
  input = input >> 7;
  if(input == 0){ pBuf[7] |= VARINT_END_BITMASK; return 8;}
  // Write byte 8 (starting from 0)
  pBuf[8] = ((unsigned char)input);
  return 9;
}

