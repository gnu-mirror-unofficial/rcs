/* Check out working files from revisions of RCS files.

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
#include "co-help.c"

static char const quietarg[] = "-q";

static char const *expandarg, *suffixarg, *versionarg, *zonearg;
/* Revisions to be joined.  */
static char const **joinlist;
static int joinlength;
static FILE *neworkptr;
static int exitstatus;
static int forceflag;
/* Index of last element in `joinlist'.  */
static int lastjoin;
/* -1 -> unlock, 0 -> do nothing, 1 -> lock.  */
static int lockflag;
static int mtimeflag;
/* Deltas to be generated.  */
static struct hshentries *gendeltas;
/* Final delta to be generated.  */
static struct hshentry *targetdelta;
static struct stat workstat;

char const cmdid[] = "co";

static void
cleanup (void)
{
  if (nerror)
    exitstatus = EXIT_FAILURE;
  Izclose (&finptr);
  ORCSclose ();
#if !large_memory
  if (fcopy != workstdout)
    Ozclose (&fcopy);
#endif
  if (neworkptr != workstdout)
    Ozclose (&neworkptr);
  dirtempunlink ();
}

void
exiterr (void)
{
  ORCSerror ();
  dirtempunlink ();
  tempunlink ();
  _exit (EXIT_FAILURE);
}

static int
rmworkfile (void)
/* Prepare to remove workname, if it exists, and if it is read-only.
   Otherwise (file writable), if !quietmode, ask the user whether to
   really delete it (default: fail); otherwise fail.
   Return true if permission is gotten.  */
{
  if (workstat.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH) && !forceflag)
    {
      /* File is writable.  */
      if (!yesorno (false, "writable %s exists%s; remove it? [ny](n): ",
                    workname, (myself (workstat.st_uid)
                               ? ""
                               : ", and you do not own it")))
        {
          error (!quietflag && ttystdin ()
                 ? "checkout aborted"
                 : "writable %s exists; checkout aborted", workname);
          return false;
        }
    }
  /* Actual unlink is done later by caller.  */
  return true;
}

static int
rmlock (struct hshentry const *delta)
/* Remove the lock held by caller on `delta'.  Return -1 if
  someone else holds the lock, 0 if there is no lock on delta,
  and 1 if a lock was found and removed.  */
{
  register struct rcslock *next, *trail;
  char const *num;
  struct rcslock dummy;
  int whomatch, nummatch;

  num = delta->num;
  dummy.nextlock = next = Locks;
  trail = &dummy;
  while (next)
    {
      whomatch = strcmp (getcaller (), next->login);
      nummatch = strcmp (num, next->delta->num);
      if ((whomatch == 0) && (nummatch == 0))
        break;
      /* Found a lock on delta by caller.  */
      if ((whomatch != 0) && (nummatch == 0))
        {
          rcserror ("revision %s locked by %s; use co -r or rcs -u",
                    num, next->login);
          return -1;
        }
      trail = next;
      next = next->nextlock;
    }
  if (next)
    {
      /* Found one; delete it.  */
      trail->nextlock = next->nextlock;
      Locks = dummy.nextlock;
      next->delta->lockedby = NULL;
      /* Success.  */
      return 1;
    }
  else
    /* No lock on `delta'.  */
    return 0;
}

static char *
addjoin (char *joinrev)
/* Add the number of `joinrev' to `joinlist'; return address
   of char past `joinrev', or NULL if no such revision exists.  */
{
  register char *j;
  register struct hshentry *d;
  char terminator;
  struct buf numrev;
  struct hshentries *joindeltas;

  j = joinrev;
  for (;;)
    {
      switch (*j++)
        {
        default:
          continue;
        case 0:
        case ' ':
        case '\t':
        case '\n':
        case ':':
        case ',':
        case ';':
          break;
        }
      break;
    }
  terminator = *--j;
  *j = '\0';
  bufautobegin (&numrev);
  d = NULL;
  if (expandsym (joinrev, &numrev))
    d = gr_revno (numrev.string, &joindeltas);
  bufautoend (&numrev);
  *j = terminator;
  if (d)
    {
      joinlist[++lastjoin] = d->num;
      return j;
    }
  return NULL;
}

static char const *
getancestor (char const *r1, char const *r2)
/* Return the common ancestor of `r1' and `r2' if successful,
   `NULL' otherwise.  Work reliably only if `r1' and `r2' are not
   branch numbers.   */
{
  static struct buf t1, t2;

  int l1, l2, l3;
  char const *r;

  l1 = countnumflds (r1);
  l2 = countnumflds (r2);
  if ((2 < l1 || 2 < l2) && cmpnum (r1, r2) != 0)
    {
      /* Not on main trunk or identical.  */
      l3 = 0;
      while (cmpnumfld (r1, r2, l3 + 1) == 0
             && cmpnumfld (r1, r2, l3 + 2) == 0)
        l3 += 2;
      /* This will terminate since `r1' and `r2' are not the
         same; see above.  */
      if (l3 == 0)
        {
          /* No common prefix; common ancestor on main trunk.  */
          partialno (&t1, r1, l1 > 2 ? 2 : l1);
          partialno (&t2, r2, l2 > 2 ? 2 : l2);
          r = cmpnum (t1.string, t2.string) < 0 ? t1.string : t2.string;
          if (cmpnum (r, r1) != 0 && cmpnum (r, r2) != 0)
            return r;
        }
      else if (cmpnumfld (r1, r2, l3 + 1) != 0)
        return partialno (&t1, r1, l3);
    }
  rcserror ("common ancestor of %s and %s undefined", r1, r2);
  return NULL;
}

static int
preparejoin (register char *j)
/* Parse join list `j' and place pointers to the
   revision numbers into `joinlist'.  */
{
  lastjoin = -1;
  for (;;)
    {
      while ((*j == ' ') || (*j == '\t') || (*j == ','))
        j++;
      if (*j == '\0')
        break;
      if (lastjoin >= joinlength - 2)
        {
          joinlist = (joinlength *= 2) == 0
            ? tnalloc (char const *, joinlength = 16)
            : trealloc (char const *, joinlist, joinlength);
        }
      if (!(j = addjoin (j)))
        return false;
      while ((*j == ' ') || (*j == '\t'))
        j++;
      if (*j == ':')
        {
          j++;
          while ((*j == ' ') || (*j == '\t'))
            j++;
          if (*j != '\0')
            {
              if (!(j = addjoin (j)))
                return false;
            }
          else
            {
              rcsfaterror ("join pair incomplete");
            }
        }
      else
        {
          if (lastjoin == 0)            /* first pair */
            {
              /* Common ancestor missing.  */
              joinlist[1] = joinlist[0];
              lastjoin = 1;
              /* Derive common ancestor.  */
              if (! (joinlist[0] = getancestor (targetdelta->num, joinlist[1])))
                return false;
            }
          else
            {
              rcsfaterror ("join pair incomplete");
            }
        }
    }
  if (lastjoin < 1)
    rcsfaterror ("empty join");
  return true;
}

static int
buildjoin (char const *initialfile)
/* Merge pairs of elements in `joinlist' into `initialfile'.  If
   workstdout is set, copy result to stdout.  All unlinking of
   `initialfile', `rev2', and `rev3' should be done by
   `tempunlink'.  */
{
  struct buf commarg;
  struct buf subs;
  char const *rev2, *rev3;
  int i;
  char const *cov[10], *mergev[11];
  char const **p;

  bufautobegin (&commarg);
  bufautobegin (&subs);
  rev2 = maketemp (0);
  rev3 = maketemp (3);      /* `buildrevision' may use 1 and 2 */

  cov[1] = prog_co;
  /* `cov[2]' setup below.  */
  p = &cov[3];
  if (expandarg)
    *p++ = expandarg;
  if (suffixarg)
    *p++ = suffixarg;
  if (versionarg)
    *p++ = versionarg;
  if (zonearg)
    *p++ = zonearg;
  *p++ = quietarg;
  *p++ = RCSname;
  *p = '\0';

  mergev[1] = prog_merge;
  mergev[2] = mergev[4] = "-L";
  /* Rest of `mergev' setup below.  */

  i = 0;
  while (i < lastjoin)
    {
      /* Prepare marker for merge.  */
      if (i == 0)
        bufscpy (&subs, targetdelta->num);
      else
        {
          bufscat (&subs, ",");
          bufscat (&subs, joinlist[i - 2]);
          bufscat (&subs, ":");
          bufscat (&subs, joinlist[i - 1]);
        }
      diagnose ("revision %s\n", joinlist[i]);
      bufscpy (&commarg, "-p");
      bufscat (&commarg, joinlist[i]);
      cov[2] = commarg.string;
      if (runv (-1, rev2, cov))
        goto badmerge;
      diagnose ("revision %s\n", joinlist[i + 1]);
      bufscpy (&commarg, "-p");
      bufscat (&commarg, joinlist[i + 1]);
      cov[2] = commarg.string;
      if (runv (-1, rev3, cov))
        goto badmerge;
      diagnose ("merging...\n");
      mergev[3] = subs.string;
      mergev[5] = joinlist[i + 1];
      p = &mergev[6];
      if (quietflag)
        *p++ = quietarg;
      if (lastjoin <= i + 2 && workstdout)
        *p++ = "-p";
      *p++ = initialfile;
      *p++ = rev2;
      *p++ = rev3;
      *p = '\0';
      if (diff_trouble == runv (-1, NULL, mergev))
          goto badmerge;
      i = i + 2;
    }
  bufautoend (&commarg);
  bufautoend (&subs);
  return true;

badmerge:
  nerror++;
  bufautoend (&commarg);
  bufautoend (&subs);
  return false;
}

/*:help
[options] file ...
Options:
  -f[REV]       Force overwrite of working file.
  -I[REV]       Interactive.
  -p[REV]       Write to stdout instead of the working file.
  -q[REV]       Quiet mode.
  -r[REV]       Normal checkout.
  -l[REV]       Like -r, but also lock.
  -u[REV]       Like -l, but unlock.
  -M[REV]       Reset working file mtime (relevant for -l, -u).
  -kSUBST       Use SUBST substitution, one of: kv, kvl, k, o, b, v.
  -dDATE        Select latest before or on DATE.
  -jJOINS       Merge using JOINS, a list of REV:REV pairs;
                this option is obsolete -- see rcsmerge(1).
  -sSTATE       Select matching state STATE.
  -T            Preserve the modification time on the RCS file even if
                the RCS file changes because a lock is added or removed.
  -wWHO         Select matching login WHO.
  -V            Like --version.
  -VN           Emulate RCS version N.
  -xSUFF        Specify SUFF as a slash-separated list of suffixes
                used to identify RCS file names.
  -zZONE        Specify date output format in keyword-substitution
                and also the default timezone for -dDATE.

Multiple flags in {fIlMpqru} may be used, except for -r, -l, -u, which are
mutually exclusive.  If specified, REV can be symbolic, numeric, or mixed:
  symbolic -- must have been defined previously (see ci(1))
  $        -- determine the revision number from keyword values
              in the working file
  .N       -- prepend default branch => DEFBR.N
  BR.N     -- use this
  BR       -- latest revision on branch BR
If REV is omitted, take it to be the latest on the default branch.
*/

int
main (int argc, char **argv)
{
  char *a, *joinflag, **newargv;
  char const *author, *date, *rev, *state;
  char const *joinname, *newdate, *neworkname;
  /* 1 if a lock has been changed, -1 if error.  */
  int changelock;
  int expmode, r, tostdout, workstatstat;
  int Ttimeflag;
  struct buf numericrev;           /* expanded revision number */
  char finaldate[datesize];
#if OPEN_O_BINARY
  int stdout_mode = 0;
#endif

  CHECK_HV ();

  setrid ();
  author = date = rev = state = NULL;
  joinflag = NULL;
  bufautobegin (&numericrev);
  expmode = -1;
  suffixes = X_DEFAULT;
  tostdout = false;
  Ttimeflag = false;

  argc = getRCSINIT (argc, argv, &newargv);
  argv = newargv;
  while (a = *++argv, 0 < --argc && *a++ == '-')
    {
      switch (*a++)
        {

        case 'r':
        revno:
          if (*a)
            {
              if (rev)
                warn ("redefinition of revision number");
              rev = a;
            }
          break;

        case 'f':
          forceflag = true;
          goto revno;

        case 'l':
          if (lockflag < 0)
            {
              warn ("-u overridden by -l.");
            }
          lockflag = 1;
          goto revno;

        case 'u':
          if (0 < lockflag)
            {
              warn ("-l overridden by -u.");
            }
          lockflag = -1;
          goto revno;

        case 'p':
          tostdout = true;
          goto revno;

        case 'I':
          interactiveflag = true;
          goto revno;

        case 'q':
          quietflag = true;
          goto revno;

        case 'd':
          if (date)
            redefined ('d');
          str2date (a, finaldate);
          date = finaldate;
          break;

        case 'j':
          if (*a)
            {
              if (joinflag)
                redefined ('j');
              joinflag = a;
            }
          break;

        case 'M':
          mtimeflag = true;
          goto revno;

        case 's':
          if (*a)
            {
              if (state)
                redefined ('s');
              state = a;
            }
          break;

        case 'T':
          if (*a)
            goto unknown;
          Ttimeflag = true;
          break;

        case 'w':
          if (author)
            redefined ('w');
          if (*a)
            author = a;
          else
            author = getcaller ();
          break;

        case 'x':
          suffixarg = *argv;
          suffixes = a;
          break;

        case 'V':
          versionarg = *argv;
          setRCSversion (versionarg);
          break;

        case 'z':
          zonearg = *argv;
          zone_set (a);
          break;

        case 'k':
          /* Set keyword expand mode.  */
          expandarg = *argv;
          if (0 <= expmode)
            redefined ('k');
          if (0 <= (expmode = str2expmode (a)))
            break;
          /* fall into */
        default:
        unknown:
          error ("unknown option: %s", *argv);

        };
    }
  /* (End of option processing.)  */

  /* Now handle all pathnames.  */
  if (nerror)
    cleanup ();
  else if (argc < 1)
    faterror ("no input file");
  else
    for (; 0 < argc; cleanup (), ++argv, --argc)
      {
        ffree ();

        if (pairnames
            (argc, argv, lockflag ? rcswriteopen : rcsreadopen, true,
             false) <= 0)
          continue;

        /* `RCSname' contains the name of the RCS file, and `finptr'
           points at it.  `workname' contains the name of the working file.
           Also, `RCSstat' has been set.  */
        diagnose ("%s  -->  %s\n", RCSname,
                  tostdout ? "standard output" : workname);

        workstatstat = -1;
        if (tostdout)
          {
#if OPEN_O_BINARY
            int newmode = Expand == kwsub_b ? OPEN_O_BINARY : 0;
            if (stdout_mode != newmode)
              {
                stdout_mode = newmode;
                oflush ();
                setmode (STDOUT_FILENO, newmode);
              }
#endif
            neworkname = NULL;
            neworkptr = workstdout = stdout;
          }
        else
          {
            workstatstat = stat (workname, &workstat);
            if (workstatstat == 0 && same_file (RCSstat, workstat))
              {
                rcserror ("RCS file is the same as working file %s.",
                          workname);
                continue;
              }
            neworkname = makedirtemp (1);
            if (!(neworkptr = fopenSafer (neworkname, FOPEN_W_WORK)))
              {
                if (errno == EACCES)
                  workerror ("permission denied on parent directory");
                else
                  eerror (neworkname);
                continue;
              }
          }

        /* Read in the delta tree.  */
        gettree ();

        if (!Head)
          {
            /* No revisions; create empty file.  */
            diagnose ("no revisions present; generating empty revision 0.0\n");
            if (lockflag)
              warn ("no revisions, so nothing can be %slocked",
                    lockflag < 0 ? "un" : "");
            Ozclose (&fcopy);
            if (workstatstat == 0)
              if (!rmworkfile ())
                continue;
            changelock = 0;
            newdate = NULL;
          }
        else
          {
            int locks = lockflag ? findlock (false, &targetdelta) : 0;
            if (rev)
              {
                /* Expand symbolic revision number.  */
                if (!expandsym (rev, &numericrev))
                  continue;
              }
            else
              {
                switch (locks)
                  {
                  default:
                    continue;
                  case 0:
                    bufscpy (&numericrev, Dbranch ? Dbranch : "");
                    break;
                  case 1:
                    bufscpy (&numericrev, targetdelta->num);
                    break;
                  }
              }
            /* Get numbers of deltas to be generated. */
            if (! (targetdelta = genrevs (numericrev.string, date, author,
                                          state, &gendeltas)))
              continue;
            /* Check reservations.  */
            changelock = lockflag < 0
              ? rmlock (targetdelta)
              : lockflag == 0 ? 0 : addlock (targetdelta, true);

            if (changelock < 0
                || (changelock && !checkaccesslist ())
                || dorewrite (lockflag, changelock) != 0)
              continue;

            if (0 <= expmode)
              Expand = expmode;
            if (0 < lockflag && Expand == kwsub_v)
              {
                rcserror ("cannot combine -kv and -l");
                continue;
              }

            if (joinflag && !preparejoin (joinflag))
              continue;

            diagnose ("revision %s%s\n", targetdelta->num,
                      0 < lockflag ? " (locked)" :
                      lockflag < 0 ? " (unlocked)" : "");

            /* Prepare to remove old working file if necessary.  */
            if (workstatstat == 0)
              if (!rmworkfile ())
                continue;

            /* Skip description (don't echo).  */
            getdesc (false);

            locker_expansion = 0 < lockflag;
            targetdelta->name = namedrev (rev, targetdelta);
            joinname = buildrevision (gendeltas, targetdelta,
                                      joinflag && tostdout ? NULL : neworkptr,
                                      Expand < MIN_UNEXPAND);
#if !large_memory
            if (fcopy == neworkptr)
              fcopy = NULL;             /* Don't close it twice.  */
#endif
            if_advise_access (changelock && gendeltas->first != targetdelta,
                              finptr, MADV_SEQUENTIAL);

            if (donerewrite (changelock, Ttimeflag
                             ? RCSstat.st_mtime
                             : (time_t) - 1)
                != 0)
              continue;

            if (changelock)
              {
                locks += lockflag;
                if (1 < locks)
                  rcswarn ("You now have %d locks.", locks);
              }

            newdate = targetdelta->date;
            if (joinflag)
              {
                newdate = NULL;
                if (!joinname)
                  {
                    aflush (neworkptr);
                    joinname = neworkname;
                  }
                if (Expand == kwsub_b)
                  workerror ("merging binary files");
                if (!buildjoin (joinname))
                  continue;
              }
          }
        if (!tostdout)
          {
            mode_t m = WORKMODE (RCSstat.st_mode,
                                 !(Expand == kwsub_v
                                   || (lockflag <= 0 && StrictLocks)));
            time_t t = mtimeflag
              && newdate ? date2time (newdate) : (time_t) - 1;
            aflush (neworkptr);
            ignoreints ();
            r = chnamemod (&neworkptr, neworkname, workname, 1, m, t);
            keepdirtemp (neworkname);
            restoreints ();
            if (r != 0)
              {
                eerror (workname);
                error ("see %s", neworkname);
                continue;
              }
            diagnose ("done\n");
          }
      }

  tempunlink ();
  Ofclose (workstdout);
  return exitstatus;
}

/* co.c ends here */
