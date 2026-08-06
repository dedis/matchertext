/*
** Drive the matchertext SQLite through an attack corpus, in the additive
** in-band model the paper describes: each untrusted value is placed into the
** SQL text inside a matcher-delimited hole, an M'(...)' literal for a value or
** [...] for an identifier, and the statement is prepared through the ordinary
** sqlite3_prepare_v2().  The naive path, concatenation into a quoted or raw
** value context, stays available as the breakout control. Two further arms,
** mt_v and ident_i, carry the strict-mode typed API instead: the value or
** identifier is passed separately to sqlite3_matchertext_prepare_v3() as a ?V
** or ?I argument.  Built
** with -DSQLITE_MATCHERTEXT_STRICT, prepare_v2() is refused and those two are
** the only admitted paths.
**
** One value goes into one hole by one arm, and the compiled statement is
** compared against the same hole holding a benign value.  All judgement lives
** in the Python stage: this prints measurements.
**
** Commands, one per line on stdin, one result line each.  Values are hex
** encoded so any byte survives the trip.
**
**   T          dump the host and arm tables, a self-check, then "end"
**   B          print one baseline line per legal (host, arm) pair
**   S <hex>    set the current value
**   R          run every legal pair for the current value
**   C <h> <a>  run one pair
**
** Result fields: outcome rc skel ro tail nrow name err
**   outcome  ok | rejected (VERIFY or ?V/?I refused the value) | parse (prepare failed)
**   skel     16 hex digits, FNV-1a of the EXPLAIN listing sans p4, or "-"
**   ro       sqlite3_stmt_readonly, or "-"
**   tail     non-whitespace bytes left after the first statement (stacked query)
**   nrow     rows the non-EXPLAIN run returned, or "-"
**   name     identifier the parser settled on (from "no such column: X"), or "-"
**   err      error text, hex, truncated, or "-"
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"

typedef sqlite3_int64 i64;
typedef unsigned long long u64;

#define HOLE_VALUE 1
#define HOLE_IDENT 2

#define MAX_LINE (1<<20)

/* How an arm fills the hole. */
enum { C_CONCAT, C_QUOTE, C_ESCAPE, C_BIND, C_MT, C_MTSTRICT,
       C_IDENT_CONCAT, C_IDENT_MT, C_MT_V, C_IDENT_I };

typedef struct Host {
  const char *zName, *zPre, *zPost, *zBenign;
  int mHole, bEcho, eConcat;
} Host;

typedef struct Arm {
  const char *zName;
  int mHole, eConv, bBind, bTyped;
} Arm;

static const Host aHost[] = {
  {"value_eq",     "SELECT id FROM t1 WHERE name = ",   "",                     "alice", HOLE_VALUE, 0, 0},
  {"value_like",   "SELECT id FROM t1 WHERE name LIKE ", "",                    "alice", HOLE_VALUE, 0, 0},
  {"value_insert", "INSERT INTO t1(name) VALUES(",      ")",                    "alice", HOLE_VALUE, 0, 0},
  {"value_echo",   "SELECT ",                           "",                     "alice", HOLE_VALUE, 1, 0},
  {"value_dq",     "SELECT id FROM t1 WHERE name = ",   "",                     "alice", HOLE_VALUE, 0, 2},
  {"numeric_eq",   "SELECT id FROM t1 WHERE id = ",     "",                     "1",     HOLE_VALUE, 0, 1},
  {"numeric_paren","SELECT id FROM t1 WHERE id = (",    ")",                    "1",     HOLE_VALUE, 0, 1},
  {"value_in",     "SELECT id FROM t1 WHERE id IN (",   ")",                    "1",     HOLE_VALUE, 0, 1},
  {"expr_order",   "SELECT id FROM t1 ORDER BY ",       "",                     "1",     HOLE_VALUE, 0, 1},
  {"expr_limit",   "SELECT id FROM t1 LIMIT ",          "",                     "999999", HOLE_VALUE, 0, 1},
  {"select_two",   "SELECT id,name FROM t1 WHERE name = ", "",                  "alice", HOLE_VALUE, 0, 0},
  {"select_three", "SELECT id,name,id FROM t1 WHERE name = ", "",               "alice", HOLE_VALUE, 0, 0},
  {"select_four",  "SELECT id,name,id,name FROM t1 WHERE name = ", "",           "alice", HOLE_VALUE, 0, 0},
  {"insert_pair",  "INSERT INTO t1(name,id) VALUES(",   ",3)",                  "alice", HOLE_VALUE, 0, 0},
  {"ident_order",  "SELECT id FROM t1 ORDER BY ",       "",                     "name",  HOLE_IDENT, 0, 0},
  {"ident_col",    "SELECT ",                           " FROM t1 ORDER BY 1",  "name",  HOLE_IDENT, 0, 0},
};

static const Arm aArm[] = {
  {"concat",       HOLE_VALUE, C_CONCAT,       0, 0},  /* '<v>'  naive, control */
  {"quote",        HOLE_VALUE, C_QUOTE,        0, 0},  /* %Q     escaping baseline */
  {"escape",       HOLE_VALUE, C_ESCAPE,       0, 0},  /* '%q'   escaping baseline */
  {"bind",         HOLE_VALUE, C_BIND,         1, 0},  /* ?      prepared statement */
  {"mt",           HOLE_VALUE, C_MT,           0, 0},  /* %m  -> M'(...)' value hole */
  {"mt_strict",    HOLE_VALUE, C_MTSTRICT,      0, 0},  /* VERIFY then M'(...)' */
  {"ident_concat", HOLE_IDENT, C_IDENT_CONCAT, 0, 0},  /* <v>    naive, control */
  {"ident_mt",     HOLE_IDENT, C_IDENT_MT,     0, 0},  /* [<enc>] matchertext name hole */
  /* The strict-mode typed API: the only value and identifier paths a strict
  ** build admits, exercised through sqlite3_matchertext_prepare_v3(). */
  {"mt_v",         HOLE_VALUE, C_MT_V,         0, 1},  /* ?V  verified, bound */
  {"ident_i",      HOLE_IDENT, C_IDENT_I,      0, 1},  /* ?I  verified [ ] name */
};

#define NHOST ((int)(sizeof(aHost)/sizeof(aHost[0])))
#define NARM  ((int)(sizeof(aArm)/sizeof(aArm[0])))

static sqlite3 *gDb;
static i64 gnReset;
static char gName[512];
static char zLine[MAX_LINE];
static char zVal[MAX_LINE/2 + 1];
static i64 gnVal;

static void die(const char *zMsg){
  fprintf(stderr, "sqli_driver: %s: %s\n", zMsg, gDb ? sqlite3_errmsg(gDb) : "");
  exit(1);
}

static u64 fnvByte(u64 h, int c){ h ^= (u64)(unsigned char)c; return h * 1099511628211ULL; }
static u64 fnvStr(u64 h, const char *z){
  if( z==0 ) return fnvByte(h, 0xff);
  while( *z ) h = fnvByte(h, *z++);
  return fnvByte(h, 0);
}
static u64 fnvInt(u64 h, i64 v){
  int i;
  for(i=0; i<8; i++) h = fnvByte(h, (int)((v>>(i*8)) & 0xff));
  return h;
}

/* Collapse the p4 operand to one sentinel when it is exactly the value (or its
** decoded form), so the value is the only thing p4 may differ by; the rest of
** p4 stays under the comparison. */
static u64 fnvRedact(u64 h, const char *z, const char *zA, const char *zB){
  if( z==0 ) return fnvByte(h, 0xff);
  if( (zA && strcmp(z, zA)==0) || (zB && strcmp(z, zB)==0) ){
    return fnvByte(fnvByte(h, 0x01), 0);
  }
  return fnvStr(h, z);
}

/* Build the hole text for one arm.  Returns a sqlite3_malloc string, or 0 when
** the arm refuses an unbalanced value.  bBind arms render a
** single '?'. */
static char *renderHole(int eConv, int eConcat, const char *z){
  switch( eConv ){
    case C_CONCAT:
      if( eConcat==1 ) return sqlite3_mprintf("%s", z);
      if( eConcat==2 ) return sqlite3_mprintf("\"%s\"", z);
      return sqlite3_mprintf("'%s'", z);
    case C_QUOTE:        return sqlite3_mprintf("%Q", z);
    case C_ESCAPE:       return sqlite3_mprintf("'%q'", z);
    case C_BIND:         return sqlite3_mprintf("?");
    case C_MT:           return sqlite3_mprintf("%m", z);
    case C_MTSTRICT:
      if( !sqlite3_matchertext_verify(z, gnVal) ) return 0;
      return sqlite3_mprintf("M'(%s)'", z);
    case C_IDENT_CONCAT: return sqlite3_mprintf("%s", z);
    case C_IDENT_MT: {
      char *zEnc = sqlite3_matchertext_encode(z, gnVal, 0);
      char *zOut;
      if( zEnc==0 ) return 0;
      zOut = sqlite3_mprintf("[%s]", zEnc);
      sqlite3_free(zEnc);
      return zOut;
    }
    case C_MT_V:         return sqlite3_mprintf("?V");
    case C_IDENT_I:      return sqlite3_mprintf("?I");
  }
  return 0;
}

/* Prepare/exec a driver-owned statement that carries no untrusted value.  The
** strict build refuses the legacy entry points, so route these through the
** matchertext API there; the additive build uses the ordinary ones. */
static int prepPlain(const char *zSql, sqlite3_stmt **pp){
#ifdef SQLITE_MATCHERTEXT_STRICT
  return sqlite3_matchertext_prepare_v3(gDb, zSql, -1, 0, 0, 0, pp);
#else
  return sqlite3_prepare_v2(gDb, zSql, -1, pp, 0);
#endif
}
static void execPlain(const char *zSql){
  sqlite3_stmt *p = 0;
  int rc = prepPlain(zSql, &p);
  if( rc==SQLITE_OK && p ){
    while( (rc = sqlite3_step(p))==SQLITE_ROW ){}
    if( rc==SQLITE_DONE ) rc = SQLITE_OK;
  }
  if( p ){
    int rc2 = sqlite3_finalize(p);
    if( rc==SQLITE_OK ) rc = rc2;
  }
  if( rc!=SQLITE_OK ) die("cannot execute internal SQL");
}

typedef struct Result {
  const char *zOutcome;
  int rc, ro, tail, nrow;
  u64 skel;
  int hasSkel;
  char zErr[200];
  char zName[512];
} Result;

static void printHex(const char *z, int nMax){
  int i;
  if( z==0 || z[0]==0 ){ printf("-"); return; }
  for(i=0; i<nMax && z[i]; i++) printf("%02x", (unsigned char)z[i]);
}

static void emit(const Result *p){
  printf("%s %d ", p->zOutcome, p->rc);
  if( p->hasSkel ) printf("%016llx ", p->skel); else printf("- ");
  if( p->ro>=0 ) printf("%d ", p->ro); else printf("- ");
  if( p->tail>=0 ) printf("%d ", p->tail); else printf("- ");
  if( p->nrow>=0 ) printf("%d ", p->nrow); else printf("- ");
  printHex(p->zName, (int)sizeof(p->zName)); printf(" ");
  printHex(p->zErr, 120);
  printf("\n");
}

/* Hash EXPLAIN <sql>, skipping p4 (redacted against the value) and comment. */
static int explainHash(const char *zSql, const char *zHide, u64 *pOut){
  char *zX = sqlite3_mprintf("EXPLAIN %s", zSql);
  char *zDec = sqlite3_matchertext_decode(zHide, gnVal, 0);
  sqlite3_stmt *p = 0;
  u64 h = 1469598103934665603ULL;
  int rc;
  if( zX==0 ){ sqlite3_free(zDec); return SQLITE_NOMEM; }
  rc = sqlite3_prepare_v2(gDb, zX, -1, &p, 0);
  sqlite3_free(zX);
  if( rc!=SQLITE_OK ){ sqlite3_finalize(p); sqlite3_free(zDec); return rc; }
  while( sqlite3_step(p)==SQLITE_ROW ){
    h = fnvInt(h, sqlite3_column_int64(p, 0));
    h = fnvStr(h, (const char*)sqlite3_column_text(p, 1));
    h = fnvInt(h, sqlite3_column_int64(p, 2));
    h = fnvInt(h, sqlite3_column_int64(p, 3));
    h = fnvInt(h, sqlite3_column_int64(p, 4));
    h = fnvRedact(h, (const char*)sqlite3_column_text(p, 5), zHide, zDec);
    h = fnvInt(h, sqlite3_column_int64(p, 6));
  }
  rc = sqlite3_finalize(p);
  sqlite3_free(zDec);
  *pOut = h;
  return rc;
}

/* Skeleton for a typed arm: EXPLAIN the template through the matchertext API so
** a ?V value is bound (never in the SQL) and a ?I identifier is delimited by the
** host, then hash as above.  This is the strict path's own compiler, not a
** legacy prepare, so it works under a strict build too. */
static int typedExplainHash(const char *zTmpl, const sqlite3_matchertext_arg *pArg,
                            const char *zHide, i64 nHide, u64 *pOut){
  char *zX = sqlite3_mprintf("EXPLAIN %s", zTmpl);
  char *zDec;
  sqlite3_stmt *p = 0;
  u64 h = 1469598103934665603ULL;
  int rc;
  if( zX==0 ) return SQLITE_NOMEM;
  zDec = sqlite3_matchertext_decode(zHide, nHide, 0);
  rc = sqlite3_matchertext_prepare_v3(gDb, zX, -1, 0, pArg, 1, &p);
  sqlite3_free(zX);
  if( rc!=SQLITE_OK ){ sqlite3_finalize(p); sqlite3_free(zDec); return rc; }
  while( sqlite3_step(p)==SQLITE_ROW ){
    h = fnvInt(h, sqlite3_column_int64(p, 0));
    h = fnvStr(h, (const char*)sqlite3_column_text(p, 1));
    h = fnvInt(h, sqlite3_column_int64(p, 2));
    h = fnvInt(h, sqlite3_column_int64(p, 3));
    h = fnvInt(h, sqlite3_column_int64(p, 4));
    h = fnvRedact(h, (const char*)sqlite3_column_text(p, 5), zHide, zDec);
    h = fnvInt(h, sqlite3_column_int64(p, 6));
  }
  rc = sqlite3_finalize(p);
  sqlite3_free(zDec);
  *pOut = h;
  return rc;
}

static int tailLen(const char *z){
  int n = 0;
  if( z==0 ) return 0;
  for(; *z; z++) if( *z!=' ' && *z!='\t' && *z!='\n' && *z!='\r' ) n++;
  return n;
}

static void captureName(const char *zMsg){
  static const char zPfx[] = "no such column: ";
  if( strncmp(zMsg, zPfx, sizeof(zPfx)-1)==0 ){
    sqlite3_snprintf(sizeof(gName), gName, "%s", zMsg + sizeof(zPfx)-1);
  }
}

static int fixtureIntact(void){
  sqlite3_stmt *p = 0;
  int ok = 0;
  if( prepPlain("SELECT count(*) FROM t1", &p)==SQLITE_OK
   && sqlite3_step(p)==SQLITE_ROW ){
    ok = sqlite3_column_int(p, 0)==2;
  }
  sqlite3_finalize(p);
  return ok;
}

static void openDb(void);

static void resetDb(void){
  sqlite3_close(gDb);
  gDb = 0;
  openDb();
  gnReset++;
}

static void runCase(int iHost, int iArm, const char *zValue, i64 nValue,
                    Result *pOut){
  const Host *pH = &aHost[iHost];
  const Arm *pA = &aArm[iArm];
  char *zHole, *zSql;
  const char *zTail = 0;
  sqlite3_stmt *pStmt = 0;
  int rc;

  memset(pOut, 0, sizeof(*pOut));
  pOut->ro = pOut->tail = pOut->nrow = -1;
  gName[0] = 0;
  gnVal = nValue;               /* renderHole/explainHash use the true length */

  zHole = renderHole(pA->eConv, pH->eConcat, zValue);
  if( zHole==0 ){
    pOut->zOutcome = "rejected";   /* VERIFY refused the piece */
    return;
  }
  zSql = sqlite3_mprintf("%s%s%s", pH->zPre, zHole, pH->zPost);
  sqlite3_free(zHole);
  if( zSql==0 ) die("oom");

  if( pA->bTyped ){
    /* The strict-mode path: the value or identifier is passed separately to
    ** sqlite3_matchertext_prepare_v3(), which verifies it, then binds a value
    ** as a parameter or delimits an identifier as [ ].  A verify failure is a
    ** refusal, not a parse error. */
    sqlite3_matchertext_arg arg;
    arg.type = pH->mHole==HOLE_IDENT ? SQLITE_MATCHERTEXT_IDENTIFIER
                                     : SQLITE_MATCHERTEXT_VALUE;
    arg.data = zValue;
    arg.size = (sqlite3_uint64)nValue;
    rc = sqlite3_matchertext_prepare_v3(gDb, zSql, -1, 0, &arg, 1, &pStmt);
    pOut->rc = rc;
    if( rc!=SQLITE_OK ){
      const char *zMsg = sqlite3_errmsg(gDb);
      captureName(zMsg);
      pOut->zOutcome = strstr(zMsg, "not valid matchertext") ? "rejected" : "parse";
      sqlite3_snprintf(sizeof(pOut->zErr), pOut->zErr, "%s", zMsg);
      sqlite3_snprintf(sizeof(pOut->zName), pOut->zName, "%s", gName);
      sqlite3_finalize(pStmt);
      sqlite3_free(zSql);
      return;
    }
    pOut->zOutcome = "ok";
    pOut->ro = sqlite3_stmt_readonly(pStmt);
    pOut->tail = 0;               /* the API admits one statement only */
    if( typedExplainHash(zSql, &arg, zValue, nValue, &pOut->skel)==SQLITE_OK )
      pOut->hasSkel = 1;
  }else{
    rc = sqlite3_prepare_v2(gDb, zSql, -1, &pStmt, &zTail);
    pOut->rc = rc;
    if( rc!=SQLITE_OK ){
      const char *zMsg = sqlite3_errmsg(gDb);
      captureName(zMsg);
      pOut->zOutcome = "parse";
      sqlite3_snprintf(sizeof(pOut->zErr), pOut->zErr, "%s", zMsg);
      sqlite3_snprintf(sizeof(pOut->zName), pOut->zName, "%s", gName);
      sqlite3_finalize(pStmt);
      sqlite3_free(zSql);
      return;
    }
    pOut->zOutcome = "ok";
    pOut->ro = sqlite3_stmt_readonly(pStmt);
    pOut->tail = tailLen(zTail);
    if( explainHash(zSql, zValue, &pOut->skel)==SQLITE_OK ) pOut->hasSkel = 1;
  }

  execPlain("SAVEPOINT s");
  if( pA->bBind ) sqlite3_bind_text(pStmt, 1, zValue, (int)nValue, SQLITE_STATIC);
  pOut->nrow = 0;
  while( sqlite3_step(pStmt)==SQLITE_ROW ) pOut->nrow++;
  sqlite3_snprintf(sizeof(pOut->zName), pOut->zName, "%s", gName);
  sqlite3_finalize(pStmt);
  execPlain("ROLLBACK TO s");
  execPlain("RELEASE s");
  if( !fixtureIntact() ) resetDb();
  sqlite3_free(zSql);
}

static int legal(int iHost, int iArm){
  return (aHost[iHost].mHole & aArm[iArm].mHole)!=0;
}

static int hexVal(int c){
  if( c>='0' && c<='9' ) return c-'0';
  if( c>='a' && c<='f' ) return c-'a'+10;
  if( c>='A' && c<='F' ) return c-'A'+10;
  return -1;
}
static i64 decodeHex(const char *z, char *aOut){
  i64 n = 0;
  int hi, lo;
  while( z[0] && z[0]!='\n' && z[0]!=' ' ){
    hi = hexVal((unsigned char)z[0]);
    lo = hexVal((unsigned char)z[1]);
    if( hi<0 || lo<0 ) return -1;
    aOut[n++] = (char)((hi<<4)|lo);
    z += 2;
  }
  aOut[n] = 0;
  return n;
}

static void openDb(void){
  if( sqlite3_open(":memory:", &gDb)!=SQLITE_OK ) die("open");
  execPlain("CREATE TABLE t1(name TEXT, id INTEGER)");
  execPlain("INSERT INTO t1 VALUES('alice',1),('bob',2)");
}

static void dumpTables(void){
  int i;
  sqlite3_stmt *p = 0;
  char *zM;
  int rcConcat, rcMt, rcTyped;
  sqlite3_matchertext_arg arg;
  for(i=0; i<NHOST; i++)
    printf("host %d %s %d %d\n", i, aHost[i].zName, aHost[i].mHole, aHost[i].bEcho);
  for(i=0; i<NARM; i++)
    printf("arm %d %s %d %d\n", i, aArm[i].zName, aArm[i].mHole, aArm[i].eConv);
  /* Self-check: legacy concatenation and a %m hole go through prepare_v2, which
  ** the additive build accepts and the strict build refuses; the strict-mode
  ** typed ?V path must prepare in both. */
  rcConcat = sqlite3_prepare_v2(gDb,
      "SELECT id FROM t1 WHERE name = 'x' OR '1'='1'", -1, &p, 0);
  sqlite3_finalize(p); p = 0;
  zM = sqlite3_mprintf("SELECT id FROM t1 WHERE name = %m", "' OR '1'='1");
  rcMt = zM ? sqlite3_prepare_v2(gDb, zM, -1, &p, 0) : SQLITE_NOMEM;
  sqlite3_finalize(p); p = 0;
  arg.type = SQLITE_MATCHERTEXT_VALUE; arg.data = "alice"; arg.size = 5;
  rcTyped = sqlite3_matchertext_prepare_v3(gDb,
      "SELECT id FROM t1 WHERE name = ?V", -1, 0, &arg, 1, &p);
  sqlite3_finalize(p);
  sqlite3_free(zM);
  printf("selfcheck concat_rc=%d mt_rc=%d typed_rc=%d\n", rcConcat, rcMt, rcTyped);
  printf("resets %lld\n", (long long)gnReset);
  printf("end\n");
}

int main(void){
  i64 n;
  int h, a;
  Result res;
  static char aOut[1<<20];

  sqlite3_config(SQLITE_CONFIG_MEMSTATUS, 0);
  setvbuf(stdout, aOut, _IOFBF, sizeof(aOut));
  openDb();
  zVal[0] = 0;

  while( fgets(zLine, sizeof(zLine), stdin) ){
    switch( zLine[0] ){
      case 'T':
        dumpTables();
        break;
      case 'S':
        n = decodeHex(zLine+2, zVal);
        if( n<0 ){ fprintf(stderr, "sqli_driver: bad hex\n"); return 1; }
        gnVal = n;
        printf("ok\n");
        break;
      case 'B':
        for(h=0; h<NHOST; h++) for(a=0; a<NARM; a++){
          if( !legal(h, a) ) continue;
          runCase(h, a, aHost[h].zBenign, (i64)strlen(aHost[h].zBenign), &res);
          printf("base %d %d ", h, a);
          emit(&res);
        }
        printf("end\n");
        break;
      case 'R':
        for(h=0; h<NHOST; h++) for(a=0; a<NARM; a++){
          if( !legal(h, a) ) continue;
          printf("%d %d ", h, a);
          runCase(h, a, zVal, gnVal, &res);
          emit(&res);
        }
        printf("end\n");
        break;
      case 'C':
        if( sscanf(zLine+2, "%d %d", &h, &a)!=2
         || h<0 || h>=NHOST || a<0 || a>=NARM ){
          fprintf(stderr, "sqli_driver: bad combo\n");
          return 1;
        }
        printf("%d %d ", h, a);
        runCase(h, a, zVal, gnVal, &res);
        emit(&res);
        break;
      default:
        fprintf(stderr, "sqli_driver: malformed command\n");
        return 1;
    }
    fflush(stdout);
  }
  sqlite3_close(gDb);
  return 0;
}
