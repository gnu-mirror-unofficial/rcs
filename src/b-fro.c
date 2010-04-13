/* b-fro.c --- read-only file

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
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#include <unistd.h>
#include "unistd-safer.h"
#include "b-complain.h"
#include "b-divvy.h"
#include "b-fb.h"
#include "b-fro.h"
#include "b-isr.h"

/* Size in bytes up to which we will mmap (if mmap is available).  */
#ifndef REASONABLE_MMAP_SIZE
#define REASONABLE_MMAP_SIZE  (16 * 1024 * 1024)
#endif

#if MMAP_SIGNAL
static void
mmap_deallocate (struct fro *f)
{
  if (0 > munmap (f->base, f->lim - f->base))
    fatal_sys ("munmap");
}
#endif  /* MMAP_SIGNAL */

struct fro *
fro_open (char const *name, char const *type, struct stat *status)
/* Open ‘name’ for reading, return its descriptor, and set ‘*status’.  */
{
  struct fro *f;
  FILE *stream;
  struct stat st;
  off_t s;
  int fd = fd_safer (open (name, O_RDONLY
#if OPEN_O_BINARY
                           | (strchr (type, 'b') ? OPEN_O_BINARY : 0)
#endif
                           ));

  if (0 > fd)
    return NULL;
  if (!status)
    status = &st;
  if (0 > fstat (fd, status))
    fatal_sys (name);
  if (!S_ISREG (status->st_mode))
    {
      PERR ("`%s' is not a regular file", name);
      close (fd);
      errno = EINVAL;
      return NULL;
    }
  s = status->st_size;

  f = ZLLOC (1, struct fro);

  /* Determine the read method.  */
  f->rm = status->st_size <= REASONABLE_MMAP_SIZE
    ? (MMAP_SIGNAL && status->st_size
       ? RM_MMAP
       : RM_MEM)
    : RM_STDIO;

  switch (f->rm)
    {
    case RM_MMAP:
#if MMAP_SIGNAL
      if (s != status->st_size)
        PFATAL ("%s: too large", name);
      f->stream = NULL;
      f->deallocate = NULL;
      ISR_DO (CATCHMMAPINTS);
      f->base = mmap (NULL, s, PROT_READ, MAP_SHARED, fd, 0);
      if (f->base == MAP_FAILED)
        fatal_sys (name);
      /* On many hosts, the superuser can mmap an NFS file
         it can't read.  So access the first page now, and
         print a nice message if a bus error occurs.  */
      if (has_NFS)
        access_page (ISR_SCRATCH, name, f->base);
      f->deallocate = mmap_deallocate;
      f->ptr = f->base;
      f->lim = f->base + s;
      fro_trundling (true, f);
      break;
#else
      /* fall through */
#endif

    case RM_MEM:
      /* Read it into main memory all at once; this is
         the simplest substitute for memory mapping.  */
      if (!s)
        f->base = (Iptr_type) &equal_line; /* Any nonzero address will do.  */
      else
        {
          ssize_t r;
          char *bufptr;
          size_t bufsiz = s;

          /* As ‘f’ is in ‘SHARED’, so must this go.  */
          f->base = alloc (SHARED, name, s);
          bufptr = f->base;
          do
            {
              if (0 > (r = read (fd, bufptr, bufsiz)))
                fatal_sys (name);

              if (!r)
                {
                  /* The file must have shrunk!  */
                  status->st_size = s -= bufsiz;
                  bufsiz = 0;
                }
              else
                {
                  bufptr += r;
                  bufsiz -= r;
                }
            }
          while (bufsiz);
          if (0 > lseek (fd, 0, SEEK_SET))
            fatal_sys (name);
        }
      f->ptr = f->base;
      f->lim = f->base + s;
      break;

    case RM_STDIO:
      if (!(stream = fdopen (fd, type)))
        fatal_sys (name);
      f->stream = stream;
      break;
    }

  f->fd = fd;
  return f;
}

void
fro_close (struct fro *f)
{
  int res = -1;

  if (!f)
    return;
  switch (f->rm)
    {
    case RM_MMAP:
    case RM_MEM:
      if (f->deallocate)
        (*f->deallocate) (f);
      f->base = NULL;
      res = close (f->fd);
      break;
    case RM_STDIO:
      res = fclose (f->stream);
      break;
    }
  /* Don't use ‘0 > res’ here; ‘fclose’ may return EOF.  */
  if (res)
    Ierror ();
  f->fd = -1;
}

void
fro_zclose (struct fro **p)
{
  fro_close (*p);
  *p = NULL;
}

off_t
fro_tello (struct fro *f)
{
  off_t rv = 0;

  switch (f->rm)
    {
    case RM_MMAP:
    case RM_MEM:
      rv = f->ptr - f->base;
      break;
    case RM_STDIO:
      rv = ftello (f->stream);
      break;
    }
  return rv;
}

void
fro_bob (struct fro *f)
{
  switch (f->rm)
    {
    case RM_MMAP:
    case RM_MEM:
      f->ptr = f->base;
      break;
    case RM_STDIO:
      if (0 > fseeko (f->stream, 0, SEEK_SET))
        Ierror ();
      break;
    }
}

bool
fro_getbyte (int *c, struct fro *f, bool noerror)
/* Try to get another byte from ‘f’.
   If at EOF, signal "unexpected end of file"
   or return true depending on the value of ‘noerror’.
   Otherwise, set ‘*c’ to the value and return false.  */
{
  switch (f->rm)
    {
#define DONE()  do                              \
        {                                       \
          if (noerror)                          \
            return true;                        \
          fatal_syntax                          \
            ("unexpected end of file");         \
        }                                       \
      while (0)

    case RM_MMAP:
    case RM_MEM:
      if (f->ptr == f->lim)
        DONE ();
      *c = *f->ptr++;
      break;
    case RM_STDIO:
      {
        FILE *stream = f->stream;
        int maybe = getc (stream);

        if (EOF == maybe)
          {
            testIerror (stream);
            DONE ();
          }
        *c = maybe;
      }
      break;
    }
  return false;
}

void
fro_get_prev_byte (int *c, struct fro *f)
{
  switch (f->rm)
    {
    case RM_MMAP:
    case RM_MEM:
      *c = (--(f)->ptr)[-1];
      break;
    case RM_STDIO:
      if (0 > fseeko (f->stream, -2, SEEK_CUR))
        Ierror ();
      fro_getbyte (c, f, false);
      break;
    }
}

#ifdef HAVE_MADVISE
#define USED_IF_HAVE_MADVISE
#else
#define USED_IF_HAVE_MADVISE  RCS_UNUSED
#endif

void
fro_trundling (bool sequentialp USED_IF_HAVE_MADVISE, struct fro *f)
/* Advise the mmap machinery (if applicable) that access to ‘f’
   is sequential if ‘sequentialp’, otherwise normal.  */
{
  switch (f->rm)
    {
    case RM_MMAP:
#ifdef HAVE_MADVISE
      madvise (f->base, f->lim - f->base,
               sequentialp ? MADV_SEQUENTIAL : MADV_NORMAL);
#endif
      break;
    case RM_MEM:
    case RM_STDIO:
      break;
    }
}

void
fro_spew (struct fro *f, FILE *to)
/* Copy the remainder of file ‘f’ to ‘outf’.  */
{
  switch (f->rm)
    {
    case RM_MMAP:
    case RM_MEM:
      awrite ((char const *) f->ptr, f->lim - f->ptr, to);
      f->ptr = f->lim;
      break;
    case RM_STDIO:
      {
        char buf[BUFSIZ * 8];
        size_t count;

        /* Now read the rest of the file in blocks.  */
        while (!feof (f->stream))
          {
            if (!(count = fread (buf, sizeof (*buf), sizeof (buf), f->stream)))
              {
                testIerror (f->stream);
                return;
              }
            awrite (buf, count, to);
          }
      }
      break;
    }
}

/* b-fro.c ends here */
