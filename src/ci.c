/* Check in revisions of RCS files from working files.

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
#include <ctype.h>                      /* isdigit */
#include <stdlib.h>
#include <unistd.h>
#include "same-inode.h"
#include "ci.help"
#include "b-complain.h"
#include "b-divvy.h"
#include "b-esds.h"
#include "b-excwho.h"
#include "b-fb.h"
#include "b-feph.h"
#include "b-fro.h"
#include "b-grok.h"
#include "b-isr.h"
#include "b-kwxout.h"

/* Work around a common ‘ftruncate’ bug: NFS won't let you truncate a file
   that you currently lack permissions for, even if you had permissions when
   you opened it.  Also, POSIX 1003.1b-1993 sec 5.6.7.2 p 128 l 1022 says
   ftruncate might fail because it's not supported.  */
#ifndef HAVE_FTRUNCATE
#undef ftruncate
#define ftruncate(fd,length) (-1)
#endif

struct top *top;

static FILE *exfile;
/* Working file pointer.  */
static struct fro *workptr;
/* New revision number.  */
static struct cbuf newdelnum;
static struct cbuf msg;
static int exitstatus;
/* Forces check-in.  */
static bool forceciflag;
static bool keepflag, keepworkingfile, rcsinitflag;
/* Old delta to be generated.  */
static struct delta *targetdelta;
/* New delta to be inserted.  */
static struct delta newdelta;
static struct stat workstat;
static struct link assoclst;

static void
cleanup (void)
{
  if (FLOW (erroneousp))
    exitstatus = EXIT_FAILURE;
  fro_zclose (&FLOW (from));
  fro_zclose (&workptr);
  Ozclose (&exfile);
  Ozclose (&FLOW (res));
  ORCSclose ();
  dirtempunlink ();
}

static exiting void
exiterr (void)
{
  ORCSerror ();
  dirtempunlink ();
  tempunlink ();
  _Exit (EXIT_FAILURE);
}

#define ACCF(...)  accf (SHARED, __VA_ARGS__)

#define OK(x)     (x)->string = finish_string (SHARED, &((x)->size))
#define JAM(x,s)  do { ACCF ("%s", s); OK (x); } while (0)
#define ADD(x,s)  do { ACCF ("%s", (x)->string); JAM (x, s); } while (0)

static void
incnum (char const *onum, struct cbuf *nnum)
/* Increment the last field of revision number ‘onum’
   by one into a ‘SHARED’ string and point ‘nnum’ at it.  */
{
  register char *tp, *np;
  register size_t l;

  ACCF ("%s%c", onum, '\0');
  np = finish_string (SHARED, &nnum->size);
  nnum->string = np;
  l = nnum->size - 1;
  for (tp = np + l; np != tp;)
    if (isdigit (*--tp))
      {
        if (*tp != '9')
          {
            ++*tp;
            nnum->size--;
            return;
          }
        *tp = '0';
      }
    else
      {
        tp++;
        break;
      }
  /* We changed 999 to 000; now change it to 1000.  */
  *tp = '1';
  tp = np + l;
  *tp++ = '0';
  *tp = '\0';
}

static int
removelock (struct delta *delta)
/* Find the lock held by caller on ‘delta’,
   remove it, and return nonzero if successful.
   Print an error message and return -1 if there is no such lock.
   An exception is if ‘!strictly_locking’, and caller is the owner of
   the RCS file.  If caller does not have a lock in this case,
   return 0; return 1 if a lock is actually removed.  */
{
  struct link box, *tp;
  struct rcslock const *rl;
  char const *num;

  num = delta->num;
  box.next = GROK (locks);
  if (! (tp = lock_delta_memq (&box, delta)))
    {
      if (!BE (strictly_locking) && stat_mine_p (&REPO (stat)))
        return 0;
      RERR ("no lock set by %s for revision %s", getcaller (), num);
      return -1;
    }
  rl = tp->next->entry;
  if (! caller_login_p (rl->login))
    {
      RERR ("revision %s locked by %s", num, rl->login);
      return -1;
    }
  /* We found a lock on ‘delta’ by caller; delete it.  */
  lock_drop (&box, tp);
  return 1;
}

static struct wlink newbranch;          /* new branch to be inserted */

#define BHDELTA(x)  ((struct delta *)(x)->entry)

static int
addbranch (struct delta *branchpoint, struct cbuf *num, bool removedlock)
/* Add a new branch and branch delta at ‘branchpoint’.
   If ‘num’ is the null string, append the new branch, incrementing
   the highest branch number (initially 1), and setting the level number to 1.
   The new delta and branchhead are in globals ‘newdelta’ and ‘newbranch’, resp.
   The new number is placed into a ‘SHARED’ string with ‘num’ pointing to it.
   Return -1 on error, 1 if a lock is removed, 0 otherwise.
   If ‘removedlock’, a lock was already removed.  */
{
  struct wlink **btrail;
  int result;
  int field, numlength;

  numlength = countnumflds (num->string);

  if (!branchpoint->branches)
    {
      /* Start first branch.  */
      branchpoint->branches = &newbranch;
      if (numlength == 0)
        {
          JAM (num, branchpoint->num);
          ADD (num, ".1.1");
        }
      else if (numlength & 1)
        ADD (num, ".1");
      newbranch.next = NULL;

    }
  else if (numlength == 0)
    {
      struct wlink *bhead = branchpoint->branches;

      /* Append new branch to the end.  */
      while (bhead->next)
        bhead = bhead->next;
      bhead->next = &newbranch;
      incnum (BRANCHNO (BHDELTA (bhead)->num), num);
      ADD (num, ".1");
      newbranch.next = NULL;
    }
  else
    {
      /* Place the branch properly.  */
      field = numlength - ((numlength & 1) ^ 1);
      /* Field of branch number.  */
      btrail = &branchpoint->branches;
      while (0 < (result = cmpnumfld (num->string, BHDELTA (*btrail)->num,
                                      field)))
        {
          btrail = &(*btrail)->next;
          if (!*btrail)
            {
              result = -1;
              break;
            }
        }
      if (result < 0)
        {
          /* Insert/append new branchhead.  */
          newbranch.next = *btrail;
          *btrail = &newbranch;
          if (numlength & 1)
            ADD (num, ".1");
        }
      else
        {
          /* Branch exists; append to end.  */
          targetdelta = delta_from_ref (BRANCHNO (num->string));
          if (!targetdelta)
            return -1;
          if (!NUM_GT (num->string, targetdelta->num))
            {
              RERR ("revision %s too low; must be higher than %s",
                    num->string, targetdelta->num);
              return -1;
            }
          if (!removedlock && 0 <= (removedlock = removelock (targetdelta)))
            {
              if (numlength & 1)
                incnum (targetdelta->num, num);
              targetdelta->ilk = &newdelta;
              newdelta.ilk = NULL;
            }
          return removedlock;
          /* Don't do anything to newbranch.  */
        }
    }
  newbranch.entry = &newdelta;
  newdelta.ilk = NULL;
  if (branchpoint->lockedby)
    if (caller_login_p (branchpoint->lockedby))
      return removelock (branchpoint);  /* This returns 1.  */
  return removedlock;
}

static int
addelta (struct wlink **tp_deltas)
/* Append a delta to the delta tree, whose number is given by
   ‘newdelnum’.  Update ‘REPO (tip)’, ‘newdelnum’, ‘newdelnumlength’,
   and the links in newdelta.
   Return -1 on error, 1 if a lock is removed, 0 otherwise.  */
{
  register char const *tp;
  register int i;
  int removedlock;
  int newdnumlength;            /* actual length of new rev. num. */
  struct delta *tip = REPO (tip);
  char const *defbr = REPO (r) ? GROK (branch) : NULL;

  newdnumlength = countnumflds (newdelnum.string);

  if (rcsinitflag)
    {
      /* This covers non-existing RCS file,
         and a file initialized with ‘rcs -i’.  */
      if (newdnumlength == 0 && defbr)
        {
          JAM (&newdelnum, defbr);
          newdnumlength = countnumflds (defbr);
        }
      if (newdnumlength == 0)
        JAM (&newdelnum, "1.1");
      else if (newdnumlength == 1)
        ADD (&newdelnum, ".1");
      else if (newdnumlength > 2)
        {
          RERR ("Branch point doesn't exist for revision %s.",
                newdelnum.string);
          return -1;
        }
      /* (‘newdnumlength’ == 2 is OK.)  */
      tip = REPO (tip) = &newdelta;
      newdelta.ilk = NULL;
      return 0;
    }
  if (newdnumlength == 0)
    {
      /* Derive new revision number from locks.  */
      switch (findlock (true, &targetdelta))
        {

        default:
          /* Found two or more old locks.  */
          return -1;

        case 1:
          /* Found an old lock.  Check whether locked revision exists.  */
          if (!gr_revno (targetdelta->num, tp_deltas))
            return -1;
          if (targetdelta == tip)
            {
              /* Make new head.  */
              newdelta.ilk = tip;
              tip = REPO (tip) = &newdelta;
            }
          else if (!targetdelta->ilk && countnumflds (targetdelta->num) > 2)
            {
              /* New tip revision on side branch.  */
              targetdelta->ilk = &newdelta;
              newdelta.ilk = NULL;
            }
          else
            {
              /* Middle revision; start a new branch.  */
              JAM (&newdelnum, "");
              return addbranch (targetdelta, &newdelnum, true);
            }
          incnum (targetdelta->num, &newdelnum);
          /* Successful use of existing lock.  */
          return 1;

        case 0:
          /* No existing lock; try ‘defbr’.  Update ‘newdelnum’.  */
          if (BE (strictly_locking) || !stat_mine_p (&REPO (stat)))
            {
              RERR ("no lock set by %s", getcaller ());
              return -1;
            }
          if (defbr)
            JAM (&newdelnum, defbr);
          else
            {
              incnum (tip->num, &newdelnum);
            }
          newdnumlength = countnumflds (newdelnum.string);
          /* Now fall into next statement.  */
        }
    }
  if (newdnumlength <= 2)
    {
      /* Add new head per given number.  */
      if (newdnumlength == 1)
        {
          /* Make a two-field number out of it.  */
          if (NUMF_EQ (1, newdelnum.string, tip->num))
            incnum (tip->num, &newdelnum);
          else
            ADD (&newdelnum, ".1");
        }
      if (!NUM_GT (newdelnum.string, tip->num))
        {
          RERR ("revision %s too low; must be higher than %s",
                newdelnum.string, tip->num);
          return -1;
        }
      targetdelta = tip;
      if (0 <= (removedlock = removelock (tip)))
        {
          if (!gr_revno (tip->num, tp_deltas))
            return -1;
          newdelta.ilk = tip;
          tip = REPO (tip) = &newdelta;
        }
      return removedlock;
    }
  else
    {
      struct cbuf old = newdelnum;      /* sigh */

      /* Put new revision on side branch.  First, get branch point.  */
      tp = old.string;
      for (i = newdnumlength - ((newdnumlength & 1) ^ 1); --i;)
        while (*tp++ != '.')
          continue;
      /* Ignore rest to get old delta.  */
      old.string = SHSNIP (&old.size, old.string, tp - 1);
      if (! (targetdelta = gr_revno (old.string, tp_deltas)))
        return -1;
      if (!NUM_EQ (targetdelta->num, old.string))
        {
          RERR ("can't find branch point %s", old.string);
          return -1;
        }
      return addbranch (targetdelta, &newdelnum, false);
    }
}

static bool
addsyms (char const *num)
{
  struct link *ls;
  struct u_symdef const *ud;

  for (ls = assoclst.next; ls; ls = ls->next)
    {
      ud = ls->entry;

      if (addsymbol (num, ud->u.meaningful, ud->override) < 0)
        return false;
    }
  return true;
}

static char getcurdate_buffer[datesize];

static char const *
getcurdate (void)
/* Return a pointer to the current date.  */
{
  if (!getcurdate_buffer[0])
    time2date (BE (now), getcurdate_buffer);
  return getcurdate_buffer;
}

static int
fixwork (mode_t newworkmode, time_t mtime)
{
  char const *mani_filename = MANI (filename);

  return
    1 < workstat.st_nlink
    || (newworkmode & S_IWUSR && !stat_mine_p (&workstat))
    || PROB (setmtime (mani_filename, mtime))
    ? -1 : workstat.st_mode == newworkmode ? 0
#ifdef HAVE_FCHMOD
    : !PROB (fchmod (workptr->fd, newworkmode)) ? 0
#endif
    : chmod (mani_filename, newworkmode)
    ;
}

static int
xpandfile (struct fro *unexfile, struct delta const *delta,
           char const **exname, bool dolog)
/* Read ‘unexfile’ and copy it to a file, performing keyword
   substitution with data from ‘delta’.
   Return -1 if unsuccessful, 1 if expansion occurred, 0 otherwise.
   If successful, store the name into ‘*exname’.  */
{
  char const *targetname;
  int e, r;

  targetname = makedirtemp (true);
  if (!(exfile = fopen_safer (targetname, FOPEN_W_WORK)))
    {
      syserror_errno (targetname);
      MERR ("can't build working file");
      return -1;
    }
  r = 0;
  if (MIN_UNEXPAND <= BE (kws))
    fro_spew (unexfile, exfile);
  else
    {
      struct expctx ctx = EXPCTX_1OUT (exfile, unexfile, false, dolog);

      for (;;)
        {
          e = expandline (&ctx);
          if (e < 0)
            break;
          r |= e;
          if (e <= 1)
            break;
        }
    }
  *exname = targetname;
  return r & 1;
}

/* --------------------- G E T L O G M S G --------------------------------*/

#define FIRST  "Initial revision"

static struct cbuf logmsg;

static struct cbuf
getlogmsg (void)
/* Obtain and return a log message.
   If a log message is given with ‘-m’, return that message.
   If this is the initial revision, return a standard log message.
   Otherwise, read a character string from the terminal.
   Stop after reading EOF or a single '.' on a line.
   Prompt the first time called for the log message; during all
   later calls ask whether the previous log message can be reused.  */
{
  if (msg.size)
    return msg;

  if (keepflag)
    {
      char datebuf[datesize + zonelenmax];

      /* Generate standard log message.  */
      date2str (getcurdate (), datebuf);
      ACCF ("%s%s at %s", TINYKS (ciklog), getcaller (), datebuf);
      OK (&logmsg);
      return logmsg;
    }

  if (!targetdelta && (NUM_EQ (newdelnum.string, "1.1")
                       || NUM_EQ (newdelnum.string, "1.0")))
    {
      struct cbuf const initiallog =
        {
          .string = FIRST,
          .size = sizeof (FIRST) - 1
        };

      return initiallog;
    }

  if (logmsg.size)
    {
      /*Previous log available.  */
      if (yesorno (true, "reuse log message of previous file? [yn](y): "))
        return logmsg;
    }

  /* Now read string from stdin.  */
  logmsg = getsstdin ("m", "log message", "");

  /* Now check whether the log message is not empty.  */
  if (!logmsg.size)
    {
      logmsg.string = EMPTYLOG;
      logmsg.size = sizeof (EMPTYLOG) - 1;
    }
  return logmsg;
}

static char const *
first_meaningful_symbolic_name (void)
{
  struct u_symdef const *ud = assoclst.next->entry;

  return ud->u.meaningful;
}

/*:help
[options] file...
Options:
  -f[REV]       Force new entry, even if no content changed.
  -I[REV]       Interactive.
  -i[REV]       Initial checkin; error if RCS file already exists.
  -j[REV]       Just checkin, don't init; error if RCS file does not exist.
  -k[REV]       Compute revision from working file keywords.
  -q[REV]       Quiet mode.
  -r[REV]       Do normal checkin, if REV is specified;
                otherwise, release lock and delete working file.
  -l[REV]       Like -r, but immediately checkout locked (co -l) afterwards.
  -u[REV]       Like -l, but checkout unlocked (co -u).
  -M[REV]       Reset working file mtime (relevant for -l, -u).
  -d[DATE]      Use DATE (or working file mtime).
  -mMSG         Use MSG as the log message.
  -nNAME        Assign symbolic NAME to the entry; NAME must be new.
  -NNAME        Like -n, but overwrite any previous assignment.
  -sSTATE       Set state to STATE.
  -t-TEXT       Set description to TEXT.
  -tFILENAME    Set description from text read from FILENAME.
  -T            Set the RCS file's modification time to the new
                revision's time if the former precedes the latter and there
                is a new revision; preserve the RCS file's modification
                time otherwise.
  -V            Like --version.
  -VN           Emulate RCS version N.
  -wWHO         Use WHO as the author.
  -xSUFF        Specify SUFF as a slash-separated list of suffixes
                used to identify RCS file names.
  -zZONE        Specify date output format in keyword-substitution
                and also the default timezone for -dDATE.

Multiple flags in {fiIjklMqru} may be used, except for -r, -l, -u, which are
mutually exclusive.  If specified, REV can be symbolic, numeric, or mixed:
  symbolic      Must have been defined previously (see -n, -N).
  $             Determine from keyword values in the working file.
  .N            Prepend default branch => DEFBR.N
  BR.N          Use this, but N must be greater than any existing
                on BR, or BR must be new.
  BR            Latest rev on branch BR + 1 => BR.(L+1), or BR.1 if new branch.
If REV is omitted, compute it from the last lock (co -l), perhaps
starting a new branch.  If there is no lock, use DEFBR.(L+1).
*/

/* Use a variable instead of simple #define for fast identity compare.  */
static char const default_state[] = DEFAULTSTATE;

int
main (int argc, char **argv)
{
  char altdate[datesize];
  char olddate[datesize];
  char newdatebuf[datesize + zonelenmax];
  char targetdatebuf[datesize + zonelenmax];
  char *a, **newargv, *textfile;
  char const *author, *krev, *rev, *state;
  char const *diffname, *expname;
  char const *newworkname;
  bool initflag, mustread;
  bool lockflag, lockthis, mtimeflag;
  int removedlock;
  bool Ttimeflag;
  int r;
  int changedRCS, changework;
  bool dolog, newhead;
  bool usestatdate;             /* Use mod time of file for -d.  */
  mode_t newworkmode;           /* mode for working file */
  time_t mtime, wtime;
  struct delta *workdelta;
  struct link *tp_assoc = &assoclst;
  struct wlink *deltas;                 /* Deltas to be generated.  */
  const struct program program =
    {
      .name = "ci",
      .help = help,
      .exiterr = exiterr
    };

  CHECK_HV ();
  gnurcs_init (&program);

  setrid ();

  author = rev = state = textfile = NULL;
  initflag = lockflag = mustread = false;
  mtimeflag = false;
  Ttimeflag = false;
  altdate[0] = '\0';            /* empty alternate date for -d */
  usestatdate = false;
  BE (pe) = X_DEFAULT;

  argc = getRCSINIT (argc, argv, &newargv);
  argv = newargv;
  while (a = *++argv, 0 < --argc && *a++ == '-')
    {
      switch (*a++)
        {

        case 'r':
          if (*a)
            goto revno;
          keepworkingfile = lockflag = false;
          break;

        case 'l':
          keepworkingfile = lockflag = true;
        revno:
          if (*a)
            {
              if (rev)
                PWARN ("redefinition of %s", ks_revno);
              rev = a;
            }
          break;

        case 'u':
          keepworkingfile = true;
          lockflag = false;
          goto revno;

        case 'i':
          initflag = true;
          goto revno;

        case 'j':
          mustread = true;
          goto revno;

        case 'I':
          BE (interactive) = true;
          goto revno;

        case 'q':
          BE (quiet) = true;
          goto revno;

        case 'f':
          forceciflag = true;
          goto revno;

        case 'k':
          keepflag = true;
          goto revno;

        case 'm':
          if (msg.size)
            redefined ('m');
          msg = cleanlogmsg (a, strlen (a));
          if (!msg.size)
            PERR ("missing message for -m option");
          break;

        case 'n':
        case 'N':
          {
            char option = a[-1];
            struct u_symdef *ud;

            if (!*a)
              {
                PERR ("missing symbolic name after -%c", option);
                break;
              }
            checkssym (a);
            ud = ZLLOC (1, struct u_symdef);
            ud->override = ('N' == option);
            ud->u.meaningful = a;
            tp_assoc = extend (tp_assoc, ud, SHARED);
          }
          break;

        case 's':
          if (*a)
            {
              if (state)
                redefined ('s');
              checksid (a);
              state = a;
            }
          else
            PERR ("missing state for -s option");
          break;

        case 't':
          if (*a)
            {
              if (textfile)
                redefined ('t');
              textfile = a;
            }
          break;

        case 'd':
          if (altdate[0] || usestatdate)
            redefined ('d');
          altdate[0] = '\0';
          if (!(usestatdate = !*a))
            str2date (a, altdate);
          break;

        case 'M':
          mtimeflag = true;
          goto revno;

        case 'w':
          if (*a)
            {
              if (author)
                redefined ('w');
              checksid (a);
              author = a;
            }
          else
            PERR ("missing author for -w option");
          break;

        case 'x':
          BE (pe) = a;
          break;

        case 'V':
          setRCSversion (*argv);
          break;

        case 'z':
          zone_set (a);
          break;

        case 'T':
          if (!*a)
            {
              Ttimeflag = true;
              break;
            }
          /* fall into */
        default:
          bad_option (*argv);
        };
    }
  /* (End processing of options.)  */

  /* Handle all filenames.  */
  if (FLOW (erroneousp))
    cleanup ();
  else if (argc < 1)
    PFATAL ("no input file");
  else
    for (; 0 < argc; cleanup (), ++argv, --argc)
      {
        char const *mani_filename, *pv;
        struct fro *from;
        struct stat *repo_stat;
        FILE *frew;
        struct delta *tip;
        int kws;
        struct cbuf newdesc =
          {
            .string = NULL,
            .size = 0
          };

        targetdelta = NULL;
        ffree ();

        switch (pairnames (argc, argv, rcswriteopen, mustread, false))
          {

          case -1:
            /* New RCS file.  */
            if (currently_setuid_p ())
              {
                MERR
                  ("setuid initial checkin prohibited; use `rcs -i -a' first");
                continue;
              }
            rcsinitflag = true;
            break;

          case 0:
            /* Error.  */
            continue;

          case 1:
            /* Normal checkin with previous RCS file.  */
            if (initflag)
              {
                RERR ("already exists");
                continue;
              }
            rcsinitflag = !(tip = REPO (tip));
          }

        /* ‘REPO (filename)’ contains the name of the RCS file,
           and ‘MANI (filename)’ contains the name of the working file.
           If the RCS file exists, ‘FLOW (from)’ contains the file
           descriptor for the RCS file, and ‘REPO (stat)’ is set.
           The admin node is initialized.  */
        mani_filename = MANI (filename);
        from = FLOW (from);
        repo_stat = &REPO (stat);
        kws = BE (kws);

        diagnose ("%s  <--  %s", REPO (filename), mani_filename);

        if (!(workptr = fro_open (mani_filename, FOPEN_R_WORK, &workstat)))
          {
            syserror_errno (mani_filename);
            continue;
          }

        if (from)
          {
            if (SAME_INODE (REPO (stat), workstat))
              {
                RERR ("RCS file is the same as working file %s.",
                      mani_filename);
                continue;
              }
            if (!checkaccesslist ())
              continue;
          }

        krev = rev;
        if (keepflag)
          {
            /* Get keyword values from working file.  */
            if (!getoldkeys (workptr))
              continue;
            if (!rev && !(krev = PREV (rev)))
              {
                MERR ("can't find a %s", ks_revno);
                continue;
              }
            if (!PREV (date) && *altdate == '\0' && usestatdate == false)
              MWARN ("can't find a date");
            if (!PREV (author) && !author)
              MWARN ("can't find an author");
            if (!PREV (state) && !state)
              MWARN ("can't find a state");
          }
        /* (End processing keepflag.)  */

        /* Expand symbolic revision number.  */
        if (!fully_numeric (&newdelnum, krev, workptr))
          continue;

        /* Splice new delta into tree.  */
        if (PROB (removedlock = addelta (&deltas)))
          continue;
        tip = REPO (tip);

        newdelta.num = newdelnum.string;
        newdelta.branches = NULL;
        /* This might be changed by ‘addlock’.  */
        newdelta.lockedby = NULL;
        newdelta.selector = true;
        newdelta.name = NULL;

        /* Set author.  */
        if (author)
          /* Given by ‘-w’.  */
          newdelta.author = author;
        else if (keepflag && (pv = PREV (author)))
            /* Preserve old author if possible.  */
          newdelta.author = pv;
        else
          /* Otherwise use caller's id.  */
          newdelta.author = getcaller ();

        /* Set state.  */
        newdelta.state = default_state;
        if (state)
          /* Given by ‘-s’.  */
          newdelta.state = state;
        else if (keepflag && (pv = PREV (state)))
          /* Preserve old state if possible.  */
          newdelta.state = pv;

        /* Compute date.  */
        if (usestatdate)
          {
            time2date (workstat.st_mtime, altdate);
          }
        if (*altdate != '\0')
          /* Given by ‘-d’.  */
          newdelta.date = altdate;
        else if (keepflag && (pv = PREV (date)))
          {
            /* Preserve old date if possible.  */
            str2date (pv, olddate);
            newdelta.date = olddate;
          }
        else
          /* Use current date.  */
          newdelta.date = getcurdate ();
        /* Now check validity of date -- needed because of ‘-d’ and ‘-k’.  */
        if (targetdelta && DATE_LT (newdelta.date, targetdelta->date))
          {
            RERR ("Date %s precedes %s in revision %s.",
                  date2str (newdelta.date, newdatebuf),
                  date2str (targetdelta->date, targetdatebuf),
                  targetdelta->num);
            continue;
          }

        if (lockflag && addlock (&newdelta, true) < 0)
          continue;

        if (keepflag && (pv = PREV (name)))
          if (addsymbol (newdelta.num, pv, false) < 0)
            continue;
        if (!addsyms (newdelta.num))
          continue;

        putadmin ();
        frew = FLOW (rewr);
        puttree (tip, frew);
        putdesc (&newdesc, false, textfile);

        changework = kws < MIN_UNCHANGED_EXPAND;
        dolog = true;
        lockthis = lockflag;
        workdelta = &newdelta;

        /* Build rest of file.  */
        if (rcsinitflag)
          {
            diagnose ("initial revision: %s", newdelta.num);
            /* Get logmessage.  */
            newdelta.pretty_log = getlogmsg ();
            putdftext (&newdelta, workptr, frew, false);
            repo_stat->st_mode = workstat.st_mode;
            repo_stat->st_nlink = 0;
            changedRCS = true;
            if (from)
              IGNORE_REST (from);
          }
        else
          {
            diffname = maketemp (0);
            newhead = tip == &newdelta;
            if (!newhead)
              FLOW (to) = frew;
            expname = buildrevision (deltas, targetdelta, NULL, false);
            if (!forceciflag
                && STR_SAME (newdelta.state, targetdelta->state)
                && ((changework = rcsfcmp (workptr, &workstat, expname,
                                          targetdelta))
                    <= 0))
              {
                diagnose
                  ("file is unchanged; reverting to previous revision %s",
                   targetdelta->num);
                if (removedlock < lockflag)
                  {
                    diagnose
                      ("previous revision was not locked; ignoring -l option");
                    lockthis = 0;
                  }
                dolog = false;
                if (!(changedRCS = lockflag < removedlock || assoclst.next))
                  {
                    workdelta = targetdelta;
                    SAME_AFTER (from, targetdelta->text);
                  }
                else
                  /* We have started to build the wrong new RCS file.
                     Start over from the beginning.  */
                  {
                    off_t hwm = ftello (frew);
                    bool bad_truncate;

                    Orewind (frew);
                    bad_truncate = PROB (ftruncate (fileno (frew), (off_t) 0));
                    grok_resynch (REPO (r));
                    if (! (workdelta = delta_from_ref (targetdelta->num)))
                      continue;
                    workdelta->pretty_log = targetdelta->pretty_log;
                    if (newdelta.state != default_state)
                      workdelta->state = newdelta.state;
                    if (lockthis < removedlock && removelock (workdelta) < 0)
                      continue;
                    if (!addsyms (workdelta->num))
                      continue;
                    if (PROB (dorewrite (true, true)))
                      continue;
                    VERBATIM (from, GROK (neck));
                    fro_spew (from, frew);
                    if (bad_truncate)
                      while (ftello (frew) < hwm)
                        /* White out any earlier mistake with '\n's.
                           This is unlikely.  */
                        afputc ('\n', frew);
                  }
              }
            else
              {
                int wfd = workptr->fd;
                struct stat checkworkstat;
                char const *diffv[6 + !!OPEN_O_BINARY], **diffp;
                off_t wo;

                diagnose ("new revision: %s; previous revision: %s",
                          newdelta.num, targetdelta->num);
                SAME_AFTER (from, targetdelta->text);
                newdelta.pretty_log = getlogmsg ();
                if (STDIO_P (workptr))
                  {
                    bool badness = false;

                    fro_bob (workptr);
                    if (CAN_FFLUSH_IN)
                      badness = PROB (fflush (workptr->stream));
                    else
                      {
                        wo = lseek (wfd, 0, SEEK_CUR);
                        badness = (-1 == wo
                                   || (wo && PROB (lseek (wfd, 0, SEEK_SET))));
                      }
                    if (badness)
                      Ierror ();
                  }
                diffp = diffv;
                *++diffp = prog_diff;
                *++diffp = diff_flags;
#if OPEN_O_BINARY
                if (kws == kwsub_b)
                  *++diffp = "--binary";
#endif
                *++diffp = newhead ? "-" : expname;
                *++diffp = newhead ? expname : "-";
                *++diffp = NULL;
                if (DIFF_TROUBLE == runv (wfd, diffname, diffv))
                  RFATAL ("diff failed");
                if (STDIO_P (workptr)
                    && !CAN_FFLUSH_IN
                    && PROB (lseek (wfd, wo, SEEK_CUR)))
                  Ierror ();
                if (newhead)
                  {
                    fro_bob (workptr);
                    putdftext (&newdelta, workptr, frew, false);
                    if (!putdtext (targetdelta, diffname, frew, true))
                      continue;
                  }
                else if (!putdtext (&newdelta, diffname, frew, true))
                  continue;

                /* Check whether the working file changed during checkin,
                   to avoid producing an inconsistent RCS file.  */
                if (PROB (fstat (wfd, &checkworkstat))
                    || workstat.st_mtime != checkworkstat.st_mtime
                    || workstat.st_size != checkworkstat.st_size)
                  {
                    MERR ("file changed during checkin");
                    continue;
                  }

                changedRCS = true;
              }
          }

        /* Deduce ‘time_t’ of new revision if it is needed later.  */
        wtime = (time_t) - 1;
        if (mtimeflag | Ttimeflag)
          wtime = date2time (workdelta->date);

        if (PROB (donerewrite (changedRCS, !Ttimeflag
                               ? (time_t) - 1
                               : from && wtime < (repo_stat->st_mtime
                                                  ? repo_stat->st_mtime
                                                  : wtime))))
          continue;

        if (!keepworkingfile)
          {
            fro_zclose (&workptr);
            /* Get rid of old file.  */
            r = un_link (mani_filename);
          }
        else
          {
            newworkmode = WORKMODE (repo_stat->st_mode,
                                    !(kws == kwsub_v
                                      || lockthis < BE (strictly_locking)));
            mtime = mtimeflag ? wtime : (time_t) - 1;

            /* Expand if it might change or if we can't fix mode, time.  */
            if (changework || PROB (r = fixwork (newworkmode, mtime)))
              {
                fro_bob (workptr);
                /* Expand keywords in file.  */
                BE (inclusive_of_Locker_in_Id_val) = lockthis;
                workdelta->name =
                  namedrev (assoclst.next
                            ? first_meaningful_symbolic_name ()
                            : (keepflag && (pv = PREV (name))
                               ? pv
                               : rev),
                            workdelta);
                switch (xpandfile (workptr, workdelta, &newworkname, dolog))
                  {
                  default:
                    continue;

                  case 0:
                    /* No expansion occurred; try to reuse working file
                       unless we already tried and failed.  */
                    if (changework)
                      if ((r = fixwork (newworkmode, mtime)) == 0)
                        break;
                    /* fall into */
                  case 1:
                    fro_zclose (&workptr);
                    aflush (exfile);
                    IGNOREINTS ();
                    r = chnamemod (&exfile, newworkname, mani_filename,
                                   1, newworkmode, mtime);
                    keepdirtemp (newworkname);
                    RESTOREINTS ();
                  }
              }
          }
        if (PROB (r))
          {
            syserror_errno (mani_filename);
            continue;
          }
        diagnose ("done");

      }

  tempunlink ();
  return exitstatus;
}

/* ci.c ends here */
