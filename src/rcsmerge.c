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

#include "base.h"
#include <string.h>
#include "rcsmerge.help"
#include "b-complain.h"
#include "b-divvy.h"
#include "b-feph.h"
#include "b-fro.h"

struct top *top;

static exiting void
exiterr (void)
{
  tempunlink ();
  _Exit (DIFF_TROUBLE);
}

/*:help
[options] file
Options:
  -p[REV]       Write to stdout instead of overwriting the working file.
  -q[REV]       Quiet mode.
  -rREV         (one or two times) specify a revision.
  -kSUBST       Substitute using mode SUBST (see co(1)).
  -V            Like --version.
  -VN           Emulate RCS version N.
  -xSUFF        Specify SUFF as a slash-separated list of suffixes
                used to identify RCS file names.
  -zZONE        Specify date output format in keyword-substitution.

One or two revisions must be specified (using -p, -q, or -r).
If only one is specified, use the latest revision on the default
branch to be the second revision.
*/

const struct program program =
  {
    .name = "rcsmerge",
    .help = help,
    .exiterr = exiterr
  };

#define quietarg  "-q"

int
main (int argc, char **argv)
{
  register int i;
  char *a, **newargv;
  char const *arg[3];
  char const *rev[3], *xrev[3];         /*revision numbers */
  char const *edarg, *expandarg, *suffixarg, *versionarg, *zonearg;
  bool tostdout;
  int status;
  struct fro *workptr;
  struct hshentry *target;

  CHECK_HV ();
  gnurcs_init ();

  edarg = rev[1] = rev[2] = NULL;
  status = 0;                           /* Keep lint happy.  */
  tostdout = false;
  expandarg = suffixarg = versionarg = zonearg = quietarg;      /* no-op */
  BE (pe) = X_DEFAULT;

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
          BE (quiet) = true;
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
            PERR ("too many revision numbers");
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
          BE (pe) = a;
          break;
        case 'z':
          zonearg = *argv;
          zone_set (a);
          break;
        case 'T':
          /* Ignore ‘-T’, so that env var ‘RCSINIT’ can contain ‘-T’.  */
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
          bad_option (*argv);
        };
    }
  /* (End of option processing.)  */

  if (!rev[1])
    PFATAL ("no base revision number given");

  /* Now handle all filenames.  */
  if (!FLOW (erroneousp))
    {
      if (argc < 1)
        PFATAL ("no input file");
      if (0 < pairnames (argc, argv, rcsreadopen, true, false))
        {
          struct cbuf numericrev;

          if (argc > 2 || (argc == 2 && argv[1]))
            PWARN ("excess arguments ignored");
          if (BE (kws) == kwsub_b)
            MERR ("merging binary files");
          diagnose ("RCS file: %s", REPO (filename));
          if (!(workptr = fro_open (MANI (filename), FOPEN_R_WORK, NULL)))
            fatal_sys (MANI (filename));

          if (!REPO (tip))
            RFATAL ("no revisions present");

          if (!*rev[1])
            rev[1] = GROK (branch) ? GROK (branch) : REPO (tip)->num;
          if (fully_numeric (&numericrev, rev[1], workptr)
              && (target = delta_from_ref (numericrev.string)))
            {
              xrev[1] = target->num;
              if (!rev[2] || !*rev[2])
                rev[2] = GROK (branch) ? GROK (branch) : REPO (tip)->num;
              if (fully_numeric (&numericrev, rev[2], workptr)
                  && (target = delta_from_ref (numericrev.string)))
                {
                  xrev[2] = target->num;

                  if (STR_SAME (xrev[1], xrev[2]))
                    {
                      if (tostdout)
                        {
                          fro_spew (workptr, stdout);
                          fclose (stdout);
                        }
                    }
                  else
                    {
                      fro_zclose (&workptr);

                      for (i = 1; i <= 2; i++)
                        {
                          struct cbuf commarg = minus_p (xrev[i], rev[i]);

                          if (run (-1,
                                   /* Don't collide with merger.c ‘maketemp’.  */
                                   arg[i] = maketemp (i + 2),
                                   prog_co, quietarg, commarg.string,
                                   expandarg, suffixarg, versionarg, zonearg,
                                   REPO (filename), NULL))
                            RFATAL ("co failed");
                        }
                      diagnose
                        ("Merging differences between %s and %s into %s%s",
                         xrev[1], xrev[2], MANI (filename),
                         tostdout ? "; result to stdout" : "");

                      arg[0] = xrev[0] = MANI (filename);
                      status = merge (tostdout, edarg, xrev, arg);
                    }
                }
            }

          fro_zclose (&workptr);
        }
    }
  tempunlink ();
  return FLOW (erroneousp) ? DIFF_TROUBLE : status;
}

/* rcsmerge.c ends here */
