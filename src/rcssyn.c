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

/******************************************************************************
 *                       Syntax Analysis.
 *                       Keyword table
 *                       Testprogram: define SYNTEST
 *                       Compatibility with Release 2: define COMPAT2=1
 ******************************************************************************
 */


/* version COMPAT2 reads files of the format of release 2 and 3, but
 * generates files of release 3 format. Need not be defined if no
 * old RCS files generated with release 2 exist.
 */

#include "rcsbase.h"

static char const *getkeyval (char const *, enum tokens, int);
static int getdelta (void);
static int strn2expmode (char const *, size_t);
static struct hshentry *getdnum (void);
static void badDiffOutput (char const *) exiting;
static void diffLineNumberTooLarge (char const *) exiting;
static void getsemi (char const *);

/* keyword table */

char const
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
  Kstrict[] = "strict", Ksymbols[] = "symbols", Ktext[] = "text";

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
int StrictLocks;
struct hshentry *Head;
char const *Dbranch;
int TotalDeltas;

static void
getsemi (char const *key)
/* Get a semicolon to finish off a phrase started by KEY.  */
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

  Dbranch = 0;
  if (getkeyopt (Kbranch))
    {
      if ((delta = getnum ()))
        Dbranch = delta->num;
      getsemi (Kbranch);
    }

#if COMPAT2
  /* read suffix. Only in release 2 format */
  if (getkeyopt (Ksuffix))
    {
      if (nexttok == STRING)
        {
          readstring ();
          nextlex ();           /* Throw away the suffix.  */
        }
      else if (nexttok == ID)
        {
          nextlex ();
        }
      getsemi (Ksuffix);
    }
#endif

  getkey (Kaccess);
  LastAccess = &AccessList;
  while ((id = getid ()))
    {
      newaccess = ftalloc (struct access);
      newaccess->login = id;
      *LastAccess = newaccess;
      LastAccess = &newaccess->nextaccess;
    }
  *LastAccess = 0;
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
        {                       /*add new pair to association list */
          newassoc = ftalloc (struct assoc);
          newassoc->symbol = id;
          newassoc->num = delta->num;
          *LastSymbol = newassoc;
          LastSymbol = &newassoc->nextassoc;
        }
    }
  *LastSymbol = 0;
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
        {                       /*add new pair to lock list */
          newlock = ftalloc (struct rcslock);
          newlock->login = id;
          newlock->delta = delta;
          *LastLock = newlock;
          LastLock = &newlock->nextlock;
        }
    }
  *LastLock = 0;
  getsemi (Klocks);

  if ((StrictLocks = getkeyopt (Kstrict)))
    getsemi (Kstrict);

  clear_buf (&Comment);
  if (getkeyopt (Kcomment))
    {
      if (nexttok == STRING)
        {
          Comment = savestring (&Commleader);
          nextlex ();
        }
      getsemi (Kcomment);
    }

  Expand = KEYVAL_EXPAND;
  if (getkeyopt (Kexpand))
    {
      if (nexttok == STRING)
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

char const *const expand_names[] = {
  /* These must agree with *_EXPAND in rcsbase.h.  */
  "kv", "kvl", "k", "v", "o", "b",
  0
};

int
str2expmode (char const *s)
/* Yield expand mode corresponding to S, or -1 if bad.  */
{
  return strn2expmode (s, strlen (s));
}

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
ignorephrases (const char *key)
/*
* Ignore a series of phrases that do not start with KEY.
* Stop when the next phrase starts with a token that is not an identifier,
* or is KEY.
*/
{
  for (;;)
    {
      nextlex ();
      if (nexttok != ID || strcmp (NextString, key) == 0)
        break;
      warnignore ();
      hshenter = false;
      for (;; nextlex ())
        {
          switch (nexttok)
            {
            case SEMI:
              hshenter = true;
              break;
            case ID:
            case NUM:
              ffree1 (NextString);
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

static int
getdelta (void)
/* Function: reads a delta block.
 * returns false if the current block does not start with a number.
 */
{
  register struct hshentry *Delta, *num;
  struct branchhead **LastBranch, *NewBranch;

  if (!(Delta = getdnum ()))
    return false;

  hshenter = false;             /*Don't enter dates into hashtable */
  Delta->date = getkeyval (Kdate, NUM, false);
  hshenter = true;              /*reset hshenter for revision numbers. */

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
  *LastBranch = 0;
  getsemi (K_branches);

  getkey (Knext);
  Delta->next = num = getdnum ();
  getsemi (Knext);
  Delta->lockedby = 0;
  Delta->log.string = 0;
  Delta->selector = true;
  Delta->ig = getphrases (Kdesc);
  TotalDeltas++;
  return (true);
}

void
gettree (void)
/* Function: Reads in the delta tree with getdelta(), then
 * updates the lockedby fields.
 */
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
getdesc (int prdesc)
/* Function: read in descriptive text
 * nexttok is not advanced afterwards.
 * If prdesc is set, the text is printed to stdout.
 */
{

  getkeystring (Kdesc);
  if (prdesc)
    printstring ();             /*echo string */
  else
    readstring ();              /*skip string */
}

static char const *
getkeyval (char const *keyword, enum tokens token, int optional)
/* reads a pair of the form
 * <keyword> <token> ;
 * where token is one of <id> or <num>. optional indicates whether
 * <token> is optional. A pointer to
 * the actual character string of <id> or <num> is returned.
 */
{
  register char const *val = 0;

  getkey (keyword);
  if (nexttok == token)
    {
      val = NextString;
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

void
unexpected_EOF (void)
{
  rcsfaterror ("unexpected EOF in diff output");
}

void
initdiffcmd (register struct diffcmd *dc)
/* Initialize *dc suitably for getdiffcmd(). */
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
getdiffcmd (RILE *finfile, int delimiter, FILE *foutfile, struct diffcmd *dc)
/* Get a editing command output by 'diff -n' from fin.
 * The input is delimited by SDELIM if delimiter is set, EOF otherwise.
 * Copy a clean version of the command to fout (if nonnull).
 * Yield 0 for 'd', 1 for 'a', and -1 for EOF.
 * Store the command's line number and length into dc->line1 and dc->nlines.
 * Keep dc->adprev and dc->dafter up to date.
 */
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
  cachegeteof_ (c,
                {
                  if (delimiter)
                    unexpected_EOF ();
                  return -1;
                })
  if (delimiter)
    {
      if (c == SDELIM)
        {
          cacheget_ (c)
          if (c == SDELIM)
            {
              buf[0] = c;
              buf[1] = 0;
              badDiffOutput (buf);
            }
          uncache (fin);
          nextc = c;
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
      cachegeteof_ (c, unexpected_EOF ();)
    }
  while (c != '\n');
  uncache (fin);
  if (delimiter)
    ++rcsline;
  *p = '\0';
  for (p = buf + 1; (c = *p++) == ' ';)
    continue;
  line1 = 0;
  while (isdigit (c))
    {
      if (LONG_MAX / 10 < line1 ||
          (t = line1 * 10, (line1 = t + (c - '0')) < t))
        diffLineNumberTooLarge (buf);
      c = *p++;
    }
  while (c == ' ')
    c = *p++;
  nlines = 0;
  while (isdigit (c))
    {
      if (LONG_MAX / 10 < nlines ||
          (t = nlines * 10, (nlines = t + (c - '0')) < t))
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

char const cmdid[] = "syntest";

int
main (int argc, char *argv[])
{

  if (argc < 2)
    {
      aputs ("No input file\n", stderr);
      exitmain (EXIT_FAILURE);
    }
  if (!(finptr = Iopen (argv[1], FOPEN_R, (struct stat *) 0)))
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
  exitmain (EXIT_SUCCESS);
}

void
exiterr (void)
{
  _exit (EXIT_FAILURE);
}

#endif
