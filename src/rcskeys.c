/* RCS keyword table and match operation

   Copyright (C) 2010 Thien-Thi Nguyen
   Copyright (C) 1990, 1991, 1992, 1993, 1995 Paul Eggert
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

#include "rcsbase.h"

char const *const Keyword[] = {
  /* This must be in the same order as rcsbase.h's enum markers type. */
  0,
  AUTHOR, DATE, HEADER, IDH,
  LOCKER, LOG, NAME, RCSFILE, REVISION, SOURCE, STATE
};

enum markers
trymatch (char const *string)
/* function: Checks whether string starts with a keyword followed
 * by a KDELIM or a VDELIM.
 * If successful, returns the appropriate marker, otherwise Nomatch.
 */
{
  register int j;
  register char const *p, *s;
  for (j = sizeof (Keyword) / sizeof (*Keyword); (--j);)
    {
      /* try next keyword */
      p = Keyword[j];
      s = string;
      while (*p++ == *s++)
        {
          if (!*p)
            switch (*s)
              {
              case KDELIM:
              case VDELIM:
                return (enum markers) j;
              default:
                return Nomatch;
              }
        }
    }
  return (Nomatch);
}
