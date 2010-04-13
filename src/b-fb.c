/* b-fb.c --- basic file operations

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
#include <errno.h>
#include <unistd.h>
#include "unistd-safer.h"
#include "b-complain.h"

void
Ierror (void)
{
  fatal_sys ("input error");
}

void
testIerror (FILE *f)
{
  if (ferror (f))
    Ierror ();
}

void
Oerror (void)
{
  if (BE (Oerrloop))
    PROGRAM (exiterr) ();
  BE (Oerrloop) = true;
  fatal_sys ("output error");
}

void
testOerror (FILE *o)
{
  if (ferror (o))
    Oerror ();
}

FILE *
fopen_safer (char const *filename, char const *type)
/* Like ‘fopen’, except the result is never stdin, stdout, or stderr.  */
{
  FILE *stream = fopen (filename, type);

  if (stream)
    {
      int fd = fileno (stream);

      if (STDIN_FILENO <= fd && fd <= STDERR_FILENO)
        {
          int f = dup_safer (fd);

          if (f < 0)
            {
              int e = errno;

              fclose (stream);
              errno = e;
              return NULL;
            }
          if (fclose (stream) != 0)
            {
              int e = errno;

              close (f);
              errno = e;
              return NULL;
            }
          stream = fdopen (f, type);
        }
    }
  return stream;
}

/* b-fb.c ends here */
