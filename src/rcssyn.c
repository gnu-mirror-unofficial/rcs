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

#include "base.h"
#include <string.h>
#include <ctype.h>
#include "b-complain.h"
#include "b-divvy.h"
#include "b-esds.h"
#include "b-fro.h"

#if COMPAT2
static TINY_DECL (suffix);
#endif

static void
getsemi (struct tinysym const *key)
/* Get a semicolon to finish off a phrase started by ‘key’.  */
{
  if (!getlex (SEMI))
    fatal_syntax ("missing ';' after '%s'", TINYS (key));
}

static struct hshentry *
getdnum (void)
/* Get a delta number.  */
{
  register struct hshentry *delta = getnum ();

  if (delta && countnumflds (delta->num) & 1)
    fatal_syntax ("%s isn't a delta number", delta->num);
  return delta;
}

static struct hshentry *
must_get_colon_delta_num (char const *role)
{
  struct hshentry *rv;

  if (!getlex (COLON))
    fatal_syntax ("missing ':' in %s", role);
  if (!(rv = getnum ()))
    fatal_syntax ("missing number in %s", role);
  return rv;
}

void
getadmin (void)
/* Read an <admin> and initialize the appropriate global variables.  */
{
  struct link fake, *tp;
  struct wlink wfake, *wtp;
  register char const *id;
  struct hshentry *delta;
  struct cbuf cb;

  REPO (ndelt) = 0;

  getkey (&TINY (head));
  ADMIN (head) = getdnum ();
  getsemi (&TINY (head));

  ADMIN (defbr) = NULL;
  if (getkeyopt (&TINY (branch)))
    {
      if ((delta = getnum ()))
        ADMIN (defbr) = delta->num;
      getsemi (&TINY (branch));
    }

#if COMPAT2
  /* Read suffix.  Only in release 2 format.  */
  if (getkeyopt (&TINY (suffix)))
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
      getsemi (&TINY (suffix));
    }
#endif  /* COMPAT2 */

  getkey (&TINY (access));
  fake.next = ADMIN (allowed);
  tp = &fake;
  while ((id = getid ()))
    tp = extend (tp, id, SINGLE);
  ADMIN (allowed) = fake.next;
  getsemi (&TINY (access));

  getkey (&TINY (symbols));
  wfake.next = ADMIN (assocs);
  wtp = &wfake;
  while ((id = getid ()))
    {
      struct symdef *d;

      delta = must_get_colon_delta_num ("symbolic name definition");
      /* Add new pair to association list.  */
      d = FALLOC (struct symdef);
      d->meaningful = id;
      d->underlying = delta->num;
      wtp = wextend (wtp, d, SINGLE);
    }
  ADMIN (assocs) = wfake.next;
  getsemi (&TINY (symbols));

  getkey (&TINY (locks));
  wfake.next = ADMIN (locks);
  wtp = &wfake;
  while ((id = getid ()))
    {
      struct rcslock *rl;

      delta = must_get_colon_delta_num ("lock");
      /* Add new pair to lock list.  */
      rl = FALLOC (struct rcslock);
      rl->login = id;
      rl->delta = delta;
      wtp = wextend (wtp, rl, SINGLE);
    }
  ADMIN (locks) = wfake.next;
  getsemi (&TINY (locks));

  if ((BE (strictly_locking) = getkeyopt (&TINY (strict))))
    getsemi (&TINY (strict));

  clear_buf (&ADMIN (log_lead));
  if (getkeyopt (&TINY (comment)))
    {
      if (NEXT (tok) == STRING)
        {
          ADMIN (log_lead) = savestring ();
          nextlex ();
        }
      getsemi (&TINY (comment));
    }

  BE (kws) = kwsub_kv;
  if (getkeyopt (&TINY (expand)))
    {
      if (NEXT (tok) == STRING)
        {
          cb = savestring ();
          if (0 > (BE (kws) = recognize_kwsub (&cb)))
            fatal_syntax ("unknown expand mode %.*s", (int) cb.size, cb.string);
          nextlex ();
        }
      getsemi (&TINY (expand));
    }
}

static char const *
getkeyval (struct tinysym const *keyword, enum tokens token, bool optional)
/* Read a pair of the form:
   <keyword> <token> ;
   where ‘token’ is one of ‘ID’ or ‘NUM’.  ‘optional’ indicates whether
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
        fatal_syntax ("missing %s", TINYS (keyword));
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
  Delta->date = getkeyval (&TINY (date), NUM, false);
  /* Reset BE (receptive_to_next_hash_key) for revision numbers.  */
  BE (receptive_to_next_hash_key) = true;

  Delta->author = getkeyval (&TINY (author), ID, false);

  Delta->state = getkeyval (&TINY (state), ID, true);

  getkey (&TINY (branches));
  LastBranch = &Delta->branches;
  while ((num = getdnum ()))
    {
      NewBranch = FALLOC (struct branchhead);
      NewBranch->hsh = num;
      *LastBranch = NewBranch;
      LastBranch = &NewBranch->nextbranch;
    }
  *LastBranch = NULL;
  getsemi (&TINY (branches));

  getkey (&TINY (next));
  Delta->next = num = getdnum ();
  getsemi (&TINY (next));
  Delta->lockedby = NULL;
  Delta->log.string = NULL;
  Delta->selector = true;
  /* CVS adds ‘commitid’; roll with it.  */
  Delta->commitid = NULL;
  if (getkeyopt (&TINY (commitid)))
    {
      /* TODO: Use ‘checkssym’ if/when it takes ‘char const *’.  */
      if (strchr (NEXT (str), '.'))
        PFATAL ("invalid symbol `%s'", NEXT (str));
      Delta->commitid = NEXT (str);
      nextlex ();
      getsemi (&TINY (commitid));
    }
  REPO (ndelt)++;
  return true;
}

void
gettree (void)
/* Read in the delta tree with ‘getdelta’,
   then update the ‘lockedby’ fields.  */
{
  while (getdelta ())
    continue;
  for (struct wlink *ls = ADMIN (locks); ls; ls = ls->next)
    {
      struct rcslock *rl = ls->entry;

      rl->delta->lockedby = rl->login;
    }
}

void
getdesc (bool prdesc)
/* Read in descriptive text.  ‘NEXT (tok)’ is not advanced afterwards.
   If ‘prdesc’ is set, then print text to stdout.  */
{
  getkeystring (&TINY (desc));
  if (prdesc)
    printstring ();                     /* echo string */
  else
    readstring ();                      /* skip string */
}

void
unexpected_EOF (void)
{
  RFATAL ("unexpected EOF in diff output");
}

void
initdiffcmd (register struct diffcmd *dc)
/* Initialize ‘*dc’ suitably for ‘getdiffcmd’.  */
{
  dc->adprev = 0;
  dc->dafter = 0;
}

static void
badDiffOutput (char const *buf)
{
  RFATAL ("bad diff output line: %s", buf);
}

static void
diffLineNumberTooLarge (char const *buf)
{
  RFATAL ("diff line number too large: %s", buf);
}

int
getdiffcmd (struct fro *finfile, bool delimiter, FILE *foutfile,
            struct diffcmd *dc)
/* Get an editing command output by "diff -n" from ‘finfile’.  The input
   is delimited by ‘SDELIM’ if ‘delimiter’ is set, EOF otherwise.  Copy
   a clean version of the command to ‘foutfile’ (if non-NULL).  Return 0
   for 'd', 1 for 'a', and -1 for EOF.  Store the command's line number
   and length into ‘dc->line1’ and ‘dc->nlines’.  Keep ‘dc->adprev’ and
   ‘dc->dafter’ up to date.  */
{
  int c;
  register FILE *fout;
  register char *p;
  register struct fro *fin;
  long line1, nlines, t;
  char buf[BUFSIZ];

  fin = finfile;
  fout = foutfile;
  GETCHAR_OR (c, fin,
              {
                if (delimiter)
                  unexpected_EOF ();
                return -1;
              });
  if (delimiter)
    {
      if (c == SDELIM)
        {
          GETCHAR (c, fin);
          if (c == SDELIM)
            {
              buf[0] = c;
              buf[1] = 0;
              badDiffOutput (buf);
            }
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
          RFATAL ("diff output command line too long");
        }
      *p++ = c;
      GETCHAR_OR (c, fin, unexpected_EOF ());
    }
  while (c != '\n');
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
          RFATAL ("backward insertion in diff output: %s", buf);
        }
      dc->adprev = line1 + 1;
      break;
    case 'd':
      if (line1 < dc->adprev || line1 < dc->dafter)
        {
          RFATAL ("backward deletion in diff output: %s", buf);
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

/* rcssyn.c ends here */
