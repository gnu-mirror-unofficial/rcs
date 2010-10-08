/* b-peer.c --- finding the ‘execv’able name of a peer program

   Copyright (C) 2010 Thien-Thi Nguyen

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
#include "findprog.h"

struct symdef peer_co = { .meaningful = "co", .underlying = NULL };

char const *
find_peer_prog (struct symdef *prog)
{
  if (! prog->underlying)
    {
      const char *name = find_in_path (prog->meaningful);

      if (! name || prog->underlying == name)
        abort ();

      prog->underlying = str_save (name);
      free ((void *) name);
    }

  return prog->underlying;
}

/* b-peer.c ends here */
