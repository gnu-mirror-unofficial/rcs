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
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include "sig2str.h"
#include "b-complain.h"
#include "gnu-h-v.h"

/* List of blocks allocated with `ftestalloc'.  These blocks can be
   freed by ffree when we're done with the current file.  We could put
   the free block inside `struct alloclist', rather than a pointer to
   the free block, but that would be less portable.  */
struct alloclist
{
  void *alloc;
  struct alloclist *nextalloc;
};
static struct alloclist *alloced;

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
fremember (void *ptr)
/* Remember `ptr' in `alloced' so that it can be freed later.
   Return `ptr'.  */
{
  register struct alloclist *q = talloc (struct alloclist);

  q->nextalloc = alloced;
  alloced = q;
  return q->alloc = ptr;
}

void *
ftestalloc (size_t size)
/* Allocate a block, putting it in `alloced' so it can be freed later.  */
{
  return fremember (testalloc (size));
}

void
ffree (void)
/* Free all blocks allocated with `ftestalloc'.  */
{
  register struct alloclist *p, *q;

  for (p = alloced; p; p = q)
    {
      q = p->nextalloc;
      tfree (p->alloc);
      tfree (p);
    }
  alloced = NULL;
}

void
ffree1 (register char const *f)
/* Free the block `f', which was allocated by `ftestalloc'.  */
{
  register struct alloclist *p, **a = &alloced;

  while ((p = *a)->alloc != f)
    a = &p->nextalloc;
  *a = p->nextalloc;
  tfree (p->alloc);
  tfree (p);
}

char *
str_save (char const *s)
/* Save `s' in permanently allocated storage.  */
{
  size_t ssiz = strlen (s) + 1;

  return strncpy (tnalloc (char, ssiz), s, ssiz);
}

char *
fbuf_save (const struct buf *b)
/* Save `b->string' in storage that will be deallocated
   when we're done with this file.  */
{
  return strncpy (ftestalloc (1 + b->size), b->string, b->size);
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

/* Signal handling

   Standard C places too many restrictions on signal handlers.  We obey
   as many of them as we can.  POSIX places fewer restrictions, and we
   are POSIX-compatible here.  */

static sig_atomic_t volatile heldsignal, holdlevel;

static bool unsupported_SA_SIGINFO;
static siginfo_t bufsiginfo;
static siginfo_t *volatile heldsiginfo;

#if has_NFS && defined HAVE_MMAP && large_memory && MMAP_SIGNAL
static char const *accessName;

void
readAccessFilenameBuffer (char const *filename, unsigned char const *p)
{
  unsigned char volatile t;

  accessName = filename;
  t = *p;
  accessName = NULL;
}
#else  /* !(has_NFS && defined HAVE_MMAP && large_memory && MMAP_SIGNAL) */
#define accessName (NULL)
#endif  /* !(has_NFS && defined HAVE_MMAP && large_memory && MMAP_SIGNAL) */

static void
complain_signal (char const *s, int sig)
{
#ifndef HAVE_PSIGNAL
  char const *sname;

#define UNKNOWN  "Unknown signal"
#ifndef HAVE_STRSIGNAL
#define SZ  (sizeof UNKNOWN + INT_STRLEN_BOUND (int))
  char buf[SZ];

  if (0 > sig2str (sig, 3 + buf))
    snprintf (buf, SZ, "%s %d", UNKNOWN, sig);
  else
    strncpy (buf, "SIG", 3);
  sname = buf;
#undef SZ
#else  /* HAVE_STRSIGNAL */
  sname = strsignal (sig);
#endif  /* HAVE_STRSIGNAL */
  complain ("%s: %s\n", s, (sname && *sname) ? sname : UNKNOWN);

#undef UNKNOWN
#else  /* HAVE_PSIGNAL */
  psignal (sig, s);
#endif  /* HAVE_PSIGNAL */
}

static void catchsigaction (int, siginfo_t *, void *);

static void
catchsig (int s)
{
  catchsigaction (s, NULL, NULL);
}

static void
catchsigaction (int s, siginfo_t *i, void *c RCS_UNUSED)
{
#if SIG_ZAPS_HANDLER
  /* If a signal arrives before we reset the handler, we lose.  */
  signal (s, SIG_IGN);
#endif

  if (unsupported_SA_SIGINFO)
    i = NULL;

  if (holdlevel)
    {
      heldsignal = s;
      if (i)
        {
          bufsiginfo = *i;
          heldsiginfo = &bufsiginfo;
        }
      return;
    }

  ignoreints ();
  setrid ();
  if (!BE (quiet))
    {
      /* Avoid calling `sprintf' etc., in case they're not reentrant.  */
      char const *p;
      char buf[BUFSIZ], *b = buf;

      if (!(
#if defined HAVE_MMAP && large_memory && MMAP_SIGNAL
             /* Check whether this signal was planned.  */
             s == MMAP_SIGNAL && accessName
#else
             0
#endif
          ))
        {
          char const *nRCS = "\nRCS";

#if defined HAVE_MMAP && large_memory && MMAP_SIGNAL
          if (s == MMAP_SIGNAL && i && i->si_errno)
            {
              errno = i->si_errno;
              perror (nRCS++);
            }
#endif
#if defined HAVE_PSIGINFO
          if (i)
            psiginfo (i, nRCS);
          else
            complain_signal (nRCS, s);
#else
          complain_signal (nRCS, s);
#endif
        }

      for (p = "RCS: "; *p; *b++ = *p++)
        continue;
#if defined HAVE_MMAP && large_memory && MMAP_SIGNAL
      if (s == MMAP_SIGNAL)
        {
          p = accessName;
          if (!p)
            p = "Was a file changed by some other process?  ";
          else
            {
              char const *p1;

              for (p1 = p; *p1; p1++)
                continue;
              write (STDERR_FILENO, buf, b - buf);
              write (STDERR_FILENO, p, p1 - p);
              b = buf;
              p = ": Permission denied.  ";
            }
          while (*p)
            *b++ = *p++;
        }
#endif  /* defined HAVE_MMAP && large_memory && MMAP_SIGNAL */
      for (p = "Cleaning up.\n"; *p; *b++ = *p++)
        continue;
      write (STDERR_FILENO, buf, b - buf);
    }
  PROGRAM (exiterr) ();
}

void
ignoreints (void)
{
  ++holdlevel;
}

void
restoreints (void)
{
  if (!--holdlevel && heldsignal)
    catchsigaction (heldsignal, heldsiginfo, NULL);
}

#if defined HAVE_SIGACTION
static void
check_sig (int r)
{
  if (r != 0)
    fatal_sys ("signal handling");
}

static void
setup_catchsig (int const *sig, int sigs)
{
  register int i, j;
  struct sigaction act;

  for (i = sigs; 0 <= --i;)
    {
      check_sig (sigaction (sig[i], NULL, &act));
      if (act.sa_handler != SIG_IGN)
        {
          act.sa_handler = catchsig;
          if (!unsupported_SA_SIGINFO)
            {
              act.sa_sigaction = catchsigaction;
              act.sa_flags |= SA_SIGINFO;
            }
          for (j = sigs; 0 <= --j;)
            check_sig (sigaddset (&act.sa_mask, sig[j]));
          if (sigaction (sig[i], &act, NULL) != 0)
            {
              if (errno == ENOTSUP && !unsupported_SA_SIGINFO)
                {
                  /* Turn off use of SA_SIGINFO and try again.  */
                  unsupported_SA_SIGINFO = true;
                  i++;
                  continue;
                }
              check_sig (-1);
            }
        }
    }
}

#else  /* !defined HAVE_SIGACTION */
#if defined HAVE_SIGBLOCK

static void
setup_catchsig (int const *sig, int sigs)
{
  register int i;
  int mask;

  mask = 0;
  for (i = sigs; 0 <= --i;)
    mask |= 1 << (sig[i] - 1);
  mask = sigblock (mask);
  for (i = sigs; 0 <= --i;)
    if (signal (sig[i], catchsig) == SIG_IGN
        && signal (sig[i], SIG_IGN) != catchsig)
      PFATAL ("signal catcher failure");
  sigsetmask (mask);
}

#else  /* !defined HAVE_SIGBLOCK */

static void
setup_catchsig (int const *sig, int sigs)
{
  register i;

  for (i = sigs; 0 <= --i;)
    if (signal (sig[i], SIG_IGN) != SIG_IGN
        && signal (sig[i], catchsig) != SIG_IGN)
      PFATAL ("signal catcher failure");
}

#endif  /* !defined HAVE_SIGBLOCK */
#endif  /* !defined HAVE_SIGACTION */

static const int const regsigs[] = {
  SIGHUP,
  SIGINT,
  SIGPIPE,
  SIGQUIT,
  SIGTERM,
  SIGXCPU,
  SIGXFSZ,
};

void
catchints (void)
{
  static bool catching_ints;

  if (!catching_ints)
    {
      catching_ints = true;
      setup_catchsig (regsigs, (int) (sizeof (regsigs) / sizeof (*regsigs)));
    }
}

#if defined HAVE_MMAP && large_memory && MMAP_SIGNAL

/* If you mmap an NFS file, and someone on another client removes the
   last link to that file, and you later reference an uncached part of
   that file, you'll get a SIGBUS or SIGSEGV (depending on the operating
   system).  Catch the signal and report the problem to the user.
   Unfortunately, there's no portable way to differentiate between this
   problem and actual bugs in the program.  This NFS problem is rare,
   thank goodness.

   This can also occur if someone truncates the file, even without NFS.  */

static const int const mmapsigs[] = { MMAP_SIGNAL };

void
catchmmapints (void)
{
  static bool catching_mmap_ints;

  if (!catching_mmap_ints)
    {
      catching_mmap_ints = true;
      setup_catchsig (mmapsigs, (int) (sizeof (mmapsigs) / sizeof (*mmapsigs)));
    }
}
#endif  /* defined HAVE_MMAP && large_memory && MMAP_SIGNAL */

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
      *newargv = pp = tnalloc (char *, n);
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

/* Behavior control.  */
struct behavior behavior;

/* Flow.  */
struct flow flow;

/* rcsutil.c ends here */
