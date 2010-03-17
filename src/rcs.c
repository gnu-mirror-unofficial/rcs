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

#include "rcsbase.h"
#include "rcs-help.c"

struct Lockrev
{
  char const *revno;
  struct Lockrev *nextrev;
};

struct Symrev
{
  char const *revno;
  char const *ssymbol;
  bool override;
  struct Symrev *nextsym;
};

struct Message
{
  char const *revno;
  struct cbuf message;
  struct Message *nextmessage;
};

struct Status
{
  char const *revno;
  char const *status;
  struct Status *nextstatus;
};

enum changeaccess
{ append, erase };
struct chaccess
{
  char const *login;
  enum changeaccess command;
  struct chaccess *nextchaccess;
};

struct delrevpair
{
  char const *strt;
  char const *end;
  int code;
};

static struct buf numrev;
static char const *headstate;
static bool chgheadstate, lockhead, unlockcaller, suppress_mail;
static int exitstatus;
static struct Lockrev *newlocklst, *rmvlocklst;
static struct Message *messagelst, **nextmessage;
static struct Status *statelst, **nextstate;
static struct Symrev *assoclst, **nextassoc;
static struct chaccess *chaccess, **nextchaccess;
static struct delrevpair delrev;
static struct hshentry *cuthead, *cuttail, *delstrt;
static struct hshentries *gendeltas;

static void
cleanup (void)
{
  if (nerror)
    exitstatus = EXIT_FAILURE;
  Izclose (&finptr);
  Ozclose (&fcopy);
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
getassoclst (bool flag, char *sp)
/* Associate a symbolic name to a revision or branch,
   and store in `assoclst'.  */
{
  struct Symrev *pt;
  char const *temp;
  int c;

  while ((c = *++sp) == ' ' || c == '\t' || c == '\n')
    continue;
  temp = sp;
  /* Check for invalid symbolic name.  */
  sp = checksym (sp, ':');
  c = *sp;
  *sp = '\0';
  while (c == ' ' || c == '\t' || c == '\n')
    c = *++sp;

  if (c != ':' && c != '\0')
    {
      error ("invalid string %s after option -n or -N", sp);
      return;
    }

  pt = talloc (struct Symrev);
  pt->ssymbol = temp;
  pt->override = flag;
  /* Delete symbol.  */
  if (c == '\0')
    pt->revno = NULL;
  else
    {
      while ((c = *++sp) == ' ' || c == '\n' || c == '\t')
        continue;
      pt->revno = sp;
    }
  pt->nextsym = NULL;
  *nextassoc = pt;
  nextassoc = &pt->nextsym;
}

static void
getchaccess (char const *login, enum changeaccess command)
{
  register struct chaccess *pt;

  pt = talloc (struct chaccess);
  pt->login = login;
  pt->command = command;
  pt->nextchaccess = NULL;
  *nextchaccess = pt;
  nextchaccess = &pt->nextchaccess;
}

static void
getaccessor (char *opt, enum changeaccess command)
/* Get the accessor list of options `-e' and `-a'; store in `chaccess'.  */
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
          getchaccess (NULL, command);
          return;
        }
      error ("missing login name after option -a or -e");
      return;
    }

  while (c != '\0')
    {
      getchaccess (sp, command);
      sp = checkid (sp, ',');
      c = *sp;
      *sp = '\0';
      while (c == ' ' || c == '\n' || c == '\t' || c == ',')
        c = (*++sp);
    }
}

static void
getmessage (char *option)
{
  struct Message *pt;
  struct cbuf cb;
  char *m;

  if (!(m = strchr (option, ':')))
    {
      error ("-m option lacks revision number");
      return;
    }
  *m++ = '\0';
  cb = cleanlogmsg (m, strlen (m));
  if (!cb.size)
    {
      error ("-m option lacks log message");
      return;
    }
  pt = talloc (struct Message);
  pt->revno = option;
  pt->message = cb;
  pt->nextmessage = NULL;
  *nextmessage = pt;
  nextmessage = &pt->nextmessage;
}

static void
getstates (char *sp)
/* Get one state attribute and the corresponding rev; store in `statelst'.  */
{
  char const *temp;
  struct Status *pt;
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
      /* Change state of default branch or `Head'.  */
      chgheadstate = true;
      headstate = temp;
      return;
    }
  else if (c != ':')
    {
      error ("missing ':' after state in option -s");
      return;
    }

  while ((c = *++sp) == ' ' || c == '\t' || c == '\n')
    continue;
  pt = talloc (struct Status);
  pt->status = temp;
  pt->revno = sp;
  pt->nextstatus = NULL;
  *nextstate = pt;
  nextstate = &pt->nextstatus;
}

static void
getdelrev (char *sp)
/* Get revision range or branch to be deleted; place in `delrev'.  */
{
  int c;
  struct delrevpair *pt;
  int separator;

  pt = &delrev;
  while ((c = (*++sp)) == ' ' || c == '\n' || c == '\t')
    continue;

  /* Support old ambiguous '-' syntax; this will go away.  */
  if (strchr (sp, ':'))
    separator = ':';
  else
    {
      if (strchr (sp, '-') && VERSION (5) <= RCSversion)
        warn ("`-' is obsolete in `-o%s'; use `:' instead", sp);
      separator = '-';
    }

  if (c == separator)
    {                           /* -o:rev */
      while ((c = (*++sp)) == ' ' || c == '\n' || c == '\t')
        continue;
      pt->strt = sp;
      pt->code = 1;
      while (c != ' ' && c != '\n' && c != '\t' && c != '\0')
        c = (*++sp);
      *sp = '\0';
      pt->end = NULL;
      return;
    }
  else
    {
      pt->strt = sp;
      while (c != ' ' && c != '\n' && c != '\t' && c != '\0'
             && c != separator)
        c = *++sp;
      *sp = '\0';
      while (c == ' ' || c == '\n' || c == '\t')
        c = *++sp;
      if (c == '\0')
        {                       /* -o rev or branch */
          pt->code = 0;
          pt->end = NULL;
          return;
        }
      if (c != separator)
        {
          error ("invalid range %s %s after -o", pt->strt, sp);
        }
      while ((c = *++sp) == ' ' || c == '\n' || c == '\t')
        continue;
      if (!c)
        {                       /* -orev: */
          pt->code = 2;
          pt->end = NULL;
          return;
        }
    }
  /* -orev1:rev2 */
  pt->end = sp;
  pt->code = 3;
  while (c != ' ' && c != '\n' && c != '\t' && c != '\0')
    c = *++sp;
  *sp = '\0';
}

static void
scanlogtext (struct hshentry *delta, bool edit)
/* Scan delta text nodes up to and including the one given by `delta',
   or up to last one present, if `!delta'.  For the one given by
   `delta' (if `delta'), the log message is saved into `delta->log' if
   `delta == cuttail'; the text is edited if `edit' is set, else
   copied.  Assume the initial lexeme must be read in first.  Do not
   advance `nexttok' after it is finished, except if `!delta'.  */
{
  struct hshentry const *nextdelta;
  struct cbuf cb;

  for (;;)
    {
      foutptr = NULL;
      if (eoflex ())
        {
          if (delta)
            rcsfaterror ("can't find delta for revision %s", delta->num);
          return;               /* no more delta text nodes */
        }
      nextlex ();
      if (!(nextdelta = getnum ()))
        fatserror ("delta number corrupted");
      if (nextdelta->selector)
        {
          foutptr = frewrite;
          aprintf (frewrite, DELNUMFORM, nextdelta->num, Klog);
        }
      getkeystring (Klog);
      if (nextdelta == cuttail)
        {
          cb = savestring (&curlogbuf);
          if (!delta->log.string)
            delta->log = cleanlogmsg (curlogbuf.string, cb.size);
          nextlex ();
          delta->igtext = getphrases (Ktext);
        }
      else
        {
          if (nextdelta->log.string && nextdelta->selector)
            {
              foutptr = NULL;
              readstring ();
              foutptr = frewrite;
              putstring (foutptr, false, nextdelta->log, true);
              afputc (nextc, foutptr);
            }
          else
            readstring ();
          ignorephrases (Ktext);
        }
      getkeystring (Ktext);

      if (delta == nextdelta)
        break;
      readstring ();            /* skip over it */

    }
  /* got the one we're looking for */
  if (edit)
    editstring (NULL);
  else
    enterstring ();
}

static struct Lockrev **
rmnewlocklst (char const *which)
/* Remove lock to revision `which' from `newlocklst'.  */
{
  struct Lockrev *pt, **pre;

  pre = &newlocklst;
  while ((pt = *pre))
    if (strcmp (pt->revno, which) != 0)
      pre = &pt->nextrev;
    else
      {
        *pre = pt->nextrev;
        tfree (pt);
      }
  return pre;
}

static bool
doaccess (void)
{
  register struct chaccess *ch;
  register struct access **p, *t;
  register bool changed = false;

  for (ch = chaccess; ch; ch = ch->nextchaccess)
    {
      switch (ch->command)
        {
        case erase:
          if (!ch->login)
            {
              if (AccessList)
                {
                  AccessList = NULL;
                  changed = true;
                }
            }
          else
            for (p = &AccessList; (t = *p); p = &t->nextaccess)
              if (strcmp (ch->login, t->login) == 0)
                {
                  *p = t->nextaccess;
                  changed = true;
                  break;
                }
          break;
        case append:
          for (p = &AccessList;; p = &t->nextaccess)
            if (!(t = *p))
              {
                *p = t = ftalloc (struct access);
                t->login = ch->login;
                t->nextaccess = NULL;
                changed = true;
                break;
              }
            else if (strcmp (ch->login, t->login) == 0)
              break;
          break;
        }
    }
  return changed;
}

static bool
sendmail (char const *Delta, char const *who)
/* Mail to `who', informing him that his lock on `Delta' was broken by
   caller.  Ask first whether to go ahead.  Return false on error or if
   user decides not to break the lock.  */
{
#ifdef SENDMAIL
  char const *messagefile;
  int old1, old2, c, status;
  FILE *mailmess;
#endif

  aprintf (stderr, "Revision %s is already locked by %s.\n", Delta, who);
  if (suppress_mail)
    return true;
  if (!yesorno (false, "Do you want to break the lock? [ny](n): "))
    return false;

  /* Go ahead with breaking.  */
#ifdef SENDMAIL
  messagefile = maketemp (0);
  if (!(mailmess = fopenSafer (messagefile, "w+")))
    {
      efaterror (messagefile);
    }

  aprintf (mailmess,
           "Subject: Broken lock on %s\n\nYour lock on revision %s of file %s\nhas been broken by %s for the following reason:\n",
           basefilename (RCSname), Delta, getfullRCSname (), getcaller ());
  aputs
    ("State the reason for breaking the lock:\n(terminate with single '.' or end of file)\n>> ",
     stderr);
  eflush ();

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
            {
              aputs (">> ", stderr);
              eflush ();
            }
        }
    }
  Orewind (mailmess);
  aflush (mailmess);
  status = run (fileno (mailmess), NULL, SENDMAIL, who, NULL);
  Ozclose (&mailmess);
  if (status == 0)
    return true;
  warn ("Mail failed.");
#endif  /* defined SENDMAIL */
  warn ("Mail notification of broken locks is not available.");
  warn ("Please tell `%s' why you broke the lock.", who);
  return true;
}

static bool
breaklock (struct hshentry const *delta)
/* Find the lock held by caller on `delta', and remove it.
   Send mail if a lock different from the caller's is broken.
   Print an error message if there is no such lock or error.  */
{
  register struct rcslock *next, **trail;
  char const *num;

  num = delta->num;
  for (trail = &Locks; (next = *trail); trail = &next->nextlock)
    if (strcmp (num, next->delta->num) == 0)
      {
        if (strcmp (getcaller (), next->login) != 0
            && !sendmail (num, next->login))
          {
            rcserror ("revision %s still locked by %s", num, next->login);
            return false;
          }
        diagnose ("%s unlocked\n", next->delta->num);
        *trail = next->nextlock;
        next->delta->lockedby = NULL;
        return true;
      }
  rcserror ("no lock set on revision %s", num);
  return false;
}

static struct hshentry *
searchcutpt (char const *object, int length, struct hshentries *store)
/* Search store and return entry with number being `object'.
   `cuttail' is 0, if the entry is Head; otherwise, `cuttail'
   is the entry point to the one with number being `object'.  */
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
/* Check whether the deltas between `strt' and `tail' are locked or
   branch point, return 1 if any is locked or branch point; otherwise,
   return 0 and mark deleted.  */
{
  struct hshentry *pt;
  struct rcslock const *lockpt;

  for (pt = strt; pt != tail; pt = pt->next)
    {
      if (pt->branches)
        {
          /* A branch point.  */
          rcserror ("can't remove branch point %s", pt->num);
          return true;
        }
      for (lockpt = Locks; lockpt; lockpt = lockpt->nextlock)
        if (lockpt->delta == pt)
          {
            rcserror ("can't remove locked revision %s", pt->num);
            return true;
          }
      pt->selector = false;
      diagnose ("deleting revision %s\n", pt->num);
    }
  return false;
}

static bool
removerevs (void)
/* Get the revision range to be removed, and place the first revision
   removed in `delstrt', the revision before `delstrt' in `cuthead'
   (0, if `delstrt' is head), and the revision after the last removed
   revision in `cuttail' (0 if the last is a leaf).  */
{
  struct hshentry *target, *target2, *temp;
  int length;
  int cmp;

  if (!expandsym (delrev.strt, &numrev))
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
          rcserror ("Revision %s doesn't exist.", numrev.string);
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
      rcserror ("invalid branch range %s after -o", numrev.string);
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
          /* Get branch number.  */
          getbranchno (temp->num, &numrev);
          gr_revno (numrev.string, &gendeltas);
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
  if (!expandsym (delrev.end, &numrev))
    return false;
  if (length != countnumflds (numrev.string)
      || (length > 2 && compartial (numrev.string, target->num, length - 1)))
    {
      rcserror ("invalid revision range %s-%s", target->num, numrev.string);
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
              rcserror ("Revisions %s-%s don't exist.",
                        delrev.strt, delrev.end);
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
              rcserror ("Revisions %s-%s don't exist.",
                        delrev.strt, delrev.end);
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
/* Add or delete (if !revno) association that is stored in `assoclst'.  */
{
  char const *p;
  bool changed = false;
  struct Symrev const *curassoc;
  struct assoc **pre, *pt;

  /* Add new associations.  */
  for (curassoc = assoclst; curassoc; curassoc = curassoc->nextsym)
    {
      char const *ssymbol = curassoc->ssymbol;

      if (!curassoc->revno)
        {
          /* Delete symbol.  */
          for (pre = &Symbols;; pre = &pt->nextassoc)
            if (!(pt = *pre))
              {
                rcswarn ("can't delete nonexisting symbol %s", ssymbol);
                break;
              }
            else if (strcmp (pt->symbol, ssymbol) == 0)
              {
                *pre = pt->nextassoc;
                changed = true;
                break;
              }
        }
      else
        {
          if (curassoc->revno[0])
            {
              p = NULL;
              if (expandsym (curassoc->revno, &numrev))
                p = fstr_save (numrev.string);
            }
          else if (!(p = tiprev ()))
            rcserror ("no latest revision to associate with symbol %s",
                      ssymbol);
          if (p)
            changed |= addsymbol (p, ssymbol, curassoc->override);
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

  if (expandsym (rev, &numrev))
    {
      target = gr_revno (numrev.string, &gendeltas);
      if (target)
        {
          if (!(countnumflds (numrev.string) & 1)
              && cmpnum (target->num, numrev.string))
            rcserror ("can't lock nonexisting revision %s", numrev.string);
          else
            {
              if ((r = addlock (target, false)) < 0 && breaklock (target))
                r = addlock (target, true);
              if (0 <= r)
                {
                  if (r)
                    diagnose ("%s locked\n", target->num);
                  return r;
                }
            }
        }
    }
  return false;
}

static bool
dolocks (void)
/* Remove lock for caller or first lock if `unlockcaller' is set;
   remove locks which are stored in `rmvlocklst',
   add new locks which are stored in `newlocklst',
   add lock for `Dbranch' or `Head' if `lockhead' is set.  */
{
  struct Lockrev const *lockpt;
  struct hshentry *target;
  bool changed = false;

  if (unlockcaller)
    {
      /* Find lock for caller.  */
      if (Head)
        {
          if (Locks)
            {
              switch (findlock (true, &target))
                {
                case 0:
                  /* Remove most recent lock.  */
                  changed |= breaklock (Locks->delta);
                  break;
                case 1:
                  diagnose ("%s unlocked\n", target->num);
                  changed = true;
                  break;
                }
            }
          else
            {
              rcswarn ("No locks are set.");
            }
        }
      else
        {
          rcswarn ("can't unlock an empty tree");
        }
    }

  /* Remove locks which are stored in rmvlocklst.  */
  for (lockpt = rmvlocklst; lockpt; lockpt = lockpt->nextrev)
    if (expandsym (lockpt->revno, &numrev))
      {
        target = gr_revno (numrev.string, &gendeltas);
        if (target)
          {
            if (!(countnumflds (numrev.string) & 1)
                && cmpnum (target->num, numrev.string))
              rcserror ("can't unlock nonexisting revision %s", lockpt->revno);
            else
              changed |= breaklock (target);
          }
        /* `breaklock' does its own `diagnose'.  */
      }

  /* Add new locks which stored in newlocklst.  */
  for (lockpt = newlocklst; lockpt; lockpt = lockpt->nextrev)
    changed |= setlock (lockpt->revno);

  if (lockhead)
    {
      /* Lock default branch or head.  */
      if (Dbranch)
        changed |= setlock (Dbranch);
      else if (Head)
        changed |= setlock (Head->num);
      else
        rcswarn ("can't lock an empty tree");
    }
  return changed;
}

static bool
domessages (void)
{
  struct hshentry *target;
  struct Message *p;
  bool changed = false;

  for (p = messagelst; p; p = p->nextmessage)
    if (expandsym (p->revno, &numrev) &&
        (target = gr_revno (numrev.string, &gendeltas)))
      {
        /* We can't check the old log -- it's much later in the file.
           We pessimistically assume that it changed.  */
        target->log = p->message;
        changed = true;
      }
  return changed;
}

static bool
rcs_setstate (char const *rev, char const *status)
/* Given a revision or branch number, find the corresponding delta
   and sets its state to `status'.  */
{
  struct hshentry *target;

  if (expandsym (rev, &numrev))
    {
      target = gr_revno (numrev.string, &gendeltas);
      if (target)
        {
          if (!(countnumflds (numrev.string) & 1)
              && cmpnum (target->num, numrev.string))
            rcserror ("can't set state of nonexisting revision %s",
                      numrev.string);
          else if (strcmp (target->state, status) != 0)
            {
              target->state = status;
              return true;
            }
        }
    }
  return false;
}

static bool
buildeltatext (struct hshentries const *deltas)
/* Put the delta text on `frewrite' and make necessary
   change to delta text.  */
{
  register FILE *fcut;          /* temporary file to rebuild delta tree */
  char const *cutname;

  fcut = NULL;
  cuttail->selector = false;
  scanlogtext (deltas->first, false);
  if (cuthead)
    {
      cutname = maketemp (3);
      if (!(fcut = fopenSafer (cutname, FOPEN_WPLUS_WORK)))
        {
          efaterror (cutname);
        }

      while (deltas->first != cuthead)
        {
          deltas = deltas->rest;
          scanlogtext (deltas->first, true);
        }

      snapshotedit (fcut);
      Orewind (fcut);
      aflush (fcut);
    }

  while (deltas->first != cuttail)
    scanlogtext ((deltas = deltas->rest)->first, true);
  finishedit (NULL, NULL, true);
  Ozclose (&fcopy);

  if (fcut)
    {
      char const *diffname = maketemp (0);
      char const *diffv[6 + !!OPEN_O_BINARY];
      char const **diffp = diffv;

      *++diffp = prog_diff;
      *++diffp = diff_flags;
#if OPEN_O_BINARY
      if (Expand == kwsub_b)
        *++diffp == "--binary";
#endif
      *++diffp = "-";
      *++diffp = resultname;
      *++diffp = '\0';
      if (diff_trouble == runv (fileno (fcut), diffname, diffv))
        rcsfaterror ("diff failed");
      Ofclose (fcut);
      return putdtext (cuttail, diffname, frewrite, true);
    }
  else
    return putdtext (cuttail, resultname, frewrite, false);
}

static void
buildtree (void)
/* Actually remove revisions whose selector field
   is false, and rebuild the linkage of deltas.
   Ask for reconfirmation if deleting last revision.  */
{
  struct hshentry *Delta;
  struct branchhead *pt, *pre;

  if (cuthead)
    if (cuthead->next == delstrt)
      cuthead->next = cuttail;
    else
      {
        pre = pt = cuthead->branches;
        while (pt && pt->hsh != delstrt)
          {
            pre = pt;
            pt = pt->nextbranch;
          }
        if (cuttail)
          pt->hsh = cuttail;
        else if (pt == pre)
          cuthead->branches = pt->nextbranch;
        else
          pre->nextbranch = pt->nextbranch;
      }
  else
    {
      if (!cuttail && !quietflag)
        {
          if (!yesorno
              (false,
               "Do you really want to delete all revisions? [ny](n): "))
            {
              rcserror ("No revision deleted");
              Delta = delstrt;
              while (Delta)
                {
                  Delta->selector = true;
                  Delta = Delta->next;
                }
              return;
            }
        }
      Head = cuttail;
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
  struct buf branchnum;
  struct Lockrev *lockpt;
  struct Lockrev **curlock, **rmvlock;
  struct Status *curstate;

  CHECK_HV ();

  nosetid ();

  nextassoc = &assoclst;
  nextchaccess = &chaccess;
  nextmessage = &messagelst;
  nextstate = &statelst;
  branchsym = commsyml = textfile = NULL;
  branchflag = strictlock = false;
  bufautobegin (&branchnum);
  commsymlen = 0;
  curlock = &newlocklst;
  rmvlock = &rmvlocklst;
  expmode = -1;
  suffixes = X_DEFAULT;
  initflag = textflag = false;
  strict_selected = false;
  Ttimeflag = false;

  /* Preprocess command options.  */
  if (1 < argc && argv[1][0] != '-')
    warn ("No options were given; this usage is obsolescent.");

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
          getaccessor (*argv + 1, append);
          break;

        case 'A':
          /* Append access list according to accessfile.  */
          if (!*a)
            {
              error ("missing pathname after -A");
              break;
            }
          *argv = a;
          if (0 < pairnames (1, argv, rcsreadopen, true, false))
            {
              while (AccessList)
                {
                  getchaccess (str_save (AccessList->login), append);
                  AccessList = AccessList->nextaccess;
                }
              Izclose (&finptr);
            }
          break;

        case 'e':
          /* Remove accessors.  */
          getaccessor (*argv + 1, erase);
          break;

        case 'l':
          /* Lock a revision if it is unlocked.  */
          if (!*a)
            {
              /* Lock head or default branch.  */
              lockhead = true;
              break;
            }
          *curlock = lockpt = talloc (struct Lockrev);
          lockpt->revno = a;
          lockpt->nextrev = NULL;
          curlock = &lockpt->nextrev;
          break;

        case 'u':
          /* Release lock of a locked revision.  */
          if (!*a)
            {
              unlockcaller = true;
              break;
            }
          *rmvlock = lockpt = talloc (struct Lockrev);
          lockpt->revno = a;
          lockpt->nextrev = NULL;
          rmvlock = &lockpt->nextrev;
          curlock = rmnewlocklst (lockpt->revno);
          break;

        case 'L':
          /* Set strict locking.  */
          if (strict_selected)
            {
              if (!strictlock)
                warn ("-U overridden by -L");
            }
          strictlock = true;
          strict_selected = true;
          break;

        case 'U':
          /* Release strict locking.  */
          if (strict_selected)
            {
              if (strictlock)
                warn ("-L overridden by -U");
            }
          strict_selected = true;
          break;

        case 'n':
          /* Add new association: error, if name exists.  */
          if (!*a)
            {
              error ("missing symbolic name after -n");
              break;
            }
          getassoclst (false, (*argv) + 1);
          break;

        case 'N':
          /* Add or change association.  */
          if (!*a)
            {
              error ("missing symbolic name after -N");
              break;
            }
          getassoclst (true, (*argv) + 1);
          break;

        case 'm':
          /* Change log message.  */
          getmessage (a);
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
              error ("missing revision range after -o");
              break;
            }
          getdelrev ((*argv) + 1);
          break;

        case 's':
          /* Change state attribute of a revision.  */
          if (!*a)
            {
              error ("state missing after -s");
              break;
            }
          getstates ((*argv) + 1);
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
          interactiveflag = true;
          break;

        case 'q':
          quietflag = true;
          break;

        case 'x':
          suffixes = a;
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
          error ("unknown option: %s", *argv);
        };
    }
  /* (End processing of options.)  */

  /* Now handle all pathnames.  */
  if (nerror)
    cleanup ();
  else if (argc < 1)
    faterror ("no input file");
  else
    for (; 0 < argc; cleanup (), ++argv, --argc)
      {

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
                rcserror ("already exists");
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

        /* `RCSname' contains the name of the RCS file, and
           `workname' contains the name of the working file.
           If `!initflag', `finptr' contains the file descriptor
           for the RCS file.  The admin node is initialized.  */

        diagnose ("RCS file: %s\n", RCSname);

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
            changed |= StrictLocks ^ strictlock;
            StrictLocks = strictlock;
          }
        if (commsyml &&
            (commsymlen != Comment.size ||
             memcmp (commsyml, Comment.string, commsymlen) != 0))
          {
            Comment.string = commsyml;
            Comment.size = strlen (commsyml);
            changed = true;
          }
        if (0 <= expmode && Expand != expmode)
          {
            Expand = expmode;
            changed = true;
          }

        /* Update default branch.  */
        if (branchflag && expandsym (branchsym, &branchnum))
          {
            if (countnumflds (branchnum.string))
              {
                if (cmpnum (Dbranch, branchnum.string) != 0)
                  {
                    Dbranch = branchnum.string;
                    changed = true;
                  }
              }
            else if (Dbranch)
              {
                Dbranch = NULL;
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
            if (!Dbranch)
              {
                if (!Head)
                  rcswarn ("can't change states in an empty tree");
                else if (strcmp (Head->state, headstate) != 0)
                  {
                    Head->state = headstate;
                    changed = true;
                  }
              }
            else
              changed |= rcs_setstate (Dbranch, headstate);
          }
        for (curstate = statelst; curstate; curstate = curstate->nextstatus)
          changed |= rcs_setstate (curstate->revno, curstate->status);

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

        if (nerror)
          continue;

        putadmin ();
        if (Head)
          puttree (Head, frewrite);
        putdesc (textflag, textfile);

        /* Don't conditionalize on non-NULL `Head'; that prevents
           `scanlogtext' from advancing the input pointer to EOF, in
           the process "marking" the intervening log messages to be
           discarded later.  The result is bogus log messages.  See
           <http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=69193>.  */
        if (1)
          {
            if (delrev.strt || messagelst)
              {
                if (!cuttail || buildeltatext (gendeltas))
                  {
                    advise_access (finptr, MADV_SEQUENTIAL);
                    scanlogtext (NULL, false);
                    /* Copy rest of delta text nodes that are not deleted.  */
                    changed = true;
                  }
              }
          }

        if (initflag)
          {
            /* Adjust things for donerewrite's sake.  */
            if (stat (workname, &RCSstat) != 0)
              {
#if BAD_CREAT0
                mode_t m = umask (0);
                (void) umask (m);
                RCSstat.st_mode = (S_IRUSR | S_IRGRP | S_IROTH) & ~m;
#else
                changed = -1;
#endif
              }
            RCSstat.st_nlink = 0;
            keepRCStime = false;
          }
        if (donerewrite (changed,
                         keepRCStime ? RCSstat.st_mtime : (time_t) - 1) != 0)
          break;

        diagnose ("done\n");
      }

  tempunlink ();
  return exitstatus;
}

/* rcs.c ends here */
