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

#include "base.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>                   /* gettimeofday */
#include "b-complain.h"

struct manifestation manifestation;
struct repository repository;

static const char const rcsdir[] = "RCS";
#define rcslen  (sizeof rcsdir - 1)

static struct buf RCSbuf, RCSb;
static int RCSerrno;

/* Temp names to be unlinked when done, if they are not 0.
   Must be at least `DIRTEMPNAMES' (see rcsedit.c).  */
#define TEMPNAMES 5
static char *volatile tpnames[TEMPNAMES];

struct compair
{
  char const *suffix, *comlead;
};

/* This table is present only for backwards compatibility.  Normally we
   ignore this table, and use the prefix of the `$Log' line instead.  */
static const struct compair const comtable[] = {
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
static int
homegrown_mkstemp (char *template)
/* Like mkstemp(2), but never return EINVAL.  That is, never check for
   missing "XXXXXX" since we know the unique caller DTRT.  */
{
  int pid = getpid ();
  char *end = template + strlen (template);
  char const xrep[] = {"abcdefghijklmnopqrstuvwxyz"
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                       /* Omit '0' => `strlen (xrep)' is prime.  */
                       "123456789"};
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
      for (char *w = end - 6; n && w < end; w++)
        {
          *w = xrep[n % 61];
          n = n / 61;
        }
      fd = open (template, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
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
    PFATAL ("could not make temporary file name (template \"%s\")",
            template.string);

  bufscpy (filename, template.string);
  bufautoend (&template);
  close (fd);
}

char const *
maketemp (int n)
/* Create a unique pathname using n and the process id and store it into the
   `n'th slot in `tpnames'.  Because of storage in `tpnames', `tempunlink'
   can unlink the file later.  Return a pointer to the pathname created.  */
{
  struct buf rv;
  char *t = tpnames[n];

  if (t)
    return t;

  catchints ();
  bufautobegin (&rv);
  set_temporary_file_name (&rv, NULL);
  t = testalloc (rv.size);
  strncpy (t, rv.string, rv.size);
  bufautoend (&rv);
  tpnames[n] = t;
  return t;
}

void
tempunlink (void)
/* Clean up `maketemp' files.  May be invoked by signal handler.  */
{
  register int i;
  register char *p;

  for (i = TEMPNAMES; 0 <= --i;)
    if ((p = tpnames[i]))
      {
        unlink (p);
        /* We would tfree(p) here, but this might dump core if we're handling
           a signal.  We're about to exit anyway, so we won't bother.  */
        tpnames[i] = NULL;
      }
}

static char const *
bindex (register char const *sp, register int c)
/* Find the last occurrence of character `c' in string `sp' and return a
   pointer to the character just beyond it.  If the character doesn't occur
   in the string, return `sp'.  */
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

static bool
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
/* Initialize an admin node.  */
{
  register char const *Suffix;
  register int i;

  ADMIN (head) = NULL;
  ADMIN (defbr) = NULL;
  ADMIN (allowed) = NULL;
  ADMIN (assocs) = NULL;
  ADMIN (locks) = NULL;
  BE (strictly_locking) = STRICT_LOCKING;

  /* Guess the comment leader from the suffix.  */
  Suffix = bindex (MANI (filename), '.');
  if (Suffix == MANI (filename))
    /* Empty suffix; will get default.  */
    Suffix = "";
  for (i = 0; !suffix_matches (Suffix, comtable[i].suffix); i++)
    continue;
  ADMIN (log_lead).string = comtable[i].comlead;
  ADMIN (log_lead).size = strlen (comtable[i].comlead);
  BE (kws) = kwsub_kv;
  clear_buf (&ADMIN (description));
  /* Note: If `!FLOW (from)', read nothing; only initialize.  */
  Lexinit ();
}

void
bufalloc (register struct buf *b, size_t size)
/* Ensure `*b' is a name buffer of at least `size' bytes.
   Old contents of `*b' can be freed; new contents are undefined.  */
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
/* Like `bufalloc', except preserve old contents of `*b', if any.  */
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
/* Free an auto buffer at block exit.  */
{
  if (b->size)
    tfree (b->string);
}

struct cbuf
bufremember (struct buf *b, size_t s)
/* Free the buffer `b' with used size `s'.
   Return a cbuf with identical contents.
   The cbuf will be reclaimed when this input file is finished.  */
{
  struct cbuf cb;

  if ((cb.size = s))
    cb.string = fremember (trealloc (char, b->string, s));
  else
    {
      /* Abstraction violation; not really auto.  */
      bufautoend (b);
      cb.string = "";
    }
  return cb;
}

char *
bufenlarge (register struct buf *b, char const **alim)
/* Make `*b' larger.  Set `*alim' to its new limit,
   and return the relocated value of its old limit.  */
{
  size_t s = b->size;

  bufrealloc (b, s + 1);
  *alim = b->string + b->size;
  return b->string + s;
}

void
bufscat (struct buf *b, char const *s)
/* Concatenate `s' to the end of `b'.  */
{
  size_t blen = b->string ? strlen (b->string) : 0;
  size_t ssiz = strlen (s) + 1;

  bufrealloc (b, blen + ssiz);
  strncpy (b->string + blen, s, ssiz);
}

void
bufscpy (struct buf *b, char const *s)
/* Copy `s' into `b'.  */
{
  size_t ssiz = strlen (s) + 1;

  bufalloc (b, ssiz);
  strncpy (b->string, s, ssiz);
}

char const *
basefilename (char const *p)
/* Return the address of the base filename of the pathname `p'.  */
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
/* Return the length of `x', an RCS pathname suffix.  */
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
/* Return the suffix of `name' if it is an RCS pathname, NULL otherwise.  */
{
  char const *x, *p, *nz;
  size_t nl, xl;

  nl = strlen (name);
  nz = name + nl;
  x = BE (pe);
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
rcsreadopen (struct buf *RCSpath, struct stat *status, bool mustread RCS_UNUSED)
/* Open `RCSpath' for reading and return its `FILE*' descriptor.
   If successful, set `*status' to its status.
   Pass this routine to `pairnames' for read-only access to the file.  */
{
  return Iopen (RCSpath->string, FOPEN_RB, status);
}

static bool
finopen (RILE *(*rcsopen) (struct buf *, struct stat *, bool), bool mustread)
/* Use `rcsopen' to open an RCS file; `mustread' is set if the file must be
   read.  Set `FLOW (from)' to the result and return true if successful.
   `RCSb' holds the file's name.  Set `RCSbuf' to the best RCS name found
   so far, and `RCSerrno' to its errno.  Return true if successful or if
   an unusual failure.  */
{
  bool interesting, preferold;

  /* We prefer an old name to that of a nonexisting new RCS file,
     unless we tried locking the old name and failed.  */
  preferold = RCSbuf.string[0] && (mustread || 0 <= REPO (fd_lock));

  FLOW (from) = (*rcsopen) (&RCSb, &REPO (stat), mustread);
  interesting = FLOW (from) || errno != ENOENT;
  if (interesting || !preferold)
    {
      /* Use the new name.  */
      RCSerrno = errno;
      bufscpy (&RCSbuf, RCSb.string);
    }
  return interesting;
}

static bool
fin2open (char const *d, size_t dlen,
          char const *base, size_t baselen,
          char const *x, size_t xlen,
          RILE *(*rcsopen) (struct buf *, struct stat *, bool),
          bool mustread)
/* `d' is a directory name with length `dlen' (including trailing slash).
   `base' is a filename with length `baselen'.  `x' is an RCS pathname suffix
   with length `xlen'.  Use `rcsopen' to open an RCS file; `mustread' is set
   if the file must be read.  Return true if successful.  Try "dRCS/basex"
   first; if that fails and x is nonempty, try "dbasex".  Put these potential
   names in `RCSb'.  Set `RCSbuf' to the best RCS name found so far, and
   `RCSerrno' to its errno.  Return true if successful or if an unusual
   failure.  */
{
  register char *p;

  bufalloc (&RCSb, dlen + rcslen + 1 + baselen + xlen + 1);

  /* Try "dRCS/basex".  */
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

      /* Try "dbasex".  Start from scratch, because
         `finopen' may have changed `RCSb'.  */
      memcpy (p = RCSb.string, d, dlen);
      memcpy (p += dlen, base, baselen);
      memcpy (p += baselen, x, xlen);
      p[xlen] = 0;
    }
  return finopen (rcsopen, mustread);
}

int
pairnames (int argc, char **argv, open_rcsfile_fn_t *rcsopen,
           bool mustread, bool quiet)
/* Pair the pathnames pointed to by `argv'; `argc' indicates how many there
   are.  Place a pointer to the RCS pathname into `REPO (filename)', and a
   pointer to the pathname of the working file into `MANI (filename)'.  If
   both are given, and `MANI (standard_output)' is set, a print a warning.

   If the RCS file exists, place its status into `REPO (stat)'.

   If the RCS file exists, open (using `rcsopen') it for reading, place the
   file pointer into `FLOW (from)', read in the admin-node, and return 1.
   If the RCS file does not exist and `mustread', print an error unless
   `quiet' and return 0.  Otherwise, initialize the admin node and return -1.

   Return 0 on all errors, e.g. files that are not regular files.  */
{
  static struct buf tempbuf;

  register char *p, *arg, *RCS1;
  char const *base, *RCSbase, *x;
  bool paired;
  size_t arglen, dlen, baselen, xlen;

  REPO (fd_lock) = -1;

  if (!(arg = *argv))
    return 0;                   /* already paired pathname */
  if (*arg == '-')
    {
      PERR ("%s option is ignored after pathnames", arg);
      return 0;
    }

  base = basefilename (arg);
  paired = false;

  /* First check suffix to see whether it is an RCS file or not.  */
  if ((x = rcssuffix (arg)))
    {
      /* RCS pathname given.  */
      RCS1 = arg;
      RCSbase = base;
      baselen = x - base;
      if (1 < argc
          && !rcssuffix (MANI (filename) = p = argv[1])
          && baselen <= (arglen = (size_t) strlen (p))
          && ((p += arglen - baselen) == MANI (filename) || isSLASH (p[-1]))
          && memcmp (base, p, baselen) == 0)
        {
          argv[1] = NULL;
          paired = true;
        }
      else
        {
          bufscpy (&tempbuf, base);
          MANI (filename) = p = tempbuf.string;
          p[baselen] = 0;
        }
    }
  else
    {
      /* Working file given; now try to find RCS file.  */
      MANI (filename) = arg;
      baselen = strlen (base);
      /* Derive RCS pathname.  */
      if (1 < argc
          && (x = rcssuffix (RCS1 = argv[1]))
          && RCS1 + baselen <= x
          && ((RCSbase = x - baselen) == RCS1 || isSLASH (RCSbase[-1]))
          && memcmp (base, RCSbase, baselen) == 0)
        {
          argv[1] = NULL;
          paired = true;
        }
      else
        RCSbase = RCS1 = NULL;
    }
  /* Now we have a (tentative) RCS pathname in RCS1 and `MANI (filename)'.
     Second, try to find the right RCS file.  */
  if (RCSbase != RCS1)
    {
      /* A path for RCSfile is given; single RCS file to look for.  */
      bufscpy (&RCSbuf, RCS1);
      FLOW (from) = (*rcsopen) (&RCSbuf, &REPO (stat), mustread);
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
          /* No RCS pathname was given.
             Try each suffix in turn.  */
          dlen = base - arg;
          x = BE (pe);
          while (!fin2open (arg, dlen, base, baselen,
                            x, xlen = suffixlen (x), rcsopen, mustread))
            {
              x += xlen;
              if (!*x++)
                break;
            }
        }
    }
  REPO (filename) = p = RCSbuf.string;
  if (FLOW (from))
    {
      if (!S_ISREG (REPO (stat).st_mode))
        {
          PERR ("%s isn't a regular file -- ignored", p);
          return 0;
        }
      Lexinit ();
      getadmin ();
    }
  else
    {
      if (RCSerrno != ENOENT || mustread || REPO (fd_lock) < 0)
        {
          if (RCSerrno == EEXIST)
            PERR ("RCS file %s is in use", p);
          else if (!quiet || RCSerrno != ENOENT)
            syserror (RCSerrno, p);
          return 0;
        }
      InitAdmin ();
    };

  if (paired && MANI (standard_output))
    MWARN ("Working file ignored due to -p option");

  PREV (valid) = false;
  return FLOW (from) ? 1 : -1;
}

static size_t
dir_useful_len (char const *d)
/* `d' names a directory; return the number of bytes of its useful part.  To
   create a file in `d', append a `SLASH' and a file name to the useful part.
   Ignore trailing slashes if possible; not only are they ugly, but some
   non-POSIX systems misbehave unless the slashes are omitted.  */
{
  size_t dlen = strlen (d);

  if (!SLASHSLASH_IS_SLASH && dlen == 2
      && isSLASH (d[0])
      && isSLASH (d[1]))
    --dlen;
  else
    while (dlen && isSLASH (d[dlen - 1]))
      --dlen;
  return dlen;
}

char const *
getfullRCSname (void)
/* Return a pointer to the full pathname of the RCS file.
   Remove leading `./'.  */
{
  if (ROOTPATH (REPO (filename)))
    {
      return REPO (filename);
    }
  else
    {
      static struct buf rcsbuf;
#if needs_getabsname

      bufalloc (&rcsbuf, SIZEABLE_PATH + 1);
      while (getabsname (REPO (filename), rcsbuf.string, rcsbuf.size) != 0)
        if (errno == ERANGE)
          bufalloc (&rcsbuf, rcsbuf.size << 1);
        else
          fatal_sys ("getabsname");
#else  /* !needs_getabsname */
      static char const *wdptr;
      static struct buf wdbuf;
      static size_t wdlen, rsiz;

      register char const *r;
      register size_t dlen;
      register char *d;
      register char const *wd;

      if (!(wd = wdptr))
        {
          /* Get working directory for the first time.  */
          char *PWD = cgetenv ("PWD");
          struct stat PWDstat, dotstat;

          if (!((d = PWD)
                && ROOTPATH (PWD)
                && stat (PWD, &PWDstat) == 0
                && stat (".", &dotstat) == 0
                && same_file (PWDstat, dotstat)))
            {
              bufalloc (&wdbuf, SIZEABLE_PATH + 1);
#if defined HAVE_GETCWD || !defined HAVE_GETWD
              while (!(d = getcwd (wdbuf.string, wdbuf.size)))
                if (errno == ERANGE)
                  bufalloc (&wdbuf, wdbuf.size << 1);
                else if ((d = PWD))
                  break;
                else
                  fatal_sys ("getcwd");
#else  /* !(defined HAVE_GETCWD || !defined HAVE_GETWD) */
              d = getwd (wdbuf.string);
              if (!d && !(d = PWD))
                fatal_sys ("getwd");
#endif  /* !(defined HAVE_GETCWD || !defined HAVE_GETWD) */
            }
          wdlen = dir_useful_len (d);
          d[wdlen] = 0;
          wdptr = wd = d;
        }
      /* Remove leading `./'s from `REPO (filename)'.
         Do not try to handle `../', since removing it may result
         in the wrong answer in the presence of symbolic links.  */
      for (r = REPO (filename); r[0] == '.' && isSLASH (r[1]); r += 2)
        /* `.////' is equivalent to `./'.  */
        while (isSLASH (r[2]))
          r++;
      /* Build full pathname.  */
      dlen = wdlen;
      rsiz = strlen (r) + 1;
      bufalloc (&rcsbuf, dlen + rsiz + 1);
      d = rcsbuf.string;
      memcpy (d, wd, dlen);
      d += dlen;
      *d++ = SLASH;
      strncpy (d, r, rsiz);
#endif  /* !needs_getabsname */
      return rcsbuf.string;
    }
}

bool
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

#if !defined HAVE_GETCWD && !defined HAVE_GETWD
char *
getcwd (char *path, size_t size)
{
  static char const usrbinpwd[] = "/usr/bin/pwd";
#define binpwd (usrbinpwd+4)

  register FILE *fp;
  register int c;
  register char *p, *lim;
  int closeerrno, closeerror, e, fd[2], wstatus;
  bool readerror, toolong;
  pid_t child;

  if (!size)
    {
      errno = EINVAL;
      return NULL;
    }
  if (pipe (fd) != 0)
    return NULL;
#if BAD_WAIT_IF_SIGCHLD_IGNORED
#ifndef SIGCHLD
#define SIGCHLD SIGCLD
#endif
  signal (SIGCHLD, SIG_DFL);
#endif  /* BAD_WAIT_IF_SIGCHLD_IGNORED */
  if (!(child = vfork ()))
    {
      if (close (fd[0]) == 0 && (fd[1] == STDOUT_FILENO
                                 ||
#ifdef F_DUPFD
                                 (close (STDOUT_FILENO),
                                  fcntl (fd[1], F_DUPFD, STDOUT_FILENO))
#else
                                 dup2 (fd[1], STDOUT_FILENO)
#endif
                                 == STDOUT_FILENO && close (fd[1]) == 0))
        {
          close (STDERR_FILENO);
          execl (binpwd, binpwd, NULL);
          execl (usrbinpwd, usrbinpwd, NULL);
        }
      _Exit (EXIT_FAILURE);
    }
  e = errno;
  closeerror = close (fd[1]);
  closeerrno = errno;
  fp = NULL;
  wstatus = 0;
  readerror = toolong = false;
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
                      readerror = true;
                      e = errno;
                      break;
                    }
                }
              if (p == lim)
                {
                  toolong = true;
                  break;
                }
            }
        }
#if defined HAVE_WAITPID
      if (waitpid (child, &wstatus, 0) < 0)
        wstatus = 1;
#else  /* !defined HAVE_WAITPID */
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
#endif  /* !defined HAVE_WAITPID */
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
#endif  /* !defined HAVE_GETCWD && !defined HAVE_GETWD */

#ifdef PAIRTEST
/* This is a test program for `pairnames' and `getfullRCSname'.  */

static exiting void
exiterr (void)
{
  dirtempunlink ();
  tempunlink ();
  _Exit (EXIT_FAILURE);
}
const struct program program =
  {
    .name = "pairtest",
    .exiterr = exiterr
  };

int
main (int argc, char *argv[])
{
  int result;
  bool initflag;

  BE (quiet) = initflag = false;

  while (--argc, ++argv, argc >= 1 && ((*argv)[0] == '-'))
    {
      switch ((*argv)[1])
        {

        case 'p':
          MANI (standard_output) = stdout;
          break;
        case 'i':
          initflag = true;
          break;
        case 'q':
          BE (quiet) = true;
          break;
        default:
          PERR ("unknown option: %s", *argv);
          break;
        }
    }

  do
    {
      REPO (filename) = MANI (filename) = NULL;
      result = pairnames (argc, argv, rcsreadopen, !initflag, BE (quiet));
      if (result != 0)
        {
          diagnose
            ("RCS pathname: %s; working pathname: %s\nFull RCS pathname: %s",
             REPO (filename), MANI (filename), getfullRCSname ());
        }
      switch (result)
        {
        case 0:
          continue;             /* already paired file */

        case 1:
          if (initflag)
            {
              RERR ("already exists");
            }
          else
            {
              diagnose ("RCS file %s exists", REPO (filename));
            }
          Ifclose (FLOW (from));
          break;

        case -1:
          diagnose ("RCS file doesn't exist");
          break;
        }

    }
  while (++argv, --argc >= 1);

}

#endif  /* PAIRTEST */

/* rcsfnms.c ends here */
