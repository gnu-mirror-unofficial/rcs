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
   - TEST_GROK
   - TEST_REV
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

static const struct program program = { .name = "y-STR2TIME" };

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

static const struct program program = { .name = "y-FCMP" };

int
yarrrr (int argc, char *argv[argc])
/* Arguments:
   1st: comment leader
   2nd: log message
   3rd: expanded file
   4th: unexpanded file  */
{
  struct delta delta;
  struct stat st;

  REPO (log_lead).string = argv[1];
  REPO (log_lead).size = strlen (argv[1]);
  delta.pretty_log.string = argv[2];
  delta.pretty_log.size = strlen (argv[2]);
  if (rcsfcmp (fro_open (argv[3], FOPEN_R_WORK, &st), &st, argv[4], &delta))
    printf ("files are the same\n");
  else
    printf ("files are different\n");
  return EXIT_SUCCESS;
}
#endif  /* TEST_FCMP */


/* ‘pairnames’, ‘getfullRCSname’ */

#ifdef TEST_PAIRS

static const struct program program =
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

static const struct program program = { .name = "y-KEEP" };

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


/* grokking */

#ifdef TEST_GROK
#include "b-divvy.h"
#include "b-esds.h"
#include "b-grok.h"

void
spew_atat (char const *who, struct fro *f, struct atat *atat)
{
  size_t special = 0;

  if (who)
    printf ("%s:", who);
  printf (" +%llu [%u]", atat->beg, atat->count);
  if (who)
    printf ("<<");
  for (size_t i = 0; i < atat->count; i++)
    {
      bool needexp = atat->ineedexp (atat, i);
      struct range r =
        {
          .beg = 1 + (i ? atat->holes[i - 1] : atat->beg),
          .end = atat->holes[i]
        };

      printf ("\n\t[%u]: %c\"", i, needexp ? KDELIM : ' ');
      special += needexp;
      fro_spew_partial (stdout, f, &r);
      printf ("\"");
    }
  if (who)
    printf (">> %10u %10u  %8.2f%%\n", special, atat->count,
            100.0 * special / atat->count);
}

/* These are copies from b-grok.c (maintain us!).  */
struct hash
{
  size_t sz;
  struct wlink **a;
};

struct lockdef
{
  char const *login;
  char const *revno;
};

static void
dump_hash_table (struct repo *r)
{
  for (size_t i = 0; i < r->ht->sz; i++)
    {
      struct wlink *p = r->ht->a[i];
      size_t len = 0;

      if (p)
        {
          printf ("\t[%u]", i);
          while (p)
            {
              /* This relies on ‘struct notyet’ having
                 the first member ‘char const *revno’.  */
              char const **revno = p->entry;

              printf ("(%s)", *revno);
              p = p->next;
              len++;
            }
          printf (" %u\n", len);
        }
    }
}

static exiting void
exiterr (void)
{
  _Exit (1);
}

static const struct program program = { .name = "y-GROK", .exiterr = exiterr };

int
yarrrr (int argc, char *argv[argc])
{
  struct fro *f;
  struct divvy *stash;
  struct repo *r;
  struct link *pair;
  struct wlink *wpair;
  size_t i;

  REPO (filename) = argv[1];            /* FIXME: for ‘fatal_syntax’ */
  stash = SINGLE;
  f = fro_open (argv[1], "r", NULL);
  r = grok_all (stash, f);

  if (r->ht)
    dump_hash_table (r);

#define SPEW_ATAT(who,atat)  spew_atat (who, f, atat)

  printf ("%s: %s;\n", TINYKS (head), r->head);
  printf ("%s: %s;\n", TINYKS (branch), r->branch);
  printf ("%s:", TINYKS (access));
  for (i = 0, pair = r->access; i < r->access_count; i++, pair = pair->next)
    printf (" [%u] %s", i, (char *) pair->entry);
  if (pair)
    printf ("\nWTF: pair: %p\n", (void *)pair);
  printf (" [%u];\n", r->access_count);

  printf ("%s:", TINYKS (symbols));
  for (i = 0, pair = r->symbols; i < r->symbols_count; i++, pair = pair->next)
    {
      struct symdef const *sym = pair->entry;

      printf (" [%u] %s:%s", i, sym->meaningful, sym->underlying);
    }
  if (pair)
    printf ("\nWTF: pair: %p\n", (void *)pair);
  printf (" [%u];\n", r->symbols_count);

  printf ("%s:", TINYKS (locks));
  for (i = 0, pair = r->locks; i < r->locks_count; i++, pair = pair->next)
    {
      struct lockdef const *lock = pair->entry;

      printf (" [%u] %s:%s", i, lock->login, lock->revno);
    }
  if (pair)
    printf ("\nWTF: pair: %p\n", (void *)pair);
  printf (" [%u];\n", r->locks_count);

  if (r->strict)
    printf ("%s;\n", TINYKS (strict));

  printf ("%s", TINYKS (comment));
  if (r->comment)
    SPEW_ATAT (NULL, r->comment);
  printf (";\n");

  printf ("%s", TINYKS (expand));
  if (-1 < r->expand)
    printf (": %s", kwsub_string (r->expand));
  printf (";\n");

  for (i = 0, wpair = r->deltas; i < r->deltas_count; i++, wpair = wpair->next)
    {
      struct delta *d = wpair->entry, *br;
      struct wlink *ls;

      if (d->lockedby)
        printf ("|%s| ", d->lockedby);
      printf ("<%s> <%s> <%s> <%s>", d->num, d->date, d->author, d->state);
      for (ls = d->branches; ls; ls = ls->next)
        {
          br = ls->entry;
          printf (" [b %s]", br->num);
        }
      br = d->next;
      printf (" [n %s]", br ? br->num : "-");
      if (d->commitid)
        printf (" |%s|", d->commitid);
      printf ("\n");
    }
  printf ("# revisions: %u\n", r->deltas_count);

  SPEW_ATAT (TINYKS (desc), r->desc);

  for (wpair = r->deltas; wpair; wpair = wpair->next)
    {
      struct delta *d = wpair->entry;

      printf ("revno: %s\n", d->num);
      SPEW_ATAT (TINYKS (log), d->log);
      SPEW_ATAT (TINYKS (text), d->text);
    }

  return 0;

#undef SPEW_ATAT
}

#endif  /* TEST_GROK */


/* ‘genrevs’ et al */

#ifdef TEST_REV
#include "b-divvy.h"
#include "b-grok.h"

/* Test the routines that generate a sequence of delta numbers
   needed to regenerate a given delta.  */

static const struct program program =
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
  struct delta *target;

  if (argc < 2)
    {
      complain ("No input file\n");
      return EXIT_FAILURE;
    }

  if (!(FLOW (from) = fro_open (argv[1], FOPEN_R, NULL)))
    PFATAL ("can't open input file %s", argv[1]);
  REPO (filename) = argv[1];
  REPO (r) = grok_all (SINGLE, FLOW (from));
  FIXUP_OLD (GROK (desc));

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


int
main (int argc, char *argv[argc])
{
  gnurcs_init (&program);
  return yarrrr (argc, argv);
}

/* yarrrr.c ends here */
