/* b-anchor.c --- constant data and their lookup funcs

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

static const uint8_t keyword_pool[80] =
{
  11 /* count */,
  6,'A','u','t','h','o','r','\0',
  4,'D','a','t','e','\0',
  6,'H','e','a','d','e','r','\0',
  2,'I','d','\0',
  6,'L','o','c','k','e','r','\0',
  3,'L','o','g','\0',
  4,'N','a','m','e','\0',
  7,'R','C','S','f','i','l','e','\0',
  8,'R','e','v','i','s','i','o','n','\0',
  6,'S','o','u','r','c','e','\0',
  5,'S','t','a','t','e','\0'
};

static bool
pool_lookup (const uint8_t pool[], const char *start, size_t len,
             struct pool_found *found)
{
  const uint8_t *p = pool + 1;

  for (size_t i = 0; i < pool[0]; i++)
    {
      size_t symlen = *p;

      if (len == symlen && !memcmp (p + 1, start, symlen))
        {
          found->i = i;
          found->sym = (struct tinysym *) p;
          return true;
        }
      p += 1 + symlen + 1;
    }
  return false;
}

bool
recognize_keyword (char const *string, struct pool_found *found)
/* Check whether `string' starts with a keyword followed by a
   `KDELIM' or a `VDELIM'.  Return true if successful.  In that
   case, `found' will hold a pointer to the found `struct tinysym',
   as well as the associated `enum marker' value.  */
{
  const char delims[3] = { KDELIM, VDELIM, '\0' };
  size_t limit = strcspn (string, delims);

  return ((KDELIM == string[limit]
           || VDELIM == string[limit])
          && pool_lookup (keyword_pool, string, limit, found));
}

/* b-anchor.c ends here */
