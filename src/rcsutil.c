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
#include "b-isr.h"
#include "gnu-h-v.h"

void
gnurcs_init (void)
{
  shared = make_space ("shared");
  single = make_space ("single");
  top = memset (alloc (shared, "top", sizeof (*top)), 0, sizeof (*top));
  unbuffer_standard_error ();
}

static void *
okalloc (void * p)
{
  if (!p)
    PFATAL ("out of memory");
  return p;
}

void *
testalloc (size_t size)
/* Allocate a block, testing that the allocation succeeded.  */
{
  return okalloc (malloc (size));
}

void *
testrealloc (void *ptr, size_t size)
/* Reallocate a block, testing that the allocation succeeded.  */
{
  return okalloc (realloc (ptr, size));
}

void *
ftestalloc (size_t size)
/* Allocate a block from the `single' space.  */
{
  return alloc (single, __func__, size);
}

void
ffree (void)
/* Free all blocks in the `single' space.  */
{
  forget (single);
}

void
free_NEXT_str (void)
{
  brush_off (single, (void *) NEXT (str));
}

char *
str_save (char const *s)
/* Save `s' in permanently allocated storage.  */
{
  return intern0 (shared, s);
}

char *
fbuf_save (const struct buf *b)
/* Save `b->string' in storage that will be deallocated
   when we're done with this file.  */
{
  return intern0 (single, b->string);
}

char *
cgetenv (char const *name)
/* Like `getenv', but return a copy; `getenv' can overwrite old results.  */
{
  register char *p;

  return (p = getenv (name)) ? str_save (p) : p;
}

char const *
getusername (bool suspicious)
/* Get the caller's login name.  Trust only `getwpuid' if `suspicious'.  */
{
  static char *name;

  if (!name)
    {
      if (
           /* Prefer `getenv' unless `suspicious'; it's much faster.  */
#if getlogin_is_secure
           (suspicious
            || (!(name = cgetenv ("LOGNAME"))
                && !(name = cgetenv ("USER"))))
           && !(name = getlogin ())
#else
           suspicious
           || (!(name = cgetenv ("LOGNAME"))
               && !(name = cgetenv ("USER"))
               && !(name = getlogin ()))
#endif
        )
        {
#if defined HAVE_GETUID && defined HAVE_GETPWUID
          struct passwd const *pw = getpwuid (ruid ());

          if (!pw)
            PFATAL ("no password entry for userid %lu",
                    (unsigned long) ruid ());
          name = pw->pw_name;
#else  /* !(defined HAVE_GETUID && defined HAVE_GETPWUID) */
#if defined HAVE_SETUID
          PFATAL ("setuid not supported");
#else
          PFATAL ("Who are you?  Please setenv LOGNAME.");
#endif
#endif  /* !(defined HAVE_GETUID && defined HAVE_GETPWUID) */
        }
      checksid (name);
    }
  return name;
}

void
fastcopy (register RILE *inf, FILE *outf)
/* Copy the remainder of file `inf' to `outf'.  */
{
#if large_memory
#if maps_memory
  awrite ((char const *) inf->ptr, (size_t) (inf->lim - inf->ptr), outf);
  inf->ptr = inf->lim;
#else  /* !maps_memory */
  for (;;)
    {
      awrite ((char const *) inf->ptr, (size_t) (inf->readlim - inf->ptr),
              outf);
      inf->ptr = inf->readlim;
      if (inf->ptr == inf->lim)
        break;
      Igetmore (inf);
    }
#endif  /* !maps_memory */
#else   /* !large_memory */
  char buf[BUFSIZ * 8];
  register size_t rcount;

  /* Now read the rest of the file in blocks.  */
  while (!feof (inf))
    {
      if (!(rcount = fread (buf, sizeof (*buf), sizeof (buf), inf)))
        {
          testIerror (inf);
          return;
        }
      awrite (buf, (size_t) rcount, outf);
    }
#endif  /* !large_memory */
}

#ifndef SSIZE_MAX
/* This does not work in #ifs, but it's good enough for us.
   Underestimating SSIZE_MAX may slow us down, but it won't break us.  */
#define SSIZE_MAX ((unsigned)-1 >> 1)
#endif

void
awrite (char const *buf, size_t chars, FILE *f)
{
  /* POSIX 1003.1-1990 `ssize_t' hack.  */
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

static int
dupSafer (int fd)
/* Dup a file descriptor; the result must not be stdin, stdout, or stderr.  */
{
  return fcntl (fd, F_DUPFD, STDERR_FILENO + 1);
}

int
fdSafer (int fd)
/* Renumber a file descriptor so that it's not stdin, stdout, or stderr.  */
{
  if (STDIN_FILENO <= fd && fd <= STDERR_FILENO)
    {
      int f = dupSafer (fd);
      int e = errno;

      close (fd);
      errno = e;
      fd = f;
    }
  return fd;
}

FILE *
fopenSafer (char const *filename, char const *type)
/* Like `fopen', except the result is never stdin, stdout, or stderr.  */
{
  FILE *stream = fopen (filename, type);

  if (stream)
    {
      int fd = fileno (stream);

      if (STDIN_FILENO <= fd && fd <= STDERR_FILENO)
        {
          int f = dupSafer (fd);

          if (f < 0)
            {
              int e = errno;

              fclose (stream);
              errno = e;
              return NULL;
            }
          if (fclose (stream) != 0)
            {
              int e = errno;

              close (f);
              errno = e;
              return NULL;
            }
          stream = fdopen (f, type);
        }
    }
  return stream;
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
bufargcat (register struct buf *b, int c, register char const *s)
/* Append to `b' a copy of `c', plus a quoted copy of `s'.  */
{
  register char *p;
  register char const *t;
  size_t bl, sl;

  for (t = s, sl = 0; *t;)
    sl += 3 * (*t++ == '\'') + 1;
  bl = strlen (b->string);
  bufrealloc (b, bl + sl + 4);
  p = b->string + bl;
  *p++ = c;
  *p++ = '\'';
  while (*s)
    {
      if (*s == '\'')
        {
          *p++ = '\'';
          *p++ = '\\';
          *p++ = '\'';
        }
      *p++ = *s++;
    }
  *p++ = '\'';
  *p = '\0';
}

#endif  /* !defined HAVE_WORKING_FORK */

#define exec_RCS execv

int
runv (int infd, char const *outname, char const **args)
/* Run a command.
   `infd', if not -1, is the input file descriptor.
   `outname', if non-NULL, is the name of the output file.
   `args[1..]' form the command to be run; `args[0]' might be modified.  */
{
  int wstatus;

#if BAD_WAIT_IF_SIGCHLD_IGNORED
  static bool fixed_SIGCHLD;
  if (!fixed_SIGCHLD)
    {
      fixed_SIGCHLD = true;
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
            /* Avoid `perror' since it may misuse buffers.  */
            complain ("%s: I/O redirection failed\n", args[1]);
            _Exit (DIFF_TROUBLE);
          }

        if (outname)
          if (fdreopen (STDOUT_FILENO, outname,
                        O_CREAT | O_TRUNC | O_WRONLY) < 0)
            {
              /* Avoid `perror' since it may misuse buffers.  */
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

        /* Avoid `perror' since it may misuse buffers.  */
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
    static struct buf b;
    char const *p;

    /* Use `system'.  On many hosts `system' discards signals.  Yuck!  */
    p = args + 1;
    bufscpy (&b, *p);
    while (*++p)
      bufargcat (&b, ' ', *p);
    if (infd != -1 && infd != STDIN_FILENO)
      {
        char redirection[32];

        sprintf (redirection, "<&%d", infd);
        bufscat (&b, redirection);
      }
    if (outname)
      bufargcat (&b, '>', outname);
    wstatus = system (b.string);
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
   `infd', if not -1, is the input file descriptor.
   `outname', if non-NULL, is the name of the output file.
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
      display_version ();
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
      /* Count spaces in `RCSINIT' to allocate a new arg vector.
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
      *newargv = pp = pointer_array (shared, n);
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
  /* Programmer error: We used to conditionally define `ruid'
     (only when `defined HAVE_GETUID'), so it makes no sense
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
   `seteuid', because it lets us switch back and forth between arbitrary
   users.  If `seteuid' doesn't work, we fall back on `setuid', which
   works if saved setuid is supported, unless the real or effective user
   is root.  This area is such a mess that we always check switches at
   runtime.  */

static void
set_uid_to (uid_t u)
/* Become user `u'.  */
{
  if (euid () == ruid ())
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
/* Ignore all calls to `seteid' and `setrid'.  */
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
  static time_t t;

  if (!t && time (&t) == -1)
    fatal_sys ("time");
  return t;
}

/* rcsutil.c ends here */
