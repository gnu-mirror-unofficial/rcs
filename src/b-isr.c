/* b-isr.c --- interrupt service routine

   Copyright (C) 2010 Thien-Thi Nguyen
   Copyright (C) 1990, 1991, 1992, 1993, 1994, 1995 Paul Eggert
   Copyright (C) 1982, 1988, 1989 Walter Tichy

   This file is part of GNU RCS.

   GNU RCS is free software: you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   GNU RCS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
   See the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Standard C places too many restrictions on signal handlers.
   We obey as many of them as we can.  POSIX places fewer
   restrictions, and we are POSIX-compatible here.  */

#include "base.h"
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include "sig2str.h"
#include "b-complain.h"
#include "b-divvy.h"
#include "b-excwho.h"
#include "b-isr.h"

/* Avoid calling ‘sprintf’ etc., in case they're not reentrant.  */

static void
werr (char const *s)
{
  write (STDERR_FILENO, s, strlen (s));
}

void
complain_signal (char const *msg, int signo)
{
#ifndef HAVE_PSIGNAL
  werr (msg);
  werr (": ");
#ifndef HAVE_STRSIGNAL
  {
    char buf[SIG2STR_MAX + 1];

    if (PROB (sig2str (signo, buf)))
      {
        char *w = buf + SIG2STR_MAX;

        werr ("Unknown signal ");
        *w-- = '\0';
        if (!signo)
          *w-- = '0';
        else
          while (signo)
            {
              *w-- = (signo % 10) + '0';
              signo /= 10;
            }
        werr (1 + w);
      }
    else
      {
        werr ("SIG");
        werr (buf);
      }
  }
#else  /* HAVE_STRSIGNAL */
  werr (strsignal (signo));
#endif  /* HAVE_STRSIGNAL */
  werr ("\n");

#else  /* HAVE_PSIGNAL */
  psignal (signo, msg);
#endif  /* HAVE_PSIGNAL */
}

struct isr_scratch
{
  sig_atomic_t volatile held, level;
  siginfo_t bufinfo;
  siginfo_t *volatile held_info;
  char const *access_name;
  struct
  {
    bool regular;
    bool memory_map;
  } catching;
  bool *be_quiet;
};

#define ISR(x)  (scratch->x)

void
access_page (struct isr_scratch *scratch,
             char const *filename, void const *p)
{
  unsigned char volatile t;

  ISR (access_name) = filename;
  t = *((unsigned char *) p);
  ISR (access_name) = NULL;
}

static void
ignore (struct isr_scratch *scratch)
{
  ++ISR (level);
}

#if !defined HAVE_PSIGINFO
#define psiginfo(info, msg)  complain_signal (msg, info->si_signo)
#endif

static struct isr_scratch *
scratch_from_context (void *c)
{
#ifndef HAVE_SIGACTION

  /* FIXME: The reference to ISR_SCRATCH prevents this module (b-isr.c)
     from being completely reentrant, which would be a nice, but not
     necessary, for the future libgnurcs.  The useless ‘c ? c :’
     prevents a compiler warning about unused parameter ‘c’.  */

  return c ? c : ISR_SCRATCH;

#else

  ucontext_t *uc = c;

  return (struct isr_scratch *)(uc->uc_stack.ss_sp) - 1;

#endif
}

static void
catchsigaction (int signo, siginfo_t *info, void *c)
{
  struct isr_scratch *scratch = scratch_from_context (c);
  bool from_mmap = MMAP_SIGNAL && MMAP_SIGNAL == signo;

  if (SIG_ZAPS_HANDLER)
    /* If a signal arrives before we reset the handler, we lose.  */
    signal (signo, SIG_IGN);

  if (ISR (level))
    {
      ISR (held) = signo;
      if (info)
        {
          ISR (bufinfo) = *info;
          ISR (held_info) = &ISR (bufinfo);
        }
      return;
    }

  ignore (scratch);
  setrid ();
  if (!*ISR (be_quiet))
    {
      /* If this signal was planned, don't complain about it.  */
      if (!(from_mmap && ISR (access_name)))
        {
          char const *nRCS = "\nRCS";

          if (from_mmap && info && info->si_errno)
            {
              errno = info->si_errno;
              /* Bump start of string to avoid subsequent newline output.  */
              perror (nRCS++);
            }
          if (info)
            psiginfo (info, nRCS);
          else
            complain_signal (nRCS, signo);
        }

      werr ("RCS: ");
      if (from_mmap)
        {
          if (ISR (access_name))
            {
              werr (ISR (access_name));
              werr (": Permission denied.  ");
            }
          else
            werr ("Was a file changed by some other process?  ");
        }
      werr ("Cleaning up.\n");
    }
  PROGRAM (exiterr) ();
}

#if !defined HAVE_SIGACTION
static void
catchsig (int signo)
{
  catchsigaction (signo, NULL, NULL);
}
#endif  /* !defined HAVE_SIGACTION */

static void
setup_catchsig (size_t count, const int const set[count])
{
#if defined HAVE_SIGACTION

  sigset_t blocked;

#define MUST(x)  if (PROB (x)) goto fail

  sigemptyset (&blocked);
  for (size_t i = 0; i < count; i++)
    MUST (sigaddset (&blocked, set[i]));

  for (size_t i = 0; i < count; i++)
    {
      struct sigaction act;
      int sig = set[i];

      MUST (sigaction (sig, NULL, &act));
      if (SIG_IGN != act.sa_handler)
        {
          act.sa_sigaction = catchsigaction;
          act.sa_flags |= SA_SIGINFO | SA_ONSTACK;
          act.sa_mask = blocked;
          if (PROB (sigaction (sig, &act, NULL)))
            {
            fail:
              fatal_sys ("signal handling");
            }
        }
    }

#undef MUST

#else  /* !defined HAVE_SIGACTION */
#if defined HAVE_SIGBLOCK

  int mask = 0;

  for (size_t i = 0; i < count; i++)
    mask |= 1 << (set[i] - 1);
  mask = sigblock (mask);

  for (size_t i = 0; i < count; i++)
    {
      int sig = set[i];

      if (SIG_IGN == signal (sig, catchsig)
          && catchsig != signal (sig, SIG_IGN))
        PFATAL ("signal catcher failure");
    }

  sigsetmask (mask);

#else  /* !defined HAVE_SIGBLOCK */

  for (size_t i = 0; i < count; i++)
    {
      int sig = set[i];

      if (SIG_IGN != signal (sig, SIG_IGN)
          && SIG_IGN != signal (sig, catchsig))
        PFATAL ("signal catcher failure");
    }

#endif  /* !defined HAVE_SIGBLOCK */
#endif  /* !defined HAVE_SIGACTION */
}

#if defined HAVE_SIGACTION
#define ISR_STACK_SIZE  (10 * SIGSTKSZ)
#else
#define ISR_STACK_SIZE  0
#endif

#define SCRATCH_SIZE  sizeof (struct isr_scratch)

struct isr_scratch *
isr_init (bool *be_quiet)
{
  /* Allocate a contiguous range for the scratch space,
     plus (possibly) space for the alternate sig stack.  */
  struct isr_scratch *scratch = alloc (SHARED, "isr scratch",
                                       SCRATCH_SIZE + ISR_STACK_SIZE);

#if ISR_STACK_SIZE
  stack_t ss =
    {
      /* The stack base starts after the scratch space.  */
      .ss_sp = scratch + 1,
      .ss_size = ISR_STACK_SIZE,
      .ss_flags = 0
    };

  if (PROB (sigaltstack (&ss, NULL)))
    fatal_sys ("sigaltstack");
#endif

  /* Clear out the initial part, i.e., the scratch space.
     (The stack space is under system manglement.)  */
  memset (scratch, 0, SCRATCH_SIZE);

  /* Make peer-subsystem connection.  */
  ISR (be_quiet) = be_quiet;
  return scratch;
}

#define COUNT(array)  (int) (sizeof (array) / sizeof (*array))

void
isr_do (struct isr_scratch *scratch, enum isr_actions action)
{
  switch (action)
    {
    case ISR_CATCHINTS:
      {
        const int const regular[] =
          {
            SIGHUP,
            SIGINT,
            SIGQUIT,
            SIGPIPE,
            SIGTERM,
            SIGXCPU,
            SIGXFSZ,
          };

        if (!ISR (catching.regular))
          {
            ISR (catching.regular) = true;
            setup_catchsig (COUNT (regular), regular);
          }
      }
      break;

    case ISR_IGNOREINTS:
      ignore (scratch);
      break;

    case ISR_RESTOREINTS:
      if (!--ISR (level) && ISR (held))
        catchsigaction (ISR (held), ISR (held_info), NULL);
      break;

    case ISR_CATCHMMAPINTS:
      /* If you mmap an NFS file, and someone on another client removes
         the last link to that file, and you later reference an uncached
         part of that file, you'll get a SIGBUS or SIGSEGV (depending on
         the operating system).  Catch the signal and report the problem
         to the user.  Unfortunately, there's no portable way to
         differentiate between this problem and actual bugs in the
         program.  This NFS problem is rare, thank goodness.

         This can also occur if someone truncates the file,
         even without NFS.  */
      {
        const int const mmapsigs[] = { MMAP_SIGNAL };

        if (MMAP_SIGNAL && !ISR (catching.memory_map))
          {
            ISR (catching.memory_map) = true;
            setup_catchsig (COUNT (mmapsigs), mmapsigs);
          }
      }
      break;
    }
}

/* b-isr.c ends here */
