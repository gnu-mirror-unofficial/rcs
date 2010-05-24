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
#include "b-fb.h"
#include "b-fro.h"

static void
getsemi (struct tinysym const *key)
/* Get a semicolon to finish off a phrase started by ‘key’.  */
{
  if (!getlex (SEMI))
    SYNTAX_ERROR ("missing ';' after '%s'", TINYS (key));
}

static struct hshentry *
getdnum (void)
/* Get a delta number.  */
{
  register struct hshentry *delta = getnum ();

  if (delta && countnumflds (delta->num) & 1)
    SYNTAX_ERROR ("%s isn't a delta number", delta->num);
  return delta;
}

static struct hshentry *
must_get_colon_delta_num (char const *role)
{
  struct hshentry *rv;

  if (!getlex (COLON))
    SYNTAX_ERROR ("missing ':' in %s", role);
  if (!(rv = getnum ()))
    SYNTAX_ERROR ("missing number in %s", role);
  return rv;
}

void
getadmin (void)
/* Read an <admin> and initialize the appropriate global variables.  */
{
  struct link fake, *tp;
  register char const *id;
  struct hshentry *delta;
  struct cbuf cb;

  REPO (ndelt) = 0;

  getkey (&TINY (head));
  REPO (tip) = getdnum ();
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
  fake.next = ADMIN (assocs);
  tp = &fake;
  while ((id = getid ()))
    {
      struct symdef *d;

      delta = must_get_colon_delta_num ("symbolic name definition");
      /* Add new pair to association list.  */
      d = FALLOC (struct symdef);
      d->meaningful = id;
      d->underlying = delta->num;
      tp = extend (tp, d, SINGLE);
    }
  ADMIN (assocs) = fake.next;
  getsemi (&TINY (symbols));

  getkey (&TINY (locks));
  fake.next = ADMIN (locks);
  tp = &fake;
  while ((id = getid ()))
    {
      struct rcslock *rl;

      delta = must_get_colon_delta_num ("lock");
      /* Add new pair to lock list.  */
      rl = FALLOC (struct rcslock);
      rl->login = id;
      rl->delta = delta;
      tp = extend (tp, rl, SINGLE);
    }
  ADMIN (locks) = fake.next;
  getsemi (&TINY (locks));

  if ((BE (strictly_locking) = getkeyopt (&TINY (strict))))
    getsemi (&TINY (strict));

  clear_buf (&REPO (log_lead));
  if (getkeyopt (&TINY (comment)))
    {
      if (NEXT (tok) == STRING)
        {
          REPO (log_lead) = savestring ();
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
            SYNTAX_ERROR ("unknown expand mode %.*s", (int) cb.size, cb.string);
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
        SYNTAX_ERROR ("missing %s", TINYS (keyword));
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
  struct wlink wfake, *wtp;

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
  wfake.next = Delta->branches;
  wtp = &wfake;
  while ((num = getdnum ()))
    wtp = wextend (wtp, num, SINGLE);
  Delta->branches = wfake.next;
  getsemi (&TINY (branches));

  getkey (&TINY (next));
  Delta->next = num = getdnum ();
  getsemi (&TINY (next));
  Delta->lockedby = NULL;
  Delta->pretty_log.string = NULL;
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
  for (struct link *ls = ADMIN (locks); ls; ls = ls->next)
    {
      struct rcslock const *rl = ls->entry;

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

/* rcssyn.c ends here */
