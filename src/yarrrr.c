/* yarrrr.c --- yet another redolently redundant RCS rap

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
#include "b-complain.h"
#include "b-feph.h"
#include "b-fro.h"

/* This file is a collection of test programs for various modules.
   The maintainers use it sporadically to stress RCS internals.

   To compile, #define exactly one of:
   - TEST_STR2TIME
   - TEST_FCMP
   - TEST_PAIRS
   - TEST_KEEP
   - TEST_LEX
   - TEST_REV
   - TEST_SYN
   If none of these are defined, the program will not compile.

   NB: This code is not well-maintained.  In particular, there is
   no --help or --version support; but since you're reading the
   Source you might as well take a peek below for more info.  */

struct top *top;

static exiting void
bow_out (void)
{
  dirtempunlink ();
  tempunlink ();
  _Exit (EXIT_FAILURE);
}

static exiting void
scram (void)
{
  _Exit (EXIT_FAILURE);
}

/* Removed by eggert 1995-06-01.  */
#define FOPEN_R  "r"


/* ‘str2time’ */

#ifdef TEST_STR2TIME
#include "maketime.h"

const struct program program = { .name = "y-STR2TIME" };

int
yarrrr (int argc, char *argv[argc])
{
  time_t default_time = time (NULL);
  long default_zone = argv[1] ? atol (argv[1]) : 0;
  char buf[1000];

  while (fgets (buf, 1000, stdin))
    {
      time_t t = str2time (buf, default_time, default_zone);

      printf ("%s", asctime (gmtime (&t)));
    }
  return 0;
}
#endif  /* TEST_STR2TIME */


/* ‘rcsfcmp’ */

#ifdef TEST_FCMP
#include <string.h>

/* The test program prints out whether two files are identical,
   except for keywords.  */

const struct program program = { .name = "y-FCMP" };

int
yarrrr (int argc, char *argv[argc])
/* Arguments:
   1st: comment leader
   2nd: log message
   3rd: expanded file
   4th: unexpanded file  */
{
  struct hshentry delta;
  struct stat st;

  ADMIN (log_lead).string = argv[1];
  ADMIN (log_lead).size = strlen (argv[1]);
  delta.log.string = argv[2];
  delta.log.size = strlen (argv[2]);
  if (rcsfcmp (fro_open (argv[3], FOPEN_R_WORK, &st), &st, argv[4], &delta))
    printf ("files are the same\n");
  else
    printf ("files are different\n");
  return EXIT_SUCCESS;
}
#endif  /* TEST_FCMP */


/* ‘pairnames’, ‘getfullRCSname’ */

#ifdef TEST_PAIRS

const struct program program =
  {
    .name = "y-PAIRS",
    .exiterr = bow_out
  };

int
yarrrr (int argc, char *argv[argc])
{
  int result;
  bool initflag;

  BE (quiet) = initflag = false;

  while (--argc, ++argv, argc >= 1 && ((*argv)[0] == '-'))
    switch ((*argv)[1])
      {
      case 'p':
        MANI (standard_output) = stdout;
        break;
      case 'i':
        initflag = true;
        break;
      case 'q':
        BE (quiet) = true;
        break;
      default:
        bad_option (*argv);
        break;
      }

  do
    {
      REPO (filename) = MANI (filename) = NULL;
      result = pairnames (argc, argv, rcsreadopen, !initflag, BE (quiet));
      if (result != 0)
        diagnose
          ("RCS filename: %s; working filename: %s\nFull RCS filename: %s",
           REPO (filename), MANI (filename), getfullRCSname ());

      switch (result)
        {
        case 0:
          continue;                     /* already paired file */

        case 1:
          if (initflag)
            RERR ("already exists");
          else
            diagnose ("RCS file %s exists", REPO (filename));
          fro_close (FLOW (from));
          break;

        case -1:
          diagnose ("RCS file doesn't exist");
          break;
        }
    }
  while (++argv, --argc >= 1);

  return EXIT_SUCCESS;
}

#endif  /* TEST_PAIRS */


/* ‘getoldkeys’ */

#ifdef TEST_KEEP
#include <string.h>
/* Print the keyword values found.  */

const struct program program = { .name = "y-KEEP" };

void
spew (char const *what, char *s)
{
  printf ("%8s: ", what);
  if (s)
    printf ("%2d \"%s\"", strlen (s), s);
  else
    printf ("   (NULL)");
  printf ("\n");
}

int
yarrrr (int argc, char *argv[argc])
{
  while (*(++argv))
    {
      MANI (filename) = *argv;
      getoldkeys (NULL);
      spew ("filename", *argv);
      spew ("revno", PREV (rev));
      spew ("date", PREV (date));
      spew ("author", PREV (author));
      spew ("name", PREV (name));
      spew ("state", PREV (state));
      printf ("\n");
    }
  return EXIT_SUCCESS;
}
#endif  /* TEST_KEEP */


/* parsing (low level) */

#ifdef TEST_LEX
/* The test program reads a stream of lexemes, enters the revision numbers
   into the hashtable, and prints the recognized tokens.  Keywords are
   recognized as identifiers.  */

const struct program program =
  {
    .name = "y-LEX",
    .exiterr = scram
  };

int
yarrrr (int argc, char *argv[argc])
{
  if (argc < 2)
    {
      complain ("No input file\n");
      return EXIT_FAILURE;
    }

  if (!(FLOW (from) = fro_open (argv[1], FOPEN_R, NULL)))
    PFATAL ("can't open input file %s", argv[1]);

  Lexinit ();
  while (!eoflex ())
    {
      switch (NEXT (tok))
        {
        case ID:
          printf ("ID: %s", NEXT (str));
          break;

        case NUM:
          if (BE (receptive_to_next_hash_key))
            printf ("NUM: %s", NEXT (hsh)->num);
          else
            printf ("NUM, unentered: %s", NEXT (str));
          /* Alternate between dates and numbers.  */
          BE (receptive_to_next_hash_key) = !BE (receptive_to_next_hash_key);
          break;

        case COLON:
          printf ("COLON");
          break;

        case SEMI:
          printf ("SEMI");
          break;

        case STRING:
          readstring ();
          printf ("STRING");
          break;

        case UNKN:
          printf ("UNKN");
          break;

        default:
          printf ("DEFAULT");
          break;
        }
      printf (" | ");
      nextlex ();
    }
  return EXIT_SUCCESS;
}

#endif  /* TEST_LEX */


/* ‘genrevs’ et al */

#ifdef TEST_REV
/* Test the routines that generate a sequence of delta numbers
   needed to regenerate a given delta.  */

const struct program program =
  {
    .name = "y-REV",
    .exiterr = scram
  };

int
yarrrr (int argc, char *argv[argc])
{
  struct cbuf numricrevno;
  char symrevno[100];           /* used for input of revision numbers */
  char author[20];
  char state[20];
  char date[20];
  struct hshentries *gendeltas;
  struct hshentry *target;

  if (argc < 2)
    {
      complain ("No input file\n");
      return EXIT_FAILURE;
    }

  if (!(FLOW (from) = fro_open (argv[1], FOPEN_R, NULL)))
    PFATAL ("can't open input file %s", argv[1]);

  Lexinit ();
  getadmin ();

  gettree ();

  getdesc (false);

  do
    {
      /* All output goes to stderr, to have diagnostics and
         errors in sequence.  */
#define prompt complain
#define more(buf)  fgets (buf, sizeof buf, stdin)
      prompt ("\nEnter revision number or <return> or '.': ");
      if (!more (symrevno))
        break;
      if (*symrevno == '.')
        break;
      prompt ("%s;\n", symrevno);
      complain ("expanded number: %s\n",
                (fully_numeric_no_k (&numricrevno, symrevno)
                 ? numricrevno.string
                 : " <INVALID>"));
      prompt ("Date: ");
      more (date);
      prompt ("%s; ", date);
      prompt ("Author: ");
      more (author);
      prompt ("%s; ", author);
      prompt ("State: ");
      more (state);
      prompt ("%s;\n", state);
      target = genrevs (numricrevno.string,
                        *date ? date : NULL,
                        *author ? author : NULL,
                        *state ? state : NULL,
                        &gendeltas);
      if (target)
        while (gendeltas)
          {
            complain ("%s\n", gendeltas->first->num);
            gendeltas = gendeltas->rest;
          }
#undef more
#undef prompt
    }
  while (true);
  complain ("done\n");
  return EXIT_SUCCESS;
}

#endif  /* TEST_REV */


/* parsing (high level) */

#ifdef TEST_SYN
#include <unistd.h>

/* Input an RCS file and print its internal data structures.  */

const struct program program =
  {
    .name = "y-SYN",
    .exiterr = scram
  };

int
yarrrr (int argc, char *argv[argc])
{
  if (argc < 2)
    {
      complain ("No input file\n");
      return EXIT_FAILURE;
    }
  if (!(FLOW (from) = fro_open (argv[1], FOPEN_R, NULL)))
    PFATAL ("can't open input file %s", argv[1]);

  Lexinit ();
  getadmin ();
  REPO (fd_lock) = STDOUT_FILENO;
  putadmin ();

  gettree ();

  getdesc (true);

  nextlex ();

  if (!eoflex ())
    SYNTAX_ERROR ("expecting EOF");

  return EXIT_SUCCESS;
}

#endif  /* TEST_SYN */


int
main (int argc, char *argv[argc])
{
  gnurcs_init ();
  return yarrrr (argc, argv);
}

/* yarrrr.c ends here */
