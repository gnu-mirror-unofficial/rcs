/* gnu-h-v.c --- GNUish --help and --version handling

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND_VERSION                                         \
  (" (" PACKAGE_NAME ") " PACKAGE_VERSION "\n"                  \
   "Copyright (C) 2010 Thien-Thi Nguyen\n"                      \
   "Copyright (C) 1990-1995 Paul Eggert\n"                      \
   "Copyright (C) 1982,1988,1989 Walter F. Tichy, Purdue CS\n"  \
   "License GPLv2+; GNU GPL version 2 or later"                 \
   " <http://gnu.org/licenses/gpl.html>\n"                      \
   "This is free software: you are free"                        \
   " to change and redistribute it.\n"                          \
   "There is NO WARRANTY, to the extent permitted by law.\n")

#define BUGME  ("\nReport bugs to <" PACKAGE_BUGREPORT ">\n")

#define EXACTLY(k, s)  ('\0' == s[sizeof (k) - 1]               \
                        && !strncmp (k, s, sizeof (k) - 1))

void
display_version (void)
{
  printf ("%s%s", program.name, COMMAND_VERSION);
}

void
check_hv (int argc, char **argv)
{
  if (1 >= argc)
    return;

  if (EXACTLY ("--help", argv[1]))
    {
      printf ("Usage: %s %s%s", program.name, program.help, BUGME);
      exit (EXIT_SUCCESS);
    }

  if (EXACTLY ("--version", argv[1]))
    {
      display_version ();
      exit (EXIT_SUCCESS);
    }
}

/* gnu-h-v.c ends here */
