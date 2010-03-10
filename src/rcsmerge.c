/* Merge RCS revisions.

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
#include <stdbool.h>
#include "rcsmerge-help.c"

char const cmdid[] = "rcsmerge";

/*:help
[options] file

Incorporate the changes between two revisions of an RCS file
into the corresponding working file.

One or two revisions must be specified (using -p, -q, or -r).
If only one is specified, use the latest revision on the default
branch to be the second revision.

Options:
  -p[REV] -- write to stdout instead of overwriting the working file
  -q[rev] -- quiet mode
  -rREV   -- (one or two times) specify a revision
  -kSUBST -- substitute using mode SUBST (see co(1))
  -V[N]   -- if N is not specified, behave like --version;
             otherwise, N specifies the RCS version to emulate
  -xSUFF  -- specify SUFF as a slash-separated list of suffixes
             used to identify RCS file names
  -zZONE  -- specify date output format in keyword-substitution
*/

int
main (int argc, char **argv)
{
  static char const quietarg[] = "-q";

  register int i;
  char *a, **newargv;
  char const *arg[3];
  char const *rev[3], *xrev[3]; /*revision numbers */
  char const *edarg, *expandarg, *suffixarg, *versionarg, *zonearg;
  int tostdout;
  int status;
  RILE *workptr;
  struct buf commarg;
  struct buf numericrev;        /* holds expanded revision number */
  struct hshentries *gendeltas; /* deltas to be generated */
  struct hshentry *target;

  CHECK_HV ();

  bufautobegin (&commarg);
  bufautobegin (&numericrev);
  edarg = rev[1] = rev[2] = NULL;
  status = 0;                   /* Keep lint happy.  */
  tostdout = false;
  expandarg = suffixarg = versionarg = zonearg = quietarg;      /* no-op */
  suffixes = X_DEFAULT;

  argc = getRCSINIT (argc, argv, &newargv);
  argv = newargv;
  while (a = *++argv, 0 < --argc && *a++ == '-')
    {
      switch (*a++)
        {
        case 'p':
          tostdout = true;
          goto revno;

        case 'q':
          quietflag = true;
        revno:
          if (!*a)
            break;
          /* falls into -r */
        case 'r':
          if (!rev[1])
            rev[1] = a;
          else if (!rev[2])
            rev[2] = a;
          else
            error ("too many revision numbers");
          break;

        case 'A':
        case 'E':
        case 'e':
          if (*a)
            goto unknown;
          edarg = *argv;
          break;

        case 'x':
          suffixarg = *argv;
          suffixes = a;
          break;
        case 'z':
          zonearg = *argv;
          zone_set (a);
          break;
        case 'T':
          /* Ignore -T, so that RCSINIT can contain -T.  */
          if (*a)
            goto unknown;
          break;
        case 'V':
          versionarg = *argv;
          setRCSversion (versionarg);
          break;

        case 'k':
          expandarg = *argv;
          if (0 <= str2expmode (expandarg + 2))
            break;
          /* fall into */
        default:
        unknown:
          error ("unknown option: %s", *argv);
        };
    }                           /* end of option processing */

  if (!rev[1])
    faterror ("no base revision number given");

  /* Now handle all pathnames.  */

  if (!nerror)
    {
      if (argc < 1)
        faterror ("no input file");
      if (0 < pairnames (argc, argv, rcsreadopen, true, false))
        {

          if (argc > 2 || (argc == 2 && argv[1]))
            warn ("excess arguments ignored");
          if (Expand == BINARY_EXPAND)
            workerror ("merging binary files");
          diagnose ("RCS file: %s\n", RCSname);
          if (!(workptr = Iopen (workname, FOPEN_R_WORK, NULL)))
            efaterror (workname);

          gettree ();           /* reads in the delta tree */

          if (!Head)
            rcsfaterror ("no revisions present");

          if (!*rev[1])
            rev[1] = Dbranch ? Dbranch : Head->num;
          if (fexpandsym (rev[1], &numericrev, workptr)
              && (target = gr_revno (numericrev.string, &gendeltas)))
            {
              xrev[1] = target->num;
              if (!rev[2] || !*rev[2])
                rev[2] = Dbranch ? Dbranch : Head->num;
              if (fexpandsym (rev[2], &numericrev, workptr)
                  && (target = gr_revno (numericrev.string, &gendeltas)))
                {
                  xrev[2] = target->num;

                  if (strcmp (xrev[1], xrev[2]) == 0)
                    {
                      if (tostdout)
                        {
                          fastcopy (workptr, stdout);
                          Ofclose (stdout);
                        }
                    }
                  else
                    {
                      Izclose (&workptr);

                      for (i = 1; i <= 2; i++)
                        {
                          diagnose ("retrieving revision %s\n", xrev[i]);
                          bufscpy (&commarg, "-p");
                          bufscat (&commarg, rev[i]);   /* not xrev[i], for $Name's sake */
                          if (run (-1,
                                   /* Do not collide with merger.c maketemp().  */
                                   arg[i] = maketemp (i + 2),
                                   prog_co, quietarg, commarg.string,
                                   expandarg, suffixarg, versionarg, zonearg,
                                   RCSname, NULL))
                            rcsfaterror ("co failed");
                        }
                      diagnose
                        ("Merging differences between %s and %s into %s%s\n",
                         xrev[1], xrev[2], workname,
                         tostdout ? "; result to stdout" : "");

                      arg[0] = xrev[0] = workname;
                      status = merge (tostdout, edarg, xrev, arg);
                    }
                }
            }

          Izclose (&workptr);
        }
    }
  tempunlink ();
  return nerror ? diff_trouble : status;
}

void
exiterr (void)
{
  tempunlink ();
  _exit (diff_trouble);
}
