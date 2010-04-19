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

#include "config.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_MACH_MACH_H
#include <mach/mach.h>
#endif
#ifdef HAVE_NET_ERRNO_H
#include <net/errno.h>
#endif
#ifdef HAVE_VFORK_H
#include <vfork.h>
#endif

/* GCC attributes  */

#define RCS_UNUSED  _GL_UNUSED

#if __GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 7)
#define exiting              __attribute__ ((__noreturn__))
#define printf_string(m, n)  __attribute__ ((__format__ (printf, m, n)))
#else
#define exiting
#define printf_string(m, n)
#endif

#define KS(x)  (#x)

/* The set of RCS file keywords.  */
#define Kaccess    KS (access)
#define Kauthor    KS (author)
#define Kbranch    KS (branch)
#define Kcomment   KS (comment)
#define Kdate      KS (date)
#define Kdesc      KS (desc)
#define Kexpand    KS (expand)
#define Khead      KS (head)
#define Klocks     KS (locks)
#define Klog       KS (log)
#define Knext      KS (next)
#define Kstate     KS (state)
#define Kstrict    KS (strict)
#define Ksymbols   KS (symbols)
#define Ktext      KS (text)

#define ciklog      "checked in with -k by "
#define ciklog_len  ((sizeof ciklog) - 1)

/* Keyword substitution modes.  The order must agree with ‘kwsub_pool’.  */
enum kwsub
  {
    kwsub_kv,                           /* $Keyword: value $ */
    kwsub_kvl,                          /* $Keyword: value locker $ */
    kwsub_k,                            /* $Keyword$ */
    kwsub_v,                            /* value */
    kwsub_o,                            /* (old string) */
    kwsub_b                             /* (binary i/o old string) */
  };

/* begin cruft formerly from from conf.h */

#ifdef O_BINARY
/* Text and binary i/o behave differently.
   This is incompatible with POSIX and Unix.  */
#define FOPEN_RB "rb"
#define FOPEN_R_WORK (BE (kws) == kwsub_b ? "r" : "rb")
#define FOPEN_WB "wb"
#define FOPEN_W_WORK (BE (kws) == kwsub_b ? "w" : "wb")
#define FOPEN_WPLUS_WORK (BE (kws) == kwsub_b ? "w+" : "w+b")
#define OPEN_O_BINARY O_BINARY
#else
/* Text and binary i/o behave the same.
   Omit "b", since some nonstandard hosts reject it. */
#define FOPEN_RB "r"
#define FOPEN_R_WORK "r"
#define FOPEN_WB "w"
#define FOPEN_W_WORK "w"
#define FOPEN_WPLUS_WORK "w+"
#define OPEN_O_BINARY 0
#endif

/* Lock file mode.  */
#define OPEN_CREAT_READONLY (S_IRUSR|S_IRGRP|S_IROTH)

/* Extra open flags for creating lock file.  */
#define OPEN_O_LOCK 0

/* Main open flag for creating a lock file.  */
#define OPEN_O_WRONLY O_WRONLY

/* Is getlogin() secure?  Usually it's not.  */
#define getlogin_is_secure 0

/* Can ‘rename (A, B)’ falsely report success?  */
#define bad_NFS_rename 0

/* Does ‘setreuid’ work?  See top-level README.  */
#define has_setreuid 0

/* Might NFS be used?  */
#define has_NFS 1

/* Shell to run RCS subprograms.  */
#define RCS_SHELL "/bin/sh"

/* Filename component separation.
   TMPDIR       string           Default directory for temporary files.
   SLASH        char             Principal filename separator.
   SLASHes      ‘case SLASHes:’  Labels all filename separators.
   ABSFNAME(p)  expression       Is p an absolute filename?
   X_DEFAULT    string           Default value for -x option.
*/
#if !WOE
#define TMPDIR "/tmp"
#define SLASH '/'
#define SLASHes '/'
#define ABSFNAME(p)  (isSLASH ((p)[0]))
#define X_DEFAULT ",v"
#else /* WOE */
#define TMPDIR "\\tmp"
#define SLASH "'\\'"
#define SLASHes '\\': case '/': case ':'
#define ABSFNAME(p)  (isSLASH ((p)[0]) || (p)[0] && (p)[1] == ':')
#define X_DEFAULT "\\,v"
#endif

/* Must TZ be set for gmtime() to work?  */
#define TZ_must_be_set 0

#if defined HAVE_WORKING_FORK && !defined HAVE_WORKING_VFORK
#undef vfork
#define vfork fork
#endif

#if defined HAVE_SETUID && !defined HAVE_SETEUID
#undef seteuid
#define seteuid setuid
#endif

/* end cruft formerly from from conf.h */

#ifdef _POSIX_PATH_MAX
#define SIZEABLE_FILENAME_LEN  _POSIX_PATH_MAX
#else
/* Size of a large filename; not a hard limit.  */
#define SIZEABLE_FILENAME_LEN  255
#endif

/* Backwards compatibility with old versions of RCS.  */

/* Oldest output RCS format supported.  */
#define VERSION_min 3
/* Newest output RCS format supported. */
#define VERSION_max 5
/* Default RCS output format.  */
#ifndef VERSION_DEFAULT
#define VERSION_DEFAULT VERSION_max
#endif
/* Internally, 0 is the default.  */
#define VERSION(n)  ((n) - VERSION_DEFAULT)

/* Locking strictness
   false sets the default locking to non-strict;
   used in experimental environments.
   true sets the default locking to strict;
   used in production environments.
*/
#ifndef STRICT_LOCKING
#define STRICT_LOCKING  true
#endif

/* This is good through AD 9,999,999,999,999,999.  */
#define yearlength                           16
/* Size of output of ‘time2date’.  */
#define datesize                             (yearlength + 16)
/* Delimiter for keywords.  */
#define KDELIM                               '$'
/* Separates keywords from values.  */
#define VDELIM                               ':'
/* Default state of revisions.  */
#define DEFAULTSTATE                         "Exp"

/* If there is no signal, better to disable mmap entirely.
   We leave MMAP_SIGNAL as 0 to indicate this.  */
#if !MMAP_SIGNAL
#undef HAVE_MMAP
#undef HAVE_MADVISE
#endif

/* Print a char, but abort on write error.  */
#define aputc(c,o)  do                          \
    if (putc (c, o) == EOF)                     \
      testOerror (o);                           \
  while (0)

/* Computes mode of the working file: same as ‘RCSmode’,
   but write permission determined by ‘writable’.  */
#define WORKMODE(RCSmode, writable)                     \
  (((RCSmode) & (mode_t)~(S_IWUSR|S_IWGRP|S_IWOTH))     \
   | ((writable) ? S_IWUSR : 0))

/* Character classes and token codes.  */
enum tokens
{
  /* Classes.  */
  DELIM, DIGIT, IDCHAR, NEWLN, LETTER, Letter,
  PERIOD, SBEGIN, SPACE, UNKN,
  /* Tokens.  */
  COLON, ID, NUM, SEMI, STRING
};

/* The actual character is needed for string handling.
   ‘SDELIM’ must be consistent with ‘ctab’, so that ‘ctab[SDELIM] == SBEGIN’.
   There should be no overlap among ‘SDELIM’, ‘KDELIM’ and ‘VDELIM’.  */
#define SDELIM  '@'

/* Data structures for the symbol table.  */

struct buf                              /* mutable */
{
  char *string;
  size_t size;
};
struct cbuf                             /* immutable */
{
  char const *string;
  size_t size;
};

/* Hash table entry.  */
struct hshentry
{
  /* Pointer to revision number (ASCIZ).  */
  char const *num;

  /* Pointer to date of checkin, person checking in, the locker.  */
  char const *date;
  char const *author;
  char const *lockedby;

  /* State of revision (see ‘DEFAULTSTATE’).  */
  char const *state;

  /* Name (if any) by which retrieved.  */
  char const *name;

  /* Log message requested at checkin.  */
  struct cbuf log;

  /* List of first revisions on branches.  */
  struct branchhead *branches;

  /* Ignored phrases in admin part.  */
  struct cbuf ig;

  /* Ignored phrases in deltatext part.  */
  struct cbuf igtext;

  /* Next revision on same branch, with same hash value.  */
  struct hshentry *next;
  struct hshentry *nexthsh;

  /* Lines inserted and deleted (computed by rlog).  */
  long insertlns;
  long deletelns;

  /* True if selected, false if deleted.  */
  char selector;
};

/* List of hash entries.  */
struct hshentries
{
  struct hshentries *rest;
  struct hshentry *first;
};

/* List element for branch lists.  */
struct branchhead
{
  struct hshentry *hsh;
  struct branchhead *nextbranch;
};

/* Access-list element. */
struct access
{
  char const *login;
  struct access *nextaccess;
};

/* List element for locks.  */
struct rcslock
{
  char const *login;
  struct hshentry *delta;
  struct rcslock *nextlock;
};

/* List element for symbolic names.  */
struct assoc
{
  char const *symbol;
  char const *num;
  struct assoc *nextassoc;
};

/* Symbol-pool particulars.  */
struct tinysym
{
  uint8_t len;
  uint8_t bytes[];
};
struct pool_found
{
  int i;
  struct tinysym *sym;
};

/* Max length of the (working file) keywords.  */
#define keylength 8

/* This must be in the same order as in ‘keyword_pool’.  */
enum markers
{
  Author, Date, Header, Id,
  Locker, Log, Name, RCSfile, Revision, Source, State
};

/* This is used by ‘putdtext’ and ‘scanlogtext’.  */
#define DELNUMFORM      "\n\n%s\n%s\n"

/* This is used by ci and rlog.  */
#define EMPTYLOG "*** empty log message ***"

struct maybe;

/* The function ‘pairnames’ takes to open the RCS file.  */
typedef struct fro * (open_rcsfile_fn) (struct maybe *);

/* A combination of probe parameters and results for ‘pairnames’ through
   ‘fin2open’ through ‘finopen’ through {‘rcsreadopen’, ‘rcswriteopen’}
   (and ‘naturalize’ in the case of ‘rcswriteopen’).

   The probe protocol is to set ‘open’ and ‘mustread’ once, and try various
   permutations of basename, directory and extension (-x) in ‘tentative’,
   finally recording ‘errno’ in ‘eno’, the "best RCS filename found" in
   ‘bestfit’, and stat(2) info in ‘status’ (otherwise failing).  */
struct maybe
{
  /* Input parameters, constant.  */
  open_rcsfile_fn *open;
  bool mustread;

  /* Input parameter, varying.  */
  struct cbuf tentative;

  /* Scratch.  */
  struct divvy *space;

  /* Output parameters.  */
  struct cbuf bestfit;
  struct stat *status;
  int eno;
};

/* The locations of RCS programs, for internal use.  */
extern const char const prog_co[];
extern const char const prog_merge[];
extern const char const prog_diff[];
extern const char const prog_diff3[];

/* Flags to make diff(1) work with RCS.  These
   should be a single argument (no internal spaces).  */
extern const char const diff_flags[];

/* A string of 77 '=' followed by '\n'.  */
extern const char const equal_line[];

/* Every program defines this.  */
struct program
{
  /* The name of the program, for --help, --version, etc.  */
  const char const *name;
  /* Text for --help.  */
  const char const *help;
  /* Exit errorfully.  */
  void (*exiterr) (void) exiting;
};
extern const struct program program;

/* (Somewhat) fleeting files.  */
enum maker { notmade, real, effective };

struct sff
{
  char const *filename;
  /* Unlink this when done.  */
  enum maker disposition;
  /* (But only if it is in the right mood.)  */
};

/* A program controls the behavior of subsystems by setting these.
   Subsystems also communicate via these settings.  */
struct behavior
{
  bool unbufferedp;
  /* Although standard error should be unbuffered by default,
     don't rely on it.
     -- unbuffer_standard_error  */

  bool quiet;
  /* This is set from command-line option ‘-q’.  When set:
     - disable all yn -- yesorno
     - disable warnings -- generic_warn
     - disable error messages -- diagnose catchsigaction
     - don't ask about overwriting a writable workfile
     - on missing RCS file, suppress error and init instead -- pairnames
     - [ident] suppress no-keywords-found warning
     - [rcs] suppress yn when outdating all revisions
     - [rcsclean] suppress progress output  */

  bool interactive_valid;               /* -- ttystdin */
  bool interactive;
  /* Should we act as if stdin is a tty?  Set from ‘-I’.  When set:
     - enables stdin flushing and newline output -- getcstdin
     - enables yn (masked by ‘quiet’, above) -- yesorno
     - enables "enter FOO terminated by ." message -- getsstdin
     - [co] when workfile writable, include name in error message  */

  bool inclusive_of_Locker_in_Id_val;
  /* If set, append locker val when expanding ‘Id’ and locking.  */

  bool receptive_to_next_hash_key;
  /* If set, next suitable lexeme will be entered into the
     symbol table -- nextlex.  Handle with care.  */

  bool strictly_locking;
  /* When set:
     - don't inhibit error when removing self-lock -- removelock
     - enable error if not self-lock -- addelta
     - generate "; strict" in RCS file -- putadmin
     - [ci] ???
     - [co] conspires w/ kwsub_v to make workfile readonly
     - [rlog] display "strict"  */

  bool version_set;
  int version;
  /* The "effective RCS version", for backward compatability,
     normalized via ‘VERSION’ (i.e., current 0, previous -1, etc).
     ‘version_set’ true means the effective version was set from the
     command-line option ‘-V’.  Additional ‘-V’ results in a warning.
     -- setRCSversion  */

  bool stick_with_euid;
  /* Ignore all calls to ‘seteid’ and ‘setrid’.
     -- nosetid  */

  int ruid, euid;
  bool ruid_cached, euid_cached;
  /* The real and effective user-ids, and their respective
     "already-cached" state (to implement one-shot).
     -- ruid euid  */

  bool already_setuid;
  /* It's not entirely clear what this bit does.
     -- set_uid_to  */

  int kws;
  /* The keyword substitution (aka "expansion") mode, or -1 (mu).
     FIXME: Unify with ‘enum kwsub’.
     -- [co]main [rcs]main [rcsclean]main InitAdmin getadmin  */

  char const *pe;
  /* Possible endings, a slash-separated list of filename-end
     fragments to consider for recognizing the name of the RCS file.
     FIXME: Push defaulting into library.
     -- [ci]main [co]main [rcs]main [rcsclean]main [rcsdiff]main
     -- rcssuffix
     -- [rcsmerge]main [rlog]main  */

  struct zone_offset
  {
    bool valid;
    /* When set, use ‘BE (zone_offset.seconds)’ in ‘date2str’.
       Otherwise, use UTC without timezone indication.
       -- zone_set  */

    long seconds;
    /* Seconds east of UTC, or ‘TM_LOCAL_ZONE’.
       -- zone_set  */
  } zone_offset;

  char *username;
  /* The login id of the program user.
     -- getusername  */

  time_t now;
  /* Cached time from ‘time’.
     -- now  */

  bool fixed_SIGCHLD;
  /* True means SIGCHLD handler has been manually set to SIG_DFL.
     (Only meaningful if ‘BAD_WAIT_IF_SIGCHLD_IGNORED’.)
     -- runv  */

  bool Oerrloop;
  /* True means ‘Oerror’ was called already.
     -- Oerror Lexinit  */

  char *cwd;
  /* The current working directory.
     -- getfullRCSname  */

  struct sff *sff;
  /* (Somewhat) fleeting files.  */

  /* The rest of the members in ‘struct behavior’ are scratch spaces
     managed by various subsystems.  */

  struct isr_scratch *isr;
  struct editstuff *editstuff;
  struct ephemstuff *ephemstuff;
  struct maketimestuff *maketimestuff;
};

/* The working file is a manifestation of a particular revision.  */
struct manifestation
{
  /* What it's called on disk; may be relative,
     unused if writing to stdout.
     -- rcsreadopen  */
  char *filename;

  /* [co] Use this if writing to stdout.  */
  FILE *standard_output;

  /* Previous keywords, to accomodate ‘ci -k’.
     -- getoldkeys  */
  struct {
    bool valid;
    char *author;
    char *date;
    char *name;
    char *rev;
    char *state;
  } prev;

  /* Some space to (temporarily) hold key/value/line fragments.
     -- expandline  */
  struct divvy *lparts;
};

/* The parse state is used when reading the RCS file.  */
struct parse_state
{
  bool erroneousp;
  /* True means lexing encountered an error.
     -- buildjoin Lexinit syserror generic_error generic_fatal  */

  void *tokbuf;
  /* Space for buffering tokens.
     -- Lexinit nextlex  */

  struct divvy *ignore;
  /* Non-NULL if we have ignored phrases in this RCS file.
     -- warnignore Lexinit  */

  struct next
  {
    enum tokens tok;
    /* Character class and/or token code.
       -- nextlex getphrases  */

    int c;
    /* Next input character, parallel with ‘tok’.
       -- copystring enterstring editstring expandline
       -- nextlex eoflex getphrases readstring printstring savestring
       -- getdiffcmd
       -- getscript
       (all to restore stream at end-of-string).  */

    char const *str;
    /* Hold the next ID or NUM value.
       -- lookup nextlex getphrases  */

    struct hshentry *hsh;
    /* Pointer to next hash entry.
       -- lookup  */
  } next;

  struct hshentry **hshtab;
  /* Hash table.
     -- Lexinit lookup  */

  long lno;
  /* Current line-number of input.  FIXME: Make unsigned.
     -- copystring enterstring editstring expandline
     -- Lexinit nextlex eoflex getphrases readstring printstring savestring
     -- getdiffcmd
     -- getscript  */
};

/* The RCS file is the repository of revisions, plus metadata.  */
struct repository
{
  char const *filename;
  /* What it's called on disk.
     -- pairnames (PAIRTEST)main  */

  int fd_lock;
  /* The file descriptor of the RCS file lockfile.
     -- rcswriteopen ORCSclose pairnames putadmin (SYNTEST)main  */

  struct stat stat;
  /* Stat info, possibly munged.
     -- [ci]main [rcs]main fro_open (via rcs{read,write}open)  */

  struct admin
  {
    struct access *allowed;
    /* List of usernames who may modify the repo.
       -- InitAdmin doaccess [rcs]main  */

    struct assoc *assocs;
    /* List of symbolic names.
       -- addsymbol InitAdmin  */

    struct cbuf log_lead;
    /* The string to use to start lines expanded for ‘Log’.  FIXME:ZONK.
       -- [rcs]main (FCMPTEST)main InitAdmin getadmin  */

    struct cbuf description;
    /* The description string, if any.  Not functionally relevant.
       -- InitAdmin getadmin  */

    struct rcslock *locks;
    /* List of locks.
       -- rmlock addlock InitAdmin  */

    char const *defbr;
    /* The default branch, or NULL.
       -- [rcs]main InitAdmin getadmin  */

    struct hshentry *head;
    /* The revision on the tip of the default branch.
       -- addelta buildtree [rcs]main InitAdmin getadmin  */
  } admin;

  int ndelt;
  /* Counter for deltas.
     -- getadmin  */
};

/* Various data streams flow in and out of RCS programs.  */
struct flow
{
  struct fro *from;
  /* Input stream for the RCS file.
     -- rcsreadopen pairnames (LEXDB)main (REVTEST)main (SYNTEST)main  */

  FILE *rewr;
  /* Output stream for echoing input stream.
     -- putadmin  */

  FILE *to;
  /* Output stream for the RCS file.
     ``Copy of ‘rewr’, but NULL to suppress echo.''
     -- [ci]main scanlogtext dorewrite putdesc  */

  FILE *res;
  /* Output stream for the result file.  ???
     -- enterstring  */

  char const *result;
  /* The result file name.
     -- openfcopy swapeditfiles  */
};

/* The top of the structure tree (NB: does not include ‘program’).  */
struct top
{
  struct behavior behavior;
  struct manifestation manifestation;
  struct parse_state parse_state;
  struct repository repository;
  struct flow flow;
};

extern struct top *top;

/* In the future we might move ‘top’ into another structure.
   These abstractions keep the invasiveness to a minimum.  */
#define PROGRAM(x)    (program. x)
#define BE(quality)   (top->behavior. quality)
#define MANI(member)  (top->manifestation. member)
#define PREV(which)   (MANI (prev). which)
#define LEX(member)   (top->parse_state. member)
#define NEXT(which)   (LEX (next). which)
#define REPO(member)  (top->repository. member)
#define ADMIN(part)   (REPO (admin). part)
#define FLOW(member)  (top->flow. member)

/* b-anchor */
int recognize_kwsub (const char *, size_t);
#define str2expmode(s)  (recognize_kwsub ((s), strlen (s)))
const char const *kwsub_string (enum kwsub);
bool recognize_keyword (char const *, struct pool_found *);

/* merge */
int merge (bool, char const *, char const *const[3], char const *const[3]);

/* rcsedit */
struct fro *rcswriteopen (struct maybe *);
char const *getcaller (void);
int addlock (struct hshentry *, bool);
int addsymbol (char const *, char const *, bool);
bool checkaccesslist (void);
int chnamemod (FILE **, char const *, char const *, int, mode_t, time_t);
int donerewrite (int, time_t);
int dorewrite (bool, int);
int findlock (bool, struct hshentry **);
int setmtime (char const *, time_t);
void ORCSclose (void);
void ORCSerror (void);
void copystring (void);
void enterstring (void);
void finishedit (struct hshentry const *, FILE *, bool);
void openfcopy (FILE *);
void snapshotedit (FILE *);
void xpandstring (struct hshentry const *);
int un_link (char const *);
void editstring (struct hshentry const *);

/* rcsfcmp */
int rcsfcmp (struct fro *, struct stat const *, char const *,
             struct hshentry const *);

/* rcsfnms */
#define bufautobegin(b)  clear_buf (b)
#define clear_buf(b)  (((b)->string = 0, (b)->size = 0))
struct fro *rcsreadopen (struct maybe *);
char *bufenlarge (struct buf *, char const **);
char const *basefilename (char const *);
char const *getfullRCSname (void);
char const *rcssuffix (char const *);
int pairnames (int, char **, open_rcsfile_fn *, bool, bool);
void bufalloc (struct buf *, size_t);
void bufautoend (struct buf *);
void bufscat (struct buf *, char const *);
void bufscpy (struct buf *, char const *);

/* rcsgen */
char const *buildrevision (struct hshentries const *,
                           struct hshentry *, FILE *, bool);
int getcstdin (void);
bool putdtext (struct hshentry const *, char const *, FILE *, bool);
bool ttystdin (void);
bool yesorno (bool, char const *, ...) printf_string (2, 3);
struct cbuf cleanlogmsg (char const *, size_t);
struct cbuf getsstdin (char const *, char const *, char const *);
void putdesc (struct cbuf *, bool, char *);
void putdftext (struct hshentry const *, struct fro *, FILE *, bool);

/* rcskeep */
bool getoldkeys (struct fro *);

/* rcslex */
char const *getid (void);
char *checkid (char *, int);
char *checksym (char *, int);
bool eoflex (void);
bool getkeyopt (char const *);
bool getlex (enum tokens);
struct cbuf getphrases (char const *);
struct cbuf savestring (void);
struct hshentry *getnum (void);
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
void getkey (char const *);
void getkeystring (char const *);
void nextlex (void);
void oflush (void);
void printstring (void);
void readstring (void);
void redefined (int);
void warnignore (void);

/* rcsmap */
extern const enum tokens const ctab[];

/* rcsrev */
char *partialno (struct buf *, char const *, int);
char const *namedrev (char const *, struct hshentry *);
char const *tiprev (void);
int cmpdate (char const *, char const *);
int cmpnum (char const *, char const *);
int cmpnumfld (char const *, char const *, int);
int compartial (char const *, char const *, int);
bool expandsym (char const *, struct buf *);
bool fexpandsym (char const *, struct buf *, struct fro *);
struct hshentry *genrevs (char const *, char const *, char const *,
                          char const *, struct hshentries **);
struct hshentry *gr_revno (char const *revno, struct hshentries **store);
int countnumflds (char const *);
void getbranchno (char const *, struct buf *);

/* rcssyn */
/* Minimum value for no logical expansion.  */
#define MIN_UNEXPAND  kwsub_o
/* The minimum value guaranteed to yield an identical file.  */
#define MIN_UNCHANGED_EXPAND  (OPEN_O_BINARY ? kwsub_b : kwsub_o)
struct diffcmd
{
  /* Number of first line.  */
  long line1;
  /* Number of lines affected.  */
  long nlines;
  /* Previous 'a' line1+1 or 'd' line1.  */
  long adprev;
  /* Sum of previous 'd' line1 and previous 'd' nlines.  */
  long dafter;
};
extern const char const *const expand_names[];
void unexpected_EOF (void) exiting;
int getdiffcmd (struct fro *, bool, FILE *, struct diffcmd *);
void getadmin (void);
void getdesc (bool);
void gettree (void);
void ignorephrases (char const *);
void initdiffcmd (struct diffcmd *);
void putadmin (void);
void putstring (FILE *, bool, struct cbuf, bool);
void puttree (struct hshentry const *, FILE *);

/* rcstime */
/* Maxiumum length of time zone string, e.g. "+12:34:56".  */
#define zonelenmax  9
char const *date2str (char const[datesize], char[datesize + zonelenmax]);
time_t date2time (char const[datesize]);
void str2date (char const *, char[datesize]);
void time2date (time_t, char[datesize]);
void zone_set (char const *);

/* rcsutil */
void gnurcs_init (void);
void bad_option (char const *);
struct cbuf minus_p (char const *xrev, char const *rev);
char *cgetenv (char const *);
char *fbuf_save (const struct buf *);
char *str_save (char const *);
char const *getusername (bool);
int getRCSINIT (int, char **, char ***);
int run (int, char const *, ...);
int runv (int, char const *, char const **);
void *testalloc (size_t);
void *testrealloc (void *, size_t);
#define talloc(T)  ((T*) testalloc (sizeof (T)))
#define tfree(p)  free ((void *)(p))

time_t now (void);
void awrite (char const *, size_t, FILE *);
void ffree (void);
void free_NEXT_str (void);
void setRCSversion (char const *);
uid_t ruid (void);
bool myself (uid_t);
#if defined HAVE_SETUID
uid_t euid (void);
void nosetid (void);
void seteid (void);
void setrid (void);
#else
#define nosetid()
#define seteid()
#define setrid()
#endif

bool isSLASH (int c);

/* Indexes into ‘BE (sff)’.  */
#define SFFI_LOCKDIR  0
#define SFFI_NEWDIR   BAD_CREAT0

/* Idioms.  */

/* Get a character from ‘fin’, perhaps copying to a ‘frew’.  */
#define TEECHAR()  do                           \
    {                                           \
      GETCHAR (c, fin);                         \
      if (frew)                                 \
        afputc (c, frew);                       \
    }                                           \
  while (0)

/* base.h ends here */
