/* b-excwho.c --- exclusivity / identity

   Copyright (C) 2010 Thien-Thi Nguyen

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
#include "b-esds.h"

bool
caller_login_p (char const *login)
{
  return STR_SAME (getcaller (), login);
}

bool
currently_setuid_p (void)
{
#if defined HAVE_SETUID && defined HAVE_GETUID
  return euid () != ruid ();
#else
  return false;
#endif
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
lock_drop (struct link *fake, struct link *tp)
{
  struct rcslock const *rl = tp->next->entry;

  rl->delta->lockedby = NULL;
  tp->next = tp->next->next;
  GROK (locks) = fake->next;
}

/* b-excwho.c ends here */
