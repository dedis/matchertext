/*
** Time how one value reaches a prepared statement in the additive matchertext
** SQLite, for values of several lengths:
**
**   bind   prepare "... name=?" with sqlite3_prepare_v2(), then bind the value
**   mt     compose "... name=%m" (the M'(...)' value hole), then prepare it
**   typed  prepare "... name=?V" with sqlite3_matchertext_prepare_v3(), which
**          verifies the value and binds it
**
** The value is matchertext, so every path prepares it.  Each statement is
** finalized after its prepare.  Output: one line per length and path,
** "<length> <path> <nanoseconds per statement>", the median of RUNS runs.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sqlite3.h"

#define RUNS 7
#define BYTES_PER_RUN 4000000      /* value bytes prepared per timed run */

static double now(void){
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec*1e9 + t.tv_nsec;
}

static void check(int rc, sqlite3 *db, const char *what){
  if( rc!=SQLITE_OK ){
    fprintf(stderr, "prepare_bench: %s: %s\n", what, sqlite3_errmsg(db));
    exit(1);
  }
}

/* One statement by path iPath. */
static void one(sqlite3 *db, int iPath, const char *zVal, int nVal){
  sqlite3_stmt *pStmt = 0;
  int rc;
  if( iPath==0 ){
    rc = sqlite3_prepare_v2(db, "SELECT id FROM t WHERE name=?", -1, &pStmt, 0);
    check(rc, db, "bind");
    rc = sqlite3_bind_text(pStmt, 1, zVal, nVal, SQLITE_STATIC);
  }else if( iPath==1 ){
    char *zSql = sqlite3_mprintf("SELECT id FROM t WHERE name=%m", zVal);
    rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
    sqlite3_free(zSql);
  }else{
    sqlite3_matchertext_arg a = {SQLITE_MATCHERTEXT_VALUE, zVal, (sqlite3_uint64)nVal};
    rc = sqlite3_matchertext_prepare_v3(db, "SELECT id FROM t WHERE name=?V", -1, 0,
                                        &a, 1, &pStmt);
  }
  check(rc, db, "prepare");
  sqlite3_finalize(pStmt);
}

static int cmp(const void *a, const void *b){
  double x = *(const double*)a, y = *(const double*)b;
  return (x>y) - (x<y);
}

int main(void){
  static const int aLen[] = {16, 256, 4096, 65536};
  static const char *azPath[] = {"bind", "mt", "typed"};
  sqlite3 *db;
  int i, j, k, r;
  check(sqlite3_open(":memory:", &db), db, "open");
  check(sqlite3_exec(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)", 0, 0, 0),
        db, "schema");
  for(i=0; i<(int)(sizeof(aLen)/sizeof(aLen[0])); i++){
    int nVal = aLen[i];
    int nIter = BYTES_PER_RUN/nVal < 2000 ? 2000 : BYTES_PER_RUN/nVal;
    char *zVal = malloc(nVal+1);
    if( zVal==0 ) return 1;
    /* Matchertext with nested pairs, as a value of the corpus might carry. */
    for(k=0; k<nVal; k++) zVal[k] = "a(b[c]d)e "[k%10];
    if( nVal>=10 && nVal%10 ) memset(zVal + nVal - nVal%10, 'x', nVal%10);
    zVal[nVal] = 0;
    for(j=0; j<3; j++){
      double aNs[RUNS];
      for(k=0; k<nIter/10; k++) one(db, j, zVal, nVal);     /* warm up */
      for(r=0; r<RUNS; r++){
        double t0 = now();
        for(k=0; k<nIter; k++) one(db, j, zVal, nVal);
        aNs[r] = (now()-t0)/nIter;
      }
      qsort(aNs, RUNS, sizeof(double), cmp);
      printf("%d %s %.0f\n", nVal, azPath[j], aNs[RUNS/2]);
      fflush(stdout);
    }
    free(zVal);
  }
  sqlite3_close(db);
  return 0;
}
