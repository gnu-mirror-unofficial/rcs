/* b-excwho.c --- exclusivity / identity

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
#include <stdlib.h>
#include <unistd.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include "b-complain.h"
#include "b-esds.h"

#if defined HAVE_SETUID && !defined HAVE_SETEUID
#undef seteuid
#define seteuid setuid
#endif

/* Programmer error: We used to conditionally define the
   func (only when ‘enable’ is defined), so it makes no sense
   to call it otherwise.  */
#ifdef DEBUG
#define PEBKAC(enable)  PFATAL ("%s:%d: PEBKAC (%s, %s)",       \
                                __FILE__, __LINE__,             \
                                __func__, #enable)
#else
#define PEBKAC(enable)  abort ()
#endif

#define cacheid(V,E)                            \
  if (!BE (V ## _cached))                       \
    {                                           \
      BE (V) = E;                               \
      BE (V ## _cached) = true;                 \
    }                                           \
  return BE (V)

static uid_t
ruid (void)
{
#ifndef HAVE_GETUID
  PEBKAC (HAVE_GETUID);
#endif
  cacheid (ruid, getuid ());
}

bool
stat_mine_p (struct stat *st)
{
#ifndef HAVE_GETUID
  return true;
#else
  return ruid () == st->st_uid;
#endif
}

#if defined HAVE_SETUID
static uid_t
euid (void)
{
#ifndef HAVE_GETUID
  PEBKAC (HAVE_GETUID);
#endif
  cacheid (euid, geteuid ());
}
#endif  /* defined HAVE_SETUID */

bool
currently_setuid_p (void)
{
#if defined HAVE_SETUID && defined HAVE_GETUID
  return euid () != ruid ();
#else
  return false;
#endif
}

#if defined HAVE_SETUID
static void
set_uid_to (uid_t u)
/* Become user ‘u’.  */
{
  /* Setuid execution really works only with POSIX 1003.1a Draft 5
     ‘seteuid’, because it lets us switch back and forth between
     arbitrary users.  If ‘seteuid’ doesn't work, we fall back on
     ‘setuid’, which works if saved setuid is supported, unless
     the real or effective user is root.  This area is such a mess
     that we always check switches at runtime.  */

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
      if (BE (already_setuid))
        return;
      BE (already_setuid) = true;
      PFATAL ("root setuid not supported" + (u ? 5 : 0));
    }
}
#endif  /* defined HAVE_SETUID */

void
nosetid (void)
/* Ignore all calls to ‘seteid’ and ‘setrid’.  */
{
#ifdef HAVE_SETUID
  BE (stick_with_euid) = true;
#endif
}

void
seteid (void)
/* Become effective user.  */
{
#ifdef HAVE_SETUID
  if (!BE (stick_with_euid))
    set_uid_to (euid ());
#endif
}

void
setrid (void)
/* Become real user.  */
{
#ifdef HAVE_SETUID
  if (!BE (stick_with_euid))
    set_uid_to (ruid ());
#endif
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

char const *
getcaller (void)
/* Get the caller's login name.  */
{
  return getusername (currently_setuid_p ());
}

bool
caller_login_p (char const *login)
{
  return STR_SAME (getcaller (), login);
}

struct link *
lock_memq (struct link *ls, bool loginp, void const *x)
/* Search ‘ls’, which should be initialized by caller to have its ‘.next’
   pointing to ‘GROK (locks)’, for a lock that matches ‘x’ and return the
   link whose cadr is the match, else NULL.  If ‘loginp’, ‘x’ is a login
   (string), else it is a delta.  */
{
  struct rcslock const *rl;

  for (; ls->next; ls = ls->next)
    {
      rl = ls->next->entry;
      if (loginp
          ? STR_SAME (x, rl->login)
          : x == rl->delta)
        return ls;
    }
  return NULL;
}

void
lock_drop (struct link *box, struct link *tp)
{
  struct rcslock const *rl = tp->next->entry;

  rl->delta->lockedby = NULL;
  tp->next = tp->next->next;
  GROK (locks) = box->next;
}

/* b-excwho.c ends here */
