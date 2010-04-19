/* RCS filename handling

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
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include "same-inode.h"
#include "b-complain.h"
#include "b-divvy.h"
#include "b-feph.h"
#include "b-fro.h"

static const char const rcsdir[] = "RCS";
#define rcslen  (sizeof rcsdir - 1)

struct compair
{
  char const *suffix, *comlead;
};

/* This table is present only for backwards compatibility.  Normally we
   ignore this table, and use the prefix of the ‘$Log’ line instead.  */
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

static char const *
bindex (register char const *sp, register int c)
/* Find the last occurrence of character ‘c’ in string ‘sp’ and return a
   pointer to the character just beyond it.  If the character doesn't occur
   in the string, return ‘sp’.  */
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
  /* Note: If ‘!FLOW (from)’, read nothing; only initialize.  */
  Lexinit ();
}

void
bufalloc (register struct buf *b, size_t size)
/* Ensure ‘*b’ is a name buffer of at least ‘size’ bytes.
   Old contents of ‘*b’ can be freed; new contents are undefined.  */
{
  if (b->size < size)
    {
      if (b->size)
        tfree (b->string);
      else
        b->size = sizeof (void *);
      while (b->size < size)
        b->size <<= 1;
      b->string = testalloc (b->size);
    }
}

static void
bufrealloc (register struct buf *b, size_t size)
/* Like ‘bufalloc’, except preserve old contents of ‘*b’, if any.  */
{
  if (b->size < size)
    {
      if (!b->size)
        bufalloc (b, size);
      else
        {
          while ((b->size <<= 1) < size)
            continue;
          b->string = testrealloc (b->string, b->size);
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

char *
bufenlarge (register struct buf *b, char const **alim)
/* Make ‘*b’ larger.  Set ‘*alim’ to its new limit,
   and return the relocated value of its old limit.  */
{
  size_t s = b->size;

  bufrealloc (b, s + 1);
  *alim = b->string + b->size;
  return b->string + s;
}

void
bufscat (struct buf *b, char const *s)
/* Concatenate ‘s’ to the end of ‘b’.  */
{
  size_t blen = b->string ? strlen (b->string) : 0;
  size_t ssiz = strlen (s) + 1;

  bufrealloc (b, blen + ssiz);
  strncpy (b->string + blen, s, ssiz);
}

void
bufscpy (struct buf *b, char const *s)
/* Copy ‘s’ into ‘b’.  */
{
  size_t ssiz = strlen (s) + 1;

  bufalloc (b, ssiz);
  strncpy (b->string, s, ssiz);
}

char const *
basefilename (char const *p)
/* Return the address of the base filename of the filename ‘p’.  */
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
/* Return the length of ‘x’, an RCS filename suffix.  */
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
/* Return the suffix of ‘name’ if it is an RCS filename, NULL otherwise.  */
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

struct fro *
rcsreadopen (struct buf *filename, struct stat *status, bool mustread RCS_UNUSED)
/* Open ‘filename’ for reading and return its ‘FILE*’ descriptor.
   If successful, set ‘*status’ to its status.
   Pass this routine to ‘pairnames’ for read-only access to the file.  */
{
  return fro_open (filename->string, FOPEN_RB, status);
}

/* A combination of probe parameters and results for ‘pairnames’ through
   ‘fin2open’ through ‘finopen’ through {‘rcsreadopen’, ‘rcswriteopen’}.
   The probe protocol is to set ‘open’ and ‘mustread’ once, and try various
   permutations of basename, directory and extension (-x) in ‘filename’,
   finally recording ‘errno’ in ‘eno’ and the "best RCS filename found"
   in ‘bestfit’ (otherwise failing).  */
struct maybe
{
  /* Input parameters, constant.  */
  open_rcsfile_fn_t *open;
  bool mustread;

  /* Input parameter, varying.  */
  struct buf filename;

  /* Output parameters.  */
  struct buf bestfit;
  int eno;
};

static bool
finopen (struct maybe *m)
/* Use ‘m->open’ to open an RCS file; ‘m->mustread’ is set if the file must be
   read.  Set ‘FLOW (from)’ to the result and return true if successful.
   ‘m->filename’ holds the file's name.  Set ‘m->bestfit’ to the best RCS name
   found so far, and ‘m->eno’ to its errno.  Return true if successful or if
   an unusual failure.  */
{
  bool interesting, preferold;

  /* We prefer an old name to that of a nonexisting new RCS file,
     unless we tried locking the old name and failed.  */
  preferold = m->bestfit.string[0] && (m->mustread || 0 <= REPO (fd_lock));

  FLOW (from) = (m->open) (&m->filename, &REPO (stat), m->mustread);
  interesting = FLOW (from) || errno != ENOENT;
  if (interesting || !preferold)
    {
      /* Use the new name.  */
      m->eno = errno;
      bufscpy (&m->bestfit, m->filename.string);
    }
  return interesting;
}

static bool
fin2open (char const *d, size_t dlen,
          char const *base, size_t baselen,
          char const *x, size_t xlen,
          struct maybe *m)
/* ‘d’ is a directory name with length ‘dlen’ (including trailing slash).
   ‘base’ is a filename with length ‘baselen’.
   ‘x’ is an RCS filename suffix with length ‘xlen’.
   Use ‘m->open’ to open an RCS file; ‘m->mustread’ is set if the file
   must be read.  Return true if successful.  Try "dRCS/basex" first; if
   that fails and x is nonempty, try "dbasex".  Put these potential
   names in ‘m->filename’ for ‘finopen’ to wrangle.  */
{
  register char *p;

  bufalloc (&m->filename, dlen + rcslen + 1 + baselen + xlen + 1);

  /* Try "dRCS/basex".  */
  memcpy (p = m->filename.string, d, dlen);
  memcpy (p += dlen, rcsdir, rcslen);
  p += rcslen;
  *p++ = SLASH;
  memcpy (p, base, baselen);
  memcpy (p += baselen, x, xlen);
  p[xlen] = '\0';
  if (xlen)
    {
      if (finopen (m))
        return true;

      /* Try "dbasex".  Start from scratch, because
         ‘finopen’ may have changed ‘m->filename’.  */
      memcpy (p = m->filename.string, d, dlen);
      memcpy (p += dlen, base, baselen);
      memcpy (p += baselen, x, xlen);
      p[xlen] = '\0';
    }
  return finopen (m);
}

int
pairnames (int argc, char **argv, open_rcsfile_fn_t *rcsopen,
           bool mustread, bool quiet)
/* Pair the filenames pointed to by ‘argv’; ‘argc’ indicates how many there
   are.  Place a pointer to the RCS filename into ‘REPO (filename)’, and a
   pointer to the filename of the working file into ‘MANI (filename)’.  If
   both are given, and ‘MANI (standard_output)’ is set, display a warning.

   If the RCS file exists, place its status into ‘REPO (stat)’, open it for
   reading (using ‘rcsopen’), place the file pointer into ‘FLOW (from)’, read
   in the admin-node, and return 1.

   If the RCS file does not exist and ‘mustread’, display an error unless
   ‘quiet’ and return 0.  Otherwise, initialize the admin node and return -1.

   Return 0 on all errors, e.g. files that are not regular files.  */
{
  register char *p, *arg, *RCS1;
  char const *base, *RCSbase, *x;
  bool paired;
  size_t arglen, dlen, baselen, xlen;
  struct maybe maybe =
    {
      /* ‘.filename’ initialized by ‘fin2open’.  */
      .open = rcsopen,
      .mustread = mustread
    };

  REPO (fd_lock) = -1;

  if (!(arg = *argv))
    return 0;                   /* already paired filename */
  if (*arg == '-')
    {
      PERR ("%s option is ignored after filenames", arg);
      return 0;
    }

  base = basefilename (arg);
  paired = false;

  /* First check suffix to see whether it is an RCS file or not.  */
  if ((x = rcssuffix (arg)))
    {
      /* RCS filename given.  */
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
          MANI (filename) = intern (SINGLE, base, baselen + 1);
          MANI (filename)[baselen] = '\0';
        }
    }
  else
    {
      /* Working file given; now try to find RCS file.  */
      MANI (filename) = arg;
      baselen = strlen (base);
      /* Derive RCS filename.  */
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
  /* Now we have a (tentative) RCS filename in RCS1 and ‘MANI (filename)’.
     Second, try to find the right RCS file.  */
  bufautobegin (&maybe.bestfit);
  if (RCSbase != RCS1)
    {
      /* A filename is given; single RCS file to look for.  */
      bufscpy (&maybe.bestfit, RCS1);
      FLOW (from) = (*rcsopen) (&maybe.bestfit, &REPO (stat), mustread);
      maybe.eno = errno;
    }
  else
    {
      bufscpy (&maybe.bestfit, "");
      if (RCS1)
        /* RCS filename was given without a directory component.  */
        fin2open (arg, (size_t) 0, RCSbase, baselen,
                  x, strlen (x), &maybe);
      else
        {
          /* No RCS filename was given.
             Try each suffix in turn.  */
          dlen = base - arg;
          x = BE (pe);
          while (!fin2open (arg, dlen, base, baselen,
                            x, xlen = suffixlen (x), &maybe))
            {
              x += xlen;
              if (!*x++)
                break;
            }
        }
    }
  REPO (filename) = p = str_save (maybe.bestfit.string);
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
      if (maybe.eno != ENOENT || mustread || REPO (fd_lock) < 0)
        {
          if (maybe.eno == EEXIST)
            PERR ("RCS file %s is in use", p);
          else if (!quiet || maybe.eno != ENOENT)
            syserror (maybe.eno, p);
          return 0;
        }
      InitAdmin ();
    };

  if (paired && MANI (standard_output))
    MWARN ("Working file ignored due to -p option");

  PREV (valid) = false;
  bufautoend (&maybe.filename);
  bufautoend (&maybe.bestfit);
  return FLOW (from) ? 1 : -1;
}

#ifndef DOUBLE_SLASH_IS_DISTINCT_ROOT
#define DOUBLE_SLASH_IS_DISTINCT_ROOT 0
#endif

static size_t
dir_useful_len (char const *d)
/* ‘d’ names a directory; return the number of bytes of its useful part.  To
   create a file in ‘d’, append a ‘SLASH’ and a file name to the useful part.
   Ignore trailing slashes if possible; not only are they ugly, but some
   non-POSIX systems misbehave unless the slashes are omitted.  */
{
  size_t dlen = strlen (d);

  if (DOUBLE_SLASH_IS_DISTINCT_ROOT && dlen == 2
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
/* Return a pointer to the full filename of the RCS file.
   Remove leading ‘./’.  */
{
  if (ABSFNAME (REPO (filename)))
    {
      return REPO (filename);
    }
  else
    {
      register char const *r;
      char *rv;
      size_t len;

      if (!BE (cwd))
        {
          /* Get working directory for the first time.  */
          char *PWD = cgetenv ("PWD");
          struct stat PWDstat, dotstat;

          if (!((BE (cwd) = PWD)
                && ABSFNAME (PWD)
                && stat (PWD, &PWDstat) == 0
                && stat (".", &dotstat) == 0
                && SAME_INODE (PWDstat, dotstat)))
            {
              size_t sz = 64;

              while (!(BE (cwd) = alloc (SHARED, __func__, sz),
                       getcwd (BE (cwd), sz)))
                {
                  brush_off (SHARED, BE (cwd));
                  if (errno == ERANGE)
                    sz <<= 1;
                  else if ((BE (cwd) = PWD))
                    break;
                  else
                    fatal_sys ("getcwd");
                }
            }
          BE (cwd)[dir_useful_len (BE (cwd))] = '\0';
        }
      /* Remove leading ‘./’s from ‘REPO (filename)’.
         Do not try to handle ‘../’, since removing it may result
         in the wrong answer in the presence of symbolic links.  */
      for (r = REPO (filename); r[0] == '.' && isSLASH (r[1]); r += 2)
        /* ‘.////’ is equivalent to ‘./’.  */
        while (isSLASH (r[2]))
          r++;
      /* Build full filename.  */
      accumulate_nonzero_bytes (SINGLE, BE (cwd));
      accumulate_byte          (SINGLE, SLASH);
      accumulate_nonzero_bytes (SINGLE, r);
      rv = finish_string (SINGLE, &len);
      return rv;
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

#if !defined HAVE_GETCWD
char *
getcwd (char *buf, size_t size)
{
  const char const usrbinpwd[] = "/usr/bin/pwd";
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
      if (close (fd[0]) == 0
          && (STDOUT_FILENO == fd[1]
              || (STDOUT_FILENO == (close (STDOUT_FILENO),
                                    fcntl (fd[1], F_DUPFD, STDOUT_FILENO))
                  && close (fd[1]) == 0)))
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
  p = buf;
  if (0 <= child)
    {
      fp = fdopen (fd[0], "r");
      e = errno;
      if (fp)
        {
          lim = p + size;
          for (p = buf;; *p++ = c)
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
  if (wstatus || p == buf || *--p != '\n')
    {
      errno = EACCES;
      return NULL;
    }
  *p = '\0';
  return buf;
}
#endif  /* !defined HAVE_GETCWD */

#ifdef PAIRTEST
/* This is a test program for ‘pairnames’ and ‘getfullRCSname’.  */

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
          bad_option (*argv);
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
            ("RCS filename: %s; working filename: %s\nFull RCS filename: %s",
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
          fro_close (FLOW (from));
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
