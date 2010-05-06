/* Change RCS file attributes.

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
#include <stdlib.h>
#include "rcs.help"
#include "b-complain.h"
#include "b-divvy.h"
#include "b-esds.h"
#include "b-fb.h"
#include "b-feph.h"
#include "b-fro.h"

struct top *top;

struct u_log
{
  char const *revno;
  struct cbuf message;
};

struct u_state
{
  char const *revno;
  char const *status;
};

enum changeaccess
{ append, erase };
struct chaccess
{
  char const *login;
  enum changeaccess command;
};

struct delrevpair
{
  char const *strt;
  char const *end;
  int code;
};

static struct cbuf numrev;
static char const *headstate;
static bool chgheadstate, lockhead, unlockcaller, suppress_mail;
static int exitstatus;
static struct link *newlocklst, *rmvlocklst;
static struct link messagelst;
static struct link statelst;
static struct link assoclst;
static struct link chaccess;
static struct delrevpair delrev;
static struct hshentry *cuthead, *cuttail, *delstrt;
static struct hshentries *gendeltas;

static void
cleanup (void)
{
  if (LEX (erroneousp))
    exitstatus = EXIT_FAILURE;
  fro_zclose (&FLOW (from));
  Ozclose (&FLOW (res));
  ORCSclose ();
  dirtempunlink ();
}

static exiting void
exiterr (void)
{
  ORCSerror ();
  dirtempunlink ();
  tempunlink ();
  _Exit (EXIT_FAILURE);
}

static void
getassoclst (struct link **tp, char *sp)
/* Associate a symbolic name to a revision or branch,
   and store in ‘assoclst’.  */
{
  char option = *sp++;
  struct u_symdef *ud;
  char const *name;
  int c = *sp;

#define SKIPWS()  while (c == ' ' || c == '\t' || c == '\n') c = *++sp

  SKIPWS ();
  name = sp;
  /* Check for invalid symbolic name.  */
  sp = checksym (sp, ':');
  c = *sp;
  *sp = '\0';
  SKIPWS ();

  if (c != ':' && c != '\0')
    {
      PERR ("invalid string `%s' after option `-%c'", sp, option);
      return;
    }

  ud = ZLLOC (1, struct u_symdef);
  ud->u.meaningful = name;
  ud->override = ('N' == option);
  if (c == '\0')
    /* Delete symbol.  */
    ud->u.underlying = NULL;
  else
    /* Add association.  */
    {
      c = *++sp;
      SKIPWS ();
      ud->u.underlying = sp;
    }
  *tp = extend (*tp, ud, SHARED);

#undef SKIPWS
}

static void
getchaccess (struct link **tp, char const *login, enum changeaccess command)
{
  register struct chaccess *ch;

  ch = ZLLOC (1, struct chaccess);
  ch->login = login;
  ch->command = command;
  *tp = extend (*tp, ch, SHARED);
}

static void
getaccessor (struct link **tp, char *opt, enum changeaccess command)
/* Get the accessor list of options ‘-e’ and ‘-a’; store in ‘chaccess’.  */
{
  register int c;
  register char *sp;

  sp = opt;
  while ((c = *++sp) == ' ' || c == '\n' || c == '\t' || c == ',')
    continue;
  if (c == '\0')
    {
      if (command == erase && sp - opt == 1)
        {
          getchaccess (tp, NULL, command);
          return;
        }
      PERR ("missing login name after option -a or -e");
      return;
    }

  while (c != '\0')
    {
      getchaccess (tp, sp, command);
      sp = checkid (sp, ',');
      c = *sp;
      *sp = '\0';
      while (c == ' ' || c == '\n' || c == '\t' || c == ',')
        c = (*++sp);
    }
}

static void
getmessage (struct link **tp, char *option)
{
  struct u_log *um;
  struct cbuf cb;
  char *m;

  if (!(m = strchr (option, ':')))
    {
      PERR ("-m option lacks revision number");
      return;
    }
  *m++ = '\0';
  cb = cleanlogmsg (m, strlen (m));
  if (!cb.size)
    {
      PERR ("-m option lacks log message");
      return;
    }
  um = ZLLOC (1, struct u_log);
  um->revno = option;
  um->message = cb;
  *tp = extend (*tp, um, SHARED);
}

static void
getstates (struct link **tp, char *sp)
/* Get one state attribute and the corresponding rev; store in ‘statelst’.  */
{
  char const *temp;
  struct u_state *us;
  register int c;

  while ((c = *++sp) == ' ' || c == '\t' || c == '\n')
    continue;
  temp = sp;
  /* Check for invalid state attribute.  */
  sp = checkid (sp, ':');
  c = *sp;
  *sp = '\0';
  while (c == ' ' || c == '\t' || c == '\n')
    c = *++sp;

  if (c == '\0')
    {
      /* Change state of default branch or ‘ADMIN (head)’.  */
      chgheadstate = true;
      headstate = temp;
      return;
    }
  else if (c != ':')
    {
      PERR ("missing ':' after state in option -s");
      return;
    }

  while ((c = *++sp) == ' ' || c == '\t' || c == '\n')
    continue;
  us = ZLLOC (1, struct u_state);
  us->status = temp;
  us->revno = sp;
  *tp = extend (*tp, us, SHARED);
}

static void
putdelrev (char const *b, char const *e, bool sawsep)
{
  if (delrev.strt || delrev.end)
    {
      PWARN ("ignoring spurious `-o' range `%s:%s'",
             b ? b : "(unspecified)",
             e ? e : "(unspecified)");
      return;
    }

  if (!sawsep)
    /* -o rev or branch */
    {
      delrev.strt = b;
      delrev.code = 0;
    }
  else if (!b || !b[0])
    /* -o:rev */
    {
      delrev.strt = e;                  /* FIXME: weird */
      delrev.code = 1;
    }
  else if (!e[0])
    /* -orev: */
    {
      delrev.strt = b;
      delrev.code = 2;
    }
  else
    /* -orev1:rev2 */
    {
      delrev.strt = b;
      delrev.end = e;
      delrev.code = 3;
    }
}

static void
scanlogtext (struct editstuff *es, struct hshentry *delta, bool edit)
/* Scan delta text nodes up to and including the one given by ‘delta’,
   or up to last one present, if ‘!delta’.  For the one given by
   ‘delta’ (if ‘delta’), the log message is saved into ‘delta->log’ if
   ‘delta == cuttail’; the text is edited if ‘edit’ is set, else
   copied.  Assume the initial lexeme must be read in first.  Do not
   advance ‘NEXT (tok)’ after it is finished, except if ‘!delta’.  */
{
  struct hshentry const *nextdelta;
  struct cbuf cb;

  for (;;)
    {
      FLOW (to) = NULL;
      if (eoflex ())
        {
          if (delta)
            RFATAL ("can't find delta for revision %s", delta->num);
          return;               /* no more delta text nodes */
        }
      nextdelta = must_get_delta_num ();
      if (nextdelta->selector)
        {
          FLOW (to) = FLOW (rewr);
          aprintf (FLOW (rewr), DELNUMFORM, nextdelta->num, TINYKS (log));
        }
      getkeystring (&TINY (log));
      if (nextdelta == cuttail)
        {
          cb = savestring ();
          if (!delta->log.string)
            delta->log = cleanlogmsg (cb.string, cb.size);
        }
      else
        {
          if (nextdelta->log.string && nextdelta->selector)
            {
              FLOW (to) = NULL;
              readstring ();
              FLOW (to) = FLOW (rewr);
              putstring (FLOW (to), false, nextdelta->log, true);
              afputc (NEXT (c), FLOW (to));
            }
          else
            readstring ();
        }
      nextlex ();
      getkeystring (&TINY (text));

      if (delta == nextdelta)
        break;
      readstring ();            /* skip over it */

    }
  /* got the one we're looking for */
  if (edit)
    editstring (es, NULL);
  else
    enterstring (es);
}

static struct link *
rmnewlocklst (char const *which)
/* Remove lock to revision ‘which’ from ‘newlocklst’.  */
{
  struct link *pt, **pre;

  pre = &newlocklst;
  while ((pt = *pre))
    if (STR_DIFF (pt->entry, which))
      pre = &pt->next;
    else
      *pre = pt->next;
  return *pre;
}

static bool
doaccess (void)
{
  register bool changed = false;
  struct link *ls, box, *tp;

  for (ls = chaccess.next; ls; ls = ls->next)
    {
      struct chaccess const *ch = ls->entry;
      char const *login = ch->login;

      switch (ch->command)
        {
        case erase:
          if (!login)
            {
              if (ADMIN (allowed))
                {
                  ADMIN (allowed) = NULL;
                  changed = true;
                }
            }
          else
            for (box.next = ADMIN (allowed), tp = &box;
                 tp->next; tp = tp->next)
              if (STR_SAME (login, tp->next->entry))
                {
                  tp->next = tp->next->next;
                  changed = true;
                  ADMIN (allowed) = box.next;
                  break;
                }
          break;
        case append:
          for (box.next = ADMIN (allowed), tp = &box;
               tp->next; tp = tp->next)
            if (STR_SAME (login, tp->next->entry))
              /* Do nothing; already present.  */
              break;
          if (!tp->next)
            {
              extend (tp, login, SINGLE);
              changed = true;
              ADMIN (allowed) = box.next;
            }
          break;
        }
    }
  return changed;
}

static bool
sendmail (char const *Delta, char const *who)
/* Mail to ‘who’, informing him that his lock on ‘Delta’ was broken by
   caller.  Ask first whether to go ahead.  Return false on error or if
   user decides not to break the lock.  */
{
#ifdef SENDMAIL
  char const *messagefile;
  int old1, old2, c, status;
  FILE *mailmess;
#endif

  complain ("Revision %s is already locked by %s.\n", Delta, who);
  if (suppress_mail)
    return true;
  if (!yesorno (false, "Do you want to break the lock? [ny](n): "))
    return false;

  /* Go ahead with breaking.  */
#ifdef SENDMAIL
  messagefile = maketemp (0);
  if (!(mailmess = fopen_safer (messagefile, "w+")))
    {
      fatal_sys (messagefile);
    }

  aprintf (mailmess,
           "Subject: Broken lock on %s\n\nYour lock on revision %s of file %s\nhas been broken by %s for the following reason:\n",
           basefilename (REPO (filename)), Delta, getfullRCSname (), getcaller ());
  complain ("%s\n%s\n>> ",
            "State the reason for breaking the lock:",
            "(terminate with single '.' or end of file)")

  old1 = '\n';
  old2 = ' ';
  for (;;)
    {
      c = getcstdin ();
      if (feof (stdin))
        {
          aprintf (mailmess, "%c\n", old1);
          break;
        }
      else if (c == '\n' && old1 == '.' && old2 == '\n')
        break;
      else
        {
          afputc (old1, mailmess);
          old2 = old1;
          old1 = c;
          if (c == '\n')
            complain (">> ");
        }
    }
  Orewind (mailmess);
  aflush (mailmess);
  status = run (fileno (mailmess), NULL, SENDMAIL, who, NULL);
  Ozclose (&mailmess);
  if (status == 0)
    return true;
  PWARN ("Mail failed.");
#endif  /* defined SENDMAIL */
  PWARN ("Mail notification of broken locks is not available.");
  PWARN ("Please tell `%s' why you broke the lock.", who);
  return true;
}

static bool
breaklock (struct hshentry const *delta)
/* Find the lock held by caller on ‘delta’, and remove it.
   Send mail if a lock different from the caller's is broken.
   Print an error message if there is no such lock or error.  */
{
  struct wlink wfake, *wtp;
  char const *num;

  num = delta->num;
  for (wfake.next = ADMIN (locks), wtp = &wfake; wtp->next; wtp = wtp->next)
    {
      struct rcslock *rl = wtp->next->entry;
      struct hshentry *d = rl->delta;

      if (STR_SAME (num, d->num))
        {
          char const *before = rl->login;

          if (STR_DIFF (getcaller (), before)
              && !sendmail (num, before))
            {
              RERR ("revision %s still locked by %s", num, before);
              return false;
            }
          diagnose ("%s unlocked", num);
          wtp->next = wtp->next->next;
          ADMIN (locks) = wfake.next;
          d->lockedby = NULL;
          return true;
        }
    }
  RERR ("no lock set on revision %s", num);
  return false;
}

static struct hshentry *
searchcutpt (char const *object, int length, struct hshentries *store)
/* Search store and return entry with number being ‘object’.
   ‘cuttail’ is 0, if the entry is ‘ADMIN (head)’; otherwise, it
   is the entry point to the one with number being ‘object’.  */
{
  cuthead = NULL;
  while (compartial (store->first->num, object, length))
    {
      cuthead = store->first;
      store = store->rest;
    }
  return store->first;
}

static bool
branchpoint (struct hshentry *strt, struct hshentry *tail)
/* Check whether the deltas between ‘strt’ and ‘tail’ are locked or
   branch point, return 1 if any is locked or branch point; otherwise,
   return 0 and mark deleted.  */
{
  struct hshentry *pt;

  for (pt = strt; pt != tail; pt = pt->next)
    {
      if (pt->branches)
        {
          /* A branch point.  */
          RERR ("can't remove branch point %s", pt->num);
          return true;
        }
      for (struct wlink *ls = ADMIN (locks); ls; ls = ls->next)
        {
          struct rcslock const *rl = ls->entry;

          if (pt == rl->delta)
            {
              RERR ("can't remove locked revision %s", pt->num);
              return true;
            }
        }
      pt->selector = false;
      diagnose ("deleting revision %s", pt->num);
    }
  return false;
}

static bool
removerevs (void)
/* Get the revision range to be removed, and place the first revision
   removed in ‘delstrt’, the revision before ‘delstrt’ in ‘cuthead’
   (0, if ‘delstrt’ is head), and the revision after the last removed
   revision in ‘cuttail’ (0 if the last is a leaf).  */
{
  struct hshentry *target, *target2, *temp;
  int length;
  int cmp;

  if (!fully_numeric_no_k (&numrev, delrev.strt))
    return false;
  target = gr_revno (numrev.string, &gendeltas);
  if (!target)
    return false;
  cmp = cmpnum (target->num, numrev.string);
  length = countnumflds (numrev.string);

  if (delrev.code == 0)
    {                           /* -o rev or -o branch */
      if (length & 1)
        temp = searchcutpt (target->num, length + 1, gendeltas);
      else if (cmp)
        {
          RERR ("Revision %s doesn't exist.", numrev.string);
          return false;
        }
      else
        temp = searchcutpt (numrev.string, length, gendeltas);
      cuttail = target->next;
      if (branchpoint (temp, cuttail))
        {
          cuttail = NULL;
          return false;
        }
      delstrt = temp;           /* first revision to be removed */
      return true;
    }

  if (length & 1)
    {                           /* invalid branch after -o */
      RERR ("invalid branch range %s after -o", numrev.string);
      return false;
    }

  if (delrev.code == 1)
    {                           /* -o -rev */
      if (length > 2)
        {
          temp = searchcutpt (target->num, length - 1, gendeltas);
          cuttail = target->next;
        }
      else
        {
          temp = searchcutpt (target->num, length, gendeltas);
          cuttail = target;
          while (cuttail && !cmpnumfld (target->num, cuttail->num, 1))
            cuttail = cuttail->next;
        }
      if (branchpoint (temp, cuttail))
        {
          cuttail = NULL;
          return false;
        }
      delstrt = temp;
      return true;
    }

  if (delrev.code == 2)
    {                           /* -o rev- */
      if (length == 2)
        {
          temp = searchcutpt (target->num, 1, gendeltas);
          if (cmp)
            cuttail = target;
          else
            cuttail = target->next;
        }
      else
        {
          if (cmp)
            {
              cuthead = target;
              if (!(temp = target->next))
                return false;
            }
          else
            temp = searchcutpt (target->num, length, gendeltas);
          gr_revno (BRANCHNO (temp->num), &gendeltas);
        }
      if (branchpoint (temp, cuttail))
        {
          cuttail = NULL;
          return false;
        }
      delstrt = temp;
      return true;
    }

  /* -o rev1-rev2 */
  if (!fully_numeric_no_k (&numrev, delrev.end))
    return false;
  if (length != countnumflds (numrev.string)
      || (length > 2 && compartial (numrev.string, target->num, length - 1)))
    {
      RERR ("invalid revision range %s-%s", target->num, numrev.string);
      return false;
    }

  target2 = gr_revno (numrev.string, &gendeltas);
  if (!target2)
    return false;

  if (length > 2)
    {                           /* delete revisions on branches */
      if (cmpnum (target->num, target2->num) > 0)
        {
          cmp = cmpnum (target2->num, numrev.string);
          temp = target;
          target = target2;
          target2 = temp;
        }
      if (cmp)
        {
          if (!cmpnum (target->num, target2->num))
            {
              RERR ("Revisions %s-%s don't exist.", delrev.strt, delrev.end);
              return false;
            }
          cuthead = target;
          temp = target->next;
        }
      else
        temp = searchcutpt (target->num, length, gendeltas);
      cuttail = target2->next;
    }
  else
    {                           /* delete revisions on trunk */
      if (cmpnum (target->num, target2->num) < 0)
        {
          temp = target;
          target = target2;
          target2 = temp;
        }
      else
        cmp = cmpnum (target2->num, numrev.string);
      if (cmp)
        {
          if (!cmpnum (target->num, target2->num))
            {
              RERR ("Revisions %s-%s don't exist.", delrev.strt, delrev.end);
              return false;
            }
          cuttail = target2;
        }
      else
        cuttail = target2->next;
      temp = searchcutpt (target->num, length, gendeltas);
    }
  if (branchpoint (temp, cuttail))
    {
      cuttail = NULL;
      return false;
    }
  delstrt = temp;
  return true;
}

static bool
doassoc (void)
/* Add or delete (if !underlying) association that is stored in ‘assoclst’.  */
{
  char const *p;
  bool changed = false;

  for (struct link *cur = assoclst.next; cur; cur = cur->next)
    {
      struct u_symdef const *u = cur->entry;
      char const *ssymbol = u->u.meaningful;
      char const *under = u->u.underlying;

      if (!under)
        /* Delete symbol.  */
        {
          struct wlink box, *tp;
          struct symdef *d = NULL;

          for (box.next = ADMIN (assocs), tp = &box; tp->next; tp = tp->next)
            {
              d = tp->next->entry;
              if (STR_SAME (ssymbol, d->meaningful))
                {
                  tp->next = tp->next->next;
                  changed = true;
                  break;
                }
            }
          ADMIN (assocs) = box.next;
          if (!d)
            RWARN ("can't delete nonexisting symbol %s", ssymbol);
        }
      else
        /* Add new association.  */
        {
          if (under[0])
            p = fully_numeric_no_k (&numrev, under)
              ? numrev.string
              : NULL;
          else if (!(p = tiprev ()))
            RERR ("no latest revision to associate with symbol %s", ssymbol);
          if (p)
            changed |= addsymbol (p, ssymbol, u->override);
        }
    }
  return changed;
}

static bool
setlock (char const *rev)
/* Given a revision or branch number, find the corresponding
   delta and locks it for caller.  */
{
  struct hshentry *target;
  int r;

  if (fully_numeric_no_k (&numrev, rev))
    {
      target = gr_revno (numrev.string, &gendeltas);
      if (target)
        {
          if (!(countnumflds (numrev.string) & 1)
              && cmpnum (target->num, numrev.string))
            RERR ("can't lock nonexisting revision %s", numrev.string);
          else
            {
              if ((r = addlock (target, false)) < 0 && breaklock (target))
                r = addlock (target, true);
              if (0 <= r)
                {
                  if (r)
                    diagnose ("%s locked", target->num);
                  return r;
                }
            }
        }
    }
  return false;
}

static bool
dolocks (void)
/* Remove lock for caller or first lock if ‘unlockcaller’ is set;
   remove locks which are stored in ‘rmvlocklst’,
   add new locks which are stored in ‘newlocklst’,
   add lock for ‘ADMIN (defbr)’ or ‘ADMIN (head)’ if ‘lockhead’ is set.  */
{
  struct link const *lockpt;
  struct hshentry *target;
  bool changed = false;

  if (unlockcaller)
    {
      /* Find lock for caller.  */
      if (ADMIN (head))
        {
          if (ADMIN (locks))
            {
              switch (findlock (true, &target))
                {
                case 0:
                  /* Remove most recent lock.  */
                  {
                    struct rcslock *rl = ADMIN (locks)->entry;

                    changed |= breaklock (rl->delta);
                  }
                  break;
                case 1:
                  diagnose ("%s unlocked", target->num);
                  changed = true;
                  break;
                }
            }
          else
            {
              RWARN ("No locks are set.");
            }
        }
      else
        {
          RWARN ("can't unlock an empty tree");
        }
    }

  /* Remove locks which are stored in rmvlocklst.  */
  for (lockpt = rmvlocklst; lockpt; lockpt = lockpt->next)
    if (fully_numeric_no_k (&numrev, lockpt->entry))
      {
        target = gr_revno (numrev.string, &gendeltas);
        if (target)
          {
            if (!(countnumflds (numrev.string) & 1)
                && cmpnum (target->num, numrev.string))
              RERR ("can't unlock nonexisting revision %s",
                    (char const *) lockpt->entry);
            else
              changed |= breaklock (target);
          }
        /* ‘breaklock’ does its own ‘diagnose’.  */
      }

  /* Add new locks which stored in newlocklst.  */
  for (lockpt = newlocklst; lockpt; lockpt = lockpt->next)
    changed |= setlock (lockpt->entry);

  if (lockhead)
    {
      /* Lock default branch or head.  */
      if (ADMIN (defbr))
        changed |= setlock (ADMIN (defbr));
      else if (ADMIN (head))
        changed |= setlock (ADMIN (head)->num);
      else
        RWARN ("can't lock an empty tree");
    }
  return changed;
}

static bool
domessages (void)
{
  struct hshentry *target;
  bool changed = false;

  for (struct link *ls = messagelst.next; ls; ls = ls->next)
    {
      struct u_log const *um = ls->entry;

      if (fully_numeric_no_k (&numrev, um->revno)
          && (target = gr_revno (numrev.string, &gendeltas)))
        {
          /* We can't check the old log -- it's much later in the file.
             We pessimistically assume that it changed.  */
          target->log = um->message;
          changed = true;
        }
    }
  return changed;
}

static bool
rcs_setstate (char const *rev, char const *status)
/* Given a revision or branch number, find the corresponding delta
   and sets its state to ‘status’.  */
{
  struct hshentry *target;

  if (fully_numeric_no_k (&numrev, rev))
    {
      target = gr_revno (numrev.string, &gendeltas);
      if (target)
        {
          if (!(countnumflds (numrev.string) & 1)
              && cmpnum (target->num, numrev.string))
            RERR ("can't set state of nonexisting revision %s", numrev.string);
          else if (STR_DIFF (target->state, status))
            {
              target->state = status;
              return true;
            }
        }
    }
  return false;
}

static bool
buildeltatext (struct editstuff *es, struct hshentries const *deltas)
/* Put the delta text on ‘FLOW (rewr)’ and make necessary
   change to delta text.  */
{
  FILE *fcut;                       /* temporary file to rebuild delta tree */
  char const *cutname;

  fcut = NULL;
  cuttail->selector = false;
  scanlogtext (es, deltas->first, false);
  if (cuthead)
    {
      cutname = maketemp (3);
      if (!(fcut = fopen_safer (cutname, FOPEN_WPLUS_WORK)))
        {
          fatal_sys (cutname);
        }

      while (deltas->first != cuthead)
        {
          deltas = deltas->rest;
          scanlogtext (es, deltas->first, true);
        }

      snapshotedit (es, fcut);
      Orewind (fcut);
      aflush (fcut);
    }

  while (deltas->first != cuttail)
    scanlogtext (es, (deltas = deltas->rest)->first, true);
  finishedit (es, NULL, NULL, true);
  Ozclose (&FLOW (res));

  if (fcut)
    {
      char const *diffname = maketemp (0);
      char const *diffv[6 + !!OPEN_O_BINARY];
      char const **diffp = diffv;

      *++diffp = prog_diff;
      *++diffp = diff_flags;
#if OPEN_O_BINARY
      if (BE (kws) == kwsub_b)
        *++diffp == "--binary";
#endif
      *++diffp = "-";
      *++diffp = FLOW (result);
      *++diffp = '\0';
      if (DIFF_TROUBLE == runv (fileno (fcut), diffname, diffv))
        RFATAL ("diff failed");
      Ozclose (&fcut);
      return putdtext (cuttail, diffname, FLOW (rewr), true);
    }
  else
    return putdtext (cuttail, FLOW (result), FLOW (rewr), false);
}

static void
buildtree (void)
/* Actually remove revisions whose selector field
   is false, and rebuild the linkage of deltas.
   Ask for reconfirmation if deleting last revision.  */
{
  struct hshentry *Delta;

  if (cuthead)
    if (cuthead->next == delstrt)
      cuthead->next = cuttail;
    else
      {
        struct wlink *pt = cuthead->branches, *pre = pt;

        while (pt && pt->entry != delstrt)
          {
            pre = pt;
            pt = pt->next;
          }
        if (cuttail)
          pt->entry = cuttail;
        else if (pt == pre)
          cuthead->branches = pt->next;
        else
          pre->next = pt->next;
      }
  else
    {
      if (!cuttail && !BE (quiet))
        {
          if (!yesorno
              (false,
               "Do you really want to delete all revisions? [ny](n): "))
            {
              RERR ("No revision deleted");
              Delta = delstrt;
              while (Delta)
                {
                  Delta->selector = true;
                  Delta = Delta->next;
                }
              return;
            }
        }
      ADMIN (head) = cuttail;
    }
  return;
}

/*:help
[options] file ...
Options:
  -i              Create and initialize a new RCS file.
  -L              Set strict locking.
  -U              Set non-strict locking.
  -M              Don't send mail when breaking someone else's lock.
  -T              Preserve the modification time on the RCS file
                  unless a revision is removed.
  -I              Interactive.
  -q              Quiet mode.
  -aLOGINS        Append LOGINS (comma-separated) to access-list.
  -e[LOGINS]      Erase LOGINS (all if unspecified) from access-list.
  -AFILENAME      Append access-list of FILENAME to current access-list.
  -b[REV]         Set default branch to that of REV or
                  highest branch on trunk if REV is omitted.
  -l[REV]         Lock revision REV.
  -u[REV]         Unlock revision REV.
  -cSTRING        Set comment leader to STRING; don't use: obsolete.
  -kSUBST         Set default keyword substitution to SUBST (see co(1)).
  -mREV:MSG       Replace REV's log message with MSG.
  -nNAME[:[REV]]  If :REV is omitted, delete symbolic NAME.
                  Otherwise, associate NAME with REV; NAME must be new.
  -NNAME[:[REV]]  Like -n, but overwrite any previous assignment.
  -oRANGE         Outdate revisions in RANGE:
                    REV       -- single revision
                    BR        -- latest revision on branch BR
                    REV1:REV2 -- REV1 to REV2 on same branch
                    :REV      -- beginning of branch to REV
                    REV:      -- REV to end of branch
  -sSTATE[:REV]   Set state of REV to STATE.
  -t-TEXT         Replace description in RCS file with TEXT.
  -tFILENAME      Replace description in RCS file with contents of FILENAME.
  -V              Like --version.
  -VN             Emulate RCS version N.
  -xSUFF          Specify SUFF as a slash-separated list of suffixes
                  used to identify RCS file names.
  -zZONE          No effect; included for compatibility with other commands.

REV defaults to the latest revision on the default branch.
*/

const struct program program =
  {
    .name = "rcs",
    .help = help,
    .exiterr = exiterr
  };

int
main (int argc, char **argv)
{
  char *a, **newargv, *textfile;
  char const *branchsym, *commsyml;
  bool branchflag, initflag, textflag;
  int changed, expmode;
  bool strictlock, strict_selected, Ttimeflag;
  bool keepRCStime;
  size_t commsymlen;
  struct cbuf branchnum;
  struct link fakelock, *tplock;
  struct link fakerm, *tprm;
  struct link *tp_assoc, *tp_chacc, *tp_log, *tp_state;

  CHECK_HV ();
  gnurcs_init ();

  nosetid ();

  tp_assoc = &assoclst;
  tp_chacc = &chaccess;
  tp_log = &messagelst;
  tp_state = &statelst;
  branchsym = commsyml = textfile = NULL;
  branchflag = strictlock = false;
  commsymlen = 0;
  fakelock.next = newlocklst;
  tplock = &fakelock;
  fakerm.next = rmvlocklst;
  tprm = &fakerm;
  expmode = -1;
  BE (pe) = X_DEFAULT;
  initflag = textflag = false;
  strict_selected = false;
  Ttimeflag = false;

  /* Preprocess command options.  */
  if (1 < argc && argv[1][0] != '-')
    PWARN ("No options were given; this usage is obsolescent.");

  argc = getRCSINIT (argc, argv, &newargv);
  argv = newargv;
  while (a = *++argv, 0 < --argc && *a++ == '-')
    {
      switch (*a++)
        {

        case 'i':
          /* Initial version.  */
          initflag = true;
          break;

        case 'b':
          /* Change default branch.  */
          if (branchflag)
            redefined ('b');
          branchflag = true;
          branchsym = a;
          break;

        case 'c':
          /* Change comment symbol.  */
          if (commsyml)
            redefined ('c');
          commsyml = a;
          commsymlen = strlen (a);
          break;

        case 'a':
          /* Add new accessor.  */
          getaccessor (&tp_chacc, *argv + 1, append);
          break;

        case 'A':
          /* Append access list according to accessfile.  */
          if (!*a)
            {
              PERR ("missing filename after -A");
              break;
            }
          *argv = a;
          if (0 < pairnames (1, argv, rcsreadopen, true, false))
            {
              while (ADMIN (allowed))
                {
                  getchaccess (&tp_chacc, str_save (ADMIN (allowed)->entry),
                               append);
                  ADMIN (allowed) = ADMIN (allowed)->next;
                }
              fro_zclose (&FLOW (from));
            }
          break;

        case 'e':
          /* Remove accessors.  */
          getaccessor (&tp_chacc, *argv + 1, erase);
          break;

        case 'l':
          /* Lock a revision if it is unlocked.  */
          if (!*a)
            {
              /* Lock head or default branch.  */
              lockhead = true;
              break;
            }
          tplock = extend (tplock, a, SHARED);
          break;

        case 'u':
          /* Release lock of a locked revision.  */
          if (!*a)
            {
              unlockcaller = true;
              break;
            }
          tprm = extend (tprm, a, SHARED);
          newlocklst = fakelock.next;
          tplock = rmnewlocklst (a);
          break;

        case 'L':
          /* Set strict locking.  */
          if (strict_selected)
            {
              if (!strictlock)
                PWARN ("-U overridden by -L");
            }
          strictlock = true;
          strict_selected = true;
          break;

        case 'U':
          /* Release strict locking.  */
          if (strict_selected)
            {
              if (strictlock)
                PWARN ("-L overridden by -U");
            }
          strict_selected = true;
          break;

        case 'n':
          /* Add new association: error, if name exists.  */
        case 'N':
          /* Add or change association.  */
          if (!*a)
            {
              PERR ("missing symbolic name after -%c", (*argv)[1]);
              break;
            }
          getassoclst (&tp_assoc, (*argv) + 1);
          break;

        case 'm':
          /* Change log message.  */
          getmessage (&tp_log, a);
          break;

        case 'M':
          /* Do not send mail.  */
          suppress_mail = true;
          break;

        case 'o':
          /* Delete revisions.  */
          if (delrev.strt)
            redefined ('o');
          if (!*a)
            {
              PERR ("missing revision range after -o");
              break;
            }
          parse_revpairs ('o', (*argv) + 2, putdelrev);
          break;

        case 's':
          /* Change state attribute of a revision.  */
          if (!*a)
            {
              PERR ("state missing after -s");
              break;
            }
          getstates (&tp_state, (*argv) + 1);
          break;

        case 't':
          /* Change descriptive text.  */
          textflag = true;
          if (*a)
            {
              if (textfile)
                redefined ('t');
              textfile = a;
            }
          break;

        case 'T':
          /* Do not update last-mod time for minor changes.  */
          if (*a)
            goto unknown;
          Ttimeflag = true;
          break;

        case 'I':
          BE (interactive) = true;
          break;

        case 'q':
          BE (quiet) = true;
          break;

        case 'x':
          BE (pe) = a;
          break;

        case 'V':
          setRCSversion (*argv);
          break;

        case 'z':
          zone_set (a);
          break;

        case 'k':
          /* Set keyword expand mode.  */
          if (0 <= expmode)
            redefined ('k');
          if (0 <= (expmode = str2expmode (a)))
            break;
          /* fall into */
        default:
        unknown:
          bad_option (*argv);
        };
    }
  newlocklst = fakelock.next;
  rmvlocklst = fakerm.next;
  /* (End processing of options.)  */

  /* Now handle all filenames.  */
  if (LEX (erroneousp))
    cleanup ();
  else if (argc < 1)
    PFATAL ("no input file");
  else
    for (; 0 < argc; cleanup (), ++argv, --argc)
      {
        struct cbuf newdesc =
          {
            .string = NULL,
            .size = 0
          };

        ffree ();

        if (initflag)
          {
            switch (pairnames (argc, argv, rcswriteopen, false, false))
              {
              case -1:
                break;          /* not exist; ok */
              case 0:
                continue;       /* error */
              case 1:
                RERR ("already exists");
                continue;
              }
          }
        else
          {
            switch (pairnames (argc, argv, rcswriteopen, true, false))
              {
              case -1:
                continue;       /* not exist */
              case 0:
                continue;       /* errors */
              case 1:
                break;          /* file exists; ok */
              }
          }

        /* ‘REPO (filename)’ contains the name of the RCS file, and
           ‘MANI (filename)’ contains the name of the working file.
           If ‘!initflag’, ‘FLOW (from)’ contains the file descriptor
           for the RCS file.  The admin node is initialized.  */

        diagnose ("RCS file: %s", REPO (filename));

        changed = initflag | textflag;
        keepRCStime = Ttimeflag;
        if (!initflag)
          {
            if (!checkaccesslist ())
              continue;
            /* Read the delta tree.  */
            gettree ();
          }

        /* Update admin. node.  */
        if (strict_selected)
          {
            changed |= BE (strictly_locking) ^ strictlock;
            BE (strictly_locking) = strictlock;
          }
        if (commsyml &&
            (commsymlen != ADMIN (log_lead).size ||
             memcmp (commsyml, ADMIN (log_lead).string, commsymlen) != 0))
          {
            ADMIN (log_lead).string = commsyml;
            ADMIN (log_lead).size = commsymlen;
            changed = true;
          }
        if (0 <= expmode && BE (kws) != expmode)
          {
            BE (kws) = expmode;
            changed = true;
          }

        /* Update default branch.  */
        if (branchflag && fully_numeric_no_k (&branchnum, branchsym))
          {
            if (countnumflds (branchnum.string))
              {
                if (cmpnum (ADMIN (defbr), branchnum.string) != 0)
                  {
                    ADMIN (defbr) = branchnum.string;
                    changed = true;
                  }
              }
            else if (ADMIN (defbr))
              {
                ADMIN (defbr) = NULL;
                changed = true;
              }
          }

        /* Update access list.  */
        changed |= doaccess ();

        /* Update association list.  */
        changed |= doassoc ();

        /* Update locks.  */
        changed |= dolocks ();

        /* Update log messages.  */
        changed |= domessages ();

        /* Update state attribution.  */
        if (chgheadstate)
          {
            /* Change state of default branch or head.  */
            if (!ADMIN (defbr))
              {
                if (!ADMIN (head))
                  RWARN ("can't change states in an empty tree");
                else if (STR_DIFF (ADMIN (head)->state, headstate))
                  {
                    ADMIN (head)->state = headstate;
                    changed = true;
                  }
              }
            else
              changed |= rcs_setstate (ADMIN (defbr), headstate);
          }
        for (struct link *ls = statelst.next; ls; ls = ls->next)
          {
            struct u_state const *us = ls->entry;

            changed |= rcs_setstate (us->revno, us->status);
          }

        cuthead = cuttail = NULL;
        if (delrev.strt && removerevs ())
          {
            /* Rebuild delta tree if some deltas are deleted.  */
            if (cuttail)
              gr_revno (cuttail->num, &gendeltas);
            buildtree ();
            changed = true;
            keepRCStime = false;
          }

        if (LEX (erroneousp))
          continue;

        putadmin ();
        if (ADMIN (head))
          puttree (ADMIN (head), FLOW (rewr));
        putdesc (&newdesc, textflag, textfile);

        /* Don't conditionalize on non-NULL ‘ADMIN (head)’; that prevents
           ‘scanlogtext’ from advancing the input pointer to EOF, in
           the process "marking" the intervening log messages to be
           discarded later.  The result is bogus log messages.  See
           <http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=69193>.  */
        if (1)
          {
            if (delrev.strt || messagelst.next)
              {
                struct editstuff *es = make_editstuff ();

                if (!cuttail || buildeltatext (es, gendeltas))
                  {
                    fro_trundling (true, FLOW (from));
                    scanlogtext (es, NULL, false);
                    /* Copy rest of delta text nodes that are not deleted.  */
                    changed = true;
                  }
                unmake_editstuff (es);
              }
          }

        if (initflag)
          {
            /* Adjust things for donerewrite's sake.  */
            if (stat (MANI (filename), &REPO (stat)) != 0)
              {
#if BAD_CREAT0
                mode_t m = umask (0);
                umask (m);
                REPO (stat).st_mode = (S_IRUSR | S_IRGRP | S_IROTH) & ~m;
#else
                changed = -1;
#endif
              }
            REPO (stat).st_nlink = 0;
            keepRCStime = false;
          }
        if (donerewrite (changed, keepRCStime
                         ? REPO (stat).st_mtime
                         : (time_t) - 1) != 0)
          break;

        diagnose ("done");
      }

  tempunlink ();
  return exitstatus;
}

/* rcs.c ends here */
