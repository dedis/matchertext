/*
** Test driver for injection-research/sqlite/src/matchertext.c.
**
** The routines under test take no arguments a test harness cannot supply, so
** this driver is deliberately thin: it decodes commands, calls one function,
** and prints one number.  All test logic lives in matchertext_test.go, which
** owns the expected values and the Go oracle.
**
** Commands arrive one per line on stdin.  Values are hex encoded so that zero
** bytes, newlines and invalid UTF-8 all survive the trip.
**
**   V <hex>   print sqlite3MatchertextVerify(value, length)
**   E <hex>   print sqlite3MatchertextEnd(value), a zero byte appended
**   C <hex>   print sqlite3MatchertextEncode(value) as hex
**   D <hex>   print sqlite3MatchertextDecode(value) as hex
**
** Each command prints one decimal result.  A malformed command is a harness
** bug, not a test failure, so the driver exits non-zero and says so.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqliteInt.h"

int sqlite3MatchertextVerify(const unsigned char *z, i64 n);
i64 sqlite3MatchertextEnd(const unsigned char *z);
char *sqlite3MatchertextEncode(const char *z, i64 n, i64 *pnOut);
i64 sqlite3MatchertextDecodeInPlace(char *z, i64 n, int bFull);

/* Long enough for the deepest nesting the tests generate, hex encoded. */
#define MAX_LINE  (1<<20)

static char zLine[MAX_LINE];
static unsigned char aVal[MAX_LINE/2 + 1];

static int hexVal(int c){
  if( c>='0' && c<='9' ) return c - '0';
  if( c>='a' && c<='f' ) return c - 'a' + 10;
  if( c>='A' && c<='F' ) return c - 'A' + 10;
  return -1;
}

/* Decode zHex into aVal[].  Return the byte count, or -1 if malformed. */
static i64 decode(const char *zHex){
  i64 n = 0;
  int hi, lo;
  while( zHex[0] && zHex[0]!='\n' ){
    hi = hexVal((unsigned char)zHex[0]);
    lo = hexVal((unsigned char)zHex[1]);
    if( hi<0 || lo<0 ) return -1;
    aVal[n++] = (unsigned char)((hi<<4) | lo);
    zHex += 2;
  }
  return n;
}

int main(void){
  while( fgets(zLine, sizeof(zLine), stdin) ){
    i64 n;
    char op = zLine[0];
    if( (op!='V' && op!='E' && op!='C' && op!='D') || zLine[1]!=' ' ){
      fprintf(stderr, "driver: malformed command\n");
      return 1;
    }
    n = decode(zLine+2);
    if( n<0 ){
      fprintf(stderr, "driver: malformed hex\n");
      return 1;
    }
    if( op=='C' || op=='D' ){
      /* Encode or decode, and print the result as hex so that any byte
      ** survives the trip back. */
      i64 nOut = 0, m;
      char *zRes;
      if( op=='C' ){
        zRes = sqlite3MatchertextEncode((const char*)aVal, n, &nOut);
        if( zRes==0 ){
          fprintf(stderr, "driver: out of memory\n");
          return 1;
        }
      }else{
        zRes = (char*)aVal;
        nOut = sqlite3MatchertextDecodeInPlace(zRes, n, 1);
      }
      for(m=0; m<nOut; m++) printf("%02x", (unsigned char)zRes[m]);
      printf("\n");
      if( op=='C' ) free(zRes);
    }else if( op=='V' ){
      printf("%d\n", sqlite3MatchertextVerify(aVal, n));
    }else{
      /* FINDEMBEDEND reads to a zero byte, so terminate the value here
      ** rather than making every caller remember to. */
      aVal[n] = 0;
      printf("%lld\n", (long long int)sqlite3MatchertextEnd(aVal));
    }
  }
  return 0;
}
