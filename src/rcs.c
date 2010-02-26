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

struct Lockrev
{
  char const *revno;
  struct Lockrev *nextrev;
};

struct Symrev
{
  char const *revno;
  char const *ssymbol;
  int override;
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

static int branchpoint (struct hshentry *, struct hshentry *);
static int breaklock (struct hshentry const *);
static int buildeltatext (struct hshentries const *);
static int doaccess (void);
static int doassoc (void);
static int dolocks (void);
static int domessages (void);
static int rcs_setstate (char const *, char const *);
static int removerevs (void);
static int sendmail (char const *, char const *);
static int setlock (char const *);
static struct Lockrev **rmnewlocklst (char const *);
static struct hshentry *searchcutpt (char const *, int, struct hshentries *);
static void buildtree (void);
static void cleanup (void);
static void getaccessor (char *, enum changeaccess);
static void getassoclst (int, char *);
static void getchaccess (char const *, enum changeaccess);
static void getdelrev (char *);
static void getmessage (char *);
static void getstates (char *);
static void scanlogtext (struct hshentry *, int);

static struct buf numrev;
static char const *headstate;
static int chgheadstate, exitstatus, lockhead, unlockcaller;
static int suppress_mail;
static struct Lockrev *newlocklst, *rmvlocklst;
static struct Message *messagelst, **nextmessage;
static struct Status *statelst, **nextstate;
static struct Symrev *assoclst, **nextassoc;
static struct chaccess *chaccess, **nextchaccess;
static struct delrevpair delrev;
static struct hshentry *cuthead, *cuttail, *delstrt;
static struct hshentries *gendeltas;

char const cmdid[] = "rcs";

int
main (int argc, char **argv)
{
  static char const cmdusage[] =
    "\nrcs usage: rcs -{ae}logins -Afile -{blu}[rev] -cstring -{iILqTU} -ksubst -mrev:msg -{nN}name[:[rev]] -orange -sstate[:rev] -t[text] -Vn -xsuff -zzone file ...";

  char *a, **newargv, *textfile;
  char const *branchsym, *commsyml;
  int branchflag, changed, expmode, initflag;
  int strictlock, strict_selected, textflag;
  int keepRCStime, Ttimeflag;
  size_t commsymlen;
  struct buf branchnum;
  struct Lockrev *lockpt;
  struct Lockrev **curlock, **rmvlock;
  struct Status *curstate;

  nosetid ();

  nextassoc = &assoclst;
  nextchaccess = &chaccess;
  nextmessage = &messagelst;
  nextstate = &statelst;
  branchsym = commsyml = textfile = 0;
  branchflag = strictlock = false;
  bufautobegin (&branchnum);
  commsymlen = 0;
  curlock = &newlocklst;
  rmvlock = &rmvlocklst;
  expmode = -1;
  suffixes = X_DEFAULT;
  initflag = textflag = false;
  strict_selected = 0;
  Ttimeflag = false;

  /*  preprocessing command options    */
  if (1 < argc && argv[1][0] != '-')
    warn ("No options were given; this usage is obsolescent.");

  argc = getRCSINIT (argc, argv, &newargv);
  argv = newargv;
  while (a = *++argv, 0 < --argc && *a++ == '-')
    {
      switch (*a++)
        {

        case 'i':              /*  initial version  */
          initflag = true;
          break;

        case 'b':              /* change default branch */
          if (branchflag)
            redefined ('b');
          branchflag = true;
          branchsym = a;
          break;

        case 'c':              /*  change comment symbol   */
          if (commsyml)
            redefined ('c');
          commsyml = a;
          commsymlen = strlen (a);
          break;

        case 'a':              /*  add new accessor   */
          getaccessor (*argv + 1, append);
          break;

        case 'A':              /*  append access list according to accessfile  */
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

        case 'e':              /*  remove accessors   */
          getaccessor (*argv + 1, erase);
          break;

        case 'l':              /*   lock a revision if it is unlocked   */
          if (!*a)
            {
              /* Lock head or default branch.  */
              lockhead = true;
              break;
            }
          *curlock = lockpt = talloc (struct Lockrev);
          lockpt->revno = a;
          lockpt->nextrev = 0;
          curlock = &lockpt->nextrev;
          break;

        case 'u':              /*  release lock of a locked revision   */
          if (!*a)
            {
              unlockcaller = true;
              break;
            }
          *rmvlock = lockpt = talloc (struct Lockrev);
          lockpt->revno = a;
          lockpt->nextrev = 0;
          rmvlock = &lockpt->nextrev;
          curlock = rmnewlocklst (lockpt->revno);
          break;

        case 'L':              /*  set strict locking */
          if (strict_selected)
            {
              if (!strictlock)  /* Already selected -U? */
                warn ("-U overridden by -L");
            }
          strictlock = true;
          strict_selected = true;
          break;

        case 'U':              /*  release strict locking */
          if (strict_selected)
            {
              if (strictlock)   /* Already selected -L? */
                warn ("-L overridden by -U");
            }
          strict_selected = true;
          break;

        case 'n':              /*  add new association: error, if name exists */
          if (!*a)
            {
              error ("missing symbolic name after -n");
              break;
            }
          getassoclst (false, (*argv) + 1);
          break;

        case 'N':              /*  add or change association   */
          if (!*a)
            {
              error ("missing symbolic name after -N");
              break;
            }
          getassoclst (true, (*argv) + 1);
          break;

        case 'm':              /*  change log message  */
          getmessage (a);
          break;

        case 'M':              /*  do not send mail */
          suppress_mail = true;
          break;

        case 'o':              /*  delete revisions  */
          if (delrev.strt)
            redefined ('o');
          if (!*a)
            {
              error ("missing revision range after -o");
              break;
            }
          getdelrev ((*argv) + 1);
          break;

        case 's':              /*  change state attribute of a revision  */
          if (!*a)
            {
              error ("state missing after -s");
              break;
            }
          getstates ((*argv) + 1);
          break;

        case 't':              /*  change descriptive text   */
          textflag = true;
          if (*a)
            {
              if (textfile)
                redefined ('t');
              textfile = a;
            }
          break;

        case 'T':              /*  do not update last-mod time for minor changes */
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

        case 'k':              /*  set keyword expand mode  */
          if (0 <= expmode)
            redefined ('k');
          if (0 <= (expmode = str2expmode (a)))
            break;
          /* fall into */
        default:
        unknown:
          error ("unknown option: %s%s", *argv, cmdusage);
        };
    }                           /* end processing of options */

  /* Now handle all pathnames.  */
  if (nerror)
    cleanup ();
  else if (argc < 1)
    faterror ("no input file%s", cmdusage);
  else
    for (; 0 < argc; cleanup (), ++argv, --argc)
      {

        ffree ();

        if (initflag)
          {
            switch (pairnames (argc, argv, rcswriteopen, false, false))
              {
              case -1:
                break;          /*  not exist; ok */
              case 0:
                continue;       /*  error         */
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
                continue;       /*  not exist      */
              case 0:
                continue;       /*  errors         */
              case 1:
                break;          /*  file exists; ok */
              }
          }

        /*
         * RCSname contains the name of the RCS file, and
         * workname contains the name of the working file.
         * if !initflag, finptr contains the file descriptor for the
         * RCS file. The admin node is initialized.
         */

        diagnose ("RCS file: %s\n", RCSname);

        changed = initflag | textflag;
        keepRCStime = Ttimeflag;
        if (!initflag)
          {
            if (!checkaccesslist ())
              continue;
            gettree ();         /* Read the delta tree.  */
          }

        /*  update admin. node    */
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

        /* update default branch */
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
                Dbranch = 0;
                changed = true;
              }
          }

        changed |= doaccess (); /* Update access list.  */

        changed |= doassoc ();  /* Update association list.  */

        changed |= dolocks ();  /* Update locks.  */

        changed |= domessages ();       /* Update log messages.  */

        /*  update state attribution  */
        if (chgheadstate)
          {
            /* change state of default branch or head */
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

        cuthead = cuttail = 0;
        if (delrev.strt && removerevs ())
          {
            /*  rebuild delta tree if some deltas are deleted   */
            if (cuttail)
              genrevs (cuttail->num, (char *) 0, (char *) 0, (char *) 0,
                       &gendeltas);
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

        if (Head)
          {
            if (delrev.strt || messagelst)
              {
                if (!cuttail || buildeltatext (gendeltas))
                  {
                    advise_access (finptr, MADV_SEQUENTIAL);
                    scanlogtext ((struct hshentry *) 0, false);
                    /* copy rest of delta text nodes that are not deleted      */
                    changed = true;
                  }
              }
          }

        if (initflag)
          {
            /* Adjust things for donerewrite's sake.  */
            if (stat (workname, &RCSstat) != 0)
              {
#		    if bad_creat0
                mode_t m = umask (0);
                (void) umask (m);
                RCSstat.st_mode = (S_IRUSR | S_IRGRP | S_IROTH) & ~m;
#		    else
                changed = -1;
#		    endif
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
  exitmain (exitstatus);
}                               /* end of main (rcs) */

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

void
exiterr (void)
{
  ORCSerror ();
  dirtempunlink ();
  tempunlink ();
  _exit (EXIT_FAILURE);
}

static void
getassoclst (int flag, char *sp)
/*  Function:   associate a symbolic name to a revision or branch,      */
/*              and store in assoclst                                   */

{
  struct Symrev *pt;
  char const *temp;
  int c;

  while ((c = *++sp) == ' ' || c == '\t' || c == '\n')
    continue;
  temp = sp;
  sp = checksym (sp, ':');      /*  check for invalid symbolic name  */
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
  if (c == '\0')                /*  delete symbol  */
    pt->revno = 0;
  else
    {
      while ((c = *++sp) == ' ' || c == '\n' || c == '\t')
        continue;
      pt->revno = sp;
    }
  pt->nextsym = 0;
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
  pt->nextchaccess = 0;
  *nextchaccess = pt;
  nextchaccess = &pt->nextchaccess;
}

static void
getaccessor (char *opt, enum changeaccess command)
/*   Function:  get the accessor list of options -e and -a,     */
/*		and store in chaccess				*/

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
          getchaccess ((char *) 0, command);
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
  *m++ = 0;
  cb = cleanlogmsg (m, strlen (m));
  if (!cb.size)
    {
      error ("-m option lacks log message");
      return;
    }
  pt = talloc (struct Message);
  pt->revno = option;
  pt->message = cb;
  pt->nextmessage = 0;
  *nextmessage = pt;
  nextmessage = &pt->nextmessage;
}

static void
getstates (char *sp)
/*   Function:  get one state attribute and the corresponding   */
/*              revision and store in statelst                  */

{
  char const *temp;
  struct Status *pt;
  register int c;

  while ((c = *++sp) == ' ' || c == '\t' || c == '\n')
    continue;
  temp = sp;
  sp = checkid (sp, ':');       /* check for invalid state attribute */
  c = *sp;
  *sp = '\0';
  while (c == ' ' || c == '\t' || c == '\n')
    c = *++sp;

  if (c == '\0')
    {                           /*  change state of def. branch or Head  */
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
  pt->nextstatus = 0;
  *nextstate = pt;
  nextstate = &pt->nextstatus;
}

static void
getdelrev (char *sp)
/*   Function:  get revision range or branch to be deleted,     */
/*              and place in delrev                             */
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
      pt->end = 0;
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
        {                       /*   -o rev or branch   */
          pt->code = 0;
          pt->end = 0;
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
          pt->end = 0;
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
scanlogtext (struct hshentry *delta, int edit)
/* Function: Scans delta text nodes up to and including the one given
 * by delta, or up to last one present, if !delta.
 * For the one given by delta (if delta), the log message is saved into
 * delta->log if delta==cuttail; the text is edited if EDIT is set, else copied.
 * Assumes the initial lexeme must be read in first.
 * Does not advance nexttok after it is finished, except if !delta.
 */
{
  struct hshentry const *nextdelta;
  struct cbuf cb;

  for (;;)
    {
      foutptr = 0;
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
              foutptr = 0;
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
    editstring ((struct hshentry *) 0);
  else
    enterstring ();
}

static struct Lockrev **
rmnewlocklst (char const *which)
/* Remove lock to revision WHICH from newlocklst.  */
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

static int
doaccess (void)
{
  register struct chaccess *ch;
  register struct access **p, *t;
  register int changed = false;

  for (ch = chaccess; ch; ch = ch->nextchaccess)
    {
      switch (ch->command)
        {
        case erase:
          if (!ch->login)
            {
              if (AccessList)
                {
                  AccessList = 0;
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
                t->nextaccess = 0;
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

static int
sendmail (char const *Delta, char const *who)
/*   Function:  mail to who, informing him that his lock on delta was
 *   broken by caller. Ask first whether to go ahead. Return false on
 *   error or if user decides not to break the lock.
 */
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

  /* go ahead with breaking  */
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
  status = run (fileno (mailmess), (char *) 0, SENDMAIL, who, (char *) 0);
  Ozclose (&mailmess);
  if (status == 0)
    return true;
  warn ("Mail failed.");
#endif
  warn ("Mail notification of broken locks is not available.");
  warn ("Please tell `%s' why you broke the lock.", who);
  return (true);
}

static int
breaklock (struct hshentry const *delta)
/* function: Finds the lock held by caller on delta,
 * and removes it.
 * Sends mail if a lock different from the caller's is broken.
 * Prints an error message if there is no such lock or error.
 */
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
        next->delta->lockedby = 0;
        return true;
      }
  rcserror ("no lock set on revision %s", num);
  return false;
}

static struct hshentry *
searchcutpt (char const *object, int length, struct hshentries *store)
/*   Function:  Search store and return entry with number being object. */
/*		cuttail = 0, if the entry is Head; otherwise, cuttail   */
/*              is the entry point to the one with number being object  */

{
  cuthead = 0;
  while (compartial (store->first->num, object, length))
    {
      cuthead = store->first;
      store = store->rest;
    }
  return store->first;
}

static int
branchpoint (struct hshentry *strt, struct hshentry *tail)
/*   Function: check whether the deltas between strt and tail	*/
/*		are locked or branch point, return 1 if any is  */
/*		locked or branch point; otherwise, return 0 and */
/*		mark deleted					*/

{
  struct hshentry *pt;
  struct rcslock const *lockpt;

  for (pt = strt; pt != tail; pt = pt->next)
    {
      if (pt->branches)
        {                       /*  a branch point  */
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

static int
removerevs (void)
/*   Function:  get the revision range to be removed, and place the     */
/*              first revision removed in delstrt, the revision before  */
/*		delstrt in cuthead (0, if delstrt is head), and the	*/
/*		revision after the last removed revision in cuttail (0	*/
/*              if the last is a leaf                                   */
{
  struct hshentry *target, *target2, *temp;
  int length;
  int cmp;

  if (!expandsym (delrev.strt, &numrev))
    return 0;
  target =
    genrevs (numrev.string, (char *) 0, (char *) 0, (char *) 0, &gendeltas);
  if (!target)
    return 0;
  cmp = cmpnum (target->num, numrev.string);
  length = countnumflds (numrev.string);

  if (delrev.code == 0)
    {                           /*  -o  rev    or    -o  branch   */
      if (length & 1)
        temp = searchcutpt (target->num, length + 1, gendeltas);
      else if (cmp)
        {
          rcserror ("Revision %s doesn't exist.", numrev.string);
          return 0;
        }
      else
        temp = searchcutpt (numrev.string, length, gendeltas);
      cuttail = target->next;
      if (branchpoint (temp, cuttail))
        {
          cuttail = 0;
          return 0;
        }
      delstrt = temp;           /* first revision to be removed   */
      return 1;
    }

  if (length & 1)
    {                           /*  invalid branch after -o   */
      rcserror ("invalid branch range %s after -o", numrev.string);
      return 0;
    }

  if (delrev.code == 1)
    {                           /*  -o  -rev   */
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
          cuttail = 0;
          return 0;
        }
      delstrt = temp;
      return 1;
    }

  if (delrev.code == 2)
    {                           /*  -o  rev-   */
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
                return 0;
            }
          else
            temp = searchcutpt (target->num, length, gendeltas);
          getbranchno (temp->num, &numrev);     /* get branch number */
          genrevs (numrev.string, (char *) 0, (char *) 0, (char *) 0,
                   &gendeltas);
        }
      if (branchpoint (temp, cuttail))
        {
          cuttail = 0;
          return 0;
        }
      delstrt = temp;
      return 1;
    }

  /*   -o   rev1-rev2   */
  if (!expandsym (delrev.end, &numrev))
    return 0;
  if (length != countnumflds (numrev.string)
      || (length > 2 && compartial (numrev.string, target->num, length - 1)))
    {
      rcserror ("invalid revision range %s-%s", target->num, numrev.string);
      return 0;
    }

  target2 =
    genrevs (numrev.string, (char *) 0, (char *) 0, (char *) 0, &gendeltas);
  if (!target2)
    return 0;

  if (length > 2)
    {                           /* delete revisions on branches  */
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
              return 0;
            }
          cuthead = target;
          temp = target->next;
        }
      else
        temp = searchcutpt (target->num, length, gendeltas);
      cuttail = target2->next;
    }
  else
    {                           /*  delete revisions on trunk  */
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
              return 0;
            }
          cuttail = target2;
        }
      else
        cuttail = target2->next;
      temp = searchcutpt (target->num, length, gendeltas);
    }
  if (branchpoint (temp, cuttail))
    {
      cuttail = 0;
      return 0;
    }
  delstrt = temp;
  return 1;
}

static int
doassoc (void)
/* Add or delete (if !revno) association that is stored in assoclst.  */
{
  char const *p;
  int changed = false;
  struct Symrev const *curassoc;
  struct assoc **pre, *pt;

  /*  add new associations   */
  for (curassoc = assoclst; curassoc; curassoc = curassoc->nextsym)
    {
      char const *ssymbol = curassoc->ssymbol;

      if (!curassoc->revno)
        {                       /* delete symbol  */
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
              p = 0;
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

static int
dolocks (void)
/* Function: remove lock for caller or first lock if unlockcaller is set;
 *           remove locks which are stored in rmvlocklst,
 *           add new locks which are stored in newlocklst,
 *           add lock for Dbranch or Head if lockhead is set.
 */
{
  struct Lockrev const *lockpt;
  struct hshentry *target;
  int changed = false;

  if (unlockcaller)
    {                           /*  find lock for caller  */
      if (Head)
        {
          if (Locks)
            {
              switch (findlock (true, &target))
                {
                case 0:
                  /* remove most recent lock */
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

  /*  remove locks which are stored in rmvlocklst   */
  for (lockpt = rmvlocklst; lockpt; lockpt = lockpt->nextrev)
    if (expandsym (lockpt->revno, &numrev))
      {
        target =
          genrevs (numrev.string, (char *) 0, (char *) 0, (char *) 0,
                   &gendeltas);
        if (target)
          if (!(countnumflds (numrev.string) & 1)
              && cmpnum (target->num, numrev.string))
            rcserror ("can't unlock nonexisting revision %s", lockpt->revno);
          else
            changed |= breaklock (target);
        /* breaklock does its own diagnose */
      }

  /*  add new locks which stored in newlocklst  */
  for (lockpt = newlocklst; lockpt; lockpt = lockpt->nextrev)
    changed |= setlock (lockpt->revno);

  if (lockhead)                 /*  lock default branch or head  */
    if (Dbranch)
      changed |= setlock (Dbranch);
    else if (Head)
      changed |= setlock (Head->num);
    else
      rcswarn ("can't lock an empty tree");
  return changed;
}

static int
setlock (char const *rev)
/* Function: Given a revision or branch number, finds the corresponding
 * delta and locks it for caller.
 */
{
  struct hshentry *target;
  int r;

  if (expandsym (rev, &numrev))
    {
      target = genrevs (numrev.string, (char *) 0, (char *) 0,
                        (char *) 0, &gendeltas);
      if (target)
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
  return 0;
}

static int
domessages (void)
{
  struct hshentry *target;
  struct Message *p;
  int changed = false;

  for (p = messagelst; p; p = p->nextmessage)
    if (expandsym (p->revno, &numrev) &&
        (target =
         genrevs (numrev.string, (char *) 0, (char *) 0, (char *) 0,
                  &gendeltas)))
      {
        /*
         * We can't check the old log -- it's much later in the file.
         * We pessimistically assume that it changed.
         */
        target->log = p->message;
        changed = true;
      }
  return changed;
}

static int
rcs_setstate (char const *rev, char const *status)
/* Function: Given a revision or branch number, finds the corresponding delta
 * and sets its state to status.
 */
{
  struct hshentry *target;

  if (expandsym (rev, &numrev))
    {
      target = genrevs (numrev.string, (char *) 0, (char *) 0,
                        (char *) 0, &gendeltas);
      if (target)
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
  return false;
}

static int
buildeltatext (struct hshentries const *deltas)
/*   Function:  put the delta text on frewrite and make necessary   */
/*              change to delta text                                */
{
  register FILE *fcut;          /* temporary file to rebuild delta tree */
  char const *cutname;

  fcut = 0;
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
  finishedit ((struct hshentry *) 0, (FILE *) 0, true);
  Ozclose (&fcopy);

  if (fcut)
    {
      char const *diffname = maketemp (0);
      char const *diffv[6 + !!OPEN_O_BINARY];
      char const **diffp = diffv;
      *++diffp = DIFF;
      *++diffp = DIFFFLAGS;
#	    if OPEN_O_BINARY
      if (Expand == BINARY_EXPAND)
        *++diffp == "--binary";
#	    endif
      *++diffp = "-";
      *++diffp = resultname;
      *++diffp = 0;
      switch (runv (fileno (fcut), diffname, diffv))
        {
        case DIFF_FAILURE:
        case DIFF_SUCCESS:
          break;
        default:
          rcsfaterror ("diff failed");
        }
      Ofclose (fcut);
      return putdtext (cuttail, diffname, frewrite, true);
    }
  else
    return putdtext (cuttail, resultname, frewrite, false);
}

static void
buildtree (void)
/*   Function:  actually removes revisions whose selector field  */
/*		is false, and rebuilds the linkage of deltas.	 */
/*              asks for reconfirmation if deleting last revision*/
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
