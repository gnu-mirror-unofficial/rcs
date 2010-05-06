/* Check in revisions of RCS files from working files.

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
#include <ctype.h>                      /* isdigit */
#include <stdlib.h>
#include <unistd.h>
#include "same-inode.h"
#include "ci.help"
#include "b-complain.h"
#include "b-divvy.h"
#include "b-esds.h"
#include "b-fb.h"
#include "b-feph.h"
#include "b-fro.h"
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
/* Deltas to be generated.  */
static struct hshentries *gendeltas;
/* Old delta to be generated.  */
static struct hshentry *targetdelta;
/* New delta to be inserted.  */
static struct hshentry newdelta;
static struct stat workstat;
static struct link assoclst;

static void
cleanup (void)
{
  if (LEX (erroneousp))
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
removelock (struct hshentry *delta)
/* Find the lock held by caller on ‘delta’,
   remove it, and return nonzero if successful.
   Print an error message and return -1 if there is no such lock.
   An exception is if ‘!strictly_locking’, and caller is the owner of
   the RCS file.  If caller does not have a lock in this case,
   return 0; return 1 if a lock is actually removed.  */
{
  struct wlink wfake, *wtp;
  char const *num;

  num = delta->num;
  for (wfake.next = ADMIN (locks), wtp = &wfake; wtp->next; wtp = wtp->next)
    {
      struct rcslock *rl = wtp->next->entry;

      if (rl->delta == delta)
        {
          if (STR_SAME (getcaller (), rl->login))
            {
              /* We found a lock on ‘delta’ by caller; delete it.  */
              wtp->next = wtp->next->next;
              ADMIN (locks) = wfake.next;
              delta->lockedby = NULL;
              return 1;
            }
          else
            {
              RERR ("revision %s locked by %s", num, rl->login);
              return -1;
            }
        }
    }
  if (!BE (strictly_locking) && myself (REPO (stat).st_uid))
    return 0;
  RERR ("no lock set by %s for revision %s", getcaller (), num);
  return -1;
}

static struct branchhead newbranch;   /* new branch to be inserted */

static int
addbranch (struct hshentry *branchpoint, struct cbuf *num, bool removedlock)
/* Add a new branch and branch delta at ‘branchpoint’.
   If ‘num’ is the null string, append the new branch, incrementing
   the highest branch number (initially 1), and setting the level number to 1.
   The new delta and branchhead are in globals ‘newdelta’ and ‘newbranch’, resp.
   The new number is placed into a ‘SHARED’ string with ‘num’ pointing to it.
   Return -1 on error, 1 if a lock is removed, 0 otherwise.
   If ‘removedlock’, a lock was already removed.  */
{
  struct branchhead *bhead, **btrail;
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
      newbranch.nextbranch = NULL;

    }
  else if (numlength == 0)
    {
      /* Append new branch to the end.  */
      bhead = branchpoint->branches;
      while (bhead->nextbranch)
        bhead = bhead->nextbranch;
      bhead->nextbranch = &newbranch;
      incnum (BRANCHNO (bhead->hsh->num), num);
      ADD (num, ".1");
      newbranch.nextbranch = NULL;
    }
  else
    {
      /* Place the branch properly.  */
      field = numlength - ((numlength & 1) ^ 1);
      /* Field of branch number.  */
      btrail = &branchpoint->branches;
      while (0 < (result = cmpnumfld (num->string, (*btrail)->hsh->num,
                                      field)))
        {
          btrail = &(*btrail)->nextbranch;
          if (!*btrail)
            {
              result = -1;
              break;
            }
        }
      if (result < 0)
        {
          /* Insert/append new branchhead.  */
          newbranch.nextbranch = *btrail;
          *btrail = &newbranch;
          if (numlength & 1)
            ADD (num, ".1");
        }
      else
        {
          /* Branch exists; append to end.  */
          targetdelta = gr_revno (BRANCHNO (num->string), &gendeltas);
          if (!targetdelta)
            return -1;
          if (cmpnum (num->string, targetdelta->num) <= 0)
            {
              RERR ("revision %s too low; must be higher than %s",
                    num->string, targetdelta->num);
              return -1;
            }
          if (!removedlock && 0 <= (removedlock = removelock (targetdelta)))
            {
              if (numlength & 1)
                incnum (targetdelta->num, num);
              targetdelta->next = &newdelta;
              newdelta.next = NULL;
            }
          return removedlock;
          /* Don't do anything to newbranch.  */
        }
    }
  newbranch.hsh = &newdelta;
  newdelta.next = NULL;
  if (branchpoint->lockedby)
    if (STR_SAME (branchpoint->lockedby, getcaller ()))
      return removelock (branchpoint);  /* This returns 1.  */
  return removedlock;
}

static int
addelta (void)
/* Append a delta to the delta tree, whose number is given by
   ‘newdelnum’.  Update ‘ADMIN (head)’, ‘newdelnum’, ‘newdelnumlength’,
   and the links in newdelta.
   Return -1 on error, 1 if a lock is removed, 0 otherwise.  */
{
  register char const *tp;
  register int i;
  int removedlock;
  int newdnumlength;            /* actual length of new rev. num. */

  newdnumlength = countnumflds (newdelnum.string);

  if (rcsinitflag)
    {
      /* This covers non-existing RCS file,
         and a file initialized with ‘rcs -i’.  */
      if (newdnumlength == 0 && ADMIN (defbr))
        {
          JAM (&newdelnum, ADMIN (defbr));
          newdnumlength = countnumflds (ADMIN (defbr));
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
      ADMIN (head) = &newdelta;
      newdelta.next = NULL;
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
          if (!gr_revno (targetdelta->num, &gendeltas))
            return -1;
          if (targetdelta == ADMIN (head))
            {
              /* Make new head.  */
              newdelta.next = ADMIN (head);
              ADMIN (head) = &newdelta;
            }
          else if (!targetdelta->next && countnumflds (targetdelta->num) > 2)
            {
              /* New tip revision on side branch.  */
              targetdelta->next = &newdelta;
              newdelta.next = NULL;
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
          /* No existing lock; try ‘ADMIN (defbr)’.  Update ‘newdelnum’.  */
          if (BE (strictly_locking) || !myself (REPO (stat).st_uid))
            {
              RERR ("no lock set by %s", getcaller ());
              return -1;
            }
          if (ADMIN (defbr))
            JAM (&newdelnum, ADMIN (defbr));
          else
            {
              incnum (ADMIN (head)->num, &newdelnum);
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
          if (cmpnumfld (newdelnum.string, ADMIN (head)->num, 1) == 0)
            incnum (ADMIN (head)->num, &newdelnum);
          else
            ADD (&newdelnum, ".1");
        }
      if (cmpnum (newdelnum.string, ADMIN (head)->num) <= 0)
        {
          RERR ("revision %s too low; must be higher than %s",
                newdelnum.string, ADMIN (head)->num);
          return -1;
        }
      targetdelta = ADMIN (head);
      if (0 <= (removedlock = removelock (ADMIN (head))))
        {
          if (!gr_revno (ADMIN (head)->num, &gendeltas))
            return -1;
          newdelta.next = ADMIN (head);
          ADMIN (head) = &newdelta;
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
      accumulate_range (SHARED, old.string, tp - 1);
      old.string = finish_string (SHARED, &old.size);
      if (! (targetdelta = gr_revno (old.string, &gendeltas)))
        return -1;
      if (cmpnum (targetdelta->num, old.string) != 0)
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
    time2date (now (), getcurdate_buffer);
  return getcurdate_buffer;
}

static int
fixwork (mode_t newworkmode, time_t mtime)
{
  return
    1 < workstat.st_nlink
    || (newworkmode & S_IWUSR && !myself (workstat.st_uid))
    || setmtime (MANI (filename), mtime) != 0
    ? -1 : workstat.st_mode == newworkmode ? 0
#ifdef HAVE_FCHMOD
    : fchmod (workptr->fd, newworkmode) == 0 ? 0
#endif
    : chmod (MANI (filename), newworkmode)
    ;
}

static int
xpandfile (struct fro *unexfile, struct hshentry const *delta,
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

  if (!targetdelta && (cmpnum (newdelnum.string, "1.1") == 0
                       || cmpnum (newdelnum.string, "1.0") == 0))
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
  -j[REV]       Just checkin, don't initialize; error if RCS file does not exist.
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
  -T            Set the RCS file's modification time to the new revision's
                time if the former precedes the latter and there is a new
                revision; preserve the RCS file's modification time otherwise.
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

const struct program program =
  {
    .name = "ci",
    .help = help,
    .exiterr = exiterr
  };

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
  struct hshentry *workdelta;
  struct link *tp_assoc = &assoclst;

  CHECK_HV ();
  gnurcs_init ();

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
                PWARN ("redefinition of revision number");
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
  if (LEX (erroneousp))
    cleanup ();
  else if (argc < 1)
    PFATAL ("no input file");
  else
    for (; 0 < argc; cleanup (), ++argv, --argc)
      {
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
#if defined HAVE_SETUID && defined HAVE_GETUID
            if (euid () != ruid ())
              {
                MERR
                  ("setuid initial checkin prohibited; use `rcs -i -a' first");
                continue;
              }
#endif
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
            rcsinitflag = !ADMIN (head);
          }

        /* ‘REPO (filename)’ contains the name of the RCS file,
           and ‘MANI (filename)’ contains the name of the working file.
           If the RCS file exists, ‘FLOW (from)’ contains the file
           descriptor for the RCS file, and ‘REPO (stat)’ is set.
           The admin node is initialized.  */

        diagnose ("%s  <--  %s", REPO (filename), MANI (filename));

        if (!(workptr = fro_open (MANI (filename), FOPEN_R_WORK, &workstat)))
          {
            syserror_errno (MANI (filename));
            continue;
          }

        if (FLOW (from))
          {
            if (SAME_INODE (REPO (stat), workstat))
              {
                RERR ("RCS file is the same as working file %s.",
                      MANI (filename));
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
                MERR ("can't find a revision number");
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

        /* Read the delta tree.  */
        if (FLOW (from))
          gettree ();

        /* Expand symbolic revision number.  */
        if (!fully_numeric (&newdelnum, krev, workptr))
          continue;

        /* Splice new delta into tree.  */
        if ((removedlock = addelta ()) < 0)
          continue;

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
        else if (keepflag && PREV (author))
            /* Preserve old author if possible.  */
          newdelta.author = PREV (author);
        else
          /* Otherwise use caller's id.  */
          newdelta.author = getcaller ();

        /* Set state.  */
        newdelta.state = default_state;
        if (state)
          /* Given by ‘-s’.  */
          newdelta.state = state;
        else if (keepflag && PREV (state))
          /* Preserve old state if possible.  */
          newdelta.state = PREV (state);

        /* Compute date.  */
        if (usestatdate)
          {
            time2date (workstat.st_mtime, altdate);
          }
        if (*altdate != '\0')
          /* Given by ‘-d’.  */
          newdelta.date = altdate;
        else if (keepflag && PREV (date))
          {
            /* Preserve old date if possible.  */
            str2date (PREV (date), olddate);
            newdelta.date = olddate;
          }
        else
          /* Use current date.  */
          newdelta.date = getcurdate ();
        /* Now check validity of date -- needed because of ‘-d’ and ‘-k’.  */
        if (targetdelta && cmpdate (newdelta.date, targetdelta->date) < 0)
          {
            RERR ("Date %s precedes %s in revision %s.",
                  date2str (newdelta.date, newdatebuf),
                  date2str (targetdelta->date, targetdatebuf),
                  targetdelta->num);
            continue;
          }

        if (lockflag && addlock (&newdelta, true) < 0)
          continue;

        if (keepflag && PREV (name))
          if (addsymbol (newdelta.num, PREV (name), false) < 0)
            continue;
        if (!addsyms (newdelta.num))
          continue;

        putadmin ();
        puttree (ADMIN (head), FLOW (rewr));
        putdesc (&newdesc, false, textfile);

        changework = BE (kws) < MIN_UNCHANGED_EXPAND;
        dolog = true;
        lockthis = lockflag;
        workdelta = &newdelta;

        /* Build rest of file.  */
        if (rcsinitflag)
          {
            diagnose ("initial revision: %s", newdelta.num);
            /* Get logmessage.  */
            newdelta.log = getlogmsg ();
            putdftext (&newdelta, workptr, FLOW (rewr), false);
            REPO (stat).st_mode = workstat.st_mode;
            REPO (stat).st_nlink = 0;
            changedRCS = true;
          }
        else
          {
            diffname = maketemp (0);
            newhead = ADMIN (head) == &newdelta;
            if (!newhead)
              FLOW (to) = FLOW (rewr);
            expname =
              buildrevision (gendeltas, targetdelta, NULL, false);
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
                  workdelta = targetdelta;
                else
                  /* We have started to build the wrong new RCS file.
                     Start over from the beginning.  */
                  {
                    off_t hwm = ftello (FLOW (rewr));
                    bool bad_truncate;

                    Orewind (FLOW (rewr));
                    bad_truncate = 0 > ftruncate (fileno (FLOW (rewr)), (off_t) 0);

                    fro_bob (FLOW (from));
                    Lexinit ();
                    getadmin ();
                    gettree ();
                    if (! (workdelta = gr_revno (targetdelta->num, &gendeltas)))
                      continue;
                    workdelta->log = targetdelta->log;
                    if (newdelta.state != default_state)
                      workdelta->state = newdelta.state;
                    if (lockthis < removedlock && removelock (workdelta) < 0)
                      continue;
                    if (!addsyms (workdelta->num))
                      continue;
                    if (dorewrite (true, true) != 0)
                      continue;
                    fro_spew (FLOW (from), FLOW (rewr));
                    if (bad_truncate)
                      while (ftello (FLOW (rewr)) < hwm)
                        /* White out any earlier mistake with '\n's.
                           This is unlikely.  */
                        afputc ('\n', FLOW (rewr));
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
                newdelta.log = getlogmsg ();
                if (STDIO_P (workptr))
                  {
                    bool badness = false;

                    fro_bob (workptr);
                    if (CAN_FFLUSH_IN)
                      badness = (0 > fflush (workptr->stream));
                    else
                      {
                        wo = lseek (wfd, 0, SEEK_CUR);
                        badness = (-1 == wo
                                   || (wo && 0 > lseek (wfd, 0, SEEK_SET)));
                      }
                    if (badness)
                      Ierror ();
                  }
                diffp = diffv;
                *++diffp = prog_diff;
                *++diffp = diff_flags;
#if OPEN_O_BINARY
                if (BE (kws) == kwsub_b)
                  *++diffp = "--binary";
#endif
                *++diffp = newhead ? "-" : expname;
                *++diffp = newhead ? expname : "-";
                *++diffp = NULL;
                if (DIFF_TROUBLE == runv (wfd, diffname, diffv))
                  RFATAL ("diff failed");
                if (STDIO_P (workptr)
                    && !CAN_FFLUSH_IN
                    && 0 > lseek (wfd, wo, SEEK_CUR))
                  Ierror ();
                if (newhead)
                  {
                    fro_bob (workptr);
                    putdftext (&newdelta, workptr, FLOW (rewr), false);
                    if (!putdtext (targetdelta, diffname, FLOW (rewr), true))
                      continue;
                  }
                else if (!putdtext (&newdelta, diffname, FLOW (rewr), true))
                  continue;

                /* Check whether the working file changed during checkin,
                   to avoid producing an inconsistent RCS file.  */
                if (fstat (wfd, &checkworkstat) != 0
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

        if (donerewrite (changedRCS, !Ttimeflag
                         ? (time_t) - 1
                         : FLOW (from) && wtime < (REPO (stat).st_mtime
                                                   ? REPO (stat).st_mtime
                                                   : wtime))
            != 0)
          continue;

        if (!keepworkingfile)
          {
            fro_zclose (&workptr);
            /* Get rid of old file.  */
            r = un_link (MANI (filename));
          }
        else
          {
            newworkmode = WORKMODE (REPO (stat).st_mode,
                                    !(BE (kws) == kwsub_v
                                      || lockthis < BE (strictly_locking)));
            mtime = mtimeflag ? wtime : (time_t) - 1;

            /* Expand if it might change or if we can't fix mode, time.  */
            if (changework || (r = fixwork (newworkmode, mtime)) != 0)
              {
                fro_bob (workptr);
                /* Expand keywords in file.  */
                BE (inclusive_of_Locker_in_Id_val) = lockthis;
                workdelta->name =
                  namedrev (assoclst.next
                            ? first_meaningful_symbolic_name ()
                            : (keepflag && PREV (name)
                               ? PREV (name)
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
                    r = chnamemod (&exfile, newworkname, MANI (filename),
                                   1, newworkmode, mtime);
                    keepdirtemp (newworkname);
                    RESTOREINTS ();
                  }
              }
          }
        if (r != 0)
          {
            syserror_errno (MANI (filename));
            continue;
          }
        diagnose ("done");

      }

  tempunlink ();
  return exitstatus;
}

/* ci.c ends here */
