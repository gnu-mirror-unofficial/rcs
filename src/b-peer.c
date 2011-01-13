/* b-peer.c --- finding the ‘execv’able name of a peer program

   Copyright (C) 2010, 2011 Thien-Thi Nguyen

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

#include "base.h"
#include <stdlib.h>
#include <string.h>
#include "findprog.h"
#include "b-complain.h"
#include "b-divvy.h"

struct symdef peer_co = { .meaningful = "co", .underlying = NULL };

#define LOSE(blurb)                                             \
  PFATAL ("installation problem: " blurb, PROGRAM (invoke))

static char const *
invocation_full_name (void)
{
  char const *name = find_in_path (PROGRAM (invoke));

  if (! name)
    LOSE ("cannot find `%s' in PATH");

  return name;
}

char const *
find_peer_prog (struct symdef *prog)
{
  if (! prog->underlying)
    {
      size_t len;

      /* Find the driver's invocation directory, once.  */
      if (! BE (invdir))
        {
          char const *name = invocation_full_name ();
          char const *end = strrchr (name, SLASH);

          if (!end)
            LOSE ("cannot find last slash in `%s'");
          BE (invdir) = intern (PLEXUS, name, end + 1 - name);
          if (name != PROGRAM (invoke))
            free ((void *) name);
        }

      /* Concat the invocation directory with the base name.  */
      accf (PLEXUS, "%s%s", BE (invdir), prog->meaningful);
      prog->underlying = finish_string (PLEXUS, &len);
    }

  return prog->underlying;
}

/* b-peer.c ends here */
