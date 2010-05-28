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

bool
currently_setuid_p (void)
{
#if defined HAVE_SETUID && defined HAVE_GETUID
  return euid () != ruid ();
#else
  return false;
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
