/* RCS filename and pathname handling

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

/****************************************************************************
 *                     creation and deletion of /tmp temporaries
 *		       pairing of RCS pathnames and working pathnames.
 *                     Testprogram: define PAIRTEST
 ****************************************************************************
 */

#include "rcsbase.h"
#include <stdint.h>

static char const *bindex (char const *, int);
static int fin2open (char const *, size_t, char const *, size_t,
                     char const *, size_t,
                     RILE * (*) (struct buf *, struct stat *, int),
                     int);
static int finopen (RILE * (*) (struct buf *, struct stat *, int),
                    int);
static int suffix_matches (char const *, char const *);
static size_t dir_useful_len (char const *);
static size_t suffixlen (char const *);
static void InitAdmin (void);

char const *RCSname;
char *workname;
int fdlock;
FILE *workstdout;
struct stat RCSstat;
char const *suffixes;

static char const rcsdir[] = "RCS";
#define rcslen (sizeof(rcsdir)-1)

static struct buf RCSbuf, RCSb;
static int RCSerrno;

/* Temp names to be unlinked when done, if they are not 0.  */
#define TEMPNAMES 5             /* must be at least DIRTEMPNAMES (see rcsedit.c) */
static char *volatile tpnames[TEMPNAMES];

struct compair
{
  char const *suffix, *comlead;
};

/*
* This table is present only for backwards compatibility.
* Normally we ignore this table, and use the prefix of the `$Log' line instead.
*/
static struct compair const comtable[] = {
  {"a",    "-- "},              /* Ada */
  {"ada",  "-- "},
  {"adb",  "-- "},
  {"ads",  "-- "},
  {"asm",  ";; "},              /* assembler (MS-DOS) */
  {"bat",  ":: "},              /* batch (MS-DOS) */
  {"body", "-- "},              /* Ada */
  {"c",    " * "},              /* C */
  {"c++",  "// "},              /* C++ in all its infinite guises */
  {"cc",   "// "},
  {"cpp",  "// "},
  {"cxx",  "// "},
  {"cl",   ";;; "},             /* Common Lisp */
  {"cmd",  ":: "},              /* command (OS/2) */
  {"cmf",  "c "},               /* CM Fortran */
  {"cs",   " * "},              /* C* */
  {"el",   "; "},               /* Emacs Lisp */
  {"f",    "c "},               /* Fortran */
  {"for",  "c "},
  {"h",    " * "},              /* C-header */
  {"hpp",  "// "},              /* C++ header */
  {"hxx",  "// "},
  {"l",    " * "},              /* lex (NOTE: franzlisp disagrees) */
  {"lisp", ";;; "},             /* Lucid Lisp */
  {"lsp",  ";; "},              /* Microsoft Lisp */
  {"m",    "// "},              /* Objective C */
  {"mac",  ";; "},              /* macro (DEC-10, MS-DOS, PDP-11, VMS, etc) */
  {"me",   ".\\\" "},           /* troff -me */
  {"ml",   "; "},               /* mocklisp */
  {"mm",   ".\\\" "},           /* troff -mm */
  {"ms",   ".\\\" "},           /* troff -ms */
  {"p",    " * "},              /* Pascal */
  {"pas",  " * "},
  {"ps",   "% "},               /* PostScript */
  {"spec", "-- "},              /* Ada */
  {"sty",  "% "},               /* LaTeX style */
  {"tex",  "% "},               /* TeX */
  {"y",    " * "},              /* yacc */
  {NULL,   "# "}                /* default for unknown suffix; must be last */
};

#ifndef HAVE_MKSTEMP
int
homegrown_mkstemp (char *template)
/* Like mkstemp(2), but never return EINVAL.  That is, never check for
   missing "XXXXXX" since we know the unique caller DTRT.  */
{
  int pid = getpid ();
  char *end = template + strlen (template);
  static char const xrep[] = ("abcdefghijklmnopqrstuvwxyz"
                              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                              /* Omit '0' => `strlen (xrep)' is prime.  */
                              "123456789");
  struct timeval tv;
  uint64_t n;
  int fd = -1;

  for (int patience = 42 * 42;
       0 > fd && patience;
       patience--)
    {
      if (0 > gettimeofday (&tv, NULL))
        return -1;
      /* Cast to ensure 64-bit shift.  */
      n = pid | (uint64_t)(tv.tv_sec ^ tv.tv_usec) << 32;
      fprintf (stderr, "BEF: %s %llx [%lu.%lu]\n", end - 6, n, tv.tv_sec, tv.tv_usec);
      for (char *w = end - 6; n && w < end; w++)
        {
          *w = xrep[n % 61];
          n = n / 61;
        }
      fd = open (template, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
      fprintf (stderr, "AFT: %s %d\n", end - 6, 42 * 42 - patience);
    }
  if (0 > fd)
    errno = EEXIST;
  return fd;
}

#define mkstemp  homegrown_mkstemp
#endif  /* !defined HAVE_MKSTEMP */

/* Set contents of FILENAME to the name of a temporary file made from a
   template with that starts with PREFIX.  If PREFIX is NULL, use the system
   "temporary directory".  (Specify the empty string for cwd.)  If no name is
   possible, signal a fatal error.  */
void
set_temporary_file_name (struct buf *filename, const char *prefix)
{
  static struct buf tmpdir =
    {
      .string = NULL,
      .size = 0
    };
  struct buf template;
  int fd;

  bufautobegin (&template);
  if (prefix)
    bufscpy (&template, prefix);
  else
    {
      if (! tmpdir.string)
        {
          char *v;

#define TRY(envvarname)                         \
          if (! tmpdir.string                   \
              && (v = cgetenv (#envvarname)))   \
            bufscpy (&tmpdir, v)
          TRY (TMPDIR);                 /* Unix tradition */
          TRY (TMP);                    /* DOS tradition */
          TRY (TEMP);                   /* another DOS tradition */
#undef TRY
          if (! tmpdir.string)
            {
              char slash[2] = { SLASH, '\0' };

              bufscpy (&tmpdir, P_tmpdir);
              bufscat (&tmpdir, slash);
            }
        }
      bufscpy (&template, tmpdir.string);
    }
  /* Support the 8.3 MS-DOG restriction, blech.  Truncate the non-directory
     filename component to two bytes so that the maximum non-extension name
     is 2 + 6 (Xs) = 8.  The extension is left empty.  What a waste.  */
  if ('/' != SLASH)
    {
      char *end = template.string + template.size;
      char *lastsep = strrchr (template.string, SLASH);
      char *ndfc = lastsep ? 1 + lastsep : template.string;
      char *dot;

      if (ndfc + 2 < end)
        *(ndfc + 2) = '\0';
      /* If any of the (up to 2) remaining bytes are '.', replace it
         with the lowest (decimal) digit of the pid.  Double blech.  */
      if ((dot = strchr (ndfc, '.')))
        *dot = '0' + getpid () % 10;
    }
  bufscat (&template, "XXXXXX");

  if (0 > (fd = mkstemp (template.string)))
    faterror ("could not make temporary file name (template \"%s\")",
              template.string);

  bufscpy (filename, template.string);
  bufautoend (&template);
  close (fd);
}

char const *
maketemp (int n)
/* Create a unique pathname using n and the process id and store it
 * into the nth slot in tpnames.
 * Because of storage in tpnames, tempunlink() can unlink the file later.
 * Return a pointer to the pathname created.
 */
{
  struct buf rv;
  char *t = tpnames[n];

  if (t)
    return t;

  catchints ();
  bufautobegin (&rv);
  set_temporary_file_name (&rv, NULL);
  t = testalloc (rv.size);
  strcpy (t, rv.string);
  bufautoend (&rv);
  tpnames[n] = t;
  return t;
}

void
tempunlink (void)
/* Clean up maketemp() files.  May be invoked by signal handler.
 */
{
  register int i;
  register char *p;

  for (i = TEMPNAMES; 0 <= --i;)
    if ((p = tpnames[i]))
      {
        unlink (p);
        /*
         * We would tfree(p) here,
         * but this might dump core if we're handing a signal.
         * We're about to exit anyway, so we won't bother.
         */
        tpnames[i] = NULL;
      }
}

static char const *
bindex (register char const *sp, register int c)
/* Function: Finds the last occurrence of character c in string sp
 * and returns a pointer to the character just beyond it. If the
 * character doesn't occur in the string, sp is returned.
 */
{
  register char const *r;
  r = sp;
  while (*sp)
    {
      if (*sp++ == c)
        r = sp;
    }
  return r;
}

static int
suffix_matches (register char const *suffix, register char const *pattern)
{
  register int c;
  if (!pattern)
    return true;
  for (;;)
    switch (*suffix++ - (c = *pattern++))
      {
      case 0:
        if (!c)
          return true;
        break;

      case 'A' - 'a':
        if (ctab[c] == Letter)
          break;
        /* fall into */
      default:
        return false;
      }
}

static void
InitAdmin (void)
/* function: initializes an admin node */
{
  register char const *Suffix;
  register int i;

  Head = NULL;
  Dbranch = NULL;
  AccessList = NULL;
  Symbols = NULL;
  Locks = NULL;
  StrictLocks = STRICT_LOCKING;

  /* guess the comment leader from the suffix */
  Suffix = bindex (workname, '.');
  if (Suffix == workname)
    Suffix = "";                /* empty suffix; will get default */
  for (i = 0; !suffix_matches (Suffix, comtable[i].suffix); i++)
    continue;
  Comment.string = comtable[i].comlead;
  Comment.size = strlen (comtable[i].comlead);
  Expand = KEYVAL_EXPAND;
  clear_buf (&Ignored);
  Lexinit ();                   /* note: if !finptr, reads nothing; only initializes */
}

void
bufalloc (register struct buf *b, size_t size)
/* Ensure *B is a name buffer of at least SIZE bytes.
 * *B's old contents can be freed; *B's new contents are undefined.
 */
{
  if (b->size < size)
    {
      if (b->size)
        tfree (b->string);
      else
        b->size = sizeof (void *);
      while (b->size < size)
        b->size <<= 1;
      b->string = tnalloc (char, b->size);
    }
}

void
bufrealloc (register struct buf *b, size_t size)
/* like bufalloc, except *B's old contents, if any, are preserved */
{
  if (b->size < size)
    {
      if (!b->size)
        bufalloc (b, size);
      else
        {
          while ((b->size <<= 1) < size)
            continue;
          b->string = trealloc (char, b->string, b->size);
        }
    }
}

void
bufautoend (struct buf *b)
/* Free an auto buffer at block exit. */
{
  if (b->size)
    tfree (b->string);
}

struct cbuf
bufremember (struct buf *b, size_t s)
/*
 * Free the buffer B with used size S.
 * Yield a cbuf with identical contents.
 * The cbuf will be reclaimed when this input file is finished.
 */
{
  struct cbuf cb;

  if ((cb.size = s))
    cb.string = fremember (trealloc (char, b->string, s));
  else
    {
      bufautoend (b);           /* not really auto */
      cb.string = "";
    }
  return cb;
}

char *
bufenlarge (register struct buf *b, char const **alim)
/* Make *B larger.  Set *ALIM to its new limit, and yield the relocated value
 * of its old limit.
 */
{
  size_t s = b->size;
  bufrealloc (b, s + 1);
  *alim = b->string + b->size;
  return b->string + s;
}

void
bufscat (struct buf *b, char const *s)
/* Concatenate S to B's end. */
{
  size_t blen = b->string ? strlen (b->string) : 0;
  bufrealloc (b, blen + strlen (s) + 1);
  strcpy (b->string + blen, s);
}

void
bufscpy (struct buf *b, char const *s)
/* Copy S into B. */
{
  bufalloc (b, strlen (s) + 1);
  strcpy (b->string, s);
}

char const *
basefilename (char const *p)
/* Yield the address of the base filename of the pathname P.  */
{
  register char const *b = p, *q = p;
  for (;;)
    switch (*q++)
      {
      case SLASHes:
        b = q;
        break;
      case 0:
        return b;
      }
}

static size_t
suffixlen (char const *x)
/* Yield the length of X, an RCS pathname suffix.  */
{
  register char const *p;

  p = x;
  for (;;)
    switch (*p)
      {
      case 0:
      case SLASHes:
        return p - x;

      default:
        ++p;
        continue;
      }
}

char const *
rcssuffix (char const *name)
/* Yield the suffix of NAME if it is an RCS pathname, NULL otherwise.  */
{
  char const *x, *p, *nz;
  size_t nl, xl;

  nl = strlen (name);
  nz = name + nl;
  x = suffixes;
  do
    {
      if ((xl = suffixlen (x)))
        {
          if (xl <= nl && memcmp (p = nz - xl, x, xl) == 0)
            return p;
        }
      else
        for (p = name; p < nz - rcslen; p++)
          if (isSLASH (p[rcslen])
              && (p == name || isSLASH (p[-1]))
              && memcmp (p, rcsdir, rcslen) == 0)
            return nz;
      x += xl;
    }
  while (*x++);
  return NULL;
}

RILE *
rcsreadopen (struct buf *RCSpath, struct stat *status, int mustread RCS_UNUSED)
/* Open RCSPATH for reading and yield its FILE* descriptor.
 * If successful, set *STATUS to its status.
 * Pass this routine to pairnames() for read-only access to the file.  */
{
  return Iopen (RCSpath->string, FOPEN_RB, status);
}

static int
finopen (RILE *(*rcsopen) (struct buf *, struct stat *, int), int mustread)
/*
 * Use RCSOPEN to open an RCS file; MUSTREAD is set if the file must be read.
 * Set finptr to the result and yield true if successful.
 * RCSb holds the file's name.
 * Set RCSbuf to the best RCS name found so far, and RCSerrno to its errno.
 * Yield true if successful or if an unusual failure.
 */
{
  int interesting, preferold;

  /*
   * We prefer an old name to that of a nonexisting new RCS file,
   * unless we tried locking the old name and failed.
   */
  preferold = RCSbuf.string[0] && (mustread || 0 <= fdlock);

  finptr = (*rcsopen) (&RCSb, &RCSstat, mustread);
  interesting = finptr || errno != ENOENT;
  if (interesting || !preferold)
    {
      /* Use the new name.  */
      RCSerrno = errno;
      bufscpy (&RCSbuf, RCSb.string);
    }
  return interesting;
}

static int
fin2open (char const *d, size_t dlen,
          char const *base, size_t baselen,
          char const *x, size_t xlen,
          RILE *(*rcsopen) (struct buf *, struct stat *, int),
          int mustread)
/*
 * D is a directory name with length DLEN (including trailing slash).
 * BASE is a filename with length BASELEN.
 * X is an RCS pathname suffix with length XLEN.
 * Use RCSOPEN to open an RCS file; MUSTREAD is set if the file must be read.
 * Yield true if successful.
 * Try dRCS/basex first; if that fails and x is nonempty, try dbasex.
 * Put these potential names in RCSb.
 * Set RCSbuf to the best RCS name found so far, and RCSerrno to its errno.
 * Yield true if successful or if an unusual failure.
 */
{
  register char *p;

  bufalloc (&RCSb, dlen + rcslen + 1 + baselen + xlen + 1);

  /* Try dRCS/basex.  */
  memcpy (p = RCSb.string, d, dlen);
  memcpy (p += dlen, rcsdir, rcslen);
  p += rcslen;
  *p++ = SLASH;
  memcpy (p, base, baselen);
  memcpy (p += baselen, x, xlen);
  p[xlen] = 0;
  if (xlen)
    {
      if (finopen (rcsopen, mustread))
        return true;

      /* Try dbasex.  */
      /* Start from scratch, because finopen() may have changed RCSb.  */
      memcpy (p = RCSb.string, d, dlen);
      memcpy (p += dlen, base, baselen);
      memcpy (p += baselen, x, xlen);
      p[xlen] = 0;
    }
  return finopen (rcsopen, mustread);
}

int
pairnames (int argc, char **argv,
           RILE *(*rcsopen) (struct buf *, struct stat *, int),
           int mustread, int quiet)
/*
 * Pair the pathnames pointed to by argv; argc indicates
 * how many there are.
 * Place a pointer to the RCS pathname into RCSname,
 * and a pointer to the pathname of the working file into workname.
 * If both are given, and workstdout
 * is set, a warning is printed.
 *
 * If the RCS file exists, places its status into RCSstat.
 *
 * If the RCS file exists, it is RCSOPENed for reading, the file pointer
 * is placed into finptr, and the admin-node is read in; returns 1.
 * If the RCS file does not exist and MUSTREAD,
 * print an error unless QUIET and return 0.
 * Otherwise, initialize the admin node and return -1.
 *
 * 0 is returned on all errors, e.g. files that are not regular files.
 */
{
  static struct buf tempbuf;

  register char *p, *arg, *RCS1;
  char const *base, *RCSbase, *x;
  int paired;
  size_t arglen, dlen, baselen, xlen;

  fdlock = -1;

  if (!(arg = *argv))
    return 0;                   /* already paired pathname */
  if (*arg == '-')
    {
      error ("%s option is ignored after pathnames", arg);
      return 0;
    }

  base = basefilename (arg);
  paired = false;

  /* first check suffix to see whether it is an RCS file or not */
  if ((x = rcssuffix (arg)))
    {
      /* RCS pathname given */
      RCS1 = arg;
      RCSbase = base;
      baselen = x - base;
      if (1 < argc &&
          !rcssuffix (workname = p = argv[1]) &&
          baselen <= (arglen = (size_t) strlen (p)) &&
          ((p += arglen - baselen) == workname || isSLASH (p[-1])) &&
          memcmp (base, p, baselen) == 0)
        {
          argv[1] = NULL;
          paired = true;
        }
      else
        {
          bufscpy (&tempbuf, base);
          workname = p = tempbuf.string;
          p[baselen] = 0;
        }
    }
  else
    {
      /* working file given; now try to find RCS file */
      workname = arg;
      baselen = strlen (base);
      /* Derive RCS pathname.  */
      if (1 < argc &&
          (x = rcssuffix (RCS1 = argv[1])) &&
          RCS1 + baselen <= x &&
          ((RCSbase = x - baselen) == RCS1 || isSLASH (RCSbase[-1])) &&
          memcmp (base, RCSbase, baselen) == 0)
        {
          argv[1] = NULL;
          paired = true;
        }
      else
        RCSbase = RCS1 = NULL;
    }
  /* Now we have a (tentative) RCS pathname in RCS1 and workname.  */
  /* Second, try to find the right RCS file */
  if (RCSbase != RCS1)
    {
      /* a path for RCSfile is given; single RCS file to look for */
      bufscpy (&RCSbuf, RCS1);
      finptr = (*rcsopen) (&RCSbuf, &RCSstat, mustread);
      RCSerrno = errno;
    }
  else
    {
      bufscpy (&RCSbuf, "");
      if (RCS1)
        /* RCS filename was given without path.  */
        fin2open (arg, (size_t) 0, RCSbase, baselen,
                  x, strlen (x), rcsopen, mustread);
      else
        {
          /* No RCS pathname was given.  */
          /* Try each suffix in turn.  */
          dlen = base - arg;
          x = suffixes;
          while (!fin2open (arg, dlen, base, baselen,
                            x, xlen = suffixlen (x), rcsopen, mustread))
            {
              x += xlen;
              if (!*x++)
                break;
            }
        }
    }
  RCSname = p = RCSbuf.string;
  if (finptr)
    {
      if (!S_ISREG (RCSstat.st_mode))
        {
          error ("%s isn't a regular file -- ignored", p);
          return 0;
        }
      Lexinit ();
      getadmin ();
    }
  else
    {
      if (RCSerrno != ENOENT || mustread || fdlock < 0)
        {
          if (RCSerrno == EEXIST)
            error ("RCS file %s is in use", p);
          else if (!quiet || RCSerrno != ENOENT)
            enerror (RCSerrno, p);
          return 0;
        }
      InitAdmin ();
    };

  if (paired && workstdout)
    workwarn ("Working file ignored due to -p option");

  prevkeys = false;
  return finptr ? 1 : -1;
}

char const *
getfullRCSname (void)
/*
 * Return a pointer to the full pathname of the RCS file.
 * Remove leading `./'.
 */
{
  if (ROOTPATH (RCSname))
    {
      return RCSname;
    }
  else
    {
      static struct buf rcsbuf;
#	    if needs_getabsname
      bufalloc (&rcsbuf, SIZEABLE_PATH + 1);
      while (getabsname (RCSname, rcsbuf.string, rcsbuf.size) != 0)
        if (errno == ERANGE)
          bufalloc (&rcsbuf, rcsbuf.size << 1);
        else
          efaterror ("getabsname");
#	    else
      static char const *wdptr;
      static struct buf wdbuf;
      static size_t wdlen;

      register char const *r;
      register size_t dlen;
      register char *d;
      register char const *wd;

      if (!(wd = wdptr))
        {
          /* Get working directory for the first time.  */
          char *PWD = cgetenv ("PWD");
          struct stat PWDstat, dotstat;
          if (!((d = PWD) &&
                ROOTPATH (PWD) &&
                stat (PWD, &PWDstat) == 0 &&
                stat (".", &dotstat) == 0 && same_file (PWDstat, dotstat, 1)))
            {
              bufalloc (&wdbuf, SIZEABLE_PATH + 1);
#			if defined HAVE_GETCWD || !defined HAVE_GETWD
              while (!(d = getcwd (wdbuf.string, wdbuf.size)))
                if (errno == ERANGE)
                  bufalloc (&wdbuf, wdbuf.size << 1);
                else if ((d = PWD))
                  break;
                else
                  efaterror ("getcwd");
#			else
              d = getwd (wdbuf.string);
              if (!d && !(d = PWD))
                efaterror ("getwd");
#			endif
            }
          wdlen = dir_useful_len (d);
          d[wdlen] = 0;
          wdptr = wd = d;
        }
      /*
       * Remove leading `./'s from RCSname.
       * Do not try to handle `../', since removing it may yield
       * the wrong answer in the presence of symbolic links.
       */
      for (r = RCSname; r[0] == '.' && isSLASH (r[1]); r += 2)
        /* `.////' is equivalent to `./'.  */
        while (isSLASH (r[2]))
          r++;
      /* Build full pathname.  */
      dlen = wdlen;
      bufalloc (&rcsbuf, dlen + strlen (r) + 2);
      d = rcsbuf.string;
      memcpy (d, wd, dlen);
      d += dlen;
      *d++ = SLASH;
      strcpy (d, r);
#	    endif
      return rcsbuf.string;
    }
}

static size_t
dir_useful_len (char const *d)
/*
* D names a directory; yield the number of characters of D's useful part.
* To create a file in D, append a SLASH and a file name to D's useful part.
* Ignore trailing slashes if possible; not only are they ugly,
* but some non-Posix systems misbehave unless the slashes are omitted.
*/
{
  size_t dlen = strlen (d);
  if (!SLASHSLASH_IS_SLASH && dlen == 2 && isSLASH (d[0]) && isSLASH (d[1]))
    --dlen;
  else
    while (dlen && isSLASH (d[dlen - 1]))
      --dlen;
  return dlen;
}

int
isSLASH (int c)
{
#if !WOE
  return (SLASH == c);
#else
  switch (c)
    {
    case SLASHes:
      return true;
    default:
      return false;
    }
#endif
}

#if !defined HAVE_GETWD && !defined HAVE_GETWD

char *
getcwd (char *path, size_t size)
{
  static char const usrbinpwd[] = "/usr/bin/pwd";
#	define binpwd (usrbinpwd+4)

  register FILE *fp;
  register int c;
  register char *p, *lim;
  int closeerrno, closeerror, e, fd[2], readerror, toolong, wstatus;
  pid_t child;

  if (!size)
    {
      errno = EINVAL;
      return NULL;
    }
  if (pipe (fd) != 0)
    return NULL;
#	if BAD_WAIT_IF_SIGCHLD_IGNORED
#		ifndef SIGCHLD
#		define SIGCHLD SIGCLD
#		endif
  signal (SIGCHLD, SIG_DFL);
#	endif
  if (!(child = vfork ()))
    {
      if (close (fd[0]) == 0 && (fd[1] == STDOUT_FILENO ||
#				ifdef F_DUPFD
                                 (close (STDOUT_FILENO),
                                  fcntl (fd[1], F_DUPFD, STDOUT_FILENO))
#				else
                                 dup2 (fd[1], STDOUT_FILENO)
#				endif
                                 == STDOUT_FILENO && close (fd[1]) == 0))
        {
          close (STDERR_FILENO);
          execl (binpwd, binpwd, NULL);
          execl (usrbinpwd, usrbinpwd, NULL);
        }
      _exit (EXIT_FAILURE);
    }
  e = errno;
  closeerror = close (fd[1]);
  closeerrno = errno;
  fp = NULL;
  readerror = toolong = wstatus = 0;
  p = path;
  if (0 <= child)
    {
      fp = fdopen (fd[0], "r");
      e = errno;
      if (fp)
        {
          lim = p + size;
          for (p = path;; *p++ = c)
            {
              if ((c = getc (fp)) < 0)
                {
                  if (feof (fp))
                    break;
                  if (ferror (fp))
                    {
                      readerror = 1;
                      e = errno;
                      break;
                    }
                }
              if (p == lim)
                {
                  toolong = 1;
                  break;
                }
            }
        }
#		if defined HAVE_WAITPID
      if (waitpid (child, &wstatus, 0) < 0)
        wstatus = 1;
#		else
      {
        pid_t w;
        do
          {
            if ((w = wait (&wstatus)) < 0)
              {
                wstatus = 1;
                break;
              }
          }
        while (w != child);
      }
#		endif
    }
  if (!fp)
    {
      close (fd[0]);
      errno = e;
      return NULL;
    }
  if (fclose (fp) != 0)
    return NULL;
  if (readerror)
    {
      errno = e;
      return NULL;
    }
  if (closeerror)
    {
      errno = closeerrno;
      return NULL;
    }
  if (toolong)
    {
      errno = ERANGE;
      return NULL;
    }
  if (wstatus || p == path || *--p != '\n')
    {
      errno = EACCES;
      return NULL;
    }
  *p = '\0';
  return path;
}
#endif

#ifdef PAIRTEST
/* test program for pairnames() and getfullRCSname() */

char const cmdid[] = "pair";

int
main (int argc, char *argv[])
{
  int result;
  int initflag;
  quietflag = initflag = false;

  while (--argc, ++argv, argc >= 1 && ((*argv)[0] == '-'))
    {
      switch ((*argv)[1])
        {

        case 'p':
          workstdout = stdout;
          break;
        case 'i':
          initflag = true;
          break;
        case 'q':
          quietflag = true;
          break;
        default:
          error ("unknown option: %s", *argv);
          break;
        }
    }

  do
    {
      RCSname = workname = NULL;
      result = pairnames (argc, argv, rcsreadopen, !initflag, quietflag);
      if (result != 0)
        {
          diagnose
            ("RCS pathname: %s; working pathname: %s\nFull RCS pathname: %s\n",
             RCSname, workname, getfullRCSname ());
        }
      switch (result)
        {
        case 0:
          continue;             /* already paired file */

        case 1:
          if (initflag)
            {
              rcserror ("already exists");
            }
          else
            {
              diagnose ("RCS file %s exists\n", RCSname);
            }
          Ifclose (finptr);
          break;

        case -1:
          diagnose ("RCS file doesn't exist\n");
          break;
        }

    }
  while (++argv, --argc >= 1);

}

void
exiterr (void)
{
  dirtempunlink ();
  tempunlink ();
  _exit (EXIT_FAILURE);
}
#endif
