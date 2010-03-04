/* gnu-h-v.h --- GNUish --help and --version handling

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

/* Display the version blurb to stdout, starting with:
   | CMDID (GNU RCS) PACKAGE_VERSION
   | ...
   and ending with newline.  */
extern void
display_version (const char *cmdid);

/* If ARGC is less than 2, do nothing.
   If ARGV[1] is "--version", use `display_version' and exit successfully.
   If ARGV[1] is "--help", display the help blurb, starting with:
   | CMDID HELP
   and exit successfully.  */
extern void
check_hv (int argc, char **argv, const char *cmdid, const char *help);

/* Idiom.  */
#define CHECK_HV()  check_hv (argc, argv, cmdid, help)

/* gnu-h-v.h ends here */
