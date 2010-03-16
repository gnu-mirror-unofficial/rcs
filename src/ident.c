/* Identify RCS keyword strings in files.

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

#include "rcsbase.h"
#include <stdbool.h>
#include "ident-help.c"

char const cmdid[] = "ident";

void
exiterr (void)
{
  _exit (EXIT_FAILURE);
}

static void
reportError (char const *s)
{
  int e = errno;

  fprintf (stderr, "%s error: ", cmdid);
  errno = e;
  perror (s);
}

static int
match (register FILE *fp)
/* Group substring between two KDELIM's; then do pattern match.  */
{
  char line[BUFSIZ];
  register int c;
  register char *tp;

  tp = line;
  while ((c = getc (fp)) != VDELIM)
    {
      if (c == EOF && feof (fp) | ferror (fp))
        return c;
      switch (ctab[c])
        {
        case LETTER:
        case Letter:
          *tp++ = c;
          if (tp < line + sizeof (line) - 4)
            break;
          /* fall into */
        default:
           /* Anything but 0 or KDELIM or EOF.  */
          return c ? c : '\n';
        }
    }
  if (tp == line)
    return c;
  *tp++ = c;
  if ((c = getc (fp)) != ' ')
    return c ? c : '\n';
  *tp++ = c;
  while ((c = getc (fp)) != KDELIM)
    {
      if (c == EOF && feof (fp) | ferror (fp))
        return c;
      switch (ctab[c])
        {
        default:
          *tp++ = c;
          if (tp < line + sizeof (line) - 2)
            break;
          /* fall into */
        case NEWLN:
        case UNKN:
          return c ? c : '\n';
        }
    }
  if (tp[-1] != ' ')
    return c;
  /* Append trailing KDELIM.  */
  *tp++ = c;
  *tp = '\0';
  printf ("     %c%s\n", KDELIM, line);
  return 0;
}

static int
scanfile (register FILE *file, char const *name)
/* Scan an open `file' (perhaps with `name') for keywords.  Return
   -1 if there's a write error; exit immediately on a read error.  */
{
  register int c;

  if (name)
    {
      printf ("%s:\n", name);
      if (ferror (stdout))
        return -1;
    }
  else
    name = "standard input";
  c = 0;
  while (c != EOF || !(feof (file) | ferror (file)))
    {
      if (c == KDELIM)
        {
          if ((c = match (file)))
            continue;
          if (ferror (stdout))
            return -1;
          quietflag = true;
        }
      c = getc (file);
    }
  if (ferror (file) || fclose (file) != 0)
    {
      reportError (name);
      /* The following is equivalent to `exit (EXIT_FAILURE)', but we
         invoke `exiterr' to keep lint happy.  The DOS and OS/2 ports
         need `exiterr'.  */
      fflush (stderr);
      fflush (stdout);
      exiterr ();
    }
  if (!quietflag)
    fprintf (stderr, "%s warning: no id keywords in %s\n", cmdid, name);
  return 0;
}

/*:help
[options] [file ...]
Options:
  -q            Suppress warnings if no patterns are found.
  -V            Like --version.

If no FILE is specified, scan standard input.
*/

int
main (int argc, char **argv)
{
  FILE *fp;
  int status = EXIT_SUCCESS;
  char const *a;

  CHECK_HV ();

  while ((a = *++argv) && *a == '-')
    while (*++a)
      switch (*a)
        {
        case 'q':
          quietflag = true;
          break;

        case 'V':
          display_version ();
          return EXIT_SUCCESS;

        default:
          fprintf (stderr, "ident: usage: ident -{qV} [file...]\n");
          return EXIT_FAILURE;
          break;
        }

  if (!a)
    scanfile (stdin, NULL);
  else
    do
      {
        if (!(fp = fopen (a, FOPEN_RB)))
          {
            reportError (a);
            status = EXIT_FAILURE;
          }
        else if (scanfile (fp, a) != 0
                 || (argv[1] && putchar ('\n') == EOF))
          break;
      }
    while ((a = *++argv));

  if (ferror (stdout) || fclose (stdout) != 0)
    {
      reportError ("standard output");
      status = EXIT_FAILURE;
    }
  return status;
}

/* ident.c ends here */
