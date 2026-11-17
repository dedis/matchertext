/*
** A stock SQLite reading the statements that the matchertext holes compose:
** the downgrade case of an aware writer whose reader was not upgraded.  Built
** against the amalgamation of the same tree without SQLITE_ENABLE_MATCHERTEXT.
**
** Input: one statement per line, hex encoded so any byte survives the trip.
** Output: one line per statement:
**   refused          the statement does not prepare
**   stacked          SQL follows the first statement
**   <skel> <nrow>    16 hex digits, FNV-1a of the EXPLAIN listing without its
**                    text operands; the rows the statement returns, or -1 when
**                    it stopped with an error or after a million steps
** The fixture is the driver's: t1(name TEXT, id INTEGER) with two rows.  Each
** statement runs inside a savepoint that is rolled back.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"

typedef unsigned long long u64;

#define MAX_LINE (1<<21)

static sqlite3 *gDb;
static int gnStep;

static void die(const char *zMsg){
  fprintf(stderr, "stock_reader: %s: %s\n", zMsg, gDb ? sqlite3_errmsg(gDb) : "");
  exit(1);
}

static void exec(const char *zSql){
  gnStep = 0;
  if( sqlite3_exec(gDb, zSql, 0, 0, 0)!=SQLITE_OK ) die(zSql);
}

static u64 fnvByte(u64 h, int c){ h ^= (u64)(unsigned char)c; return h * 1099511628211ULL; }
static u64 fnvInt(u64 h, sqlite3_int64 v){
  int i;
  for(i=0; i<8; i++) h = fnvByte(h, (int)((v>>(i*8)) & 0xff));
  return h;
}

/* Stop a statement after a million virtual machine steps. */
static int progress(void *p){
  (void)p;
  return ++gnStep > 1000;
}

static int hexVal(int c){
  if( c>='0' && c<='9' ) return c-'0';
  if( c>='a' && c<='f' ) return c-'a'+10;
  return -1;
}

static void readOne(const char *zSql){
  sqlite3_stmt *pStmt = 0, *pX = 0, *pTail = 0;
  const char *zTail = 0;
  char *zX;
  u64 h = 1469598103934665603ULL;
  int nRow = 0, rc;

  gnStep = 0;
  if( sqlite3_prepare_v2(gDb, zSql, -1, &pStmt, &zTail)!=SQLITE_OK || pStmt==0 ){
    sqlite3_finalize(pStmt);
    printf("refused\n");
    return;
  }
  if( zTail && zTail[0] ){
    rc = sqlite3_prepare_v2(gDb, zTail, -1, &pTail, 0);
    sqlite3_finalize(pTail);
    if( rc!=SQLITE_OK || pTail!=0 ){
      sqlite3_finalize(pStmt);
      printf("stacked\n");
      return;
    }
  }
  zX = sqlite3_mprintf("EXPLAIN %s", zSql);
  if( zX==0 ) die("oom");
  if( sqlite3_prepare_v2(gDb, zX, -1, &pX, 0)!=SQLITE_OK ) die("EXPLAIN");
  while( sqlite3_step(pX)==SQLITE_ROW ){
    const unsigned char *zOp = sqlite3_column_text(pX, 1);
    int i;
    while( zOp && *zOp ) h = fnvByte(h, *zOp++);
    h = fnvByte(h, 0);
    for(i=2; i<=4; i++) h = fnvInt(h, sqlite3_column_int64(pX, i));
    h = fnvInt(h, sqlite3_column_int64(pX, 6));
    if( sqlite3_column_type(pX, 5)!=SQLITE_TEXT ) h = fnvInt(h, sqlite3_column_int64(pX, 5));
  }
  sqlite3_finalize(pX);
  sqlite3_free(zX);

  exec("SAVEPOINT s");
  gnStep = 0;
  while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ) nRow++;
  if( rc!=SQLITE_DONE ) nRow = -1;
  sqlite3_finalize(pStmt);
  exec("ROLLBACK TO s");
  exec("RELEASE s");
  printf("%016llx %d\n", h, nRow);
}

int main(void){
  static char zLine[MAX_LINE], zSql[MAX_LINE/2 + 1];
  if( sqlite3_open(":memory:", &gDb)!=SQLITE_OK ) die("open");
  exec("CREATE TABLE t1(name TEXT, id INTEGER)");
  exec("INSERT INTO t1 VALUES('alice',1),('bob',2)");
  sqlite3_progress_handler(gDb, 1000, progress, 0);
  while( fgets(zLine, sizeof(zLine), stdin) ){
    int n = 0, hi, lo;
    const char *z = zLine;
    while( (hi = hexVal(z[0]))>=0 && (lo = hexVal(z[1]))>=0 ){
      zSql[n++] = (char)((hi<<4)|lo);
      z += 2;
    }
    if( z[0]!='\n' ) die("malformed line");
    zSql[n] = 0;
    readOne(zSql);
    fflush(stdout);
  }
  sqlite3_close(gDb);
  return 0;
}
