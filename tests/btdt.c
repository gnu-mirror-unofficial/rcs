/* btdt.c --- been there done that

   Copyright (C) 2010 Thien-Thi Nguyen

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
#include <stdlib.h>
#include "b-feph.h"

/* This file is a collection of tests for various components
   of the RCS library.  */

struct top *top;

exiting void
bow_out (void)
{
  dirtempunlink ();
  tempunlink ();
  exit_failurefully ();
}

exiting void
bad_args (char const *argv0)
{
  fprintf (stderr, "%s: bad args (try %s --help)\n",
           argv0, PROGRAM (invoke));
  exit_failurefully ();
}


typedef int (main_t) (int argc, char *argv[argc]);

struct yeah
{
  char const *component;
  char const *usage;
  main_t *whatever;
  bool scramp;
};

#define YEAH(comp,out)  { #comp, comp ## _usage, comp ## _test, out }

struct yeah yeah[] =
  {
  };

#define NYEAH  (sizeof (yeah) / sizeof (struct yeah))

int
main (int argc, char *argv[argc])
{
  char const *me = "btdt";

  if (2 > argc || STR_SAME ("--help", argv[1]))
    {
      printf ("Usage: %s COMPONENT [ARG...]\n", me);
      for (size_t i = 0; i < NYEAH; i++)
        printf ("- %-10s %s\n", yeah[i].component, yeah[i].usage);
      printf ("\n(Read the source for details.)\n");
      return EXIT_SUCCESS;
    }

  if (STR_SAME ("--version", argv[1]))
    {
      printf ("btdt (%s) %s\n", PACKAGE_NAME, PACKAGE_VERSION);
      printf ("Copyright (C) 2010 Thien-Thi Nguyen\n");
      printf ("License GPLv3+; GNU GPL version 3 or later"
              " <http://gnu.org/licenses/gpl.html>\n\n");
      argv[1] = "--help";
      return main (argc, argv);
    }

  for (size_t i = 0; i < NYEAH; i++)
    if (STR_SAME (yeah[i].component, argv[1]))
      {
        int exitstatus;
        const struct program program =
          {
            .invoke = me,
            .name  = argv[1],
            .exiterr = yeah[i].scramp ? exit_failurefully : bow_out
          };

        gnurcs_init (&program);
        exitstatus = yeah[i].whatever (argc - 1, argv + 1);
        gnurcs_goodbye ();
        return exitstatus;
      }

  fprintf (stderr, "%s: bad component (try --help): %s\n", me, argv[1]);
  return EXIT_FAILURE;
}

/* btdt.c ends here */
