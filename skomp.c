/* skomp.c -- the cover for the Scheme-to-native compiler.
 *
 * skomp is one executable with MARV's command line and a Scheme program where
 * MARV takes an assembly module.  It does no compiling of its own.  It finds
 * its private root, arranges the environment its helpers need, and runs two of
 * them in turn:
 *
 *   <home>/bin/skint   hosting (skint compile), Scheme program -> MARV module
 *   <home>/bin/marv    MARV module -> target object, or an executable
 *
 * and, for the wcpl target, MARV in turn runs <home>/bin/wcpl.  None of the
 * three is on the user's PATH; skomp puts its own bin directory at the front of
 * PATH for its children only, so a public marv or wcpl installed on the same
 * box is neither used nor disturbed.
 *
 * The style here follows marv's own driver: C99, fixed buffers with bounds
 * checks, system() to run a tool, and platform tests NESTED inside
 * `#ifndef __WCPL__` / `#ifdef _WIN32` rather than combined, which is all WCPL
 * reads.  skomp itself is not a WCPL target -- it needs system() and it reads a
 * directory -- but it stays close to that subset, because the other three parts
 * are held to it.
 *
 * Reading a directory is the one thing skomp does that MARV cannot: MARV, in
 * the WCPL C subset, has no way to tell whether a directory is empty, so it can
 * only refuse to overwrite a makefile it recognises.  skomp checks that the
 * -M directory is empty before MARV writes anything into it.  [cco]
 */

/* this is for MS headers; shouldn't affect others (as skint's s.h does it) */
#define _CRT_SECURE_NO_WARNINGS 1

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "skomp_src.h"

#define SKOMP_VERSION "0.1.0"

#define MAXPATH 1024
#define MAXCMD  8192
#define MAXDIRS 32

/* ---------------------------------------------------------------- */
/* host                                                             */

/* The test has to NEST rather than combine the two conditions, the way
 * marv/a.c does it: WCPL reads only `#ifdef __WCPL__`, `#ifndef __WCPL__`,
 * `#if 0` and `#if 1`, and skips an inactive branch whole, so a _WIN32 test
 * must sit inside one of those. */
#ifndef __WCPL__
#ifdef _WIN32
static int host_windows(void) { return 1; }
#else
static int host_windows(void) { return 0; }
#endif
#else
static int host_windows(void) { return 0; }
#endif

#ifndef __WCPL__
#ifdef _WIN32
#include <process.h>
#include <windows.h>
static long skomp_pid(void) { return (long)_getpid(); }
static int put_env(const char *assignment)
{
  return _putenv(assignment);
}
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/wait.h>
static long skomp_pid(void) { return (long)getpid(); }
static int put_env(const char *assignment)
{
  /* putenv keeps the string, so every caller passes storage that lives for
   * the rest of the run. */
  return putenv((char *)assignment);
}
#endif
#endif

static const char *path_sep(void) { return host_windows() ? "\\" : "/"; }
static const char *list_sep(void) { return host_windows() ? ";" : ":"; }

/* What system() hands back is not an exit code on every host: on POSIX it is a
 * wait status, so a child that exited 1 arrives as 256, and `return 256' from
 * main leaves the shell reading 0 -- a failed compilation would have looked
 * like a successful one.  This turns a status into the code skomp exits with,
 * keeping the child's own code where it is meaningful and 1 where it is not
 * (a signal, or a value a shell could not represent). */
#ifndef __WCPL__
#ifdef _WIN32
static int exit_code(int rc)
{
  if (rc == 0) return 0;
  return (rc > 0 && rc < 126) ? rc : 1;
}
#else
static int exit_code(int rc)
{
  if (rc == -1) return 1;
  if (WIFEXITED(rc)) {
    int e;
    e = WEXITSTATUS(rc);
    if (e == 0) return 0;
    return e < 126 ? e : 1;
  }
  return 1;
}
#endif
#else
static int exit_code(int rc) { return rc == 0 ? 0 : 1; }
#endif

/* ---------------------------------------------------------------- */
/* strings and paths                                                */

/* A bounded builder: every append is checked, and one overflow poisons the
 * buffer so the caller sees a single failure at the end rather than a quietly
 * truncated command line. */
typedef struct buf_s {
  char  s[MAXCMD];
  size_t len;
  int    ok;
} buf_t;

static void buf_init(buf_t *b) { b->s[0] = 0; b->len = 0; b->ok = 1; }

static void buf_add(buf_t *b, const char *t)
{
  size_t n;
  if (!b->ok) return;
  n = strlen(t);
  if (b->len + n + 1 > MAXCMD) { b->ok = 0; return; }
  memcpy(b->s + b->len, t, n + 1);
  b->len += n;
}

static int is_sep(int c) { return c == '/' || c == '\\'; }

static void path_dir(char *dst, size_t cap, const char *path)
{
  size_t n, i;
  n = strlen(path);
  i = n;
  while (i > 0 && !is_sep((unsigned char)path[i - 1])) i--;
  if (i == 0) { dst[0] = 0; return; }           /* no directory part */
  while (i > 1 && is_sep((unsigned char)path[i - 1])) i--;
  if (i + 1 > cap) { dst[0] = 0; return; }
  memcpy(dst, path, i);
  dst[i] = 0;
}

static const char *path_base(const char *path)
{
  size_t n, i;
  n = strlen(path);
  i = n;
  while (i > 0 && !is_sep((unsigned char)path[i - 1])) i--;
  return path + i;
}

static int path_join(char *dst, size_t cap, const char *a, const char *b)
{
  size_t na, nb, ns;
  na = strlen(a); nb = strlen(b);
  if (na == 0) {
    if (nb + 1 > cap) return 0;
    memcpy(dst, b, nb + 1);
    return 1;
  }
  ns = is_sep((unsigned char)a[na - 1]) ? 0 : 1;
  if (na + ns + nb + 1 > cap) return 0;
  memcpy(dst, a, na);
  if (ns) memcpy(dst + na, path_sep(), 1);
  memcpy(dst + na + ns, b, nb + 1);
  return 1;
}

static int has_suffix(const char *s, const char *sfx)
{
  size_t n, m;
  n = strlen(s); m = strlen(sfx);
  if (m > n) return 0;
  return strcmp(s + (n - m), sfx) == 0;
}

static int file_readable(const char *path)
{
  FILE *fp;
  fp = fopen(path, "rb");
  if (fp == NULL) return 0;
  fclose(fp);
  return 1;
}

/* ---------------------------------------------------------------- */
/* quoting                                                          */

/* A path is handed to the command interpreter, so it is restricted to a
 * character set that means the same thing to a Unix shell and to Windows cmd.
 * This is marv's rule (a.c) with the backslash added, since skomp's own paths
 * are native ones on Windows.  Anything else is reported rather than passed
 * through, because the alternative is for the shell to interpret it. */
static int shell_safe(const char *path)
{
  const unsigned char *p;
  if (path[0] == 0) return 0;
  for (p = (const unsigned char *)path; *p != 0; ++p) {
    unsigned char c;
    c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '_' || c == '-' || c == '.' || c == '/' || c == '\\' ||
        c == ':' || c == ' ')
      continue;
    return 0;
  }
  return 1;
}

static int buf_add_path(buf_t *b, const char *path)
{
  int quote;
  if (!shell_safe(path)) {
    fprintf(stderr, "skomp: unsupported character in path: %s\n", path);
    return 0;
  }
  quote = strchr(path, ' ') != NULL;
  if (quote) buf_add(b, "\"");
  buf_add(b, path);
  if (quote) buf_add(b, "\"");
  return 1;
}

/* ---------------------------------------------------------------- */
/* the private root                                                 */

/* Three sources, in order: the environment, the path compiled in by the
 * makefile, and -- so that an unpacked tree works where it stands -- the
 * directory holding skomp itself, with "skomp-home" under it.  No discovery
 * API is used; argv[0] is consulted only when it carries a directory. */
static char g_home[MAXPATH];

static int home_has(const char *home, const char *rel)
{
  char p[MAXPATH];
  if (!path_join(p, sizeof(p), home, rel)) return 0;
  return file_readable(p);
}

static int home_ok(const char *home)
{
  return home_has(home, host_windows() ? "bin\\skint.exe" : "bin/skint") &&
         home_has(home, "lib/skint/compile.sld");
}

static int resolve_home(const char *argv0, int complain)
{
  const char *e;
  char dir[MAXPATH];
  char cand[MAXPATH];
  e = getenv("SKOMP_HOME");
  if (e != NULL && e[0] != 0) {
    if (strlen(e) + 1 > sizeof(g_home)) {
      fprintf(stderr, "skomp: SKOMP_HOME is too long\n");
      return 0;
    }
    strcpy(g_home, e);
    return 1;
  }
#ifdef SKOMP_HOME_DIR
  if (strlen(SKOMP_HOME_DIR) + 1 <= sizeof(g_home) && home_ok(SKOMP_HOME_DIR)) {
    strcpy(g_home, SKOMP_HOME_DIR);
    return 1;
  }
#endif
  path_dir(dir, sizeof(dir), argv0);
  if (dir[0] != 0 && path_join(cand, sizeof(cand), dir, "skomp-home") &&
      home_ok(cand)) {
    strcpy(g_home, cand);
    return 1;
  }
#ifdef SKOMP_HOME_DIR
  if (strlen(SKOMP_HOME_DIR) + 1 <= sizeof(g_home)) {
    strcpy(g_home, SKOMP_HOME_DIR);
    return 1;
  }
#endif
  if (complain) {
    fprintf(stderr, "skomp: cannot find my own files\n");
    fprintf(stderr, "  set SKOMP_HOME to the directory holding bin/, lib/ "
                    "and target/\n");
  }
  return 0;
}

/* ---------------------------------------------------------------- */
/* directory emptiness -- the check marv cannot make                */

/* -1 does not exist, 0 exists and holds something, 1 exists and is empty. */
#ifndef __WCPL__
#ifdef _WIN32
static int dir_state(const char *dir)
{
  char pat[MAXPATH];
  WIN32_FIND_DATAA fd;
  HANDLE h;
  DWORD attr;
  int empty;
  attr = GetFileAttributesA(dir);
  if (attr == INVALID_FILE_ATTRIBUTES) return -1;
  if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) return 0;
  if (!path_join(pat, sizeof(pat), dir, "*")) return 0;
  h = FindFirstFileA(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return 1;
  empty = 1;
  do {
    if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
      continue;
    empty = 0;
    break;
  } while (FindNextFileA(h, &fd) != 0);
  FindClose(h);
  return empty;
}
#else
static int dir_state(const char *dir)
{
  DIR *d;
  struct dirent *e;
  int empty;
  d = opendir(dir);
  if (d == NULL) return -1;
  empty = 1;
  while ((e = readdir(d)) != NULL) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    empty = 0;
    break;
  }
  closedir(d);
  return empty;
}
#endif
#endif

/* ---------------------------------------------------------------- */
/* the working directory can shadow marv's own headers              */

/* marv appends -L and -I directories AFTER its defaults, which are "lib/",
 * "libs/" and "./" relative to the working directory (r.c, dirs_init).  A
 * scmrt.mvh, scmi.mvh or scmn.mvh sitting in one of those would be preferred
 * to skomp's own, and the program would be built against the wrong runtime
 * headers with nothing said.  The target description cannot be shadowed, since
 * skomp hands marv an absolute .mvt path, which marv takes directly.
 *
 * This is cheap to rule out and skomp is the part that can: five fopen calls,
 * no directory reading. */
static int cwd_shadows(void)
{
  static const char *names[] = {
    "lib/scmrt.mvh", "lib/scmi.mvh", "lib/scmn.mvh",
    "libs/scmrt.mvh", "libs/scmi.mvh", "libs/scmn.mvh",
    "scmn.mvh", NULL
  };
  int i;
  for (i = 0; names[i] != NULL; i++) {
    if (file_readable(names[i])) {
      fprintf(stderr, "skomp: %s in the working directory would be used "
                      "instead of my own\n", names[i]);
      fprintf(stderr, "  marv searches lib/, libs/ and ./ before the "
                      "directories skomp gives it;\n");
      fprintf(stderr, "  move it, or run skomp from elsewhere\n");
      return 1;
    }
  }
  return 0;
}

/* ---------------------------------------------------------------- */
/* the target description                                           */

/* The object extension a target writes in -c mode, read out of the .mvt so
 * that skomp can name the object exactly as marv would.  The file is a small
 * S-expression; the field wanted is (obj-ext ".c").  An absent or unreadable
 * field is not fatal: the caller then asks for -o instead of guessing. */
static int mvt_obj_ext(const char *mvtpath, char *dst, size_t cap)
{
  FILE *fp;
  char line[512];
  int found;
  fp = fopen(mvtpath, "rb");
  if (fp == NULL) return 0;
  found = 0;
  while (!found && fgets(line, (int)sizeof(line), fp) != NULL) {
    char *p;
    p = strstr(line, "obj-ext");
    if (p == NULL) continue;
    p = strchr(p, '"');
    if (p == NULL) continue;
    p++;
    {
      char *q;
      size_t n;
      q = strchr(p, '"');
      if (q == NULL) continue;
      n = (size_t)(q - p);
      if (n + 1 > cap) break;
      memcpy(dst, p, n);
      dst[n] = 0;
      found = 1;
    }
  }
  fclose(fp);
  return found;
}

/* ---------------------------------------------------------------- */
/* usage                                                            */

/* ---------------------------------------------------------------- */
/* the option parser                                                */
/*                                                                  */
/* egetopt, copied from marv (m.c), so that skomp answers to the    */
/* same shapes its sister programs do: clustered short flags, an    */
/* option argument attached or separate, long options spelled       */
/* --name=VALUE, and -- ending the options.  A long option is an    */
/* option-with-argument named -, which the trailing -: in the spec  */
/* declares, so --target=cc arrives as eoptarg "target=cc".         */
/*                                                                  */
/* skomp links none of marv, so this is a copy rather than a share; */
/* that it stays a copy of THAT parser is the whole point of it.    */
/* The two departures from the original: int where marv has bool,   */
/* because this file uses no <stdbool.h>, and the stored strings    */
/* are kept rather than duplicated, because both are literals here. */
/* [cco] */

static const char *g_progname = "skomp";
static const char *g_usage;

static const char *progname(void) { return g_progname; }
static const char *usage(void) { return g_usage; }
static void setusage(const char *str) { g_usage = str; }

/* eusage: report wrong usage, then exit */
static void eusage(const char *fmt, ...)
{
  va_list args;
  fflush(stdout);
  fprintf(stderr, "%s: ", progname());
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  if (usage() != NULL)
    fprintf(stderr, "usage: %s %s\n", progname(), usage());
  exit(1);
}

/* -h and --help answer on standard output and succeed; eusage sends the
 * same text to standard error and exits 1.  One stored string, so they
 * cannot drift apart. */
static void skhelp(void)
{
  fprintf(stdout, "usage: %s %s\n", progname(), usage());
}

static int eoptind = 1, eopterr = 1, eoptopt = 0, eoptich = 0;
static char *eoptarg = NULL;
static int g_eargc = 0;
static char **g_eargv = NULL;

/* the last option long_opt matched, as it would be written */
static const char *elongopt = NULL;
static char g_elongbuf[64];

static int egetopt(int argc, char *argv[], const char *opts)
{
  int c; char *popt;

  /* long_opt needs these to reach a separated option argument */
  g_eargc = argc; g_eargv = argv;

  /* check if it is time to stop */
  if (eoptich == 0) {
    if (eoptind >= argc)
      return EOF;
    if (argv[eoptind][0] != '-' ||
        argv[eoptind][1] == '\0')
      return EOF;
    if (strcmp(argv[eoptind], "--") == 0) {
      ++eoptind;
      return EOF;
    }
  }

  /* get next option char */
  c = argv[eoptind][++eoptich];

  /* check if it is legal */
  if (c == ':' || (popt = strchr(opts, c)) == NULL) {
    if (eopterr) {
      eusage("illegal option: -%c", c);
    }
    if (argv[eoptind][++eoptich] == '\0') {
      ++eoptind;
      eoptich = 0;
    }
    eoptopt = c;
    return '?';
  }

  /* check if it should be accompanied by arg */
  if (popt[1] == ':') {
    if (argv[eoptind][++eoptich] != '\0') {
      eoptarg  = argv[eoptind++] + eoptich;
    } else if (++eoptind >= argc) {
      if (eopterr) {
        eusage("option -%c requires an argument", c);
      }
      eoptich = 0;
      eoptopt = c;
      return '?';
    } else {
      eoptarg = argv[eoptind++];
    }
    eoptich = 0;
  } else {
    if (argv[eoptind][eoptich + 1] == '\0') {
      ++eoptind;
      eoptich = 0;
    }
    eoptarg  = NULL;
  }

  /* eoptopt, eoptind and eoptarg are updated */
  return c;
}

/* Long options are parsed outside egetopt, which knows only that --
 * takes an argument: for --target=cc it hands over "target=cc", and for
 * --target cc it hands over "target" with eoptind left on "cc".
 * long_opt hides that difference, so a caller writes one line per
 * option and accepts both spellings.
 *
 * LOPT is the option name, with a trailing = where it takes a value.  A
 * match returns true, sets elongopt to the option as written, and for a
 * value-taking option leaves the value in eoptarg.  A value-taking
 * option with nothing to take is reported here, so no caller needs a
 * list of which options take values.
 *
 * A name that is a prefix of another is not a hazard: --target-dir=x
 * fails long_opt("target=") because the character after the name is
 * neither = nor the end, so the order of the tests does not matter.
 * Copied from marv (m.c), like egetopt above. [cco] */
static void set_elongopt(const char *lopt, size_t n)
{
  if (n + 3 > sizeof(g_elongbuf)) n = sizeof(g_elongbuf) - 3;
  g_elongbuf[0] = g_elongbuf[1] = (char)45;   /* a hyphen, twice */
  memcpy(g_elongbuf + 2, lopt, n);
  g_elongbuf[n + 2] = 0;
  elongopt = g_elongbuf;
}

static int long_opt(const char *lopt)
{
  size_t n;
  int wantsarg;
  n = strlen(lopt);
  wantsarg = (n > 0 && lopt[n-1] == '=');
  if (wantsarg) --n;
  if (strncmp(eoptarg, lopt, n) != 0) return 0;
  if (!wantsarg) {
    if (eoptarg[n] != 0) return 0;
    set_elongopt(lopt, n);
    return 1;
  }
  if (eoptarg[n] == '=') {
    set_elongopt(lopt, n);
    eoptarg = eoptarg + n + 1;
    return 1;
  }
  if (eoptarg[n] != 0) return 0;      /* a longer name, not this one */
  set_elongopt(lopt, n);
  if (eoptind >= g_eargc)
    eusage("option %s requires an argument", elongopt);
  eoptarg = g_eargv[eoptind++];
  return 1;
}

static int streql(const char *s1, const char *s2)
{
  if (s1 == NULL && s2 == NULL) return 1;
  if (s1 == NULL || s2 == NULL) return 0;
  return strcmp(s1, s2) == 0;
}

static char *strprf(const char *str, const char *prefix)
{
  while (*str && *str == *prefix) ++str, ++prefix;
  if (*prefix) return NULL;
  return (char *)str;
}


static void version(void)
{
  printf("skomp %s\n", SKOMP_VERSION);
  printf("  skint %s\n", SKOMP_SRC_SKINT);
  printf("  marv  %s\n", SKOMP_SRC_MARV);
  printf("  wcpl  %s\n", SKOMP_SRC_WCPL);
  printf("  home  %s\n", g_home[0] != 0 ? g_home : "(not found)");
}

/* ---------------------------------------------------------------- */
/* running a tool                                                   */

static int g_verbose;

static int run(const buf_t *cmd, const char *what)
{
  if (!cmd->ok) {
    fprintf(stderr, "skomp: %s command line is too long\n", what);
    return -1;
  }
  if (g_verbose) fprintf(stderr, "%s\n", cmd->s);
#ifndef __WCPL__
  return system(cmd->s);
#else
  fprintf(stdout, "%s\n", cmd->s);
  return 0;
#endif
}

/* ---------------------------------------------------------------- */
/* main                                                             */

/* An optimization level, as -O, --optimize and --scheme-optimize all want
 * one.  WHAT is the option as written, so the complaint names the spelling
 * the caller used rather than one of its synonyms. */
static int opt_level(const char *what, const char *val)
{
  if (val[0] < '0' || val[0] > '3' || val[1] != 0)
    eusage("%s takes 0, 1, 2 or 3, not %s", what, val);
  return val[0] - '0';
}

/* --stack-pages=N */
static long page_count(const char *val)
{
  char *end;
  long n;
  n = strtol(val, &end, 10);
  if (*val == 0 || *end != 0 || n < 1)
    eusage("%s takes a page count of 1 or more, not %s", elongopt, val);
  return n;
}

/* one more directory in a fixed-size list */
static void add_dir(const char *what, const char **v, int *n, const char *dir)
{
  if (*n >= MAXDIRS) eusage("too many %s options", what);
  v[(*n)++] = dir;
}

int main(int argc, char **argv)
{
  const char *targdes = NULL;
  const char *output = NULL;
  const char *input = NULL;
  const char *mdir = NULL;
  const char *splitfuncs = NULL;
  const char *prelibs[MAXDIRS];
  const char *postlibs[MAXDIRS];
  const char *targdirs[MAXDIRS];
  int nprelibs = 0, npostlibs = 0, ntargdirs = 0;
  int conly = 0, marvonly = 0, dryrun = 0;
  int optlevel = -1, schemeopt = -1;
  long stackpages = -1;   /* -1 = not given; the compiler's own default stands */
  int i, opt, rc;
  char mvt[MAXPATH], srt[MAXPATH], skint[MAXPATH], marv[MAXPATH];
  char lib[MAXPATH], ssc[MAXPATH], mvs[MAXPATH];
  static char libdirs[MAXCMD];
  static char pathvar[MAXCMD];
  static char wcplinc[MAXPATH + 64];
  static char wcpllib[MAXPATH + 64];
  buf_t cmd;

  /* ---- options.  egetopt is the parser marv and wcpl use, so all three
     answer to the same shapes: clustered short flags, an option argument
     attached or separate, long options spelled --name=VALUE, and -- ending
     the options.  Options come before the input file, as they must for the
     sister programs too. */
  setusage
    ("[-t TARGDES] [-c] [OPTION]... FILE\n"
     "  FILE is one Scheme program: the compiler is a "
     "whole-program compiler\n"
     "  -t, --target=TARGDES   target description "
     "(default: cl on Windows, cc elsewhere)\n"
     "  -T, --target-dir=DIR   add DIR to the .mvt lookup path\n"
     "  -c, --compile          compile to a target object file, "
     "do not link\n"
     "  -S, --marv-source      stop after writing the MARV "
     "module (.mvs)\n"
     "  -o, --output=FILE      output file; required unless -c or "
     "-S is given\n"
     "  -O, --optimize=LEVEL   backend optimization level "
     "(0, 1, 2, or 3)\n"
     "  -I, --prepend-libdir=DIR  prepend a Scheme library "
     "search directory\n"
     "  -A, --append-libdir=DIR   append a Scheme library "
     "search directory\n"
     "  -L DIR                 synonym for -A\n"
     "      --scheme-optimize=N  compiler optimization level "
     "(0..3, default 1)\n"
     "      --stack-pages=N    root stack size in pages of "
     "64K 8-byte words (default 128)\n"
     "  -K, --split-funcs=N    passed to marv\n"
     "  -v, --verbose          print each step before running it\n"
     "  -n, --dry-run          print the backend commands instead "
     "of running them\n"
     "  -M, --makefile-dir=DIR package the build and makefile "
     "in DIR\n"
     "  -h, --help             print this\n"
     "  -V, --version          print version information");
  while ((opt = egetopt(argc, argv, "cSnvhVt:T:o:O:I:A:L:M:K:-:")) != EOF) {
    switch (opt) {
      case 'c': conly = 1; break;
      case 'S': marvonly = 1; break;
      case 'n': dryrun = 1; break;
      case 'v': g_verbose = 1; break;
      case 'h': skhelp(); return 0;
      case 'V': resolve_home(argv[0], 0); version(); return 0;
      case 't': targdes = eoptarg; break;
      case 'o': output = eoptarg; break;
      case 'M': mdir = eoptarg; break;
      case 'K': splitfuncs = eoptarg; break;
      case 'O': optlevel = opt_level("-O", eoptarg); break;
      case 'T': add_dir("-T", targdirs, &ntargdirs, eoptarg); break;
      case 'I': add_dir("-I", prelibs, &nprelibs, eoptarg); break;
      case 'A':
      case 'L': add_dir("-A", postlibs, &npostlibs, eoptarg); break;
      case '-': {
        /* long_opt takes the name, with a trailing = where a value follows,
         * and accepts both --name=VALUE and --name VALUE; the value arrives
         * in eoptarg and the option as written in elongopt. */
        if (long_opt("compile")) conly = 1;
        else if (long_opt("marv-source")) marvonly = 1;
        else if (long_opt("verbose")) g_verbose = 1;
        else if (long_opt("dry-run")) dryrun = 1;
        else if (long_opt("help")) { skhelp(); return 0; }
        else if (long_opt("version"))
          { resolve_home(argv[0], 0); version(); return 0; }
        else if (long_opt("target-dir="))
          add_dir(elongopt, targdirs, &ntargdirs, eoptarg);
        else if (long_opt("target=")) targdes = eoptarg;
        else if (long_opt("output=")) output = eoptarg;
        else if (long_opt("optimize=")) optlevel = opt_level(elongopt, eoptarg);
        else if (long_opt("prepend-libdir="))
          add_dir(elongopt, prelibs, &nprelibs, eoptarg);
        else if (long_opt("append-libdir="))
          add_dir(elongopt, postlibs, &npostlibs, eoptarg);
        else if (long_opt("scheme-optimize="))
          schemeopt = opt_level(elongopt, eoptarg);
        else if (long_opt("stack-pages=")) stackpages = page_count(eoptarg);
        else if (long_opt("split-funcs=")) splitfuncs = eoptarg;
        else if (long_opt("makefile-dir=")) mdir = eoptarg;
        else eusage("illegal option: --%s", eoptarg);
      } break;
    }
  }
  for (; eoptind < argc; ++eoptind) {
    if (input != NULL)
      eusage("more than one input file: %s -- this is a whole-program "
             "compiler, so it takes one program", argv[eoptind]);
    input = argv[eoptind];
  }

  if (input == NULL) eusage("no input file specified");
  if (conly && marvonly) eusage("-c and -S ask for different outputs");
  if (mdir != NULL && (conly || marvonly))
    eusage("-M cannot be combined with -c or -S");
  if (mdir != NULL && output == NULL)
    eusage("-M needs -o to name the executable");
  if (!conly && !marvonly && output == NULL)
    eusage("-o is required unless -c or -S is given");
  if (!file_readable(input)) {
    fprintf(stderr, "skomp: cannot read %s\n", input);
    return 1;
  }
  if (targdes == NULL) targdes = host_windows() ? "cl" : "cc";

  if (!resolve_home(argv[0], 1)) return 1;

  /* ---- the private parts, by absolute path */
  if (!path_join(skint, sizeof(skint), g_home,
                 host_windows() ? "bin\\skint.exe" : "bin/skint") ||
      !path_join(marv, sizeof(marv), g_home,
                 host_windows() ? "bin\\marv.exe" : "bin/marv") ||
      !path_join(lib, sizeof(lib), g_home, "lib") ||
      !path_join(ssc, sizeof(ssc), g_home, "lib/skomp.ssc") ||
      !path_join(srt, sizeof(srt), g_home, "lib/skint/compile/srt")) {
    fprintf(stderr, "skomp: my own path is too long: %s\n", g_home);
    return 1;
  }
  if (!file_readable(skint)) {
    fprintf(stderr, "skomp: %s is missing\n", skint);
    fprintf(stderr, "  set SKOMP_HOME to the directory holding bin/, lib/ "
                    "and target/\n");
    return 1;
  }
  if (!file_readable(ssc)) {
    fprintf(stderr, "skomp: %s is missing\n", ssc);
    return 1;
  }

  /* ---- the target description.  A path ending in .mvt is used as given,
     which is what marv does; a bare name is skomp's own. */
  if (has_suffix(targdes, ".mvt") && file_readable(targdes)) {
    if (strlen(targdes) + 1 > sizeof(mvt)) {
      fprintf(stderr, "skomp: target path is too long\n");
      return 1;
    }
    strcpy(mvt, targdes);
  } else {
    char rel[MAXPATH];
    if (strlen(targdes) + 12 > sizeof(rel)) {
      fprintf(stderr, "skomp: target name is too long\n");
      return 1;
    }
    strcpy(rel, "target/");
    strcat(rel, targdes);
    strcat(rel, ".mvt");
    if (!path_join(mvt, sizeof(mvt), g_home, rel)) {
      fprintf(stderr, "skomp: my own path is too long: %s\n", g_home);
      return 1;
    }
    if (!file_readable(mvt)) {
      if (ntargdirs > 0) {
        /* a -T directory may hold it; let marv resolve the bare name */
        if (strlen(targdes) + 1 > sizeof(mvt)) {
          fprintf(stderr, "skomp: target name is too long\n");
          return 1;
        }
        strcpy(mvt, targdes);
      } else {
        fprintf(stderr, "skomp: no target description for %s\n", targdes);
        fprintf(stderr, "  looked for %s\n", mvt);
        fprintf(stderr, "  -T DIR adds a directory of your own .mvt files\n");
        return 1;
      }
    }
  }

  /* ---- the two checks marv cannot make for itself */
  if (mdir != NULL) {
#ifndef __WCPL__
    if (dir_state(mdir) == 0) {
      fprintf(stderr, "skomp: %s already exists and is not an empty "
                      "directory\n", mdir);
      fprintf(stderr, "  -M writes a whole build directory; give it an "
                      "empty or a new one\n");
      return 1;
    }
#endif
  }
  if (cwd_shadows()) return 1;

  /* ---- where the MARV module goes.  In -S mode it is the product; otherwise
     it is a temporary beside the system's other temporaries. */
  if (marvonly) {
    if (output != NULL) {
      if (strlen(output) + 1 > sizeof(mvs)) {
        fprintf(stderr, "skomp: output path is too long\n");
        return 1;
      }
      strcpy(mvs, output);
    } else {
      char dir[MAXPATH], base[MAXPATH];
      const char *b;
      size_t n;
      path_dir(dir, sizeof(dir), input);
      b = path_base(input);
      n = strlen(b);
      if (n > 4 && strcmp(b + (n - 4), ".scm") == 0) n -= 4;
      if (n + 5 > sizeof(base)) {
        fprintf(stderr, "skomp: input name is too long\n");
        return 1;
      }
      memcpy(base, b, n);
      base[n] = 0;
      strcat(base, ".mvs");
      if (!path_join(mvs, sizeof(mvs), dir, base)) {
        fprintf(stderr, "skomp: output path is too long\n");
        return 1;
      }
    }
  } else {
    const char *tmp;
    char name[MAXPATH];
    const char *b;
    size_t n;
    tmp = getenv("TMPDIR");
    if (tmp == NULL || tmp[0] == 0) tmp = getenv("TMP");
    if (tmp == NULL || tmp[0] == 0) tmp = getenv("TEMP");
    if (tmp == NULL || tmp[0] == 0) tmp = host_windows() ? "." : "/tmp";
    b = path_base(input);
    n = strlen(b);
    if (n > 4 && strcmp(b + (n - 4), ".scm") == 0) n -= 4;
    if (n > 64) n = 64;
    sprintf(name, "skomp-%ld-", skomp_pid());
    {
      size_t k = strlen(name);
      if (k + n + 5 > sizeof(name)) {
        fprintf(stderr, "skomp: temporary name is too long\n");
        return 1;
      }
      memcpy(name + k, b, n);
      name[k + n] = 0;
      strcat(name, ".mvs");
    }
    if (!path_join(mvs, sizeof(mvs), tmp, name)) {
      fprintf(stderr, "skomp: temporary path is too long: %s\n", tmp);
      return 1;
    }
  }

  /* ---- the environment the children need.  SKINT_LIBDIRS is set rather than
     extended, so a value inherited from the user's shell cannot add libraries
     the compiler would then serve; PATH gains skomp's own bin directory at the
     front, which is how marv's stock wcpl.mvt (link-cmd "wcpl -o ~exe ~src")
     reaches skomp's wcpl rather than a public one.  Both strings are static:
     putenv keeps what it is given. */
  {
    size_t k = 0;
    int j;
    libdirs[0] = 0;
    strcpy(libdirs, "SKINT_LIBDIRS=");
    k = strlen(libdirs);
    for (j = 0; j < nprelibs; j++) {
      size_t n = strlen(prelibs[j]);
      if (k + n + 2 > sizeof(libdirs)) {
        fprintf(stderr, "skomp: too many library directories\n");
        return 1;
      }
      memcpy(libdirs + k, prelibs[j], n); k += n;
      libdirs[k++] = list_sep()[0];
    }
    {
      size_t n = strlen(lib);
      if (k + n + 2 > sizeof(libdirs)) {
        fprintf(stderr, "skomp: too many library directories\n");
        return 1;
      }
      memcpy(libdirs + k, lib, n); k += n;
    }
    for (j = 0; j < npostlibs; j++) {
      size_t n = strlen(postlibs[j]);
      if (k + n + 2 > sizeof(libdirs)) {
        fprintf(stderr, "skomp: too many library directories\n");
        return 1;
      }
      libdirs[k++] = list_sep()[0];
      memcpy(libdirs + k, postlibs[j], n); k += n;
    }
    libdirs[k] = 0;
    if (put_env(libdirs) != 0) {
      fprintf(stderr, "skomp: cannot set SKINT_LIBDIRS\n");
      return 1;
    }
  }
  /* WCPL resolves scmi.c's `#include "scmrt.h"' to a module, and looks for
     that module where WCPL_LIBRARY_PATH says; the header itself comes from
     WCPL_INCLUDE_PATH.  marv's stock wcpl.mvt runs plain `wcpl -o ~exe ~src',
     with no -L or -I of its own, so without these two the wasm target cannot
     build the runtime at all.  Both want a trailing separator (wcpl appends a
     file name to the directory as given). */
  {
    size_t k;
    strcpy(wcplinc, "WCPL_INCLUDE_PATH=");
    k = strlen(wcplinc);
    if (k + strlen(srt) + 2 > sizeof(wcplinc)) {
      fprintf(stderr, "skomp: my own path is too long: %s\n", g_home);
      return 1;
    }
    strcpy(wcplinc + k, srt);
    k += strlen(srt);
    wcplinc[k++] = path_sep()[0];
    wcplinc[k] = 0;
    strcpy(wcpllib, "WCPL_LIBRARY_PATH=");
    strcat(wcpllib, wcplinc + strlen("WCPL_INCLUDE_PATH="));
    if (put_env(wcplinc) != 0 || put_env(wcpllib) != 0) {
      fprintf(stderr, "skomp: cannot set the WCPL lookup paths\n");
      return 1;
    }
  }
  {
    const char *old;
    char bin[MAXPATH];
    size_t k;
    if (!path_join(bin, sizeof(bin), g_home, "bin")) {
      fprintf(stderr, "skomp: my own path is too long: %s\n", g_home);
      return 1;
    }
    old = getenv("PATH");
    if (old == NULL) old = "";
    strcpy(pathvar, "PATH=");
    k = strlen(pathvar);
    if (k + strlen(bin) + 1 + strlen(old) + 1 > sizeof(pathvar)) {
      fprintf(stderr, "skomp: PATH is too long to extend\n");
      return 1;
    }
    strcpy(pathvar + k, bin);
    k += strlen(bin);
    pathvar[k++] = list_sep()[0];
    strcpy(pathvar + k, old);
    if (put_env(pathvar) != 0) {
      fprintf(stderr, "skomp: cannot set PATH\n");
      return 1;
    }
  }

  /* ---- step one: the Scheme program becomes a MARV module */
  buf_init(&cmd);
  if (!buf_add_path(&cmd, skint)) return 1;
  buf_add(&cmd, " -q ");
  if (!buf_add_path(&cmd, ssc)) return 1;
  buf_add(&cmd, " ");
  if (!buf_add_path(&cmd, input)) return 1;
  buf_add(&cmd, " ");
  if (!buf_add_path(&cmd, mvs)) return 1;
  /* The driver's two optional slots are positional, so a stack-pages
   * value needs the optimize slot filled; "-" there means no value, and
   * leaves the compiler's own default in place. */
  if (schemeopt != -1) {
    char n[8];
    sprintf(n, " %d", schemeopt);
    buf_add(&cmd, n);
  } else if (stackpages != -1) {
    buf_add(&cmd, " -");
  }
  if (stackpages != -1) {
    char n[32];
    sprintf(n, " %ld", stackpages);
    buf_add(&cmd, n);
  }
  rc = run(&cmd, "skint");
  if (rc != 0) return exit_code(rc);
  if (!file_readable(mvs)) {
    fprintf(stderr, "skomp: the compiler wrote no module\n");
    return 1;
  }
  if (marvonly) {
    if (g_verbose) fprintf(stderr, "skomp: wrote %s\n", mvs);
    return 0;
  }

  /* ---- step two: marv assembles it, and links unless -c */
  buf_init(&cmd);
  if (!buf_add_path(&cmd, marv)) return 1;
  buf_add(&cmd, " -t ");
  if (!buf_add_path(&cmd, mvt)) return 1;
  for (i = 0; i < ntargdirs; i++) {
    buf_add(&cmd, " -T ");
    if (!buf_add_path(&cmd, targdirs[i])) return 1;
  }
  buf_add(&cmd, " -L ");
  if (!buf_add_path(&cmd, srt)) return 1;
  buf_add(&cmd, " -I ");
  if (!buf_add_path(&cmd, srt)) return 1;
  if (conly) buf_add(&cmd, " -c");
  if (g_verbose) buf_add(&cmd, " -v");
  if (dryrun) buf_add(&cmd, " -n");
  if (optlevel != -1) {
    char n[8];
    sprintf(n, " -O %d", optlevel);
    buf_add(&cmd, n);
  }
  if (splitfuncs != NULL) {
    buf_add(&cmd, " --split-funcs=");
    buf_add(&cmd, splitfuncs);
  }
  if (mdir != NULL) {
    buf_add(&cmd, " -M ");
    if (!buf_add_path(&cmd, mdir)) return 1;
  }
  if (output != NULL) {
    buf_add(&cmd, " -o ");
    if (!buf_add_path(&cmd, output)) return 1;
  } else if (conly) {
    /* marv would name the object after the module, which lives in the
       temporary directory; name it after the input instead, with the
       extension the target description gives. */
    char ext[32], dir[MAXPATH], obj[MAXPATH], base[MAXPATH];
    const char *b;
    size_t n;
    if (!mvt_obj_ext(mvt, ext, sizeof(ext))) {
      fprintf(stderr, "skomp: cannot tell the object extension of %s; "
                      "give -o\n", mvt);
      remove(mvs);
      return 1;
    }
    path_dir(dir, sizeof(dir), input);
    b = path_base(input);
    n = strlen(b);
    if (n > 4 && strcmp(b + (n - 4), ".scm") == 0) n -= 4;
    if (n + strlen(ext) + 1 > sizeof(base)) {
      fprintf(stderr, "skomp: input name is too long\n");
      remove(mvs);
      return 1;
    }
    memcpy(base, b, n);
    base[n] = 0;
    strcat(base, ext);
    if (!path_join(obj, sizeof(obj), dir, base)) {
      fprintf(stderr, "skomp: output path is too long\n");
      remove(mvs);
      return 1;
    }
    buf_add(&cmd, " -o ");
    if (!buf_add_path(&cmd, obj)) { remove(mvs); return 1; }
  }
  buf_add(&cmd, " ");
  if (!buf_add_path(&cmd, mvs)) { remove(mvs); return 1; }
  rc = run(&cmd, "marv");
  remove(mvs);
  return exit_code(rc);
}
