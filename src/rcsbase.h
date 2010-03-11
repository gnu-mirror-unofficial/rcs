/* RCS common definitions and data structures

   Copyright (C) 2010 Thien-Thi Nguyen
   Copyright (C) 1990, 1991, 1992, 1993, 1994, 1995 Paul Eggert
   Copyright (C) 1982, 1988, 1989 Walter Tichy

   This file is part of GNU RCS.

   GNU RCS is free software: you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.

   GNU RCS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
   See the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "auto-sussed.h"

/* begin cruft formerly from from conf.h */

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#ifdef HAVE_FCNTL_H
#	include <fcntl.h>
#endif
#ifdef HAVE_LIMITS_H
#	  include <limits.h>
#endif
#ifdef HAVE_MACH_MACH_H
#	  include <mach/mach.h>
#endif
#ifdef HAVE_NET_ERRNO_H
#	  include <net/errno.h>
#endif
#ifdef HAVE_PWD_H
#	  include <pwd.h>
#endif
#ifdef HAVE_SIGINFO_H
#	  include <siginfo.h>
#endif
#ifdef HAVE_SIGNAL_H
#	  include <signal.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#	  include <sys/mman.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#	  include <sys/wait.h>
#endif
#ifdef HAVE_UCONTEXT_H
#	  include <ucontext.h>
#endif
#ifdef HAVE_UNISTD_H
#	  include <unistd.h>
#endif
#ifdef HAVE_UTIME_H
#	  include <utime.h>
#endif
#ifdef HAVE_VFORK_H
#	  include <vfork.h>
#endif

/* GCC attributes  */

#ifdef GCC_HAS_ATTRIBUTE_UNUSED
#	define RCS_UNUSED __attribute__ ((unused))
#else
#	define RCS_UNUSED
#endif

#ifdef GCC_HAS_ATTRIBUTE_NORETURN
#	define exiting __attribute__ ((noreturn))
#else
#	define exiting
#endif

#ifdef GCC_HAS_ATTRIBUTE_FORMAT
#	define printf_string(m, n) __attribute__ ((format(printf, m, n)))
#else
#	define printf_string(m, n)
#endif

#if defined GCC_HAS_ATTRIBUTE_FORMAT && defined GCC_HAS_ATTRIBUTE_NORETURN
	/* Work around a bug in GCC 2.5.x.  */
#	define printf_string_exiting(m, n) __attribute__ ((format(printf, m, n), noreturn))
#else
#	define printf_string_exiting(m, n) printf_string (m, n) exiting
#endif

#ifdef O_BINARY
	/* Text and binary i/o behave differently.  */
	/* This is incompatible with Posix and Unix.  */
#	define FOPEN_RB "rb"
#	define FOPEN_R_WORK (Expand==BINARY_EXPAND ? "r" : "rb")
#	define FOPEN_WB "wb"
#	define FOPEN_W_WORK (Expand==BINARY_EXPAND ? "w" : "wb")
#	define FOPEN_WPLUS_WORK (Expand==BINARY_EXPAND ? "w+" : "w+b")
#	define OPEN_O_BINARY O_BINARY
#else
	/*
	* Text and binary i/o behave the same.
	* Omit "b", since some nonstandard hosts reject it.
	*/
#	define FOPEN_RB "r"
#	define FOPEN_R_WORK "r"
#	define FOPEN_WB "w"
#	define FOPEN_W_WORK "w"
#	define FOPEN_WPLUS_WORK "w+"
#	define OPEN_O_BINARY 0
#endif

/* This may need changing on non-Unix systems (notably DOS).  */
#define OPEN_CREAT_READONLY (S_IRUSR|S_IRGRP|S_IROTH) /* lock file mode */
#define OPEN_O_LOCK 0 /* extra open flags for creating lock file */
#define OPEN_O_WRONLY O_WRONLY /* main open flag for creating a lock file */

/* Is getlogin() secure?  Usually it's not.  */
#define getlogin_is_secure 0

/* Can rename(A,B) falsely report success?  */
#define bad_NFS_rename 0

/* Does setreuid() work?  See top-level README.  */
#define has_setreuid 0

/* Must we define getabsname?  */
#define needs_getabsname 0

/* Might NFS be used?  */
#define has_NFS 1

/* Shell to run RCS subprograms.  */
#define RCS_SHELL "/bin/sh"

/* Can main memory hold entire RCS files?  */
#if MMAP_SIGNAL
#	define large_memory 1
#else
#	define large_memory 0
#endif

/* Do struct stat s and t describe the same file?  Answer d if unknown.  */
#define same_file(s,t,d) ((s).st_ino==(t).st_ino && (s).st_dev==(t).st_dev)

/* Filename component separation.
   TMPDIR -- string -- default directory for temporary files.
   SLASH -- char -- principal filename separator.
   SLASHes -- `case SLASHes:' --  labels all filename separators.
   ROOTPATH(p) -- expression -- Is p an absolute pathname?
   X_DEFAULT -- string -- default value for -x option
*/
#if !WOE
#	define TMPDIR "/tmp"
#	define SLASH '/'
#	define SLASHes '/'
#	define ROOTPATH(p)  (isSLASH ((p)[0]))
#	define X_DEFAULT ",v"
#else /* WOE */
#	define TMPDIR "\\tmp"
#	define SLASH "'\\'"
#	define SLASHes '\\': case '/': case ':'
#	define ROOTPATH(p)  (isSLASH ((p)[0]) || (p)[0] && (p)[1] == ':')
#	define X_DEFAULT "\\,v"
#endif

#ifndef P_tmpdir
#define P_tmpdir  TMPDIR
#endif

/* Must TZ be set for gmtime() to work?  */
#define TZ_must_be_set 0

/* <fcntl.h> */
#ifdef O_CREAT
#	define open_can_creat 1
#else
#	define open_can_creat 0
#	define O_RDONLY 0
#	define O_WRONLY 1
#	define O_RDWR 2
#	define O_CREAT 01000
#	define O_TRUNC 02000
#endif
#ifndef O_EXCL
#define O_EXCL 0
#endif

/* <sys/stat.h> */
#ifndef S_IRUSR
#	ifdef S_IREAD
#		define S_IRUSR S_IREAD
#	else
#		define S_IRUSR 0400
#	endif
#	ifdef S_IWRITE
#		define S_IWUSR S_IWRITE
#	else
#		define S_IWUSR (S_IRUSR/2)
#	endif
#endif
#ifndef S_IRGRP
#	if defined HAVE_GETUID
#		define S_IRGRP (S_IRUSR / 0010)
#		define S_IWGRP (S_IWUSR / 0010)
#		define S_IROTH (S_IRUSR / 0100)
#		define S_IWOTH (S_IWUSR / 0100)
#	else
		/* single user OS -- not Posix or Unix */
#		define S_IRGRP 0
#		define S_IWGRP 0
#		define S_IROTH 0
#		define S_IWOTH 0
#	endif
#endif
#ifndef S_ISREG
#define S_ISREG(n) (((n) & S_IFMT) == S_IFREG)
#endif

/* <sys/wait.h> */
#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#undef WIFEXITED /* Avoid 4.3BSD incompatibility with Posix.  */
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val)  &  0377) == 0)
#endif
#ifndef WTERMSIG
#define WTERMSIG(stat_val) ((stat_val) & 0177)
#undef WIFSIGNALED /* Avoid 4.3BSD incompatibility with Posix.  */
#endif
#ifndef WIFSIGNALED
#define WIFSIGNALED(stat_val) ((unsigned)(stat_val) - 1  <  0377)
#endif

/* <unistd.h> */
char *getlogin (void);
#ifndef STDIN_FILENO
#	define STDIN_FILENO 0
#	define STDOUT_FILENO 1
#	define STDERR_FILENO 2
#endif
#if defined HAVE_WORKING_FORK && !defined HAVE_WORKING_VFORK
#	undef vfork
#	define vfork fork
#endif
#if defined HAVE_SETUID && !defined HAVE_SETEUID
#	undef seteuid
#	define seteuid setuid
#endif

/* end cruft formerly from from conf.h */

#ifdef _POSIX_PATH_MAX
#	define SIZEABLE_PATH _POSIX_PATH_MAX
#else
#	define SIZEABLE_PATH 255        /* size of a large path; not a hard limit */
#endif

/*
 * Parameters
 */

/* backwards compatibility with old versions of RCS */
#define VERSION_min 3           /* old output RCS format supported */
#define VERSION_max 5           /* newest output RCS format supported */
#ifndef VERSION_DEFAULT         /* default RCS output format */
#	define VERSION_DEFAULT VERSION_max
#endif
#define VERSION(n) ((n) - VERSION_DEFAULT)      /* internally, 0 is the default */

#ifndef STRICT_LOCKING
#define STRICT_LOCKING 1
#endif
                              /* 0 sets the default locking to non-strict;  */
                              /* used in experimental environments.         */
                              /* 1 sets the default locking to strict;      */
                              /* used in production environments.           */

#define yearlength	   16   /* (good through AD 9,999,999,999,999,999)    */
#define datesize (yearlength+16)        /* size of output of time2date */
#define RCSTMPPREFIX '_'        /* prefix for temp files in working dir  */
#define KDELIM            '$'   /* delimiter for keywords                     */
#define VDELIM            ':'   /* separates keywords from values             */
#define DEFAULTSTATE    "Exp"   /* default state of revisions                 */

/*
 * RILE - readonly file
 * declarecache; - declares local cache for RILE variable(s)
 * setupcache - sets up the local RILE cache, but does not initialize it
 * cache, uncache - caches and uncaches the local RILE;
 *	(uncache,cache) is needed around functions that advance the RILE pointer
 * Igeteof(f,c,s) - get a char c from f, executing statement s at EOF
 * cachegeteof(c,s) - Igeteof applied to the local RILE
 * Iget(f,c) - like Igeteof, except EOF is an error
 * cacheget(c) - Iget applied to the local RILE
 * cacheunget(f,c) - read c backwards from cached f
 * Ifileno, Ioffset_type, Irewind, Itell - analogs to stdio routines
 *
 * By conventions, macros whose names end in _ are statements, not expressions.
 * Following such macros with `; else' results in a syntax error.
 */

/* If there is no signal, better to disable mmap entirely.  */
#if !MMAP_SIGNAL
#undef HAVE_MMAP
#undef HAVE_MADVISE
#undef MMAP_SIGNAL
#endif

#define maps_memory (0 || defined HAVE_MMAP)

#if large_memory
typedef unsigned char const *Iptr_type;
typedef struct RILE
{
  Iptr_type ptr, lim;
  unsigned char *base;          /* not Iptr_type for lint's sake */
  unsigned char *readlim;
  int fd;
#		if maps_memory
  void (*deallocate) (struct RILE *);
#		else
  FILE *stream;
#		endif
} RILE;
#	if maps_memory
#		define declarecache register Iptr_type ptr, lim
#		define setupcache(f) (lim = (f)->lim)
#		define Igeteof(f,c,s)  do       \
    if ((f)->ptr == (f)->lim)                   \
      { s; }                                    \
    else                                        \
      (c) = *(f)->ptr++;                        \
  while (0)
#		define cachegeteof(c,s)  do     \
    if (ptr==lim)                               \
      { s; }                                    \
    else                                        \
      (c) = *ptr++;                             \
  while (0)
#	else
int Igetmore (RILE *);
#		define declarecache register Iptr_type ptr; register RILE *rRILE
#		define setupcache(f) (rRILE = (f))
#		define Igeteof(f,c,s)  do       \
    if ((f)->ptr == (f)->readlim                \
        && !Igetmore (f))                       \
      { s; }                                    \
    else                                        \
      (c) = *(f)->ptr++;                        \
  while (0)
#		define cachegeteof(c,s)  do     \
    if (ptr == rRILE->readlim                   \
        && !Igetmore (rRILE))                   \
      { s; }                                    \
    else                                        \
      (c) = *ptr++;                             \
  while (0)
#	endif
#	define uncache(f) ((f)->ptr = ptr)
#	define cache(f) (ptr = (f)->ptr)
#	define Iget(f,c)  Igeteof (f, c, Ieof ())
#	define cacheget(c)  cachegeteof (c, Ieof ())
#	define cacheunget(f,c)  ((c) = (--ptr)[-1])
#	define Ioffset_type size_t
#	define Itell(f) ((f)->ptr - (f)->base)
#	define Irewind(f) ((f)->ptr = (f)->base)
#	define cacheptr() ptr
#	define Ifileno(f) ((f)->fd)
#else
#	define RILE FILE
#	define declarecache register FILE *ptr
#	define setupcache(f) (ptr = (f))
#	define uncache(f)
#	define cache(f)
#	define Igeteof(f,c,s)  do               \
    if (((c) = getc (f)) == EOF)                \
      {                                         \
        testIerror (f);                         \
        if (feof (f))                           \
          { s; }                                \
      }                                         \
  while (0)
#	define cachegeteof(c,s)  Igeteof (ptr, c, s)
#	define Iget(f,c)  do                    \
    if (((c) = getc (f)) == EOF)                \
      testIeof (f);                             \
  while (0)
#	define cacheget(c)  Iget (ptr, c)
#	define cacheunget(f,c)  do              \
    if (fseek (ptr, -2L, SEEK_CUR))             \
      Ierror ();                                \
    else                                        \
      cacheget (c);                             \
  while (0)
#	define Ioffset_type long
#	define Itell(f) ftell(f)
#	define Ifileno(f) fileno(f)
#endif

/* Print a char, but abort on write error.  */
#define aputc(c,o)  do                          \
    if (putc (c, o) == EOF)                     \
      testOerror (o);                           \
  while (0)

/* Get a character from an RCS file, perhaps copying to a new RCS file.  */
#define GETC(o,c) do { cacheget (c); if (o) aputc (c, o); } while (0)

#define WORKMODE(RCSmode, writable) (((RCSmode)&(mode_t)~(S_IWUSR|S_IWGRP|S_IWOTH)) | ((writable)?S_IWUSR:0))
/* computes mode of working file: same as RCSmode, but write permission     */
/* determined by writable */

/* character classes and token codes */
enum tokens
{
  /* classes */
  DELIM, DIGIT, IDCHAR, NEWLN, LETTER, Letter,
  PERIOD, SBEGIN, SPACE, UNKN,
  /* tokens */
  COLON, ID, NUM, SEMI, STRING
};

#define SDELIM  '@'             /* the actual character is needed for string handling */
/* SDELIM must be consistent with ctab[], so that ctab[SDELIM]==SBEGIN.
 * there should be no overlap among SDELIM, KDELIM, and VDELIM
 */

/***************************************
 * Data structures for the symbol table
 ***************************************/

/* Buffer of arbitrary data */
struct buf
{
  char *string;
  size_t size;
};
struct cbuf
{
  char const *string;
  size_t size;
};

/* Hash table entry */
struct hshentry
{
  char const *num;              /* pointer to revision number (ASCIZ) */
  char const *date;             /* pointer to date of checkin         */
  char const *author;           /* login of person checking in        */
  char const *lockedby;         /* who locks the revision             */
  char const *state;            /* state of revision (Exp by default) */
  char const *name;             /* name (if any) by which retrieved   */
  struct cbuf log;              /* log message requested at checkin   */
  struct branchhead *branches;  /* list of first revisions on branches */
  struct cbuf ig;               /* ignored phrases in admin part      */
  struct cbuf igtext;           /* ignored phrases in deltatext part  */
  struct hshentry *next;        /* next revision on same branch       */
  struct hshentry *nexthsh;     /* next revision with same hash value */
  long insertlns;               /* lines inserted (computed by rlog)  */
  long deletelns;               /* lines deleted  (computed by rlog)  */
  char selector;                /* true if selected, false if deleted */
};

/* list of hash entries */
struct hshentries
{
  struct hshentries *rest;
  struct hshentry *first;
};

/* list element for branch lists */
struct branchhead
{
  struct hshentry *hsh;
  struct branchhead *nextbranch;
};

/* accesslist element */
struct access
{
  char const *login;
  struct access *nextaccess;
};

/* list element for locks  */
struct rcslock
{
  char const *login;
  struct hshentry *delta;
  struct rcslock *nextlock;
};

/* list element for symbolic names */
struct assoc
{
  char const *symbol;
  char const *num;
  struct assoc *nextassoc;
};

/*
 * Markers for keyword expansion (used in co and ident)
 *	Every byte must have class LETTER or Letter.
 */
#define AUTHOR          "Author"
#define DATE            "Date"
#define HEADER          "Header"
#define IDH             "Id"
#define LOCKER          "Locker"
#define LOG             "Log"
#define NAME		"Name"
#define RCSFILE         "RCSfile"
#define REVISION        "Revision"
#define SOURCE          "Source"
#define STATE           "State"
#define keylength 8             /* max length of any of the above keywords */

enum markers
{ Nomatch, Author, Date, Header, Id,
  Locker, Log, Name, RCSfile, Revision, Source, State
};
        /* This must be in the same order as rcskeys.c's Keyword[] array. */

#define DELNUMFORM      "\n\n%s\n%s\n"
/* used by putdtext and scanlogtext */

#define EMPTYLOG "*** empty log message ***"    /* used by ci and rlog */

/* main program */
extern char const cmdid[];
void exiterr (void) exiting;

/* merge */
int merge (int, char const *, char const *const[3], char const *const[3]);

/* rcsedit */
#define ciklogsize 23           /* sizeof("checked in with -k by ") */
extern FILE *fcopy;
extern char const *resultname;
extern char const ciklog[ciklogsize];
extern int locker_expansion;
RILE *rcswriteopen (struct buf *, struct stat *, int);
char const *makedirtemp (int);
char const *getcaller (void);
int addlock (struct hshentry *, int);
int addsymbol (char const *, char const *, int);
int checkaccesslist (void);
int chnamemod (FILE **, char const *, char const *, int, mode_t, time_t);
int donerewrite (int, time_t);
int dorewrite (int, int);
int expandline (RILE *, FILE *, struct hshentry const *, int, FILE *, int);
int findlock (int, struct hshentry **);
int setmtime (char const *, time_t);
void ORCSclose (void);
void ORCSerror (void);
void copystring (void);
void dirtempunlink (void);
void enterstring (void);
void finishedit (struct hshentry const *, FILE *, int);
void keepdirtemp (char const *);
void openfcopy (FILE *);
void snapshotedit (FILE *);
void xpandstring (struct hshentry const *);
#if has_NFS || BAD_UNLINK
int un_link (char const *);
#else
#	define un_link(s) unlink(s)
#endif
#if large_memory
void edit_string (void);
#	define editstring(delta) edit_string()
#else
void editstring (struct hshentry const *);
#endif

/* rcsfcmp */
int rcsfcmp (RILE *, struct stat const *, char const *,
             struct hshentry const *);

/* rcsfnms */
#define bufautobegin(b) clear_buf(b)
#define clear_buf(b) (((b)->string = 0, (b)->size = 0))
extern FILE *workstdout;
extern char *workname;
extern char const *RCSname;
extern char const *suffixes;
extern int fdlock;
extern struct stat RCSstat;
RILE *rcsreadopen (struct buf *, struct stat *, int);
char *bufenlarge (struct buf *, char const **);
char const *basefilename (char const *);
char const *getfullRCSname (void);
void set_temporary_file_name (struct buf *filename, const char *prefix);
char const *maketemp (int);
char const *rcssuffix (char const *);
int pairnames (int, char **,
               RILE * (*) (struct buf *, struct stat *, int),
               int, int);
struct cbuf bufremember (struct buf *, size_t);
void bufalloc (struct buf *, size_t);
void bufautoend (struct buf *);
void bufrealloc (struct buf *, size_t);
void bufscat (struct buf *, char const *);
void bufscpy (struct buf *, char const *);
void tempunlink (void);

/* rcsgen */
extern int interactiveflag;
extern struct buf curlogbuf;
char const *buildrevision (struct hshentries const *,
                           struct hshentry *, FILE *, int);
int getcstdin (void);
int putdtext (struct hshentry const *, char const *, FILE *, int);
int ttystdin (void);
int yesorno (int, char const *, ...) printf_string (2, 3);
struct cbuf cleanlogmsg (char *, size_t);
struct cbuf getsstdin (char const *, char const *,
                       char const *, struct buf *);
void putdesc (int, char *);
void putdftext (struct hshentry const *, RILE *, FILE *, int);

/* rcskeep */
extern int prevkeys;
extern struct buf prevauthor, prevdate, prevname, prevrev, prevstate;
int getoldkeys (RILE *);

/* rcskeys */
extern char const *const Keyword[];
enum markers trymatch (char const *);

/* rcslex */
extern FILE *foutptr;
extern FILE *frewrite;
extern RILE *finptr;
extern char const *NextString;
extern enum tokens nexttok;
extern int hshenter;
extern int nerror;
extern int nextc;
extern int quietflag;
extern long rcsline;
char const *getid (void);
void efaterror (char const *) exiting;
void enfaterror (int, char const *) exiting;
void fatcleanup (int) exiting;
void faterror (char const *, ...) printf_string_exiting (1, 2);
void fatserror (char const *, ...) printf_string_exiting (1, 2);
void rcsfaterror (char const *, ...) printf_string_exiting (1, 2);
void Ieof (void) exiting;
void Ierror (void) exiting;
void Oerror (void) exiting;
char *checkid (char *, int);
char *checksym (char *, int);
int eoflex (void);
int getkeyopt (char const *);
int getlex (enum tokens);
struct cbuf getphrases (char const *);
struct cbuf savestring (struct buf *);
struct hshentry *getnum (void);
void Ifclose (RILE *);
void Izclose (RILE **);
void Lexinit (void);
void Ofclose (FILE *);
void Orewind (FILE *);
void Ozclose (FILE **);
void aflush (FILE *);
void afputc (int, FILE *);
void aprintf (FILE *, char const *, ...) printf_string (2, 3);
void aputs (char const *, FILE *);
void checksid (char *);
void checkssym (char *);
void diagnose (char const *, ...) printf_string (1, 2);
void eerror (char const *);
void eflush (void);
void enerror (int, char const *);
void error (char const *, ...) printf_string (1, 2);
void fvfprintf (FILE *, char const *, va_list);
void getkey (char const *);
void getkeystring (char const *);
void nextlex (void);
void oflush (void);
void printstring (void);
void readstring (void);
void redefined (int);
void rcserror (char const *, ...) printf_string (1, 2);
void rcswarn (char const *, ...) printf_string (1, 2);
void testIerror (FILE *);
void testOerror (FILE *);
void warn (char const *, ...) printf_string (1, 2);
void warnignore (void);
void workerror (char const *, ...) printf_string (1, 2);
void workwarn (char const *, ...) printf_string (1, 2);
#if defined HAVE_MADVISE && defined HAVE_MMAP && large_memory
void advise_access (RILE *, int);
#	define if_advise_access(p,f,advice) if (p) advise_access(f,advice)
#else
#	define advise_access(f,advice)
#	define if_advise_access(p,f,advice)
#endif
#if large_memory && maps_memory
RILE *I_open (char const *, struct stat *);
#	define Iopen(f,m,s) I_open(f,s)
#else
RILE *Iopen (char const *, char const *, struct stat *);
#endif
#if !large_memory
void testIeof (FILE *);
void Irewind (RILE *);
#endif

/* rcsmap */
extern enum tokens const ctab[];

/* rcsrev */
char *partialno (struct buf *, char const *, int);
char const *namedrev (char const *, struct hshentry *);
char const *tiprev (void);
int cmpdate (char const *, char const *);
int cmpnum (char const *, char const *);
int cmpnumfld (char const *, char const *, int);
int compartial (char const *, char const *, int);
int expandsym (char const *, struct buf *);
int fexpandsym (char const *, struct buf *, RILE *);
struct hshentry *genrevs (char const *, char const *, char const *,
                          char const *, struct hshentries **);
struct hshentry *gr_revno (char const *revno, struct hshentries **store);
int countnumflds (char const *);
void getbranchno (char const *, struct buf *);

/* rcssyn */
/* These expand modes must agree with Expand_names[] in rcssyn.c.  */
#define KEYVAL_EXPAND 0         /* -kkv `$Keyword: value $' */
#define KEYVALLOCK_EXPAND 1     /* -kkvl `$Keyword: value locker $' */
#define KEY_EXPAND 2            /* -kk `$Keyword$' */
#define VAL_EXPAND 3            /* -kv `value' */
#define OLD_EXPAND 4            /* -ko use old string, omitting expansion */
#define BINARY_EXPAND 5         /* -kb like -ko, but use binary mode I/O */
#define MIN_UNEXPAND OLD_EXPAND /* min value for no logical expansion */
#define MIN_UNCHANGED_EXPAND (OPEN_O_BINARY ? BINARY_EXPAND : OLD_EXPAND)
                        /* min value guaranteed to yield an identical file */
struct diffcmd
{
  long line1,              /* number of first line */
    nlines,                /* number of lines affected */
    adprev,                /* previous 'a' line1+1 or 'd' line1 */
    dafter;                /* sum of previous 'd' line1 and previous 'd' nlines */
};
extern char const *Dbranch;
extern struct access *AccessList;
extern struct assoc *Symbols;
extern struct cbuf Comment;
extern struct cbuf Ignored;
extern struct rcslock *Locks;
extern struct hshentry *Head;
extern int Expand;
extern int StrictLocks;
extern int TotalDeltas;
extern char const *const expand_names[];
extern char const Kaccess[], Kauthor[], Kbranch[], Kcomment[],
  Kdate[], Kdesc[], Kexpand[], Khead[], Klocks[], Klog[],
  Knext[], Kstate[], Kstrict[], Ksymbols[], Ktext[];
void unexpected_EOF (void) exiting;
int getdiffcmd (RILE *, int, FILE *, struct diffcmd *);
int str2expmode (char const *);
void getadmin (void);
void getdesc (int);
void gettree (void);
void ignorephrases (char const *);
void initdiffcmd (struct diffcmd *);
void putadmin (void);
void putstring (FILE *, int, struct cbuf, int);
void puttree (struct hshentry const *, FILE *);

/* rcstime */
#define zonelenmax 9            /* maxiumum length of time zone string, e.g. "+12:34:56" */
char const *date2str (char const[datesize], char[datesize + zonelenmax]);
time_t date2time (char const[datesize]);
void str2date (char const *, char[datesize]);
void time2date (time_t, char[datesize]);
void zone_set (char const *);

/* rcsutil */
extern int RCSversion;
FILE *fopenSafer (char const *, char const *);
char *cgetenv (char const *);
char *fstr_save (char const *);
char *str_save (char const *);
char const *getusername (int);
int fdSafer (int);
int getRCSINIT (int, char **, char ***);
int run (int, char const *, ...);
int runv (int, char const *, char const **);
void *fremember (void *);
void *ftestalloc (size_t);
void *testalloc (size_t);
void *testrealloc (void *, size_t);
#define ftalloc(T) ftnalloc(T,1)
#define talloc(T) tnalloc(T,1)

#define ftnalloc(T,n) ((T*) ftestalloc(sizeof(T)*(n)))
#define tnalloc(T,n) ((T*) testalloc(sizeof(T)*(n)))
#define trealloc(T,p,n) ((T*) testrealloc((void *)(p), sizeof(T)*(n)))
#define tfree(p) free((void *)(p))

time_t now (void);
void awrite (char const *, size_t, FILE *);
void fastcopy (RILE *, FILE *);
void ffree (void);
void ffree1 (char const *);
void setRCSversion (char const *);
#if defined HAVE_SIGNAL_H
void catchints (void);
void ignoreints (void);
void restoreints (void);
#else
#	define catchints()
#	define ignoreints()
#	define restoreints()
#endif
#if defined HAVE_MMAP && large_memory
#   if has_NFS && MMAP_SIGNAL
void catchmmapints (void);
void readAccessFilenameBuffer (char const *, unsigned char const *);
#   else
#	define catchmmapints()
#   endif
#endif
#if defined HAVE_GETUID
uid_t ruid (void);
#	define myself(u) ((u) == ruid())
#else
#	define myself(u) true
#endif
#if defined HAVE_SETUID
uid_t euid (void);
void nosetid (void);
void seteid (void);
void setrid (void);
#else
#	define nosetid()
#	define seteid()
#	define setrid()
#endif

extern int isSLASH (int c);

/* The locations of RCS programs, for internal use.  */
extern const char const prog_co[];
extern const char const prog_merge[];
extern const char const prog_diff[];
extern const char const prog_diff3[];

/* Flags to make diff(1) work with RCS.  These
   should be a single argument (no internal spaces).  */
extern const char const diff_flags[];

/* Return values for diff(1).  */
extern const int diff_success;
extern const int diff_failure;
extern const int diff_trouble;

/* A string of 77 '=' followed by '\n'.  */
extern char const equal_line[];
