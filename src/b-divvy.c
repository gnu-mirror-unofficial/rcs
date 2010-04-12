/* b-divvy.c --- dynamic memory manglement

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
#include <stdbool.h>
#include <obstack.h>
#include <stdlib.h>
#include "b-complain.h"
#include "b-divvy.h"

struct divvy *shared;
struct divvy *single;

static void
oom (void)
{
  PFATAL ("out of memory");
}

static void *
allocate (size_t size, bool clearp)
{
  void *p = (clearp
             ? calloc (1, size)
             : malloc (size));

  if (!p)
    oom ();
  return p;
}

#define TMALLOC(type)  allocate (sizeof (type), false)
#define TCALLOC(type)  allocate (sizeof (type), true)

static void *
xmalloc (size_t size)
{
  return allocate (size, false);
}

#define obstack_chunk_alloc xmalloc
#define obstack_chunk_free free

struct divvy *
make_space (const char const name[])
{
  struct divvy *divvy = TCALLOC (struct divvy);

  divvy->name = name;
  divvy->space = TCALLOC (struct obstack);
  obstack_alloc_failed_handler = oom;
  obstack_init (divvy->space);
  divvy->first = obstack_next_free ((struct obstack *) divvy->space);
#ifdef DEBUG
  complain ("%s: %32s %p\n", name, "first", divvy->first);
#endif
  divvy->count = 0;
  return divvy;
}

#ifdef DEBUG
#define USED_FOR_DEBUG
#else
#define USED_FOR_DEBUG  RCS_UNUSED
#endif

void *
alloc (struct divvy *divvy, char const *what USED_FOR_DEBUG, size_t len)
{
#ifdef DEBUG
  complain ("%s: %6u  %s\n", divvy->name, len, what);
#endif
  divvy->count++;
  return obstack_alloc (divvy->space, len);
}

void *
zlloc (struct divvy *divvy, char const *what, size_t len)
{
  return memset (alloc (divvy, what, len), 0, len);
}

char *
intern (struct divvy *divvy, char const *s, size_t len)
{
#ifdef DEBUG
  complain ("%s: %6us %c%s%c\n", divvy->name, len,
            ('\0' == s[len]) ? '"' : '[',
            ('\0' == s[len]) ? s : "some bytes",
            ('\0' == s[len]) ? '"' : ']');
#endif
  divvy->count++;
  return obstack_copy0 (divvy->space, s, len);
}

void
brush_off (struct divvy *divvy, void *ptr)
{
  divvy->count--;
  obstack_free (divvy->space, ptr);
}

void
forget (struct divvy *divvy)
{
#ifdef DEBUG
  complain ("%s: %32s %p (count=%u, room=%u)\n",
            divvy->name, "forget", divvy->first, divvy->count,
            obstack_room (divvy->space));
#endif
  brush_off (divvy, divvy->first);
  divvy->count = 0;
}

void
accumulate_byte (struct divvy *divvy, int c)
{
  obstack_1grow (divvy->space, c);
}

void
accumulate_nonzero_bytes (struct divvy *divvy, char const *s)
{
  struct obstack *o = divvy->space;

  while (*s)
    obstack_1grow (o, *s++);
}

char *
finish_string (struct divvy *divvy, size_t *result_len)
{
  struct obstack *o = divvy->space;
  char *rv;

  *result_len = obstack_object_size (o);
  obstack_1grow (o, '\0');
  rv = obstack_finish (o);
#ifdef DEBUG
  complain ("%s: %6ua \"%s\"\n", divvy->name, *result_len, rv);
#endif
  return rv;
}

void *
pointer_array (struct divvy *divvy, size_t count)
{
  struct obstack *o = divvy->space;

#ifdef DEBUG
  complain ("%s: %6up (%u void*)\n", divvy->name,
            sizeof (void *) * count, count);
#endif
  while (count--)
    obstack_ptr_grow (o, NULL);
  return obstack_finish (o);
}

void
close_space (struct divvy *divvy)
{
  brush_off (divvy->space, NULL);
  divvy->count = 0;
  divvy->first = NULL;
  free (divvy->space);
  divvy->space = NULL;
  free (divvy);
}

/* b-divvy.c ends here */
