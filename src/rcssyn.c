/* RCS file syntactic analysis

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
#include <ctype.h>

/* Keyword table.  */
const char const
  Kaccess[] = "access",
  Kauthor[] = "author",
  Kbranch[] = "branch",
  Kcomment[] = "comment",
  Kdate[] = "date",
  Kdesc[] = "desc",
  Kexpand[] = "expand",
  Khead[] = "head",
  Klocks[] = "locks",
  Klog[] = "log",
  Knext[] = "next",
  Kstate[] = "state",
  Kstrict[] = "strict",
  Ksymbols[] = "symbols",
  Ktext[] = "text";

static char const
#if COMPAT2
  Ksuffix[] = "suffix",
#endif
  K_branches[] = "branches";

static struct buf Commleader;
struct cbuf Comment;
struct cbuf Ignored;
struct access *AccessList;
struct assoc *Symbols;
struct rcslock *Locks;
int Expand;
struct hshentry *Head;
char const *Dbranch;
int TotalDeltas;

static void
getsemi (char const *key)
/* Get a semicolon to finish off a phrase started by `key'.  */
{
  if (!getlex (SEMI))
    fatserror ("missing ';' after '%s'", key);
}

static struct hshentry *
getdnum (void)
/* Get a delta number.  */
{
  register struct hshentry *delta = getnum ();

  if (delta && countnumflds (delta->num) & 1)
    fatserror ("%s isn't a delta number", delta->num);
  return delta;
}

const char const *const expand_names[] = {
  /* These must agree with kwsub_* in rcsbase.h.  */
  "kv", "kvl", "k", "v", "o", "b",
  NULL
};

static int
strn2expmode (char const *s, size_t n)
{
  char const *const *p;

  for (p = expand_names; *p; ++p)
    if (memcmp (*p, s, n) == 0 && !(*p)[n])
      return p - expand_names;
  return -1;
}

void
getadmin (void)
/* Read an <admin> and initialize the appropriate global variables.  */
{
  register char const *id;
  struct access *newaccess;
  struct assoc *newassoc;
  struct rcslock *newlock;
  struct hshentry *delta;
  struct access **LastAccess;
  struct assoc **LastSymbol;
  struct rcslock **LastLock;
  struct buf b;
  struct cbuf cb;

  TotalDeltas = 0;

  getkey (Khead);
  Head = getdnum ();
  getsemi (Khead);

  Dbranch = NULL;
  if (getkeyopt (Kbranch))
    {
      if ((delta = getnum ()))
        Dbranch = delta->num;
      getsemi (Kbranch);
    }

#if COMPAT2
  /* Read suffix.  Only in release 2 format.  */
  if (getkeyopt (Ksuffix))
    {
      if (NEXT (tok) == STRING)
        {
          readstring ();
          /* Throw away the suffix.  */
          nextlex ();
        }
      else if (NEXT (tok) == ID)
        {
          nextlex ();
        }
      getsemi (Ksuffix);
    }
#endif  /* COMPAT2 */

  getkey (Kaccess);
  LastAccess = &AccessList;
  while ((id = getid ()))
    {
      newaccess = ftalloc (struct access);
      newaccess->login = id;
      *LastAccess = newaccess;
      LastAccess = &newaccess->nextaccess;
    }
  *LastAccess = NULL;
  getsemi (Kaccess);

  getkey (Ksymbols);
  LastSymbol = &Symbols;
  while ((id = getid ()))
    {
      if (!getlex (COLON))
        fatserror ("missing ':' in symbolic name definition");
      if (!(delta = getnum ()))
        {
          fatserror ("missing number in symbolic name definition");
        }
      else
        {
          /* Add new pair to association list.  */
          newassoc = ftalloc (struct assoc);
          newassoc->symbol = id;
          newassoc->num = delta->num;
          *LastSymbol = newassoc;
          LastSymbol = &newassoc->nextassoc;
        }
    }
  *LastSymbol = NULL;
  getsemi (Ksymbols);

  getkey (Klocks);
  LastLock = &Locks;
  while ((id = getid ()))
    {
      if (!getlex (COLON))
        fatserror ("missing ':' in lock");
      if (!(delta = getdnum ()))
        {
          fatserror ("missing number in lock");
        }
      else
        {
          /* Add new pair to lock list.  */
          newlock = ftalloc (struct rcslock);
          newlock->login = id;
          newlock->delta = delta;
          *LastLock = newlock;
          LastLock = &newlock->nextlock;
        }
    }
  *LastLock = NULL;
  getsemi (Klocks);

  if ((BE (strictly_locking) = getkeyopt (Kstrict)))
    getsemi (Kstrict);

  clear_buf (&Comment);
  if (getkeyopt (Kcomment))
    {
      if (NEXT (tok) == STRING)
        {
          Comment = savestring (&Commleader);
          nextlex ();
        }
      getsemi (Kcomment);
    }

  Expand = kwsub_kv;
  if (getkeyopt (Kexpand))
    {
      if (NEXT (tok) == STRING)
        {
          bufautobegin (&b);
          cb = savestring (&b);
          if ((Expand = strn2expmode (cb.string, cb.size)) < 0)
            fatserror ("unknown expand mode %.*s", (int) cb.size, cb.string);
          bufautoend (&b);
          nextlex ();
        }
      getsemi (Kexpand);
    }
  Ignored = getphrases (Kdesc);
}

int
str2expmode (char const *s)
/* Return expand mode corresponding to `s', or -1 if bad.  */
{
  return strn2expmode (s, strlen (s));
}

void
ignorephrases (const char *key)
/* Ignore a series of phrases that do not start with `key'.  Stop when the
   next phrase starts with a token that is not an identifier, or is `key'.  */
{
  for (;;)
    {
      nextlex ();
      if (NEXT (tok) != ID || strcmp (NEXT (str), key) == 0)
        break;
      warnignore ();
      BE (receptive_to_next_hash_key) = false;
      for (;; nextlex ())
        {
          switch (NEXT (tok))
            {
            case SEMI:
              BE (receptive_to_next_hash_key) = true;
              break;
            case ID:
            case NUM:
              ffree1 (NEXT (str));
              continue;
            case STRING:
              readstring ();
              continue;
            default:
              continue;
            }
          break;
        }
    }
}

static char const *
getkeyval (char const *keyword, enum tokens token, bool optional)
/* Read a pair of the form:
   <keyword> <token> ;
   where `token' is one of `ID' or `NUM'.  `optional' indicates whether
   <token> is optional.  Return a pointer to the actual character string
   of <id> or <num>.  */
{
  register char const *val = NULL;

  getkey (keyword);
  if (NEXT (tok) == token)
    {
      val = NEXT (str);
      nextlex ();
    }
  else
    {
      if (!optional)
        fatserror ("missing %s", keyword);
    }
  getsemi (keyword);
  return (val);
}

static bool
getdelta (void)
/* Read a delta block.  Return false if the
   current block does not start with a number.  */
{
  register struct hshentry *Delta, *num;
  struct branchhead **LastBranch, *NewBranch;

  if (!(Delta = getdnum ()))
    return false;

  /* Don't enter dates into hashtable.  */
  BE (receptive_to_next_hash_key) = false;
  Delta->date = getkeyval (Kdate, NUM, false);
  /* Reset BE (receptive_to_next_hash_key) for revision numbers.  */
  BE (receptive_to_next_hash_key) = true;

  Delta->author = getkeyval (Kauthor, ID, false);

  Delta->state = getkeyval (Kstate, ID, true);

  getkey (K_branches);
  LastBranch = &Delta->branches;
  while ((num = getdnum ()))
    {
      NewBranch = ftalloc (struct branchhead);
      NewBranch->hsh = num;
      *LastBranch = NewBranch;
      LastBranch = &NewBranch->nextbranch;
    }
  *LastBranch = NULL;
  getsemi (K_branches);

  getkey (Knext);
  Delta->next = num = getdnum ();
  getsemi (Knext);
  Delta->lockedby = NULL;
  Delta->log.string = NULL;
  Delta->selector = true;
  Delta->ig = getphrases (Kdesc);
  TotalDeltas++;
  return true;
}

void
gettree (void)
/* Read in the delta tree with `getdelta',
   then update the `lockedby' fields.  */
{
  struct rcslock const *currlock;

  while (getdelta ())
    continue;
  currlock = Locks;
  while (currlock)
    {
      currlock->delta->lockedby = currlock->login;
      currlock = currlock->nextlock;
    }
}

void
getdesc (bool prdesc)
/* Read in descriptive text.  `NEXT (tok)' is not advanced afterwards.
   If `prdesc' is set, then print text to stdout.  */
{
  getkeystring (Kdesc);
  if (prdesc)
    printstring ();                     /* echo string */
  else
    readstring ();                      /* skip string */
}

void
unexpected_EOF (void)
{
  rcsfaterror ("unexpected EOF in diff output");
}

void
initdiffcmd (register struct diffcmd *dc)
/* Initialize `*dc' suitably for `getdiffcmd'.  */
{
  dc->adprev = 0;
  dc->dafter = 0;
}

static void
badDiffOutput (char const *buf)
{
  rcsfaterror ("bad diff output line: %s", buf);
}

static void
diffLineNumberTooLarge (char const *buf)
{
  rcsfaterror ("diff line number too large: %s", buf);
}

int
getdiffcmd (RILE *finfile, bool delimiter, FILE *foutfile, struct diffcmd *dc)
/* Get an editing command output by "diff -n" from `finfile'.  The input
   is delimited by `SDELIM' if `delimiter' is set, EOF otherwise.  Copy
   a clean version of the command to `foutfile' (if non-NULL).  Return 0
   for 'd', 1 for 'a', and -1 for EOF.  Store the command's line number
   and length into `dc->line1' and `dc->nlines'.  Keep `dc->adprev' and
   `dc->dafter' up to date.  */
{
  register int c;
  declarecache;
  register FILE *fout;
  register char *p;
  register RILE *fin;
  long line1, nlines, t;
  char buf[BUFSIZ];

  fin = finfile;
  fout = foutfile;
  setupcache (fin);
  cache (fin);
  cachegeteof (c,
               {
                 if (delimiter)
                   unexpected_EOF ();
                 return -1;
               });
  if (delimiter)
    {
      if (c == SDELIM)
        {
          cacheget (c);
          if (c == SDELIM)
            {
              buf[0] = c;
              buf[1] = 0;
              badDiffOutput (buf);
            }
          uncache (fin);
          NEXT (c) = c;
          if (fout)
            aprintf (fout, "%c%c", SDELIM, c);
          return -1;
        }
    }
  p = buf;
  do
    {
      if (buf + BUFSIZ - 2 <= p)
        {
          rcsfaterror ("diff output command line too long");
        }
      *p++ = c;
      cachegeteof (c, unexpected_EOF ());
    }
  while (c != '\n');
  uncache (fin);
  if (delimiter)
    ++LEX (lno);
  *p = '\0';
  for (p = buf + 1; (c = *p++) == ' ';)
    continue;
  line1 = 0;
  while (isdigit (c))
    {
      if (LONG_MAX / 10 < line1
          || (t = line1 * 10, (line1 = t + (c - '0')) < t))
        diffLineNumberTooLarge (buf);
      c = *p++;
    }
  while (c == ' ')
    c = *p++;
  nlines = 0;
  while (isdigit (c))
    {
      if (LONG_MAX / 10 < nlines
          || (t = nlines * 10, (nlines = t + (c - '0')) < t))
        diffLineNumberTooLarge (buf);
      c = *p++;
    }
  if (c == '\r')
    c = *p++;
  if (c || !nlines)
    {
      badDiffOutput (buf);
    }
  if (line1 + nlines < line1)
    diffLineNumberTooLarge (buf);
  switch (buf[0])
    {
    case 'a':
      if (line1 < dc->adprev)
        {
          rcsfaterror ("backward insertion in diff output: %s", buf);
        }
      dc->adprev = line1 + 1;
      break;
    case 'd':
      if (line1 < dc->adprev || line1 < dc->dafter)
        {
          rcsfaterror ("backward deletion in diff output: %s", buf);
        }
      dc->adprev = line1;
      dc->dafter = line1 + nlines;
      break;
    default:
      badDiffOutput (buf);
    }
  if (fout)
    {
      aprintf (fout, "%s\n", buf);
    }
  dc->line1 = line1;
  dc->nlines = nlines;
  return buf[0] == 'a';
}

#ifdef SYNTEST
/* Input an RCS file and print its internal data structures.  */

static exiting void
exiterr (void)
{
  _Exit (EXIT_FAILURE);
}

const struct program program =
  {
    .name = "syntest"
    .exiterr = exiterr
  };

int
main (int argc, char *argv[])
{
  if (argc < 2)
    {
      aputs ("No input file\n", stderr);
      return EXIT_FAILURE;
    }
  if (!(finptr = Iopen (argv[1], FOPEN_R, NULL)))
    {
      faterror ("can't open input file %s", argv[1]);
    }
  Lexinit ();
  getadmin ();
  fdlock = STDOUT_FILENO;
  putadmin ();

  gettree ();

  getdesc (true);

  nextlex ();

  if (!eoflex ())
    {
      fatserror ("expecting EOF");
    }
  return EXIT_SUCCESS;
}

#endif  /* defined SYNTEST */

/* rcssyn.c ends here */
