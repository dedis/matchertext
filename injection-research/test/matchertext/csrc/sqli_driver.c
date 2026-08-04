/*
** Drive the matchertext SQLite through an attack corpus, in the model the
** fork now enforces: external SQL enters only through
** sqlite3_matchertext_prepare_v3(), the untrusted value never appears in the
** SQL text, and the legacy raw entry points are refused outright.
**
** A template carries one ?V (a bound value) or ?I (a delimited identifier).
** Each payload is delivered as that one argument and the composed statement is
** compared against the same template holding a benign argument.  All judgement
** lives in the Python stage: this prints measurements.
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
** Result fields: outcome rc skel ro nrow name err
**   outcome  ok | rejected (a checked input the API refused) |
**            legacy (a raw entry point refused by the migration gate) |
**            error (composed but failed for another reason)
**   skel     16 hex digits, FNV-1a of the EXPLAIN listing sans p4, or "-"
**   ro       sqlite3_stmt_readonly, or "-"
**   nrow     rows the non-EXPLAIN run returned, or "-"
**   name     the identifier SQLite settled on (from "no such column: X"), or "-"
**   err      the error text, hex, truncated, or "-"
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"

typedef sqlite3_int64 i64;
typedef unsigned long long u64;
typedef sqlite3_matchertext_arg Arg;

#define HOLE_VALUE 1
#define HOLE_IDENT 2

#define MAX_LINE (1<<20)

/* How the payload is delivered. */
enum { D_V, D_V_ENC, D_I, D_LEGACY };

typedef struct Host {
  const char *zName;    /* stable id, appears in the CSV */
  const char *zTmpl;    /* prepare_v3 template, with one ?V or ?I */
  const char *zRawPre;  /* for the legacy arm: SQL before the raw hole */
  const char *zRawPost; /* and after */
  const char *zBenign;  /* the benign argument */
  int mHole;
  int bEcho;            /* column 0 of the result is the argument itself */
} Host;

typedef struct Arm {
  const char *zName;
  int mHole, eDeliver;
} Arm;

/* The legacy arms concatenate the payload into raw SQL and hand it to
** sqlite3_prepare_v2, which the fork now refuses whatever the payload is.  The
** quote here is only so the raw string is well formed; it is never reached. */
static const Host aHost[] = {
  {"value_eq",    "SELECT id FROM t1 WHERE name = ?V", "SELECT id FROM t1 WHERE name = '", "'", "alice", HOLE_VALUE, 0},
  {"value_like",  "SELECT id FROM t1 WHERE name LIKE ?V", "SELECT id FROM t1 WHERE name LIKE '", "'", "alice", HOLE_VALUE, 0},
  {"value_insert","INSERT INTO t1(name) VALUES(?V)", "INSERT INTO t1(name) VALUES('", "')", "alice", HOLE_VALUE, 0},
  {"value_echo",  "SELECT ?V", "SELECT '", "'", "alice", HOLE_VALUE, 1},
  {"ident_order", "SELECT id FROM t1 ORDER BY ?I", "SELECT id FROM t1 ORDER BY ", "", "name", HOLE_IDENT, 0},
  {"ident_col",   "SELECT ?I FROM t1 ORDER BY 1", "SELECT ", " FROM t1 ORDER BY 1", "name", HOLE_IDENT, 0},
};

static const Arm aArm[] = {
  {"mtv",      HOLE_VALUE, D_V},       /* value, raw, through ?V */
  {"mtv_enc",  HOLE_VALUE, D_V_ENC},   /* value, encoded first, through ?V */
  {"mti",      HOLE_IDENT, D_I},       /* identifier, raw, through ?I */
  {"legacy_v", HOLE_VALUE, D_LEGACY},  /* value, concatenated, raw prepare */
  {"legacy_i", HOLE_IDENT, D_LEGACY},  /* identifier, concatenated, raw prepare */
};

#define NHOST ((int)(sizeof(aHost)/sizeof(aHost[0])))
#define NARM  ((int)(sizeof(aArm)/sizeof(aArm[0])))

static sqlite3 *gDb;
static i64 gnReset;
static char gName[512];       /* identifier SQLite settled on */
static char zLine[MAX_LINE];
static char zVal[MAX_LINE/2 + 1];
static i64 gnVal;             /* true byte length of zVal, which may span a NUL */

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

/* Compose EXPLAIN <tmpl> with the one argument and hash the bytecode, skipping
** p4 (the only operand a value can occupy) and the comment. */
static int explainSkel(const char *zTmpl, const Arg *pArg, u64 *pOut){
  char *zX = sqlite3_mprintf("EXPLAIN %s", zTmpl);
  sqlite3_stmt *p = 0;
  u64 h = 1469598103934665603ULL;
  int rc;
  if( zX==0 ) return SQLITE_NOMEM;
  rc = sqlite3_matchertext_prepare_v3(gDb, zX, -1, 0, pArg, 1, &p);
  sqlite3_free(zX);
  if( rc!=SQLITE_OK ){ sqlite3_finalize(p); return rc; }
  while( sqlite3_step(p)==SQLITE_ROW ){
    h = fnvInt(h, sqlite3_column_int64(p, 0));
    h = fnvStr(h, (const char*)sqlite3_column_text(p, 1));
    h = fnvInt(h, sqlite3_column_int64(p, 2));
    h = fnvInt(h, sqlite3_column_int64(p, 3));
    h = fnvInt(h, sqlite3_column_int64(p, 4));
    h = fnvInt(h, sqlite3_column_int64(p, 6));
  }
  rc = sqlite3_finalize(p);
  *pOut = h;
  return rc;
}

typedef struct Result {
  const char *zOutcome;
  int rc, ro, nrow;
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
  if( p->nrow>=0 ) printf("%d ", p->nrow); else printf("- ");
  printHex(p->zName, (int)sizeof(p->zName)); printf(" ");
  printHex(p->zErr, 120);
  printf("\n");
}

/* "no such column: X" -> copy X into gName, so the settled identifier can be
** compared against the argument even when the statement did not prepare. */
static void captureName(const char *zMsg){
  static const char zPfx[] = "no such column: ";
  if( strncmp(zMsg, zPfx, sizeof(zPfx)-1)==0 ){
    sqlite3_snprintf(sizeof(gName), gName, "%s", zMsg + sizeof(zPfx)-1);
  }
}

/* Classify a prepare error. The fork's own messages are intent, not accident:
** a refused checked input and a refused legacy call are both by design. */
static const char *classify(const char *zMsg){
  if( strstr(zMsg, "matchertext requires") ) return "legacy";
  if( strstr(zMsg, "is not valid matchertext")
   || strstr(zMsg, "must be non-NULL")
   || strstr(zMsg, "must contain one SQL statement")
   || strstr(zMsg, "template is not valid matchertext")
   || strstr(zMsg, "composed SQL is not valid matchertext") ) return "rejected";
  return "error";
}

static int fixtureIntact(void){
  sqlite3_stmt *p = 0;
  int ok = 0;
  Arg a; a.type = SQLITE_MATCHERTEXT_IDENTIFIER; a.data = "t1"; a.size = 2;
  if( sqlite3_matchertext_prepare_v3(gDb,
        "SELECT count(*) FROM ?I", -1, 0, &a, 1, &p)==SQLITE_OK
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
  Arg arg;
  char *zEnc = 0;
  sqlite3_stmt *pStmt = 0;
  int rc;

  memset(pOut, 0, sizeof(*pOut));
  pOut->ro = pOut->nrow = -1;
  gName[0] = 0;

  /* Legacy arm: concatenate into raw SQL and hand it to the old entry point,
  ** which the fork refuses whatever the payload is. */
  if( pA->eDeliver==D_LEGACY ){
    char *zSql = sqlite3_mprintf("%s%s%s", pH->zRawPre, zValue, pH->zRawPost);
    if( zSql==0 ) die("oom");
    rc = sqlite3_prepare_v2(gDb, zSql, -1, &pStmt, 0);
    pOut->rc = rc;
    if( rc==SQLITE_OK ){
      pOut->zOutcome = "ok";  /* a breakout: the raw path let it through */
      pOut->ro = sqlite3_stmt_readonly(pStmt);
    }else{
      const char *zMsg = sqlite3_errmsg(gDb);
      pOut->zOutcome = classify(zMsg);
      sqlite3_snprintf(sizeof(pOut->zErr), pOut->zErr, "%s", zMsg);
    }
    sqlite3_finalize(pStmt);
    sqlite3_free(zSql);
    return;
  }

  /* Matchertext arms: the argument is checked, then composed. */
  arg.type = (pA->mHole==HOLE_IDENT) ? SQLITE_MATCHERTEXT_IDENTIFIER
                                     : SQLITE_MATCHERTEXT_VALUE;
  if( pA->eDeliver==D_V_ENC ){
    i64 nEnc = 0;
    zEnc = sqlite3_matchertext_encode(zValue, nValue, &nEnc);
    if( zEnc==0 ) die("encode oom");
    arg.data = zEnc; arg.size = (sqlite3_uint64)nEnc;
  }else{
    arg.data = zValue; arg.size = (sqlite3_uint64)nValue;
  }

  if( explainSkel(pH->zTmpl, &arg, &pOut->skel)==SQLITE_OK ) pOut->hasSkel = 1;

  rc = sqlite3_matchertext_prepare_v3(gDb, pH->zTmpl, -1, 0, &arg, 1, &pStmt);
  pOut->rc = rc;
  if( rc!=SQLITE_OK ){
    const char *zMsg = sqlite3_errmsg(gDb);
    captureName(zMsg);
    pOut->zOutcome = classify(zMsg);
    sqlite3_snprintf(sizeof(pOut->zErr), pOut->zErr, "%s", zMsg);
    sqlite3_snprintf(sizeof(pOut->zName), pOut->zName, "%s", gName);
    sqlite3_finalize(pStmt);
    sqlite3_free(zEnc);
    return;
  }
  pOut->zOutcome = "ok";
  pOut->ro = sqlite3_stmt_readonly(pStmt);

  sqlite3_matchertext_exec(gDb, "SAVEPOINT s", -1, 0, 0, 0, 0, 0);
  pOut->nrow = 0;
  while( sqlite3_step(pStmt)==SQLITE_ROW ) pOut->nrow++;
  sqlite3_finalize(pStmt);
  sqlite3_matchertext_exec(gDb, "ROLLBACK TO s; RELEASE s", -1, 0, 0, 0, 0, 0);
  if( !fixtureIntact() ) resetDb();
  sqlite3_free(zEnc);
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
  static const char *azSetup[] = {
    "CREATE TABLE ?I(name TEXT, id INTEGER)",
    "INSERT INTO t1(name,id) VALUES(?V,?V)",  /* alice,1 */
    "INSERT INTO t1(name,id) VALUES(?V,?V)",  /* bob,2 */
  };
  Arg t1[1] = {{SQLITE_MATCHERTEXT_IDENTIFIER, "t1", 2}};
  Arg a1[2] = {{SQLITE_MATCHERTEXT_VALUE, "alice", 5}, {SQLITE_MATCHERTEXT_VALUE, "1", 1}};
  Arg b1[2] = {{SQLITE_MATCHERTEXT_VALUE, "bob", 3}, {SQLITE_MATCHERTEXT_VALUE, "2", 1}};
  const Arg *aa[] = {t1, a1, b1};
  int an[] = {1, 2, 2};
  int i;
  if( sqlite3_open(":memory:", &gDb)!=SQLITE_OK ) die("open");
  for(i=0; i<(int)(sizeof(azSetup)/sizeof(azSetup[0])); i++){
    char *zErr = 0;
    if( sqlite3_matchertext_exec(gDb, azSetup[i], -1, aa[i], an[i], 0, 0, &zErr)
        != SQLITE_OK ){
      fprintf(stderr, "sqli_driver: setup %d: %s\n", i, zErr);
      exit(1);
    }
  }
}

static void dumpTables(void){
  int i;
  Arg vbad[1] = {{SQLITE_MATCHERTEXT_VALUE, ")' OR 1=1 --", 12}};
  sqlite3_stmt *p = 0;
  int rcLegacy, rcCheck;
  for(i=0; i<NHOST; i++)
    printf("host %d %s %d %d\n", i, aHost[i].zName, aHost[i].mHole, aHost[i].bEcho);
  for(i=0; i<NARM; i++)
    printf("arm %d %s %d %d\n", i, aArm[i].zName, aArm[i].mHole, aArm[i].eDeliver);
  /* Self-check reported to Python: the legacy path must be refused, and a
  ** known unbalanced value must be refused by the checked path. */
  rcLegacy = sqlite3_prepare_v2(gDb, "SELECT 1", -1, &p, 0);
  sqlite3_finalize(p); p = 0;
  rcCheck = sqlite3_matchertext_prepare_v3(gDb,
      "SELECT id FROM t1 WHERE name = ?V", -1, 0, vbad, 1, &p);
  sqlite3_finalize(p);
  printf("selfcheck legacy_rc=%d check_rc=%d\n", rcLegacy, rcCheck);
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
