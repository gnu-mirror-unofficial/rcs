/* Handle RCS revision numbers.

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
#include <ctype.h>
#include "b-complain.h"
#include "b-divvy.h"

static int
split (char const *s, char const **lastdot)
/* Given a pointer ‘s’ to a dotted number (date or revision number),
   return the number of fields in ‘s’, and set ‘*lastdot’ to point
   to the last '.' (or NULL if there is none).  */
{
  size_t count;

  *lastdot = NULL;
  if (!s || !*s)
    return 0;
  count = 1;
  do
    {
      if (*s++ == '.')
        {
          *lastdot = s - 1;
          count++;
        }
    }
  while (*s);
  return count;
}

int
countnumflds (char const *s)
/* Given a pointer ‘s’ to a dotted number (date or revision number),
   return the number of digitfields in ‘s’.  */
{
  register char const *sp;
  register int count;

  if (!(sp = s) || !*sp)
    return 0;
  count = 1;
  do
    {
      if (*sp++ == '.')
        count++;
    }
  while (*sp);
  return (count);
}

static void
accumulate_branchno (struct divvy *space, char const *revno)
{
  char const *end;
  int nfields = split (revno, &end);

  if (nfields & 1)
    accs (space, revno);
  else
    accumulate_range (space, revno, end);
}

struct cbuf
take (size_t count, char const *rev)
/* Copy ‘count’ (must be non-zero) fields of revision
   ‘rev’ into ‘SINGLE’.  Return the new string.  */
{
  struct cbuf rv;
  char const *end = rev;

  if (!count)
    count = -2 + (1U | (1 + countnumflds (rev)));

  while (count--)
    while ('.' != *end++)
      continue;

  accumulate_range (SINGLE, rev, end - 1);
  rv.string = finish_string (SINGLE, &rv.size);
  return rv;
}

int
cmpnum (char const *num1, char const *num2)
/* Compare the two dotted numbers ‘num1’ and ‘num2’ lexicographically
   by field.  Individual fields are compared numerically.
   Return <0, 0, >0 if ‘num1 < num2’, ‘num1 == num2’, ‘num1 > num2’,
   respectively.  Omitted fields are assumed to be higher than the existing
   ones.  */
{
  register char const *s1, *s2;
  register size_t d1, d2;
  register int r;

  s1 = num1 ? num1 : "";
  s2 = num2 ? num2 : "";

  for (;;)
    {
      /* Give precedence to shorter one.  */
      if (!*s1)
        return (unsigned char) *s2;
      if (!*s2)
        return -1;

      /* Strip leading zeros, then find number of digits.  */
      while (*s1 == '0')
        ++s1;
      while (*s2 == '0')
        ++s2;
      for (d1 = 0; isdigit (*(s1 + d1)); d1++)
        continue;
      for (d2 = 0; isdigit (*(s2 + d2)); d2++)
        continue;

      /* Do not convert to integer; it might overflow!  */
      if (d1 != d2)
        return d1 < d2 ? -1 : 1;
      if ((r = memcmp (s1, s2, d1)))
        return r;
      s1 += d1;
      s2 += d1;

      /* Skip '.'.  */
      if (*s1)
        s1++;
      if (*s2)
        s2++;
    }
}

int
cmpnumfld (char const *num1, char const *num2, int fld)
/* Compare the two dotted numbers at field ‘fld’.
   ‘num1’ and ‘num2’ must have at least ‘fld’ fields.
   ‘fld’ must be positive.  */
{
  register char const *s1, *s2;
  register size_t d1, d2;

  s1 = num1;
  s2 = num2;
  /* Skip ‘fld - 1’ fields.  */
  while (--fld)
    {
      while (*s1++ != '.')
        continue;
      while (*s2++ != '.')
        continue;
    }
  /* Now ‘s1’ and ‘s2’ point to the beginning of the respective fields.  */
  while (*s1 == '0')
    ++s1;
  for (d1 = 0; isdigit (*(s1 + d1)); d1++)
    continue;
  while (*s2 == '0')
    ++s2;
  for (d2 = 0; isdigit (*(s2 + d2)); d2++)
    continue;

  return d1 < d2 ? -1 : d1 == d2 ? memcmp (s1, s2, d1) : 1;
}

static char const *
normalizeyear (char const *date, char year[5])
{
  if (isdigit (date[0]) && isdigit (date[1]) && !isdigit (date[2]))
    {
      year[0] = '1';
      year[1] = '9';
      year[2] = date[0];
      year[3] = date[1];
      year[4] = 0;
      return year;
    }
  else
    return date;
}

int
cmpdate (char const *d1, char const *d2)
/* Compare the two dates.  This is just like ‘cmpnum’,
   except that for compatibility with old versions of RCS,
   1900 is added to dates with two-digit years.  */
{
  char year1[5], year2[5];
  int r = cmpnumfld (normalizeyear (d1, year1), normalizeyear (d2, year2), 1);

  if (r)
    return r;
  else
    {
      while (isdigit (*d1))
        d1++;
      d1 += *d1 == '.';
      while (isdigit (*d2))
        d2++;
      d2 += *d2 == '.';
      return cmpnum (d1, d2);
    }
}

static void
cantfindbranch (char const *revno, char const date[datesize],
                char const *author, char const *state)
{
  char datebuf[datesize + zonelenmax];

  RERR ("No revision on branch %s has%s%s%s%s%s%s.",
        revno,
        date ? " a date before " : "",
        date ? date2str (date, datebuf) : "",
        author ? " and author " + (date ? 0 : 4) : "",
        author ? author : "",
        state ? " and state " + (date || author ? 0 : 4) : "",
        state ? state : "");
}

static void
absent (char const *revno, int field)
{
  RERR ("%s %s absent", field & 1 ? "revision" : "branch",
        TAKE (field, revno));
}

int
compartial (char const *num1, char const *num2, int length)
/* Compare the first ‘length’ fields of two dot numbers;
   the omitted field is considered to be larger than any number.
   Restriction: At least one number has ‘length’ or more fields.  */
{
  register char const *s1, *s2;
  register size_t d1, d2;
  register int r;

  s1 = num1;
  s2 = num2;
  if (!s1)
    return 1;
  if (!s2)
    return -1;

  for (;;)
    {
      if (!*s1)
        return 1;
      if (!*s2)
        return -1;

      while (*s1 == '0')
        ++s1;
      for (d1 = 0; isdigit (*(s1 + d1)); d1++)
        continue;
      while (*s2 == '0')
        ++s2;
      for (d2 = 0; isdigit (*(s2 + d2)); d2++)
        continue;

      if (d1 != d2)
        return d1 < d2 ? -1 : 1;
      if ((r = memcmp (s1, s2, d1)))
        return r;
      if (!--length)
        return 0;

      s1 += d1;
      s2 += d1;

      if (*s1 == '.')
        s1++;
      if (*s2 == '.')
        s2++;
    }
}

static void
store1 (struct hshentries ***store, struct hshentry *next)
/* Allocate a new list node that addresses ‘next’.
   Append it to the list that ‘**store’ is the end pointer of.  */
{
  register struct hshentries *p;

  p = FALLOC (struct hshentries);
  p->first = next;
  **store = p;
  *store = &p->rest;
}

static struct hshentry *
genbranch (struct hshentry const *bpoint, char const *revno,
           int length, char const *date, char const *author,
           char const *state, struct hshentries **store)
/* Given a branchpoint, a revision number, date, author, and state, find the
   deltas necessary to reconstruct the given revision from the branch point
   on.  Pointers to the found deltas are stored in a list beginning with
   ‘store’.  ‘revno’ must be on a side branch.  Return NULL on error.
 */
{
  int field;
  register struct hshentry *next, *trail;
  register struct branchhead const *bhead;
  int result;
  char datebuf[datesize + zonelenmax];

  field = 3;
  bhead = bpoint->branches;

  do
    {
      if (!bhead)
        {
          RERR ("no side branches present for %s", TAKE (field - 1, revno));
          return NULL;
        }

      /* Find branch head.  Branches are arranged in increasing order.  */
      while (0 < (result = cmpnumfld (revno, bhead->hsh->num, field)))
        {
          bhead = bhead->nextbranch;
          if (!bhead)
            {
              RERR ("branch number %s too high", TAKE (field, revno));
              return NULL;
            }
        }

      if (result < 0)
        {
          absent (revno, field);
          return NULL;
        }

      next = bhead->hsh;
      if (length == field)
        {
          /* pick latest one on that branch */
          trail = NULL;
          do
            {
              if ((!date || cmpdate (date, next->date) >= 0)
                  && (!author || STR_SAME (author, next->author))
                  && (!state || STR_SAME (state, next->state)))
                trail = next;
              next = next->next;
            }
          while (next);

          if (!trail)
            {
              cantfindbranch (revno, date, author, state);
              return NULL;
            }
          else
            {
              /* Print up to last one suitable.  */
              next = bhead->hsh;
              while (next != trail)
                {
                  store1 (&store, next);
                  next = next->next;
                }
              store1 (&store, next);
            }
          *store = NULL;
          return next;
        }

      /* Length > field.  Find revision.  Check low.  */
      if (cmpnumfld (revno, next->num, field + 1) < 0)
        {
          RERR ("revision number %s too low", TAKE (field + 1, revno));
          return NULL;
        }
      do
        {
          store1 (&store, next);
          trail = next;
          next = next->next;
        }
      while (next && cmpnumfld (revno, next->num, field + 1) >= 0);

      if ((length > field + 1)
          /* Need exact hit.  */
          && (cmpnumfld (revno, trail->num, field + 1) != 0))
        {
          absent (revno, field + 1);
          return NULL;
        }
      if (length == field + 1)
        {
          if (date && cmpdate (date, trail->date) < 0)
            {
              RERR ("Revision %s has date %s.",
                    trail->num, date2str (trail->date, datebuf));
              return NULL;
            }
          if (author && STR_DIFF (author, trail->author))
            {
              RERR ("Revision %s has author %s.", trail->num, trail->author);
              return NULL;
            }
          if (state && STR_DIFF (state, trail->state))
            {
              RERR ("Revision %s has state %s.",
                    trail->num, trail->state ? trail->state : "<empty>");
              return NULL;
            }
        }
      bhead = trail->branches;

    }
  while ((field += 2) <= length);
  *store = NULL;
  return trail;
}

struct hshentry *
genrevs (char const *revno, char const *date, char const *author,
         char const *state, struct hshentries **store)
/* Find the deltas needed for reconstructing the revision given by ‘revno’,
   ‘date’, ‘author’, and ‘state’, and stores pointers to these deltas into a
   list whose starting address is given by ‘store’.  Return the last delta
   (target delta).  If the proper delta could not be found, return NULL.  */
{
  int length;
  register struct hshentry *next;
  int result;
  char const *branchnum;
  char datebuf[datesize + zonelenmax];

  if (!(next = ADMIN (head)))
    {
      RERR ("RCS file empty");
      goto norev;
    }

  length = countnumflds (revno);

  if (length >= 1)
    {
      /* At least one field; find branch exactly.  */
      while ((result = cmpnumfld (revno, next->num, 1)) < 0)
        {
          store1 (&store, next);
          next = next->next;
          if (!next)
            {
              RERR ("branch number %s too low", TAKE (1, revno));
              goto norev;
            }
        }

      if (result > 0)
        {
          absent (revno, 1);
          goto norev;
        }
    }
  if (length <= 1)
    {
      /* Pick latest one on given branch.  */
      branchnum = next->num;    /* works even for empty revno */
      while (next
             && cmpnumfld (branchnum, next->num, 1) == 0
             && ((date && cmpdate (date, next->date) < 0)
                 || (author && STR_DIFF (author, next->author))
                 || (state && STR_DIFF (state, next->state))))
        {
          store1 (&store, next);
          next = next->next;
        }
      if (!next || (cmpnumfld (branchnum, next->num, 1) != 0)) /* overshot */
        {
          cantfindbranch (length ? revno : TAKE (1, branchnum),
                          date, author, state);
          goto norev;
        }
      else
        {
          store1 (&store, next);
        }
      *store = NULL;
      return next;
    }

  /* Length >= 2.  Find revision; may go low if ‘length == 2’.  */
  while ((result = cmpnumfld (revno, next->num, 2)) < 0
         && (cmpnumfld (revno, next->num, 1) == 0))
    {
      store1 (&store, next);
      next = next->next;
      if (!next)
        break;
    }

  if (!next || cmpnumfld (revno, next->num, 1) != 0)
    {
      RERR ("revision number %s too low", TAKE (2, revno));
      goto norev;
    }
  if ((length > 2) && (result != 0))
    {
      absent (revno, 2);
      goto norev;
    }

  /* Print last one.  */
  store1 (&store, next);

  if (length > 2)
    return genbranch (next, revno, length, date, author, state, store);
  else
    {                                   /* length == 2 */
      if (date && cmpdate (date, next->date) < 0)
        {
          RERR ("Revision %s has date %s.",
                next->num, date2str (next->date, datebuf));
          return NULL;
        }
      if (author && STR_DIFF (author, next->author))
        {
          RERR ("Revision %s has author %s.", next->num, next->author);
          return NULL;
        }
      if (state && STR_DIFF (state, next->state))
        {
          RERR ("Revision %s has state %s.",
                next->num, next->state ? next->state : "<empty>");
          return NULL;
        }
      *store = NULL;
      return next;
    }

norev:
  return NULL;
}

struct hshentry *
gr_revno (char const *revno, struct hshentries **store)
/* An abbreviated form of ‘genrevs’, when you don't care
   about the date, author, or state.  */
{
  return genrevs (revno, NULL, NULL, NULL, store);
}

static char const *
rev_from_symbol (struct cbuf const *id)
/* Look up ‘id’ in the list of symbolic names starting with pointer
   ‘ADMIN (assocs)’, and return a pointer to the corresponding
   revision number.  Return NULL if not present.  */
{
  register struct assoc const *next;

  for (next = ADMIN (assocs); next; next = next->nextassoc)
    if (!strncmp (next->symbol, id->string, id->size))
      return next->num;
  return NULL;
}

static char const *
lookupsym (char const *id)
/* Look up ‘id’ in the list of symbolic names starting with pointer
   ‘ADMIN (assocs)’, and return a pointer to the corresponding
   revision number.  Return NULL if not present.  */
{
  register struct assoc const *next;

  for (next = ADMIN (assocs); next; next = next->nextassoc)
    if (STR_SAME (id, next->symbol))
      return next->num;
  return NULL;
}

static char const *
branchtip (char const *branch)
{
  struct hshentry *h;
  struct hshentries *hs;

  h = gr_revno (branch, &hs);
  return h ? h->num : NULL;
}

bool
fully_numeric (struct cbuf *ans, char const *source, struct fro *fp)
/* Expand ‘source’, pointing ‘ans’ at a new string in ‘SHARED’,
   with all symbolic fields replaced with their numeric values.
   Expand a branch followed by ‘.’ to the latest revision on that branch.
   Ignore ‘.’ after a revision.  Remove leading zeros.
   If ‘fp’ is non-NULL, it is used to expand "$" (i.e., ‘KDELIM’).
   Return true if successful, otherwise false.  */
{
  register char const *sp, *bp = NULL;
  int dots;
  bool have_branch = false;
  char *ugh = NULL;

  /* TODO: Allocate on ‘SINGLE’ (pending ‘free_NEXT_str’ fixup).  */

#define ACCF(...)  accf (SHARED, __VA_ARGS__)

#define FRESH()    if (ugh) brush_off (SHARED, ugh)
#define ACCB(b)    accumulate_byte (SHARED, b)
#define ACCS(s)    accs (SHARED, s)
#define ACCR(b,e)  accumulate_range (SHARED, b, e)
#define OK()       ugh = finish_string (SHARED, &ans->size), ans->string = ugh

  sp = source;
  if (!sp || !*sp)
    /* Accept NULL pointer as a legal value.  */
    goto success;
  if (sp[0] == KDELIM && !sp[1])
    {
      if (!getoldkeys (fp))
        goto sorry;
      if (!PREV (rev))
        {
          MERR ("working file lacks revision number");
          goto sorry;
        }
      ACCS (PREV (rev));
      goto success;
    }
  dots = 0;

  for (;;)
    {
      char const *was = sp;
      bool id = false;

      for (;;)
        {
          switch (ctab[(unsigned char) *sp])
            {
            case IDCHAR:
            case LETTER:
            case Letter:
              id = true;
              /* fall into */
            case DIGIT:
              sp++;
              continue;

            default:
              break;
            }
          break;
        }

      if (id)
        {
          struct cbuf orig =
            {
              .string = was,
              .size = sp - was
            };
          char const *expanded = rev_from_symbol (&orig);

          if (!expanded)
            {
              RERR ("Symbolic name `%s' is undefined.", was);
              goto sorry;
            }
          ACCS (expanded);
          have_branch = true;
        }
      else
        {
          if (was != sp)
            {
              ACCR (was, sp);
              bp = was;
            }

          /* Skip leading zeros.  */
          while ('0' == sp[0] && isdigit (sp[1]))
            sp++;

          if (!bp)
            {
              int s = 0;                /* FAKE */
              if (s || *sp != '.')
                break;
              else
                {
                  /* Insert default branch before initial ‘.’.  */
                  char const *b;

                  if (ADMIN (defbr))
                    b = ADMIN (defbr);
                  else if (ADMIN (head))
                    b = ADMIN (head)->num;
                  else
                    break;
                  OK (); FRESH ();
                  accumulate_branchno (SHARED, b);
                }
            }
        }

      switch (*sp++)
        {
        case '\0':
          goto success;

        case '.':
          if (!*sp)
            {
              if (dots & 1)
                break;
              OK ();
              if (!(bp = branchtip (ans->string)))
                goto sorry;
              ACCF ("%s%s", ans->string, bp);
              goto success;
            }
          ++dots;
          ACCB ('.');
          continue;
        }
      break;
    }

  RERR ("improper revision number: %s", source);

 sorry:
  OK ();
  FRESH ();
  return false;
 success:
  OK ();
  return true;

#undef OK
#undef ACCR
#undef ACCS
#undef ACCB
#undef FRESH
#undef ACCF
}

char const *
namedrev (char const *name, struct hshentry *delta)
/* Return ‘name’ if it names ‘delta’, NULL otherwise.  */
{
  if (name)
    {
      char const *id = NULL, *p, *val;

      for (p = name;; p++)
        switch (ctab[(unsigned char) *p])
          {
          case IDCHAR:
          case LETTER:
          case Letter:
            id = name;
            break;

          case DIGIT:
            break;

          case UNKN:
            if (!*p && id
                && (val = lookupsym (id))
                && STR_SAME (val, delta->num))
              return id;
            /* fall into */
          default:
            return NULL;
          }
    }
  return NULL;
}

char const *
tiprev (void)
{
  return ADMIN (defbr)
    ? branchtip (ADMIN (defbr))
    : ADMIN (head) ? ADMIN (head)->num : NULL;
}

/* rcsrev.c ends here */
