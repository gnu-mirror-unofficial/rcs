/* b-feph.c --- (possibly) temporary files

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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "b-complain.h"
#include "b-divvy.h"
#include "b-excwho.h"
#include "b-feph.h"

#define SFF_COUNT  (SFFI_NEWDIR + 2)

/* Temp names to be unlinked when done, if they are not 0.
   Must be at least ‘SFF_COUNT’.  */
#define TEMPNAMES  5

struct ephemstuff
{
  char *tmpdir;
  struct sff *tpnames;
};

#define EPH(x)  (BE (ephemstuff)-> x)

#ifndef HAVE_MKSTEMP
static int
homegrown_mkstemp (char *template)
/* Like mkstemp(2), but never return EINVAL.  That is, never check for
   missing "XXXXXX" since we know the unique caller DTRT.  */
{
  int pid = getpid ();
  char *end = template + strlen (template);
  char const xrep[] = {"abcdefghijklmnopqrstuvwxyz"
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                       /* Omit '0' => ‘strlen (xrep)’ is prime.  */
                       "123456789"};
  struct timeval tv;
  uint64_t n;
  int fd = -1;

  for (int patience = 42 * 42;
       0 > fd && patience;
       patience--)
    {
      if (0 > gettimeofday (&tv, NULL))
        return -1;
      /* Cast to ensure 64-bit shift.  */
      n = pid | (uint64_t)(tv.tv_sec ^ tv.tv_usec) << 32;
      for (char *w = end - 6; n && w < end; w++)
        {
          *w = xrep[n % 61];
          n = n / 61;
        }
      fd = open (template, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    }
  if (0 > fd)
    errno = EEXIST;
  return fd;
}

#define mkstemp  homegrown_mkstemp
#endif  /* !defined HAVE_MKSTEMP */

void
init_ephemstuff (void)
{
  BE (sff) = ZLLOC (SFF_COUNT, struct sff);
  BE (ephemstuff) = ZLLOC (1, struct ephemstuff);
  EPH (tpnames) = ZLLOC (TEMPNAMES, struct sff);
}

static void
jam_sff (struct sff *sff, const char *prefix)
/* Set contents of ‘sff->filename’ to the name of a temporary file made
   from a template with that starts with ‘prefix’.  If ‘prefix’ is
   NULL, use the system "temporary directory".  (Specify the empty
   string for cwd.)  If no name is possible, signal a fatal
   error.  Also, set ‘sff->disposition’ to ‘real’.  */
{
#define tmpdir  EPH (tmpdir)
  char *fn;
  size_t len;
  int fd;

  if (!prefix)
    {
      if (! tmpdir)
        {
          char slash[2] = { SLASH, '\0' };

#define TRY(envvarname)                         \
          if (! tmpdir)                         \
            tmpdir = getenv (#envvarname)
          TRY (TMPDIR);                 /* Unix tradition */
          TRY (TMP);                    /* DOS tradition */
          TRY (TEMP);                   /* another DOS tradition */
#undef TRY
          if (! tmpdir)
            tmpdir = P_tmpdir;

          accf (SHARED, "%s%s", tmpdir,
                SLASH != tmpdir[strlen (tmpdir) - 1] ? slash : "");
          tmpdir = finish_string (SHARED, &len);
        }
      prefix = tmpdir;
    }
  accf (SHARED, "%sXXXXXX", prefix);
  fn = finish_string (SHARED, &len);
  /* Support the 8.3 MS-DOG restriction, blech.  Truncate the non-directory
     filename component to two bytes so that the maximum non-extension name
     is 2 + 6 (Xs) = 8.  The extension is left empty.  What a waste.  */
  if ('/' != SLASH)
    {
      char *end = fn + len - 6;
      char *lastsep = strrchr (fn, SLASH);
      char *ndfc = lastsep ? 1 + lastsep : fn;
      char *dot;

      if (ndfc + 2 < end)
        {
          memset (ndfc + 2, 'X', 6);
          *dot = '\0';
        }
      /* If any of the (up to 2) remaining bytes are '.', replace it
         with the lowest (decimal) digit of the pid.  Double blech.  */
      if ((dot = strchr (ndfc, '.')))
        *dot = '0' + getpid () % 10;
    }

  if (0 > (fd = mkstemp (fn)))
    PFATAL ("could not make temporary file name (template \"%s\")", fn);

  close (fd);
  sff->filename = fn;
  sff->disposition = real;
#undef tmpdir
}

#define JAM_SFF(sff,prefix)  jam_sff (&sff, prefix)

char const *
maketemp (int n)
/* Create a unique filename and store it into the ‘n’th slot
   in ‘EPH (tpnames)’ (so that ‘tempunlink’ can unlink the file later).
   Return a pointer to the filename created.  */
{
  if (!EPH (tpnames)[n].filename)
    JAM_SFF (EPH (tpnames)[n], NULL);

  return EPH (tpnames)[n].filename;
}

char const *
makedirtemp (bool isworkfile)
/* Create a unique filename and store it into ‘BE (sff)’.  Because of
   storage in ‘BE (sff)’, ‘dirtempunlink’ can unlink the file later.
   Return a pointer to the filename created.
   If ‘isworkfile’, put it into the working file's directory;
   otherwise, put the unique file in RCSfile's directory.  */
{
  int slot = SFFI_NEWDIR + isworkfile;

  JAM_SFF (BE (sff)[slot], isworkfile
           ? MANI (filename)
           : REPO (filename));
  return BE (sff)[slot].filename;
}

void
keepdirtemp (char const *name)
/* Do not unlink ‘name’, either because it's not there any more,
   or because it has already been unlinked.  */
{
  for (int i = 0; i < SFF_COUNT; i++)
    if (name == BE (sff)[i].filename)
      {
        BE (sff)[i].disposition = notmade;
        return;
      }
  PFATAL ("keepdirtemp");
}

static void
reap (size_t count, struct sff all[count],
      int (*cut) (char const *filename))
{
  enum maker m;

  if (!all)
    return;

  for (size_t i = 0; i < count; i++)
    if (notmade != (m = all[i].disposition))
      {
        if (effective == m)
          seteid ();
        cut (all[i].filename);
        all[i].filename = NULL;
        if (effective == m)
          setrid ();
        all[i].disposition = notmade;
      }
}

void
tempunlink (void)
/* Clean up ‘maketemp’ files.  May be invoked by signal handler.  */
{
  reap (TEMPNAMES, EPH (tpnames), unlink);
}

void
dirtempunlink (void)
/* Clean up ‘makedirtemp’ files.
   May be invoked by signal handler.  */
{
  reap (SFF_COUNT, BE (sff), un_link);
}

/* b-feph.c ends here */
