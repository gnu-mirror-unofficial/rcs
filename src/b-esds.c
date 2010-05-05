/* b-esds.c --- embarrassingly simple data structures

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
#include "b-divvy.h"
#include "b-esds.h"

#define STRUCTALLOC(to,type)  alloc (to, #type, sizeof (type))
#define NEWPAIR(to,sub)  STRUCTALLOC (to, struct sub)

#define EXTEND_BODY(sub)                        \
  struct sub *pair = NEWPAIR (to, sub);         \
                                                \
  pair->entry = x;                              \
  pair->next = NULL;                            \
  tp->next = pair;                              \
  return pair

struct link *
extend (struct link *tp, void const *x, struct divvy *to)
{
  EXTEND_BODY (link);
}

struct wlink *
wextend (struct wlink *tp, void *x, struct divvy *to)
{
  EXTEND_BODY (wlink);
}

struct link *
prepend (void const *x, struct link *ls, struct divvy *to)
{
  struct link *pair = NEWPAIR (to, link);

  pair->entry = x;
  pair->next = ls;
  return pair;
}

/* b-esds.c ends here */
