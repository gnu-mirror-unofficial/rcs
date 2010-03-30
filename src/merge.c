/* merge - three-way file merge

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
#include "merge.help"
#include "b-complain.h"

static char const usage[] =
  "\nmerge: usage: merge [-AeEpqxX3] [-L lab [-L lab [-L lab]]] file1 file2 file3";

static void
badoption (char const *a)
{
  PERR ("unknown option: %s%s", a, usage);
}

static exiting void
exiterr (void)
{
  tempunlink ();
  _Exit (DIFF_TROUBLE);
}

/*:help
[options] receiving-sibling parent other-sibling
Options:
  -A            Use `diff3 -A' style.
  -E            Use `diff3 -E' style (default).
  -e            Use `diff3 -e' style.
  -p            Write to stdout instead of overwriting RECEIVING-SIBLING.
  -q            Quiet mode; suppress conflict warnings.
  -LLABEL       (up to three times) Specify the conflict labels for
                RECEIVING-SIBLING, PARENT and OTHER-SIBLING, respectively.
  -V            Like --version.
*/

const struct program program =
  {
    .name = "merge",
    .help = help,
    .exiterr = exiterr
  };

int
main (int argc, char **argv)
{
  register char const *a;
  char const *arg[3], *label[3], *edarg = NULL;
  int labels;
  bool tostdout = false;

  CHECK_HV ();
  unbuffer_standard_error ();

  labels = 0;

  for (; (a = *++argv) && *a++ == '-'; --argc)
    {
      switch (*a++)
        {
        case 'A':
        case 'E':
        case 'e':
          if (edarg && edarg[1] != (*argv)[1])
            PERR ("%s and %s are incompatible", edarg, *argv);
          edarg = *argv;
          break;

        case 'p':
          tostdout = true;
          break;
        case 'q':
          BE (quiet) = true;
          break;

        case 'L':
          if (3 <= labels)
            PFATAL ("too many -L options");
          if (!(label[labels++] = *++argv))
            PFATAL ("-L needs following argument");
          --argc;
          break;

        case 'V':
          display_version ();
          return 0;

        default:
          badoption (a - 2);
          continue;
        }
      if (*a)
        badoption (a - 2);
    }

  if (argc != 4)
    PFATAL ("%s arguments%s", argc < 4 ? "not enough" : "too many", usage);

  /* This copy keeps us `const'-clean.  */
  arg[0] = argv[0];
  arg[1] = argv[1];
  arg[2] = argv[2];

  for (; labels < 3; labels++)
    label[labels] = arg[labels];

  if (LEX (nerr))
    exiterr ();
  return merge (tostdout, edarg, label, arg);
}

/* merge.c ends here */
