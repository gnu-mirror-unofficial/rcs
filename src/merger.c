/* three-way file merge internals

   Copyright (C) 2010 Thien-Thi Nguyen
   Copyright (C) 1991, 1992, 1993, 1994, 1995 Paul Eggert

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
#include <stdlib.h>
#include "b-complain.h"

static char const *
normalize_arg (char const *s, char **b)
/* If `s' looks like an option, prepend ./ to it.  Return the result.
   Set `*b' to the address of any storage that was allocated.  */
{
  char *t;

  if (*s == '-')
    {
      *b = t = testalloc (strlen (s) + 3);
      sprintf (t, ".%c%s", SLASH, s);
      return t;
    }
  else
    {
      *b = NULL;
      return s;
    }
}

int
merge (bool tostdout, char const *edarg, char const *const label[3],
       char const *const argv[3])
/* Do `merge [-p] EDARG -L l0 -L l1 -L l2 a0 a1 a2', where `tostdout'
   specifies whether `-p' is present, `edarg' gives the editing type
   (e.g. "-A", or null for the default), `label' gives l0, l1 and l2, and
   `argv' gives a0, a1 and a2.  Return `DIFF_SUCCESS' or `DIFF_FAILURE'.  */
{
  register int i;
  FILE *f;
  RILE *rt;
  char const *a[3], *t;
  char *b[3];
  int s;
#if !DIFF3_BIN
  char const *d[2];
#endif

  for (i = 3; 0 <= --i;)
    a[i] = normalize_arg (argv[i], &b[i]);

  if (!edarg)
    edarg = "-E";

#if DIFF3_BIN
  t = NULL;
  if (!tostdout)
    t = maketemp (0);
  s = run (-1, t, prog_diff3, edarg, "-am",
           "-L", label[0], "-L", label[1], "-L", label[2],
           a[0], a[1], a[2], NULL);
  if (DIFF_TROUBLE == s)
    program.exiterr ();
  if (DIFF_FAILURE == s)
    PWARN ("conflicts during merge");
  if (t)
    {
      if (!(f = fopenSafer (argv[0], "w")))
        fatal_sys (argv[0]);
      if (!(rt = Iopen (t, "r", NULL)))
        fatal_sys (t);
      fastcopy (rt, f);
      Ifclose (rt);
      Ofclose (f);
    }
#else  /* !DIFF3_BIN */
  for (i = 0; i < 2; i++)
    if (DIFF_TROUBLE == run (-1, d[i] = maketemp (i), prog_diff,
                             a[i], a[2], NULL))
      PFATAL ("diff failed");
  t = maketemp (2);
  s = run (-1, t,
           prog_diff3, edarg, d[0], d[1], a[0], a[1], a[2],
           label[0], label[2], NULL);
  if (s != DIFF_SUCCESS)
    {
      s = DIFF_FAILURE;
      PWARN ("overlaps or other problems during merge");
    }
  if (!(f = fopenSafer (t, "a+")))
    fatal_sys (t);
  aputs (tostdout ? "1,$p\n" : "w\n", f);
  Orewind (f);
  aflush (f);
  if (run (fileno (f), NULL, ED, "-", a[0], NULL))
    program.exiterr ();
  Ofclose (f);
#endif  /* !DIFF3_BIN */

  tempunlink ();
  for (i = 3; 0 <= --i;)
    if (b[i])
      tfree (b[i]);
  return s;
}

/* merger.c ends here */
