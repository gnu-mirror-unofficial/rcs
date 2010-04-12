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

#include "base.h"
#include <string.h>
#include <errno.h>
#include "b-complain.h"
#include "b-divvy.h"
#include <ctype.h>

static char *
sorry (bool savep, char const *msg)
{
  if (savep)
    {
      char *partial;
      size_t len;

      partial = finish_string (SINGLE, &len);
      brush_off (SINGLE, partial);
    }
  if (msg)
    MERR (msg);
  return NULL;
}

static char *
badly_terminated (bool savep)
{
  return sorry (savep, "badly terminated keyword value");
}

static char *
get0val (register int c, register RILE *fp, bool savep, bool optional)
/* Read a keyword value from `c + fp', perhaps `optional'ly.
   Same as `getval', except `c'` is the lookahead character.  */
{
  char *val = NULL;
  size_t len;
  register bool got1;

  got1 = false;
  for (;;)
    {
      switch (c)
        {
        default:
          got1 = true;
          if (savep)
            accumulate_byte (SINGLE, c);
          break;

        case ' ':
        case '\t':
          if (savep)
            {
              val = finish_string (SINGLE, &len);
#ifdef KEEPTEST
              printf ("%s: \"%s\"%s\n", __func__, val,
                      got1 ? "" : " (but that's just whitespace!)");
#endif
              if (!got1)
                {
                  brush_off (SINGLE, val);
                  val = NULL;
                }
            }
          if (got1 && !val)
            val = "non-NULL";
          return val;

        case KDELIM:
          if (!got1 && optional)
            {
              if (val)
                brush_off (SINGLE, val);
              return NULL;
            }
          /* fall into */
        case '\n':
        case '\0':
          return badly_terminated (savep);
        }
      Igeteof (fp, c, return badly_terminated (savep));
    }
}

static char *
keepid (int c, RILE *fp)
/* Get previous identifier from `c + fp'.  */
{
  char *maybe;

  if (!c)
    Igeteof (fp, c, return sorry (true, NULL));
  if (!(maybe = get0val (c, fp, true, false)))
    return NULL;
  checksid (maybe);
  if (LEX (erroneousp))
    {
      brush_off (SINGLE, maybe);
      maybe = NULL;
    }
  return maybe;
}

static char *
getval (register RILE *fp, bool savep, bool optional)
/* Read a keyword value from `fp'; return it if found, else NULL.
   Do not report an error if `optional' is set and `kdelim' is found instead.  */
{
  int c;

  Igeteof (fp, c, return badly_terminated (savep));
  return get0val (c, fp, savep, optional);
}

static int
keepdate (RILE *fp)
/* Read a date; check format; if ok, set `PREV (date)'.
   Return 0 on error, lookahead character otherwise.  */
{
  char *d, *t;
  register int c;

  c = 0;
  if ((d = getval (fp, true, false)))
    {
      if (! (t = getval (fp, true, false)))
        brush_off (SINGLE, d);
      else
        {
          Igeteof (fp, c, c = 0);
          if (!c)
            brush_off (SINGLE, t);
          else
            {
              char buf[64];
              size_t len;

              snprintf (buf, 64, "%s%s %s%s",
                        /* Parse dates put out by old versions of RCS.  */
                        (isdigit (d[0]) && isdigit (d[1]) && !isdigit (d[2])
                         ? "19"
                         : ""),
                        d, t,
                        (!strchr (t, '-') && !strchr (t, '+')
                         ? "+0000"
                         : ""));
              /* Do it twice to keep the SINGLE count synchronized.
                 If count were moot, we could simply brush off `d'.  */
              brush_off (SINGLE, t);
              brush_off (SINGLE, d);
              PREV (date) = finish_string (SINGLE, &len);
            }
        }
    }
  return c;
}

static char const *
keeprev (RILE *fp)
/* Get previous revision from `fp'.  */
{
  char *s = getval (fp, true, false);

  if (s)
    {
      register char const *sp;
      register int dotcount = 0;

      for (sp = s;; sp++)
        {
          switch (*sp)
            {
            case 0:
              if (dotcount & 1)
                goto done;
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
      MERR ("%s is not a revision number", s);
      brush_off (SINGLE, s);
      s = NULL;
    }
 done:
  return PREV (rev) = s;
}

bool
getoldkeys (register RILE *fp)
/* Try to read keyword values for author, date, revision number, and
   state out of the file `fp'.  If `fp' is NULL, `MANI (filename)' is
   opened and closed instead of using `fp'.  The results are placed into
   MANI (prev): .author, .date, .name, .rev and .state members.  On
   error, stop searching and return false.  Returning true doesn't mean
   that any of the values were found; instead, caller must check to see
   whether the corresponding arrays contain the empty string.  */
{
  register int c;
  char keyword[keylength + 1];
  register char *tp;
  bool needs_closing;
  struct pool_found match;

  if (PREV (valid))
    return true;

  needs_closing = false;
  if (!fp)
    {
      if (!(fp = Iopen (MANI (filename), FOPEN_R_WORK, NULL)))
        {
          syserror_errno (MANI (filename));
          return false;
        }
      needs_closing = true;
    }

#define KEEPID(c,which)  (PREV (which) = keepid (c, fp))

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

          recognize_keyword (keyword, &match);
          switch (match.i)
            {
            case Author:
              if (!KEEPID ('\0', author))
                goto badness;
              c = 0;
              break;
            case Date:
              if (!(c = keepdate (fp)))
                goto badness;
              break;
            case Header:
            case Id:
              if (!(getval (fp, false, false)
                    && keeprev (fp)
                    && (c = keepdate (fp))
                    && KEEPID (c, author)
                    && KEEPID ('\0', state)))
                goto badness;
              /* Skip either ``who'' (new form) or ``Locker: who'' (old).  */
              if (getval (fp, false, true) && getval (fp, false, true))
                c = 0;
              else if (LEX (erroneousp))
                goto badness;
              else
                c = KDELIM;
              break;
            case Locker:
              getval (fp, false, false);
              c = 0;
              break;
            case Log:
            case RCSfile:
            case Source:
              if (!getval (fp, false, false))
                goto badness;
              c = 0;
              break;
            case Name:
              if ((PREV (name) = getval (fp, true, false))
                  && *PREV (name))
                checkssym (PREV (name));
              c = 0;
              break;
            case Revision:
              if (!keeprev (fp))
                goto badness;
              c = 0;
              break;
            case State:
              if (!KEEPID ('\0', state))
                goto badness;
              c = 0;
              break;
            default:
              continue;
            }
          if (!c)
            Igeteof (fp, c, c = 0);
          if (c != KDELIM)
            {
              MERR ("closing %c missing on keyword", KDELIM);
              goto badness;
            }
          if (PREV (name)
              && PREV (author) && PREV (date)
              && PREV (rev) && PREV (state))
            break;
        }
      Igeteof (fp, c, goto ok);
    }

 ok:
  if (needs_closing)
    Ifclose (fp);
  else
    Irewind (fp);
  /* Prune empty strings.  */
#define PRUNE(which)                            \
  if (PREV (which) && ! *PREV (which))          \
    PREV (which) = NULL
  PRUNE (name);
  PRUNE (author);
  PRUNE (date);
  PRUNE (rev);
  PRUNE (state);
#undef PRUNE
  PREV (valid) = true;
  return true;

 badness:
  return false;
}

#ifdef KEEPTEST
/* Print the keyword values found.  */

const struct program program = { .name = "keeptest" };

int
main (int argc, char *argv[])
{
  while (*(++argv))
    {
      MANI (filename) = *argv;
      getoldkeys (NULL);
      printf ("%s:  revision: %s, date: %s, author: %s, name: %s, state: %s\n",
              *argv, PREV (rev), PREV (date), PREV (author),
              PREV (name), PREV (state));
    }
  return EXIT_SUCCESS;
}
#endif  /* KEEPTEST */

/* rcskeep.c ends here */
