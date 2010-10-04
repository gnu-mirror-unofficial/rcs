/* Check out working files from revisions of RCS files.

   Copyright (C) 2010 Thien-Thi Nguyen
   Copyright (C) 1990, 1991, 1992, 1993, 1994, 1995 Paul Eggert
   Copyright (C) 1982, 1988, 1989 Walter Tichy

   This file is part of GNU RCS.

   GNU RCS is free software: you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
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
#include <stdlib.h>
#include "same-inode.h"
#include "co.help"
#include "b-complain.h"
#include "b-divvy.h"
#include "b-esds.h"
#include "b-excwho.h"
#include "b-fb.h"
#include "b-feph.h"
#include "b-fro.h"
#include "b-isr.h"

struct top *top;

static char const quietarg[] = "-q";

static char const *expandarg, *suffixarg, *versionarg, *zonearg;
/* Revisions to be joined.  */
static char const **joinlist;
static FILE *neworkptr;
static int exitstatus;
static bool forceflag;
/* Index of last element in `joinlist'.  */
static int lastjoin;

/* State for -j.  */
struct jstuff
{
  struct divvy *jstuff;
  struct link head, *tp;
};
static struct jstuff jstuff;

/* -1 -> unlock, 0 -> do nothing, 1 -> lock.  */
static int lockflag;
static bool mtimeflag;
/* Final delta to be generated.  */
static struct delta *targetdelta;
static struct stat workstat;

static void
cleanup (void)
{
  FILE *mstdout = MANI (standard_output);

  if (FLOW (erroneousp))
    exitstatus = EXIT_FAILURE;
  fro_zclose (&FLOW (from));
  ORCSclose ();
  if (FLOW (from)
      && STDIO_P (FLOW (from))
      && FLOW (res)
      && FLOW (res) != mstdout)
    Ozclose (&FLOW (res));
  if (neworkptr != mstdout)
    Ozclose (&neworkptr);
  dirtempunlink ();
}

static exiting void
exiterr (void)
{
  ORCSerror ();
  dirtempunlink ();
  tempunlink ();
  exit_failurefully ();
}

static bool
rmworkfile (void)
/* Prepare to remove ‘MANI (filename)’, if it exists, and if it is read-only.
   Otherwise (file writable), if !quietmode, ask the user whether to
   really delete it (default: fail); otherwise fail.
   Return true if permission is gotten.  */
{
  if (workstat.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH) && !forceflag)
    {
      char const *mani_filename = MANI (filename);

      /* File is writable.  */
      if (!yesorno (false, "writable %s exists%s; remove it? [ny](n): ",
                    mani_filename, (stat_mine_p (&workstat)
                                    ? ""
                                    : ", and you do not own it")))
        {
          PERR (!BE (quiet) && ttystdin ()
                ? "checkout aborted"
                : "writable %s exists; checkout aborted", mani_filename);
          return false;
        }
    }
  /* Actual unlink is done later by caller.  */
  return true;
}

static int
rmlock (struct delta const *delta)
/* Remove the lock held by caller on ‘delta’.  Return -1 if
  someone else holds the lock, 0 if there is no lock on delta,
  and 1 if a lock was found and removed.  */
{
  struct link box, *tp;
  struct rcslock const *rl;

  box.next = GROK (locks);
  if (! (tp = lock_delta_memq (&box, delta)))
    /* No lock on ‘delta’.  */
    return 0;
  rl = tp->next->entry;
  if (!caller_login_p (rl->login))
    /* Found a lock on ‘delta’ by someone else.  */
    {
      RERR ("revision %s locked by %s; use co -r or rcs -u",
            delta->num, rl->login);
      return -1;
    }
  /* Found a lock on ‘delta’ by caller; delete it.  */
  lock_drop (&box, tp);
  return 1;
}

static void
jpush (char const *rev)
{
  jstuff.tp = extend (jstuff.tp, rev, jstuff.jstuff);
  lastjoin++;
}

static char *
addjoin (char *joinrev)
/* Add the number of ‘joinrev’ to ‘joinlist’; return address
   of char past ‘joinrev’, or NULL if no such revision exists.  */
{
  register char *j;
  register struct delta *d;
  char terminator;
  struct cbuf numrev;

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
  d = NULL;
  if (fully_numeric_no_k (&numrev, joinrev))
    d = delta_from_ref (numrev.string);
  *j = terminator;
  if (d)
    {
      jpush (d->num);
      return j;
    }
  return NULL;
}

static char const *
getancestor (char const *r1, char const *r2)
/* Return the common ancestor of ‘r1’ and ‘r2’ if successful,
   ‘NULL’ otherwise.  Work reliably only if ‘r1’ and ‘r2’ are not
   branch numbers.   */
{
  char const *t1, *t2;
  int l1, l2, l3;
  char const *r;

  /* TODO: Don't bother saving in ‘PLEXUS’.  */

  l1 = countnumflds (r1);
  l2 = countnumflds (r2);
  if ((2 < l1 || 2 < l2) && !NUM_EQ (r1, r2))
    {
      /* Not on main trunk or identical.  */
      l3 = 0;
      while (NUMF_EQ (1 + l3, r1, r2)
             && NUMF_EQ (2 + l3, r1, r2))
        l3 += 2;
      /* This will terminate since ‘r1’ and ‘r2’ are not the
         same; see above.  */
      if (l3 == 0)
        {
          /* No common prefix; common ancestor on main trunk.  */
          t1 = TAKE (l1 > 2 ? 2 : l1, r1);
          t2 = TAKE (l2 > 2 ? 2 : l2, r2);
          r = NUM_LT (t1, t2) ? t1 : t2;
          if (!NUM_EQ (r, r1) && !NUM_EQ (r, r2))
            return str_save (r);
        }
      else if (!NUMF_EQ (1 + l3, r1, r2))
        return str_save (TAKE (l3, r1));
    }
  RERR ("common ancestor of %s and %s undefined", r1, r2);
  return NULL;
}

static bool
preparejoin (register char *j)
/* Parse join list ‘j’ and place pointers to the
   revision numbers into ‘joinlist’.
   Set ‘lastjoin’ to the last index of the list.  */
{
  bool rv = true;

  jstuff.jstuff = make_space ("jstuff");
  jstuff.head.next = NULL;
  jstuff.tp = &jstuff.head;

  lastjoin = -1;
  for (;;)
    {
      while ((*j == ' ') || (*j == '\t') || (*j == ','))
        j++;
      if (*j == '\0')
        break;
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
              RFATAL ("join pair incomplete");
            }
        }
      else
        {
          if (lastjoin == 0)            /* first pair */
            {
              char const *two = jstuff.tp->entry;

              /* Derive common ancestor.  */
              if (! (jstuff.tp->entry = getancestor (targetdelta->num, two)))
                {
                  rv = false;
                  goto done;
                }
              /* Common ancestor missing.  */
              jpush (two);
            }
          else
            {
              RFATAL ("join pair incomplete");
            }
        }
    }
  if (lastjoin < 1)
    RFATAL ("empty join");
 done:

  joinlist = pointer_array (PLEXUS, 1 + lastjoin);
  jstuff.tp = jstuff.head.next;
  for (int i = 0; i <= lastjoin; i++, jstuff.tp = jstuff.tp->next)
    joinlist[i] = jstuff.tp->entry;
  close_space (jstuff.jstuff);
  jstuff.jstuff = NULL;
  return rv;
}

static bool
buildjoin (char const *initialfile)
/* Merge pairs of elements in ‘joinlist’ into ‘initialfile’.
   If ‘MANI (standard_output)’ is set, copy result to stdout.
   All unlinking of ‘initialfile’, ‘rev2’, and ‘rev3’
   should be done by ‘tempunlink’.  */
{
  char const *rev2, *rev3;
  int i;
  char const *cov[10], *mergev[11];
  char const **p;
  size_t len;
  char const *subs = NULL;

  rev2 = maketemp (0);
  rev3 = maketemp (3);      /* ‘buildrevision’ may use 1 and 2 */

  cov[1] = prog_co;
  /* ‘cov[2]’ setup below.  */
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
  *p++ = REPO (filename);
  *p = '\0';

  mergev[1] = prog_merge;
  mergev[2] = mergev[4] = "-L";
  /* Rest of ‘mergev’ setup below.  */

  i = 0;
  while (i < lastjoin)
    {
#define ACCF(...)  accf (SINGLE, __VA_ARGS__)
      /* Prepare marker for merge.  */
      if (i == 0)
        subs = targetdelta->num;
      else
        {
          ACCF ("%s,%s:%s", subs, joinlist[i - 2], joinlist[i - 1]);
          subs = finish_string (SINGLE, &len);
        }
      diagnose ("revision %s", joinlist[i]);
      ACCF ("-p%s", joinlist[i]);
      cov[2] = finish_string (SINGLE, &len);
      if (runv (-1, rev2, cov))
        goto badmerge;
      diagnose ("revision %s", joinlist[i + 1]);
      ACCF ("-p%s", joinlist[i + 1]);
      cov[2] = finish_string (SINGLE, &len);
      if (runv (-1, rev3, cov))
        goto badmerge;
      diagnose ("merging...");
      mergev[3] = subs;
      mergev[5] = joinlist[i + 1];
      p = &mergev[6];
      if (BE (quiet))
        *p++ = quietarg;
      if (lastjoin <= i + 2 && MANI (standard_output))
        *p++ = "-p";
      *p++ = initialfile;
      *p++ = rev2;
      *p++ = rev3;
      *p = '\0';
      if (DIFF_TROUBLE == runv (-1, NULL, mergev))
          goto badmerge;
      i = i + 2;
#undef ACCF
    }
  return true;

badmerge:
  FLOW (erroneousp) = true;
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
  -T            Preserve the modification time on the RCS file
                even if it changes because a lock is added or removed.
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
  int expmode, r, workstatstat;
  bool tostdout, Ttimeflag;
  char finaldate[datesize];
#if OPEN_O_BINARY
  int stdout_mode = 0;
#endif
  struct wlink *deltas;                 /* Deltas to be generated.  */
  struct program program =
    {
      .name = "co",
      .help = help,
      .exiterr = exiterr
    };

  CHECK_HV ();
  gnurcs_init (&program);

  setrid ();
  author = date = rev = state = NULL;
  joinflag = NULL;
  expmode = -1;
  BE (pe) = X_DEFAULT;
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
                PWARN ("redefinition of %s", ks_revno);
              rev = a;
            }
          break;

        case 'f':
          forceflag = true;
          goto revno;

        case 'l':
          if (lockflag < 0)
            {
              PWARN ("-u overridden by -l.");
            }
          lockflag = 1;
          goto revno;

        case 'u':
          if (0 < lockflag)
            {
              PWARN ("-l overridden by -u.");
            }
          lockflag = -1;
          goto revno;

        case 'p':
          tostdout = true;
          goto revno;

        case 'I':
          BE (interactive) = true;
          goto revno;

        case 'q':
          BE (quiet) = true;
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
          BE (pe) = a;
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
          bad_option (*argv);

        };
    }
  /* (End of option processing.)  */

  /* Now handle all filenames.  */
  if (FLOW (erroneousp))
    cleanup ();
  else if (argc < 1)
    PFATAL ("no input file");
  else
    for (; 0 < argc; cleanup (), ++argv, --argc)
      {
        struct stat *repo_stat;
        char const *mani_filename;
        int kws;

        ffree ();

        if (pairnames
            (argc, argv, lockflag ? rcswriteopen : rcsreadopen, true,
             false) <= 0)
          continue;

        /* ‘REPO (filename)’ contains the name of the RCS file, and
           ‘FLOW (from)’ points at it.  ‘MANI (filename)’ contains the
           name of the working file.  Also, ‘REPO (stat)’ has been set.  */
        repo_stat = &REPO (stat);
        mani_filename = MANI (filename);
        kws = BE (kws);
        diagnose ("%s  -->  %s", REPO (filename),
                  tostdout ? "standard output" : mani_filename);

        workstatstat = -1;
        if (tostdout)
          {
#if OPEN_O_BINARY
            int newmode = kws == kwsub_b ? OPEN_O_BINARY : 0;
            if (stdout_mode != newmode)
              {
                stdout_mode = newmode;
                oflush ();
                setmode (STDOUT_FILENO, newmode);
              }
#endif
            neworkname = NULL;
            neworkptr = MANI (standard_output) = stdout;
          }
        else
          {
            workstatstat = stat (mani_filename, &workstat);
            if (!PROB (workstatstat) && SAME_INODE (REPO (stat), workstat))
              {
                RERR ("RCS file is the same as working file %s.",
                      mani_filename);
                continue;
              }
            neworkname = makedirtemp (true);
            if (!(neworkptr = fopen_safer (neworkname, FOPEN_W_WORK)))
              {
                if (errno == EACCES)
                  MERR ("permission denied on parent directory");
                else
                  syserror_errno (neworkname);
                continue;
              }
          }

        if (!REPO (tip))
          {
            /* No revisions; create empty file.  */
            diagnose ("no revisions present; generating empty revision 0.0");
            if (lockflag)
              PWARN ("no revisions, so nothing can be %slocked",
                     lockflag < 0 ? "un" : "");
            Ozclose (&FLOW (res));
            if (!PROB (workstatstat))
              if (!rmworkfile ())
                continue;
            changelock = 0;
            newdate = NULL;
          }
        else
          {
            struct cbuf numericrev;
            int locks = lockflag ? findlock (false, &targetdelta) : 0;
            struct fro *from = FLOW (from);

            if (rev)
              {
                /* Expand symbolic revision number.  */
                if (!fully_numeric_no_k (&numericrev, rev))
                  continue;
              }
            else
              {
                switch (locks)
                  {
                  default:
                    continue;
                  case 0:
                    numericrev.string = GROK (branch) ? GROK (branch) : "";
                    break;
                  case 1:
                    numericrev.string = str_save (targetdelta->num);
                    break;
                  }
              }
            /* Get numbers of deltas to be generated.  */
            if (! (targetdelta = genrevs (numericrev.string, date, author,
                                          state, &deltas)))
              continue;
            /* Check reservations.  */
            changelock = lockflag < 0
              ? rmlock (targetdelta)
              : lockflag == 0 ? 0 : addlock (targetdelta, true);

            if (changelock < 0
                || (changelock && !checkaccesslist ())
                || PROB (dorewrite (lockflag, changelock)))
              continue;

            if (0 <= expmode)
              kws = BE (kws) = expmode;
            if (0 < lockflag && kws == kwsub_v)
              {
                RERR ("cannot combine -kv and -l");
                continue;
              }

            if (joinflag && !preparejoin (joinflag))
              continue;

            diagnose ("revision %s%s", targetdelta->num,
                      0 < lockflag ? " (locked)" :
                      lockflag < 0 ? " (unlocked)" : "");
            SAME_AFTER (from, targetdelta->text);

            /* Prepare to remove old working file if necessary.  */
            if (!PROB (workstatstat))
              if (!rmworkfile ())
                continue;

            /* Skip description (don't echo).  */
            write_desc_maybe (FLOW (to));

            BE (inclusive_of_Locker_in_Id_val) = 0 < lockflag;
            targetdelta->name = namedrev (rev, targetdelta);
            joinname = buildrevision (deltas, targetdelta,
                                      joinflag && tostdout ? NULL : neworkptr,
                                      kws < MIN_UNEXPAND);
            if (FLOW (res) == neworkptr)
              FLOW (res) = NULL;             /* Don't close it twice.  */
            if (changelock && deltas->entry != targetdelta)
              fro_trundling (true, from);

            if (PROB (donerewrite (changelock, Ttimeflag
                                   ? repo_stat->st_mtime
                                   : (time_t) - 1)))
              continue;

            if (changelock)
              {
                locks += lockflag;
                if (1 < locks)
                  RWARN ("You now have %d locks.", locks);
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
                if (kws == kwsub_b)
                  MERR ("merging binary files");
                if (!buildjoin (joinname))
                  continue;
              }
          }
        if (!tostdout)
          {
            mode_t m = WORKMODE (repo_stat->st_mode,
                                 !(kws == kwsub_v
                                   || (lockflag <= 0 && BE (strictly_locking))));
            time_t t = mtimeflag
              && newdate ? date2time (newdate) : (time_t) - 1;
            aflush (neworkptr);
            IGNOREINTS ();
            r = chnamemod (&neworkptr, neworkname, mani_filename, 1, m, t);
            keepdirtemp (neworkname);
            RESTOREINTS ();
            if (PROB (r))
              {
                syserror_errno (mani_filename);
                PERR ("see %s", neworkname);
                continue;
              }
            diagnose ("done");
          }
      }

  tempunlink ();
  Ozclose (&MANI (standard_output));
  return exitstatus;
}

/* co.c ends here */
