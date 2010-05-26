/* RCS utility functions

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
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include "b-complain.h"
#include "b-divvy.h"
#include "b-excwho.h"
#include "b-fb.h"
#include "b-feph.h"
#include "b-isr.h"
#include "gnu-h-v.h"
#include "maketime.h"

void
gnurcs_init (struct program const *program)
{
  SHARED = make_space ("shared");
  SINGLE = make_space ("single");
  top = ZLLOC (1, struct top);
  unbuffer_standard_error ();
  top->program = program;
  ISR_SCRATCH = isr_init (&BE (quiet));
  init_ephemstuff ();
  BE (maketimestuff) = ZLLOC (1, struct maketimestuff);

  /* Set ‘BE (mem_limit)’.  */
  {
    char *v;
    long lim;

    BE (mem_limit) = (((v = getenv ("RCS_MEM_LIMIT"))
                       /* Silently ignore empty value.  */
                       && v[0])
                      ? v
                      : NULL)
      /* Clamp user-specified value to [0,LONG_MAX].  */
      ? (0 > (lim = atol (v))
         ? 0
         : lim)
      /* Default value.  */
      : 256;
  }
}

void
bad_option (char const *option)
{
  PERR ("unknown option: %s", option);
}

void
redefined (int c)
{
  PWARN ("redefinition of -%c option", c);
}

struct cbuf
minus_p (char const *xrev, char const *rev)
{
  struct cbuf rv;

  diagnose ("retrieving revision %s", xrev);
  /* Not ‘xrev’, for $Name's sake.  */
  accf (SINGLE, "-p%s", rev);
  rv.string = finish_string (SINGLE, &rv.size);
  return rv;
}

void
parse_revpairs (char option, char *arg,
                void (*put) (char const *b, char const *e, bool sawsep))
/* Destructively tokenize string ‘arg’ for ‘option’ (either 'r' or 'o')
   into comma- or semicolon-separated chunks.  For each chunk, tokenize
   a "range": either REV, REV1:, :REV1, or REV1:REV2, where ':' can also
   be '-' (old ambiguous syntax that will go away with RCS 6.x).
   Call ‘put’ for each pair thus found.  */
{
  register char c;
  int separator = strchr (arg, ':') ? ':' : '-';
  char const *b = NULL, *e = NULL;

  c = *arg;

  /* Support old ambiguous '-' syntax; this will go away.  */
  if ('-' == separator
      && strchr (arg, '-')
      && VERSION (5) <= BE (version))
    PWARN ("`-' is obsolete in `-%c%s'; use `:' instead", option, arg);

  for (;;)
    {
#define SKIPWS()  while (c == ' ' || c == '\t' || c == '\n') c = *++arg
#define NOMORE    '\0': case ' ': case '\t': case '\n': case ',': case ';'
#define TRUNDLE()                               \
      for (;; c = *++arg)                       \
        {                                       \
          switch (c)                            \
            {                                   \
            default:                            \
              continue;                         \
            case NOMORE:                        \
              break;                            \
            case ':': case '-':                 \
              if (c == separator)               \
                break;                          \
              continue;                         \
            }                                   \
          break;                                \
        }                                       \
      *arg = '\0'

      SKIPWS ();
      b = arg;
      TRUNDLE ();
      SKIPWS ();
      if (c == separator)
        {
          while ((c = *++arg) == ' ' || c == '\t' || c == '\n')
            continue;
          e = arg;
          TRUNDLE ();
          put (b, e, true);
          SKIPWS ();
        }
      else
        put (b, e, false);
      if (!c)
        break;
      else if (c == ',' || c == ';')
        c = *++arg;
      else
        PERR ("missing `,' near `%c%s'", c, arg + 1);
#undef TRUNDLE
#undef NOMORE
#undef SKIPWS
    }
}

void
ffree (void)
/* Free all blocks in the ‘SINGLE’ space.  */
{
  forget (SINGLE);
}

char *
str_save (char const *s)
/* Save ‘s’ in permanently allocated storage.  */
{
  return intern0 (SHARED, s);
}

char *
cgetenv (char const *name)
/* Like ‘getenv’, but return a copy; ‘getenv’ can overwrite old results.  */
{
  register char *p;

  return (p = getenv (name)) ? str_save (p) : p;
}

char const *
getusername (bool suspicious)
/* Get the caller's login name.  Trust only ‘getwpuid’ if ‘suspicious’.  */
{
  if (!BE (username))
    {
#define JAM(x)  (BE (username) = x)
      if (
          /* Prefer ‘getenv’ unless ‘suspicious’; it's much faster.  */
#if getlogin_is_secure
          (suspicious
           || (!JAM (cgetenv ("LOGNAME"))
               && !JAM (cgetenv ("USER")))
           && !JAM (getlogin ()))
#else
          suspicious
          || (!JAM (cgetenv ("LOGNAME"))
              && !JAM (getenv ("USER"))
              && !JAM (getlogin ()))
#endif
          )
        {
#if defined HAVE_GETUID && defined HAVE_GETPWUID
          struct passwd const *pw = getpwuid (ruid ());

          if (!pw)
            PFATAL ("no password entry for userid %lu",
                    (unsigned long) ruid ());
          JAM (pw->pw_name);
#else  /* !(defined HAVE_GETUID && defined HAVE_GETPWUID) */
#if defined HAVE_SETUID
          PFATAL ("setuid not supported");
#else
          PFATAL ("Who are you?  Please setenv LOGNAME.");
#endif
#endif  /* !(defined HAVE_GETUID && defined HAVE_GETPWUID) */
        }
      checksid (BE (username));
#undef JAM
    }
  return BE (username);
}

#ifndef SSIZE_MAX
/* This does not work in #ifs, but it's good enough for us.
   Underestimating SSIZE_MAX may slow us down, but it won't break us.  */
#define SSIZE_MAX ((unsigned)-1 >> 1)
#endif

void
awrite (char const *buf, size_t chars, FILE *f)
{
  /* POSIX 1003.1-1990 ‘ssize_t’ hack.  */
  while (SSIZE_MAX < chars)
    {
      if (fwrite (buf, sizeof (*buf), SSIZE_MAX, f) != SSIZE_MAX)
        Oerror ();
      buf += SSIZE_MAX;
      chars -= SSIZE_MAX;
    }

  if (fwrite (buf, sizeof (*buf), chars, f) != chars)
    Oerror ();
}

#if defined HAVE_WORKING_FORK

static int
movefd (int old, int new)
{
  if (old < 0 || old == new)
    return old;
  new = fcntl (old, F_DUPFD, new);
  return close (old) == 0 ? new : -1;
}

static int
fdreopen (int fd, char const *file, int flags)
{
  int newfd;

  close (fd);
  newfd = open (file, flags, S_IRUSR | S_IWUSR);
  return movefd (newfd, fd);
}

#else  /* !defined HAVE_WORKING_FORK */

static void
accumulate_arg_quoted (struct divvy *space, int c, register char const *s)
/* Accumulate to ‘space’ the byte ‘c’, plus a quoted copy of ‘s’.  */
{
  accf (space, "%c'", c);
  while ((c = *s++))
    accf (space, "%s%c", '\'' == c ? "'\\'" : "", c);
  accf (space, "'");
}

#endif  /* !defined HAVE_WORKING_FORK */

#define exec_RCS execv

int
runv (int infd, char const *outname, char const **args)
/* Run a command.
   ‘infd’, if not -1, is the input file descriptor.
   ‘outname’, if non-NULL, is the name of the output file.
   ‘args[1..]’ form the command to be run; ‘args[0]’ might be modified.  */
{
  int wstatus;

#if BAD_WAIT_IF_SIGCHLD_IGNORED
  if (!BE (fixed_SIGCHLD))
    {
      BE (fixed_SIGCHLD) = true;
#ifndef SIGCHLD
#define SIGCHLD SIGCLD
#endif
      signal (SIGCHLD, SIG_DFL);
    }
#endif  /* BAD_WAIT_IF_SIGCHLD_IGNORED */

  oflush ();
  {
#if defined HAVE_WORKING_FORK
    pid_t pid;
    if (!(pid = vfork ()))
      {
        char const *notfound;

        if (infd != -1
            && STDIN_FILENO != infd
            && STDIN_FILENO != (close (STDIN_FILENO),
                                fcntl (infd, F_DUPFD, STDIN_FILENO)))
          {
            /* Avoid ‘perror’ since it may misuse buffers.  */
            complain ("%s: I/O redirection failed\n", args[1]);
            _Exit (DIFF_TROUBLE);
          }

        if (outname)
          if (fdreopen (STDOUT_FILENO, outname,
                        O_CREAT | O_TRUNC | O_WRONLY) < 0)
            {
              /* Avoid ‘perror’ since it may misuse buffers.  */
              complain ("%s: %s: cannot create\n", args[1], outname);
              _Exit (DIFF_TROUBLE);
            }
        exec_RCS (args[1], (char **) (args + 1));
        notfound = args[1];
#ifdef RCS_SHELL
        if (errno == ENOEXEC)
          {
            args[0] = notfound = RCS_SHELL;
            execv (args[0], (char **) args);
          }
#endif

        /* Avoid ‘perror’ since it may misuse buffers.  */
        complain ("%s: not found\n", notfound);
        _Exit (DIFF_TROUBLE);
      }
    if (pid < 0)
      fatal_sys ("fork");
#if defined HAVE_WAITPID
    if (waitpid (pid, &wstatus, 0) < 0)
      fatal_sys ("waitpid");
#else  /* !defined HAVE_WAITPID */
    {
      pid_t w;

      do
        {
          if ((w = wait (&wstatus)) < 0)
            fatal_sys ("wait");
        }
      while (w != pid);
    }
#endif  /* !defined HAVE_WAITPID */
#else   /* !defined HAVE_WORKING_FORK */
    size_t len;
    char *cmd;
    char const **p;

    /* Use ‘system’.  On many hosts ‘system’ discards signals.  Yuck!  */
    p = args + 1;
    accs (SHARED, *p);
    while (*++p)
      accumulate_arg_quoted (SHARED, ' ', *p);
    if (infd != -1 && infd != STDIN_FILENO)
      accf (SHARED, "<&%d", infd);
    if (outname)
      accumulate_arg_quoted (SHARED, '>', outname);
    wstatus = system (cmd = finish_string (SHARED, &len));
    brush_off (SHARED, cmd);
#endif  /* !defined HAVE_WORKING_FORK */
  }
  if (!WIFEXITED (wstatus))
    {
      if (WIFSIGNALED (wstatus))
        {
          complain_signal (args[1], WTERMSIG (wstatus));
          PFATAL ("%s got a fatal signal", args[1]);
        }
      PFATAL ("%s failed for unknown reason", args[1]);
    }
  return WEXITSTATUS (wstatus);
}

#define CARGSMAX 20
int
run (int infd, char const *outname, ...)
/* Run a command.
   ‘infd’, if not -1, is the input file descriptor.
   ‘outname’, if non-NULL, is the name of the output file.
   The remaining arguments specify the command and its arguments.  */
{
  va_list ap;
  char const *rgargs[CARGSMAX];
  register int i;

  va_start (ap, outname);
  for (i = 1; (rgargs[i++] = va_arg (ap, char const *));)
    if (CARGSMAX <= i)
      PFATAL ("too many command arguments");
  va_end (ap);
  return runv (infd, outname, rgargs);
}

void
setRCSversion (char const *str)
{
  register char const *s = str + 2;

  if (*s)
    {
      int v = VERSION_DEFAULT;

      if (BE (version_set))
        redefined ('V');
      BE (version_set) = true;
      v = 0;
      while (isdigit (*s))
        v = 10 * v + *s++ - '0';
      if (*s)
        PERR ("%s isn't a number", str);
      else if (v < VERSION_min || VERSION_max < v)
        PERR ("%s out of range %d..%d", str, VERSION_min, VERSION_max);

      BE (version) = VERSION (v);
    }
  else
    {
      display_version (top->program);   /* TODO:ZONK */
      exit (0);
    }
}

int
getRCSINIT (int argc, char **argv, char ***newargv)
{
  register char *p, *q, **pp;
  size_t n;

  if (!(q = cgetenv ("RCSINIT")))
    *newargv = argv;
  else
    {
      n = argc + 2;
      /* Count spaces in ‘RCSINIT’ to allocate a new arg vector.
         This is an upper bound, but it's OK even if too large.  */
      for (p = q;;)
        {
          switch (*p++)
            {
            default:
              continue;

            case ' ':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
            case '\v':
              n++;
              continue;

            case '\0':
              break;
            }
          break;
        }
      *newargv = pp = pointer_array (SHARED, n);
      /* Copy program name.  */
      *pp++ = *argv++;
      for (p = q;;)
        {
          for (;;)
            {
              switch (*q)
                {
                case '\0':
                  goto copyrest;

                case ' ':
                case '\b':
                case '\f':
                case '\n':
                case '\r':
                case '\t':
                case '\v':
                  q++;
                  continue;
                }
              break;
            }
          *pp++ = p;
          ++argc;
          for (;;)
            {
              switch ((*p++ = *q++))
                {
                case '\0':
                  goto copyrest;

                case '\\':
                  if (!*q)
                    goto copyrest;
                  p[-1] = *q++;
                  continue;

                default:
                  continue;

                case ' ':
                case '\b':
                case '\f':
                case '\n':
                case '\r':
                case '\t':
                case '\v':
                  break;
                }
              break;
            }
          p[-1] = '\0';
        }
    copyrest:
      while ((*pp++ = *argv++))
        continue;
    }
  return argc;
}

#define cacheid(V,E)                            \
  if (!BE (V ## _cached))                       \
    {                                           \
      BE (V) = E;                               \
      BE (V ## _cached) = true;                 \
    }                                           \
  return BE (V)

uid_t
ruid (void)
{
#ifndef HAVE_GETUID
  /* Programmer error: We used to conditionally define ‘ruid’
     (only when ‘defined HAVE_GETUID’), so it makes no sense
     to call it otherwise.  */
  abort ();
#endif
  cacheid (ruid, getuid ());
}

bool
myself (uid_t u)
{
#ifndef HAVE_GETUID
  return true;
#else
  return u == ruid ();
#endif
}

#if defined HAVE_SETUID
uid_t
euid (void)
{
  cacheid (euid, geteuid ());
}
#endif

#if defined HAVE_SETUID

/* Setuid execution really works only with POSIX 1003.1a Draft 5
   ‘seteuid’, because it lets us switch back and forth between arbitrary
   users.  If ‘seteuid’ doesn't work, we fall back on ‘setuid’, which
   works if saved setuid is supported, unless the real or effective user
   is root.  This area is such a mess that we always check switches at
   runtime.  */

static void
set_uid_to (uid_t u)
/* Become user ‘u’.  */
{
  if (! currently_setuid_p ())
    return;
#if defined HAVE_WORKING_FORK
#if has_setreuid
  if (setreuid (u == euid ()? ruid () : euid (), u) != 0)
    fatal_sys ("setuid");
#else  /* !has_setreuid */
  if (seteuid (u) != 0)
    fatal_sys ("setuid");
#endif  /* !has_setreuid */
#endif  /* defined HAVE_WORKING_FORK */
  if (geteuid () != u)
    {
      /* FIXME: This sequence of code seems to be a no-op! --ttn  */
      if (BE (already_setuid))
        return;
      BE (already_setuid) = true;
      PFATAL ("root setuid not supported" + (u ? 5 : 0));
    }
}

void
nosetid (void)
/* Ignore all calls to ‘seteid’ and ‘setrid’.  */
{
  BE (stick_with_euid) = true;
}

void
seteid (void)
/* Become effective user.  */
{
  if (!BE (stick_with_euid))
    set_uid_to (euid ());
}

void
setrid (void)
/* Become real user.  */
{
  if (!BE (stick_with_euid))
    set_uid_to (ruid ());
}
#endif  /* defined HAVE_SETUID */

time_t
now (void)
{
  if (!BE (now) && time (&BE (now)) == -1)
    fatal_sys ("time");
  return BE (now);
}

/* rcsutil.c ends here */
