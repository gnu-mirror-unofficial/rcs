/* Extract RCS keyword string values from working files.

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

bool prevkeys;
struct buf prevauthor, prevdate, prevname, prevrev, prevstate;

static bool
badly_terminated (void)
{
  workerror ("badly terminated keyword value");
  return false;
}

static bool
get0val (register int c, register RILE *fp, struct buf *target, bool optional)
/* Read a keyword value from `c + fp' into `target', perhaps `optional'ly.
   Same as `getval', except `c'` is the lookahead character.  */
{
  register char *tp;
  char const *tlim;
  register bool got1;

  if (target)
    {
      bufalloc (target, 1);
      tp = target->string;
      tlim = tp + target->size;
    }
  else
    tlim = tp = NULL;
  got1 = false;
  for (;;)
    {
      switch (c)
        {
        default:
          got1 = true;
          if (tp)
            {
              *tp++ = c;
              if (tlim <= tp)
                tp = bufenlarge (target, &tlim);
            }
          break;

        case ' ':
        case '\t':
          if (tp)
            {
              *tp = '\0';
#ifdef KEEPTEST
              printf ("getval: %s\n", target);
#endif
            }
          return got1;

        case KDELIM:
          if (!got1 && optional)
            return false;
          /* fall into */
        case '\n':
        case 0:
          return badly_terminated ();
        }
      Igeteof (fp, c, return badly_terminated ());
    }
}

static bool
keepid (int c, RILE *fp, struct buf *b)
/* Get previous identifier from `c + fp' into `b'.  */
{
  if (!c)
    Igeteof (fp, c, return false);
  if (!get0val (c, fp, b, false))
    return false;
  checksid (b->string);
  return !nerror;
}

static bool
getval (register RILE *fp, struct buf *target, bool optional)
/* Read a keyword value from `fp' into `target'.  Returns true if one is
   found, false otherwise.  Does not modify target if it is 0.  Do not report
   an error if `optional' is set and `kdelim' is found instead.  */
{
  int c;

  Igeteof (fp, c, return badly_terminated ());
  return get0val (c, fp, target, optional);
}

static int
keepdate (RILE *fp)
/* Read a date `prevdate'; check format.
   Return 0 on error, lookahead character otherwise.  */
{
  struct buf prevday, prevtime;
  register int c;

  c = 0;
  bufautobegin (&prevday);
  if (getval (fp, &prevday, false))
    {
      bufautobegin (&prevtime);
      if (getval (fp, &prevtime, false))
        {
          Igeteof (fp, c, c = 0);
          if (c)
            {
              register char const *d = prevday.string, *t = prevtime.string;

              bufalloc (&prevdate, strlen (d) + strlen (t) + 9);
              sprintf (prevdate.string, "%s%s %s%s",
                       /* Parse dates put out by old versions of RCS.  */
                       (isdigit (d[0]) && isdigit (d[1]) && !isdigit (d[2])
                        ? "19" : ""),
                       d, t, (strchr (t, '-') || strchr (t, '+')
                              ? "" : "+0000"));
            }
        }
      bufautoend (&prevtime);
    }
  bufautoend (&prevday);
  return c;
}

static bool
checknum (char const *s)
{
  register char const *sp;
  register int dotcount = 0;

  for (sp = s;; sp++)
    {
      switch (*sp)
        {
        case 0:
          if (dotcount & 1)
            return true;
          else
            break;

        case '.':
          dotcount++;
          continue;

        default:
          if (isdigit (*sp))
            continue;
          break;
        }
      break;
    }
  workerror ("%s is not a revision number", s);
  return false;
}

static bool
keeprev (RILE *fp)
/* Get previous revision from `fp' into `prevrev'.  */
{
  return getval (fp, &prevrev, false) && checknum (prevrev.string);
}

bool
getoldkeys (register RILE *fp)
/* Try to read keyword values for author, date, revision number, and
   state out of the file `fp'.  If `fp' is NULL, `workname' is opened
   and closed instead of using `fp'.  The results are placed into
   `prevauthor', `prevdate', `prevname', `prevrev', `prevstate'.  On
   error, stop searching and return false.  Returning true doesn't mean
   that any of the values were found; instead, caller must check to see
   whether the corresponding arrays contain the empty string.  */
{
  register int c;
  char keyword[keylength + 1];
  register char *tp;
  bool needs_closing, prevname_found;

  if (prevkeys)
    return true;

  needs_closing = false;
  if (!fp)
    {
      if (!(fp = Iopen (workname, FOPEN_R_WORK, NULL)))
        {
          eerror (workname);
          return false;
        }
      needs_closing = true;
    }

  /* Initializee to empty.  */
  bufscpy (&prevauthor, "");
  bufscpy (&prevdate, "");
  bufscpy (&prevname, "");
  prevname_found = false;
  bufscpy (&prevrev, "");
  bufscpy (&prevstate, "");

  /* We can use anything but `KDELIM' here.  */
  c = '\0';
  for (;;)
    {
      if (c == KDELIM)
        {
          do
            {
              /* Try to get keyword.  */
              tp = keyword;
              for (;;)
                {
                  Igeteof (fp, c, goto ok);
                  switch (c)
                    {
                    default:
                      if (keyword + keylength <= tp)
                        break;
                      *tp++ = c;
                      continue;

                    case '\n':
                    case KDELIM:
                    case VDELIM:
                      break;
                    }
                  break;
                }
            }
          while (c == KDELIM);
          if (c != VDELIM)
            continue;
          *tp = c;
          Igeteof (fp, c, goto ok);
          switch (c)
            {
            case ' ':
            case '\t':
              break;
            default:
              continue;
            }

          switch (trymatch (keyword))
            {
            case Author:
              if (!keepid (0, fp, &prevauthor))
                return false;
              c = 0;
              break;
            case Date:
              if (!(c = keepdate (fp)))
                return false;
              break;
            case Header:
            case Id:
              if (!(getval (fp, NULL, false)
                    && keeprev (fp)
                    && (c = keepdate (fp))
                    && keepid (c, fp, &prevauthor)
                    && keepid (0, fp, &prevstate)))
                return false;
              /* Skip either ``who'' (new form) or ``Locker: who'' (old).  */
              if (getval (fp, NULL, true) && getval (fp, NULL, true))
                c = 0;
              else if (nerror)
                return false;
              else
                c = KDELIM;
              break;
            case Locker:
              (void) getval (fp, NULL, false);
              c = 0;
              break;
            case Log:
            case RCSfile:
            case Source:
              if (!getval (fp, NULL, false))
                return false;
              c = 0;
              break;
            case Name:
              if (getval (fp, &prevname, false))
                {
                  if (*prevname.string)
                    checkssym (prevname.string);
                  prevname_found = true;
                }
              c = 0;
              break;
            case Revision:
              if (!keeprev (fp))
                return false;
              c = 0;
              break;
            case State:
              if (!keepid (0, fp, &prevstate))
                return false;
              c = 0;
              break;
            default:
              continue;
            }
          if (!c)
            Igeteof (fp, c, c = 0);
          if (c != KDELIM)
            {
              workerror ("closing %c missing on keyword", KDELIM);
              return false;
            }
          if (prevname_found
              && *prevauthor.string && *prevdate.string
              && *prevrev.string && *prevstate.string)
            break;
        }
      Igeteof (fp, c, goto ok);
    }

 ok:
  if (needs_closing)
    Ifclose (fp);
  else
    Irewind (fp);
  prevkeys = true;
  return true;
}

#ifdef KEEPTEST
/* Print the keyword values found.  */

char const cmdid[] = "keeptest";

int
main (int argc, char *argv[])
{
  while (*(++argv))
    {
      workname = *argv;
      getoldkeys (NULL);
      printf ("%s:  revision: %s, date: %s, author: %s, name: %s, state: %s\n",
              *argv, prevrev.string, prevdate.string, prevauthor.string,
              prevname.string, prevstate.string);
    }
  return EXIT_SUCCESS;
}
#endif  /* KEEPTEST */

/* rcskeep.c ends here */
