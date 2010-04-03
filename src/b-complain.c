/* b-complain.c --- various ways of writing to standard error

   Copyright (C) 2010 Thien-Thi Nguyen

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

/* Although standard error should be unbuffered by default,
   don't rely on it.  */
static bool unbufferedp;

void
unbuffer_standard_error (void)
{
  unbufferedp = !setvbuf (stderr, NULL, _IONBF, 0);
}

void
vcomplain (char const *fmt, va_list args)
{
  fflush (MANI (standard_output)
          ? MANI (standard_output)
          : stdout);
  vfprintf (stderr, fmt, args);
  if (!unbufferedp)
    fflush (stderr);
}

void
complain (char const *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  vcomplain (fmt, args);
  va_end (args);
}

#define COMPLAIN_PLUS_NEWLINE()  do             \
    {                                           \
      va_list args;                             \
                                                \
      va_start (args, fmt);                     \
      vcomplain (fmt, args);                    \
      va_end (args);                            \
      complain ("\n");                          \
    }                                           \
  while (0)

void
diagnose (char const *fmt, ...)
{
  if (! BE (quiet))
    COMPLAIN_PLUS_NEWLINE ();
}

static void
whoami (char const *who)
{
  complain ("%s: ", program.name);
  if (who)
    complain ("%s: ", who);
}

#define ERRONEOUS_X()  LEX (erroneousp) = true

void
syserror (int e, char const *who)
{
  whoami (NULL);
  ERRONEOUS_X ();
  errno = e;
  perror (who);
}

void
generic_warn (char const *who, char const *fmt, ...)
{
  if (!BE (quiet))
    {
      whoami (who);
      complain ("warning: ");
      COMPLAIN_PLUS_NEWLINE ();
    }
}

void
generic_error (char const *who, char const *fmt, ...)
{
  ERRONEOUS_X ();
  whoami (who);
  COMPLAIN_PLUS_NEWLINE ();
}

static void
die (void)
{
  complain ("%s aborted\n", program.name);
  program.exiterr ();
}

void
generic_fatal (char const *who, char const *fmt, ...)
{
  ERRONEOUS_X ();
  whoami (who);
  COMPLAIN_PLUS_NEWLINE ();
  die ();
}

void
fatal_syntax (char const *fmt, ...)
{
  complain ("%s: %s:%ld: ", program.name, REPO (filename), LEX (lno));
  COMPLAIN_PLUS_NEWLINE ();
  die ();
}

void
fatal_sys (char const *who)
{
  syserror (errno, who);
  die ();
}

/* b-complain.c ends here */
