/* Clean up working files.

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
#include <errno.h>
#include <stdlib.h>
#include <dirent.h>
#include "same-inode.h"
#include "rcsclean.help"
#include "b-complain.h"
#include "b-divvy.h"

struct top *top;

static RILE *workptr;
static int exitstatus;

static void
cleanup (void)
{
  if (LEX (erroneousp))
    exitstatus = EXIT_FAILURE;
  Izclose (&FLOW (from));
  Izclose (&workptr);
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

static bool
unlock (struct hshentry *delta)
{
  register struct rcslock **al, *l;

  if (delta && delta->lockedby
      && strcmp (getcaller (), delta->lockedby) == 0)
    for (al = &ADMIN (locks); (l = *al); al = &l->nextlock)
      if (l->delta == delta)
        {
          *al = l->nextlock;
          delta->lockedby = NULL;
          return true;
        }
  return false;
}

struct link
{
  char        *entry;
  struct link *next;
};

static int
get_directory (char const *dirname, char ***aargv)
/* Put a vector of all DIRNAME's directory entries names into *AARGV.
   Ignore names of RCS files.
   Return the number of entries found.  Terminate the vector with 0.
   Allocate the storage for the vector and entry names.
   Do not sort the names.  Do not include '.' and '..'.  */
{
  DIR *d;
  struct dirent *e;
  struct divvy *justme = make_space ("justme");
  struct link *prev = NULL, *cur = NULL;
  size_t entries = 0;

  if (!(d = opendir (dirname)))
    fatal_sys (dirname);
  while ((errno = 0, e = readdir (d)))
    {
      char const *en = e->d_name;

      if (en[0] == '.' && (!en[1] || (en[1] == '.' && !en[2])))
        continue;
      if (rcssuffix (en))
        continue;
      cur = pointer_array (justme, 2);
      cur->entry = intern0 (SHARED, en);
      cur->next = prev;
      prev = cur;
      entries++;
    }
  if (errno || closedir (d) != 0)
    fatal_sys (dirname);
  *aargv = pointer_array (SHARED, entries);
  for (size_t i = 0; i < entries; i++)
    {
      (*aargv)[i] = cur->entry;
      cur = cur->next;
    }
  close_space (justme);
  return entries;
}

/*:help
[options] file ...
Options:
  -r[REV]       Specify revision.
  -u[REV]       Unlock if is locked and no differences found.
  -n[REV]       Dry run (no act, don't operate).
  -q[REV]       Quiet mode.
  -kSUBST       Substitute using mode SUBST (see co(1)).
  -T            Preserve the modification time on the RCS file even
                if the RCS file changes because a lock is removed.
  -V            Like --version.
  -VN           Emulate RCS version N.
  -xSUFF        Specify SUFF as a slash-separated list of suffixes
                used to identify RCS file names.
  -zZONE        Specify date output format in keyword-substitution.

REV defaults to the latest revision on the default branch.
*/

const struct program program =
  {
    .name = "rcsclean",
    .help = help,
    .exiterr = exiterr
  };

int
main (int argc, char **argv)
{
  static char const usage[] =
    "\nrcsclean: usage: rcsclean -ksubst -{nqru}[rev] -T -Vn -xsuff -zzone file ...";
  static struct buf revision;
  char *a, **newargv;
  char const *rev, *p;
  bool dounlock, perform, unlocked, unlockflag, waslocked, Ttimeflag;
  int expmode;
  struct hshentries *deltas;
  struct hshentry *delta;
  struct stat workstat;

  CHECK_HV ();
  gnurcs_init ();

  setrid ();

  expmode = -1;
  rev = NULL;
  BE (pe) = X_DEFAULT;
  perform = true;
  unlockflag = false;
  Ttimeflag = false;

  argc = getRCSINIT (argc, argv, &newargv);
  argv = newargv;
  for (;;)
    {
      if (--argc < 1)
        {
          argc = get_directory (".", &newargv);
          argv = newargv;
          break;
        }
      a = *++argv;
      if (!*a || *a++ != '-')
        break;
      switch (*a++)
        {
        case 'k':
          if (0 <= expmode)
            redefined ('k');
          if ((expmode = str2expmode (a)) < 0)
            goto unknown;
          break;

        case 'n':
          perform = false;
          goto handle_revision;

        case 'q':
          BE (quiet) = true;
          /* fall into */
        case 'r':
        handle_revision:
          if (*a)
            {
              if (rev)
                PWARN ("redefinition of revision number");
              rev = a;
            }
          break;

        case 'T':
          if (*a)
            goto unknown;
          Ttimeflag = true;
          break;

        case 'u':
          unlockflag = true;
          goto handle_revision;

        case 'V':
          setRCSversion (*argv);
          break;

        case 'x':
          BE (pe) = a;
          break;

        case 'z':
          zone_set (a);
          break;

        default:
        unknown:
          PERR ("unknown option: %s%s", *argv, usage);
        }
    }

  dounlock = perform & unlockflag;

  if (LEX (erroneousp))
    cleanup ();
  else
    for (; 0 < argc; cleanup (), ++argv, --argc)
      {

        ffree ();

        if (!(0 < pairnames (argc, argv,
                             dounlock ? rcswriteopen : rcsreadopen,
                             true, true)
              && (workptr = Iopen (MANI (filename), FOPEN_R_WORK, &workstat))))
          continue;

        if (SAME_INODE (REPO (stat), workstat))
          {
            RERR ("RCS file is the same as working file %s.", MANI (filename));
            continue;
          }

        gettree ();

        p = NULL;
        if (rev)
          {
            if (!fexpandsym (rev, &revision, workptr))
              continue;
            p = revision.string;
          }
        else if (ADMIN (head))
          switch (unlockflag ? findlock (false, &delta) : 0)
            {
            default:
              continue;
            case 0:
              p = ADMIN (defbr) ? ADMIN (defbr) : "";
              break;
            case 1:
              p = delta->num;
              break;
            }
        delta = NULL;
        deltas = NULL;                  /* Keep lint happy.  */
        if (p && !(delta = gr_revno (p, &deltas)))
          continue;

        waslocked = delta && delta->lockedby;
        BE (inclusive_of_Locker_in_Id_val) = unlock (delta);
        unlocked = BE (inclusive_of_Locker_in_Id_val) & unlockflag;
        if (unlocked < waslocked
            && workstat.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))
          continue;

        if (unlocked && !checkaccesslist ())
          continue;

        if (dorewrite (dounlock, unlocked) != 0)
          continue;

        if (0 <= expmode)
          BE (kws) = expmode;
        else if (waslocked
                 && BE (kws) == kwsub_kv
                 && WORKMODE (REPO (stat).st_mode, true) == workstat.st_mode)
          BE (kws) = kwsub_kvl;

        getdesc (false);

        if (!delta
            ? workstat.st_size != 0
            : 0 < rcsfcmp (workptr, &workstat,
                           buildrevision (deltas, delta, NULL, false),
                           delta))
          continue;

        if (BE (quiet) < unlocked)
          aprintf (stdout, "rcs -u%s %s\n", delta->num, REPO (filename));

        if (perform & unlocked)
          {
            if (deltas->first != delta)
              hey_trundling (true, FLOW (from));
            if (donerewrite (true, Ttimeflag
                             ? REPO (stat).st_mtime
                             : (time_t) - 1)
                != 0)
              continue;
          }

        if (!BE (quiet))
          aprintf (stdout, "rm -f %s\n", MANI (filename));
        Izclose (&workptr);
        if (perform && un_link (MANI (filename)) != 0)
          syserror_errno (MANI (filename));
      }

  tempunlink ();
  if (!BE (quiet))
    Ofclose (stdout);
  return exitstatus;
}

/* rcsclean.c ends here */
