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

#include "rcsbase.h"
#include <ctype.h>

int
countnumflds (char const *s)
/* Given a pointer `s' to a dotted number (date or revision number),
   return the number of digitfields in `s'.  */
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

void
getbranchno (char const *revno, struct buf *branchno)
/* Given a revision number `revno', copy the number of the branch on which
   `revno' is into `branchno'.  If `revno' itself is a branch number, copy
   it unchanged.  */
{
  register int numflds;
  register char *tp;

  bufscpy (branchno, revno);
  numflds = countnumflds (revno);
  if (!(numflds & 1))
    {
      tp = branchno->string;
      while (--numflds)
        while (*tp++ != '.')
          continue;
      *(tp - 1) = '\0';
    }
}

int
cmpnum (char const *num1, char const *num2)
/* Compare the two dotted numbers `num1' and `num2' lexicographically
   by field.  Individual fields are compared numerically.
   Return <0, 0, >0 if `num1 < num2', `num1 == num2', `num1 > num2',
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
/* Compare the two dotted numbers at field `fld'.
   `num1' and `num2' must have at least `fld' fields.
   `fld' must be positive.  */
{
  register char const *s1, *s2;
  register size_t d1, d2;

  s1 = num1;
  s2 = num2;
  /* Skip `fld - 1' fields.  */
  while (--fld)
    {
      while (*s1++ != '.')
        continue;
      while (*s2++ != '.')
        continue;
    }
  /* Now `s1' and `s2' point to the beginning of the respective fields.  */
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
/* Compare the two dates.  This is just like `cmpnum',
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

  rcserror ("No revision on branch %s has%s%s%s%s%s%s.",
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
  struct buf t;

  bufautobegin (&t);
  rcserror ("%s %s absent", field & 1 ? "revision" : "branch",
            partialno (&t, revno, field));
  bufautoend (&t);
}

int
compartial (char const *num1, char const *num2, int length)
/* Compare the first `length' fields of two dot numbers;
   the omitted field is considered to be larger than any number.
   Restriction: At least one number has `length' or more fields.  */
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

char *
partialno (struct buf *rev1, char const *rev2, register int length)
/* Copy `length' fields of revision number `rev2' into `rev1'.
   Return the string of `rev1'.  */
{
  register char *r1;

  bufscpy (rev1, rev2);
  r1 = rev1->string;
  while (length)
    {
      while (*r1 != '.' && *r1)
        ++r1;
      ++r1;
      length--;
    }
  /* Eliminate last '.'.  */
  *(r1 - 1) = '\0';
  return rev1->string;
}

static void
store1 (struct hshentries ***store, struct hshentry *next)
/* Allocate a new list node that addresses `next'.
   Append it to the list that `**store' is the end pointer of.  */
{
  register struct hshentries *p;

  p = ftalloc (struct hshentries);
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
   `store'.  `revno' must be on a side branch.  Return NULL on error.
 */
{
  int field;
  register struct hshentry *next, *trail;
  register struct branchhead const *bhead;
  int result;
  struct buf t;
  char datebuf[datesize + zonelenmax];

  field = 3;
  bhead = bpoint->branches;

  do
    {
      if (!bhead)
        {
          bufautobegin (&t);
          rcserror ("no side branches present for %s",
                    partialno (&t, revno, field - 1));
          bufautoend (&t);
          return NULL;
        }

      /* Find branch head.  Branches are arranged in increasing order.  */
      while (0 < (result = cmpnumfld (revno, bhead->hsh->num, field)))
        {
          bhead = bhead->nextbranch;
          if (!bhead)
            {
              bufautobegin (&t);
              rcserror ("branch number %s too high",
                        partialno (&t, revno, field));
              bufautoend (&t);
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
                  && (!author || strcmp (author, next->author) == 0)
                  && (!state || strcmp (state, next->state) == 0))
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
          bufautobegin (&t);
          rcserror ("revision number %s too low",
                    partialno (&t, revno, field + 1));
          bufautoend (&t);
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
              rcserror ("Revision %s has date %s.",
                        trail->num, date2str (trail->date, datebuf));
              return NULL;
            }
          if (author && strcmp (author, trail->author) != 0)
            {
              rcserror ("Revision %s has author %s.",
                        trail->num, trail->author);
              return NULL;
            }
          if (state && strcmp (state, trail->state) != 0)
            {
              rcserror ("Revision %s has state %s.",
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
/* Find the deltas needed for reconstructing the revision given by `revno',
   `date', `author', and `state', and stores pointers to these deltas into a
   list whose starting address is given by `store'.  Return the last delta
   (target delta).  If the proper delta could not be found, return NULL.  */
{
  int length;
  register struct hshentry *next;
  int result;
  char const *branchnum;
  struct buf t;
  char datebuf[datesize + zonelenmax];

  bufautobegin (&t);

  if (!(next = Head))
    {
      rcserror ("RCS file empty");
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
              rcserror ("branch number %s too low", partialno (&t, revno, 1));
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
                 || (author && strcmp (author, next->author) != 0)
                 || (state && strcmp (state, next->state) != 0)))
        {
          store1 (&store, next);
          next = next->next;
        }
      if (!next || (cmpnumfld (branchnum, next->num, 1) != 0)) /* overshot */
        {
          cantfindbranch (length ? revno : partialno (&t, branchnum, 1),
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

  /* Length >= 2.  Find revision; may go low if `length == 2'.  */
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
      rcserror ("revision number %s too low", partialno (&t, revno, 2));
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
          rcserror ("Revision %s has date %s.",
                    next->num, date2str (next->date, datebuf));
          return NULL;
        }
      if (author && strcmp (author, next->author) != 0)
        {
          rcserror ("Revision %s has author %s.", next->num, next->author);
          return NULL;
        }
      if (state && strcmp (state, next->state) != 0)
        {
          rcserror ("Revision %s has state %s.",
                    next->num, next->state ? next->state : "<empty>");
          return NULL;
        }
      *store = NULL;
      return next;
    }

norev:
  bufautoend (&t);
  return NULL;
}

struct hshentry *
gr_revno (char const *revno, struct hshentries **store)
/* An abbreviated form of `genrevs', when you don't care
   about the date, author, or state.  */
{
  return genrevs (revno, NULL, NULL, NULL, store);
}

static char const *
lookupsym (char const *id)
/* Look up `id' in the list of symbolic names starting with pointer
   `Symbols', and return a pointer to the corresponding revision number.
   Return NULL if not present.  */
{
  register struct assoc const *next;

  for (next = Symbols; next; next = next->nextassoc)
    if (strcmp (id, next->symbol) == 0)
      return next->num;
  return NULL;
}

bool
expandsym (char const *source, struct buf *target)
/* `source' points to a revision number.  Copy the number to `target',
   but replace all symbolic fields in the source number with their
   numeric values.  Expand a branch followed by `.' to the latest
   revision on that branch.  Ignore `.' after a revision.  Remove
   leading zeros.  Return false on error.  */
{
  return fexpandsym (source, target, NULL);
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
fexpandsym (char const *source, struct buf *target, RILE *fp)
/* Same as `expandsym', except if `fp' is non-NULL,
   it is used to expand `KDELIM'.  */
{
  register char const *sp, *bp;
  register char *tp;
  char const *tlim;
  int dots;

  sp = source;
  bufalloc (target, 1);
  tp = target->string;
  if (!sp || !*sp)
    {
      /* Accept 0 pointer as a legal value.  */
      *tp = '\0';
      return true;
    }
  if (sp[0] == KDELIM && !sp[1])
    {
      if (!getoldkeys (fp))
        return false;
      if (!PREV (rev))
        {
          workerror ("working file lacks revision number");
          return false;
        }
      bufscpy (target, PREV (rev));
      return true;
    }
  tlim = tp + target->size;
  dots = 0;

  for (;;)
    {
      register char *p = tp;
      size_t s = tp - target->string;
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
              if (tlim <= p)
                p = bufenlarge (target, &tlim);
              *p++ = *sp++;
              continue;

            default:
              break;
            }
          break;
        }
      if (tlim <= p)
        p = bufenlarge (target, &tlim);
      *p = '\0';
      tp = target->string + s;

      if (id)
        {
          bp = lookupsym (tp);
          if (!bp)
            {
              rcserror ("Symbolic name `%s' is undefined.", tp);
              return false;
            }
        }
      else
        {
          /* Skip leading zeros.  */
          for (bp = tp; *bp == '0' && isdigit (bp[1]); bp++)
            continue;

          if (!*bp)
            {
              if (s || *sp != '.')
                break;
              else
                {
                  /* Insert default branch before initial `.'.  */
                  char const *b;

                  if (Dbranch)
                    b = Dbranch;
                  else if (Head)
                    b = Head->num;
                  else
                    break;
                  getbranchno (b, target);
                  bp = tp = target->string;
                  tlim = tp + target->size;
                }
            }
        }

      while ((*tp++ = *bp++))
        if (tlim <= tp)
          tp = bufenlarge (target, &tlim);

      switch (*sp++)
        {
        case '\0':
          return true;

        case '.':
          if (!*sp)
            {
              if (dots & 1)
                break;
              if (!(bp = branchtip (target->string)))
                return false;
              bufscpy (target, bp);
              return true;
            }
          ++dots;
          tp[-1] = '.';
          continue;
        }
      break;
    }

  rcserror ("improper revision number: %s", source);
  return false;
}

char const *
namedrev (char const *name, struct hshentry *delta)
/* Return `name' if it names `delta', NULL otherwise.  */
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
                && strcmp (val, delta->num) == 0)
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
  return Dbranch ? branchtip (Dbranch) : Head ? Head->num : NULL;
}

#ifdef REVTEST
/* Test the routines that generate a sequence of delta numbers
  needed to regenerate a given delta.  */

static exiting void
exiterr (void)
{
  _Exit (EXIT_FAILURE);
}

const struct program program =
  {
    .name = "revtest"
    .exiterr = exiterr
  };

int
main (int argc, char *argv[])
{
  static struct buf numricrevno;
  char symrevno[100];           /* used for input of revision numbers */
  char author[20];
  char state[20];
  char date[20];
  struct hshentries *gendeltas;
  struct hshentry *target;
  int i;

  if (argc < 2)
    {
      aputs ("No input file\n", stderr);
      return EXIT_FAILURE;
    }
  if (!(finptr = Iopen (argv[1], FOPEN_R, NULL)))
    {
      faterror ("can't open input file %s", argv[1]);
    }
  Lexinit ();
  getadmin ();

  gettree ();

  getdesc (false);

  do
    {
      /* All output goes to stderr, to have diagnostics and
         errors in sequence.  */
      aputs ("\nEnter revision number or <return> or '.': ", stderr);
      if (!gets (symrevno))
        break;
      if (*symrevno == '.')
        break;
      aprintf (stderr, "%s;\n", symrevno);
      expandsym (symrevno, &numricrevno);
      aprintf (stderr, "expanded number: %s; ", numricrevno.string);
      aprintf (stderr, "Date: ");
      gets (date);
      aprintf (stderr, "%s; ", date);
      aprintf (stderr, "Author: ");
      gets (author);
      aprintf (stderr, "%s; ", author);
      aprintf (stderr, "State: ");
      gets (state);
      aprintf (stderr, "%s;\n", state);
      target =
        genrevs (numricrevno.string, *date ? date : NULL,
                 *author ? author : NULL, *state ? state : NULL,
                 &gendeltas);
      if (target)
        {
          while (gendeltas)
            {
              aprintf (stderr, "%s\n", gendeltas->first->num);
              gendeltas = gendeltas->next;
            }
        }
    }
  while (true);
  aprintf (stderr, "done\n");
  return EXIT_SUCCESS;
}

#endif  /* defined REVTEST */

/* rcsrev.c ends here */
