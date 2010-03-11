/* Compare RCS revisions.

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
#include "rcsdiff-help.c"

#if DIFF_L
static char const *setup_label (struct buf *, char const *, char const[datesize]);
#endif
static void cleanup (void);

static int exitstatus;
static RILE *workptr;
static struct stat workstat;

char const cmdid[] = "rcsdiff";

/*:help
[options] file ...

Run diff(1) to compare two revisions of each file given.
FILE... names the working file, or the RCS file, or a series
of alternating WORKING-FILE RCS-FILE pairs.

If REV1 and REV2 are specified, compare those revisions.
If only REV1 is specified, compare the working file with it.
If no revisions are specified, compare the working file with
the latest revision on the default branch.

Options:
  -rREV   -- (zero, one, or two times) name a revision
  -kSUBST -- substitute using mode SUBST (see co(1))
  -q      -- quiet mode
  -V[N]   -- if N is not specified, behave like --version;
             otherwise, N specifies the RCS version to emulate
  -xSUFF  -- specify SUFF as a slash-separated list of suffixes
             used to identify RCS file names
  -zZONE  -- specify date output format in keyword-substitution

Additionally, the following options (and their argument, if any) are
passed to the underlying diff(1) command:
  -0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -B, -B, -C, -D, -F, -H, -I,
  -L, -U, -W, -a, -b, -c, -d, -e, -f, -h, -i, -n, -p, -t, -u, -w, -y,
  [long options (that start with "--")].
(Not all of these options are meaningful.)
*/

int
main (int argc, char **argv)
{
  int revnums;                  /* counter for revision numbers given */
  char const *rev1, *rev2;      /* revision numbers from command line */
  char const *xrev1, *xrev2;    /* expanded revision numbers */
  char const *expandarg, *lexpandarg, *suffixarg, *versionarg, *zonearg;
#if DIFF_L
  static struct buf labelbuf[2];
  int file_labels;
  char const **diff_label1, **diff_label2;
  char date2[datesize];
#endif
  char const *cov[10 + !DIFF_L];
  char const **diffv, **diffp, **diffpend;      /* argv for subsidiary diff */
  char const **pp, *p, *diffvstr;
  struct buf commarg;
  struct buf numericrev;        /* expanded revision number */
  struct hshentries *gendeltas; /* deltas to be generated */
  struct hshentry *target;
  char *a, *dcp, **newargv;
  int no_diff_means_no_output;
  register int c;

  CHECK_HV ();

  exitstatus = diff_success;

  bufautobegin (&commarg);
  bufautobegin (&numericrev);
  revnums = 0;
  rev1 = rev2 = xrev2 = NULL;
#if DIFF_L
  file_labels = 0;
#endif
  expandarg = suffixarg = versionarg = zonearg = NULL;
  no_diff_means_no_output = true;
  suffixes = X_DEFAULT;

  /*
   * Room for runv extra + args [+ --binary] [+ 2 labels]
   * + 1 file + 1 trailing null.
   */
  diffv = tnalloc (char const *, 1 + argc + !!OPEN_O_BINARY + 2 * DIFF_L + 2);
  diffp = diffv + 1;
  *diffp++ = prog_diff;

  argc = getRCSINIT (argc, argv, &newargv);
  argv = newargv;
  while (a = *++argv, 0 < --argc && *a++ == '-')
    {
      dcp = a;
      while ((c = *a++))
        switch (c)
          {
          case 'r':
            switch (++revnums)
              {
              case 1:
                rev1 = a;
                break;
              case 2:
                rev2 = a;
                break;
              default:
                error ("too many revision numbers");
              }
            goto option_handled;
          case '-':
          case 'D':
            no_diff_means_no_output = false;
            /* fall into */
          case 'C':
          case 'F':
          case 'I':
          case 'L':
          case 'U':
          case 'W':
#if DIFF_L
            if (c == 'L' && ++file_labels == 2)
              faterror ("too many -L options");
#endif
            *dcp++ = c;
            if (*a)
              do
                *dcp++ = *a++;
              while (*a);
            else
              {
                if (!--argc)
                  faterror ("-%c needs following argument", c);
                *diffp++ = *argv++;
              }
            break;
          case 'y':
            no_diff_means_no_output = false;
            /* fall into */
          case 'B':
          case 'H':
          case '0':
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
          case 'a':
          case 'b':
          case 'c':
          case 'd':
          case 'e':
          case 'f':
          case 'h':
          case 'i':
          case 'n':
          case 'p':
          case 't':
          case 'u':
          case 'w':
            *dcp++ = c;
            break;
          case 'q':
            quietflag = true;
            break;
          case 'x':
            suffixarg = *argv;
            suffixes = *argv + 2;
            goto option_handled;
          case 'z':
            zonearg = *argv;
            zone_set (*argv + 2);
            goto option_handled;
          case 'T':
            /* Ignore -T, so that RCSINIT can contain -T.  */
            if (*a)
              goto unknown;
            break;
          case 'V':
            versionarg = *argv;
            setRCSversion (versionarg);
            goto option_handled;
          case 'k':
            expandarg = *argv;
            if (0 <= str2expmode (expandarg + 2))
              goto option_handled;
            /* fall into */
          default:
          unknown:
            error ("unknown option: %s", *argv);
          };
    option_handled:
      if (dcp != *argv + 1)
        {
          *dcp = '\0';
          *diffp++ = *argv;
        }
    }                           /* end of option processing */

  for (pp = diffv + 2, c = 0; pp < diffp;)
    c += strlen (*pp++) + 1;
  diffvstr = a = tnalloc (char, c + 1);
  for (pp = diffv + 2; pp < diffp;)
    {
      p = *pp++;
      *a++ = ' ';
      while ((*a = *p++))
        a++;
    }
  *a = '\0';

#if DIFF_L
  diff_label1 = diff_label2 = NULL;
  if (file_labels < 2)
    {
      if (!file_labels)
        diff_label1 = diffp++;
      diff_label2 = diffp++;
    }
#endif
  diffpend = diffp;

  cov[1] = prog_co;
  cov[2] = "-q";
#   if !DIFF_L
  cov[3] = "-M";
#   endif

  /* Now handle all pathnames.  */
  if (nerror)
    cleanup ();
  else if (argc < 1)
    faterror ("no input file");
  else
    for (; 0 < argc; cleanup (), ++argv, --argc)
      {
        ffree ();

        if (pairnames (argc, argv, rcsreadopen, true, false) <= 0)
          continue;
        diagnose ("%sRCS file: %s\n", equal_line + 10, RCSname);
        if (!rev2)
          {
            /* Make sure work file is readable, and get its status.  */
            if (!(workptr = Iopen (workname, FOPEN_R_WORK, &workstat)))
              {
                eerror (workname);
                continue;
              }
          }

        gettree ();             /* reads in the delta tree */

        if (!Head)
          {
            rcserror ("no revisions present");
            continue;
          }
        if (revnums == 0 || !*rev1)
          rev1 = Dbranch ? Dbranch : Head->num;

        if (!fexpandsym (rev1, &numericrev, workptr))
          continue;
        if (! (target = gr_revno (numericrev.string, &gendeltas)))
          continue;
        xrev1 = target->num;
#if DIFF_L
        if (diff_label1)
          *diff_label1 =
            setup_label (&labelbuf[0], target->num, target->date);
#endif

        lexpandarg = expandarg;
        if (revnums == 2)
          {
            if (!fexpandsym (*rev2 ? rev2 : Dbranch ? Dbranch : Head->num,
                             &numericrev, workptr))
              continue;
            if (! (target = gr_revno (numericrev.string, &gendeltas)))
              continue;
            xrev2 = target->num;
            if (no_diff_means_no_output && xrev1 == xrev2)
              continue;
          }
        else if (target->lockedby
                 && !lexpandarg
                 && Expand == KEYVAL_EXPAND
                 && WORKMODE (RCSstat.st_mode, true) == workstat.st_mode)
          lexpandarg = "-kkvl";
        Izclose (&workptr);
#if DIFF_L
        if (diff_label2)
          {
            if (revnums == 2)
              *diff_label2 =
                setup_label (&labelbuf[1], target->num, target->date);
            else
              {
                time2date (workstat.st_mtime, date2);
                *diff_label2 = setup_label (&labelbuf[1], NULL, date2);
              }
          }
#endif

        diagnose ("retrieving revision %s\n", xrev1);
        bufscpy (&commarg, "-p");
        bufscat (&commarg, rev1);       /* not xrev1, for $Name's sake */

        pp = &cov[3 + !DIFF_L];
        *pp++ = commarg.string;
        if (lexpandarg)
          *pp++ = lexpandarg;
        if (suffixarg)
          *pp++ = suffixarg;
        if (versionarg)
          *pp++ = versionarg;
        if (zonearg)
          *pp++ = zonearg;
        *pp++ = RCSname;
        *pp = '\0';

        diffp = diffpend;
#	    if OPEN_O_BINARY
        if (Expand == BINARY_EXPAND)
          *diffp++ = "--binary";
#	    endif
        diffp[0] = maketemp (0);
        if (runv (-1, diffp[0], cov))
          {
            rcserror ("co failed");
            continue;
          }
        if (!rev2)
          {
            diffp[1] = workname;
            if (*workname == '-')
              {
                char *dp = ftnalloc (char, strlen (workname) + 3);
                diffp[1] = dp;
                *dp++ = '.';
                *dp++ = SLASH;
                strcpy (dp, workname);
              }
          }
        else
          {
            diagnose ("retrieving revision %s\n", xrev2);
            bufscpy (&commarg, "-p");
            bufscat (&commarg, rev2);   /* not xrev2, for $Name's sake */
            cov[3 + !DIFF_L] = commarg.string;
            diffp[1] = maketemp (1);
            if (runv (-1, diffp[1], cov))
              {
                rcserror ("co failed");
                continue;
              }
          }
        if (!rev2)
          diagnose ("diff%s -r%s %s\n", diffvstr, xrev1, workname);
        else
          diagnose ("diff%s -r%s -r%s\n", diffvstr, xrev1, xrev2);

        diffp[2] = 0;
        {
          int s = runv (-1, NULL, diffv);

          if (diff_trouble == s)
            workerror ("diff failed");
          if (diff_failure == s
              && diff_success == exitstatus)
            exitstatus = s;
        }
      }

  tempunlink ();
  return exitstatus;
}

static void
cleanup (void)
{
  if (nerror)
    exitstatus = diff_trouble;
  Izclose (&finptr);
  Izclose (&workptr);
}

void
exiterr (void)
{
  tempunlink ();
  _exit (diff_trouble);
}

#if DIFF_L
static char const *
setup_label (struct buf *b, char const *num, char const date[datesize])
{
  char *p;
  char datestr[datesize + zonelenmax];
  date2str (date, datestr);
  bufalloc (b,
            strlen (workname)
            + sizeof datestr + 4 + (num ? strlen (num) : 0));
  p = b->string;
  if (num)
    sprintf (p, "-L%s\t%s\t%s", workname, datestr, num);
  else
    sprintf (p, "-L%s\t%s", workname, datestr);
  return p;
}
#endif
