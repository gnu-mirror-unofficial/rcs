/* Print log messages and other information about RCS files.

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
#include "rlog.help"

struct rcslockers
{
  char const *login;
  struct rcslockers *lockerlink;
};

struct stateattri
{
  char const *status;
  struct stateattri *nextstate;
};

struct authors
{
  char const *login;
  struct authors *nextauthor;
};

struct Revpairs
{
  int numfld;
  char const *strtrev;
  char const *endrev;
  struct Revpairs *rnext;
};

struct Datepairs
{
  struct Datepairs *dnext;
  char strtdate[datesize];
  char enddate[datesize];
  char ne_date;                         /* distinguish < from <= */
};

/* A version-specific format string.  */
static char const *insDelFormat;
/* The `-b' option.  */
static bool branchflag;
/* The `-l' option.  */
static bool lockflag;

/* Date range in `-d' option.  */
static struct Datepairs *datelist, *duelst;

/* Revision or branch range in `-r' option.  */
static struct Revpairs *revlist, *Revlst;

/* Login names in author option.  */
static struct authors *authorlist;

/* Lockers in locker option.  */
static struct rcslockers *lockerlist;

/* States in `-s' option.  */
static struct stateattri *statelist;

static int exitstatus;

static void
cleanup (void)
{
  if (LEX (nerr))
    exitstatus = EXIT_FAILURE;
  Izclose (&FLOW (from));
}

static exiting void
exiterr (void)
{
  _Exit (EXIT_FAILURE);
}

static void
getlocker (char *argv)
/* Get the login names of lockers from command line
   and store in `lockerlist'.  */
{
  register char c;
  struct rcslockers *newlocker;

  argv--;
  while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
         || c == ';')
    continue;
  if (c == '\0')
    {
      lockerlist = NULL;
      return;
    }

  while (c != '\0')
    {
      newlocker = talloc (struct rcslockers);
      newlocker->lockerlink = lockerlist;
      newlocker->login = argv;
      lockerlist = newlocker;
      while ((c = *++argv) && c != ',' && c != ' ' && c != '\t' && c != '\n'
             && c != ';')
        continue;
      *argv = '\0';
      if (c == '\0')
        return;
      while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
             || c == ';')
        continue;
    }
}

static void
putadelta (register struct hshentry const *node,
           register struct hshentry const *editscript,
           bool trunk)
/* Print delta `node' if `node->selector' is set.
   `editscript' indicates where the editscript is stored;
   `trunk' !false indicates this node is in trunk.  */
{
  static char emptych[] = EMPTYLOG;
  register FILE *out;
  char const *s;
  size_t n;
  struct branchhead const *newbranch;
  struct buf branchnum;
  char datebuf[datesize + zonelenmax];
  bool pre5 = BE (version) < VERSION (5);

  if (!node->selector)
    return;

  out = stdout;
  aprintf (out, "----------------------------\nrevision %s%s",
           node->num, pre5 ? "        " : "");
  if (node->lockedby)
    aprintf (out, pre5 + "\tlocked by: %s;", node->lockedby);

  aprintf (out, "\ndate: %s;  author: %s;  state: %s;",
           date2str (node->date, datebuf), node->author, node->state);

  if (editscript)
    {
      if (trunk)
        aprintf (out, insDelFormat,
                 editscript->deletelns, editscript->insertlns);
      else
        aprintf (out, insDelFormat,
                 editscript->insertlns, editscript->deletelns);
    }

  newbranch = node->branches;
  if (newbranch)
    {
      bufautobegin (&branchnum);
      aputs ("\nbranches:", out);
      while (newbranch)
        {
          getbranchno (newbranch->hsh->num, &branchnum);
          aprintf (out, "  %s;", branchnum.string);
          newbranch = newbranch->nextbranch;
        }
      bufautoend (&branchnum);
    }

  afputc ('\n', out);
  s = node->log.string;
  if (!(n = node->log.size))
    {
      s = emptych;
      n = sizeof (emptych) - 1;
    }
  awrite (s, n, out);
  if (s[n - 1] != '\n')
    afputc ('\n', out);
}

static void
putrunk (void)
/* Print revisions chosen, which are in trunk.  */
{
  register struct hshentry const *ptr;

  for (ptr = ADMIN (head); ptr; ptr = ptr->next)
    putadelta (ptr, ptr->next, true);
}

static void putforest (struct branchhead const *);

static void
putree (struct hshentry const *root)
/* Print delta tree from `root' (not including trunk)
   in reverse order on each branch.  */
{
  if (!root)
    return;
  putree (root->next);
  putforest (root->branches);
}

static void
putabranch (struct hshentry const *root)
/* Print one branch from `root'.  */
{
  if (!root)
    return;
  putabranch (root->next);
  putadelta (root, root, false);
}

static void
putforest (struct branchhead const *branchroot)
/* Print branches that have the same direct ancestor `branchroot'.  */
{
  if (!branchroot)
    return;
  putforest (branchroot->nextbranch);
  putabranch (branchroot->hsh);
  putree (branchroot->hsh);
}

static void
getscript (struct hshentry *Delta)
/* Read edit script of `Delta' and count
   how many lines added and deleted in the script.  */
{
  int ed;                               /* editor command */
  declarecache;
  register RILE *fin;
  register int c;
  register long i;
  struct diffcmd dc;

  fin = FLOW (from);
  setupcache (fin);
  initdiffcmd (&dc);
  while (0 <= (ed = getdiffcmd (fin, true, NULL, &dc)))
    if (!ed)
      Delta->deletelns += dc.nlines;
    else
      {
        /* Skip scripted lines.  */
        i = dc.nlines;
        Delta->insertlns += i;
        cache (fin);
        do
          {
            for (;;)
              {
                cacheget (c);
                switch (c)
                  {
                  default:
                    continue;
                  case SDELIM:
                    cacheget (c);
                    if (c == SDELIM)
                      continue;
                    if (--i)
                      unexpected_EOF ();
                    NEXT (c) = c;
                    uncache (fin);
                    return;
                  case '\n':
                    break;
                  }
                break;
              }
            ++LEX (lno);
          }
        while (--i);
        uncache (fin);
      }
}

static struct hshentry const *
readdeltalog (void)
/* Get the log message and skip the text of a deltatext node.
   Return the delta found.  Assume the current lexeme is not
   yet in `NEXT (tok)'; do not advance `NEXT (tok)'.  */
{
  register struct hshentry *Delta;
  struct buf logbuf;
  struct cbuf cb;

  if (eoflex ())
    fatserror ("missing delta log");
  nextlex ();
  if (!(Delta = getnum ()))
    fatserror ("delta number corrupted");
  getkeystring (Klog);
  if (Delta->log.string)
    fatserror ("duplicate delta log");
  bufautobegin (&logbuf);
  cb = savestring (&logbuf);
  Delta->log = bufremember (&logbuf, cb.size);

  ignorephrases (Ktext);
  getkeystring (Ktext);
  Delta->insertlns = Delta->deletelns = 0;
  if (Delta != ADMIN (head))
    getscript (Delta);
  else
    readstring ();
  return Delta;
}

static char
extractdelta (struct hshentry const *pdelta)
/* Return true if `pdelta' matches the selection critera.  */
{
  struct rcslock const *plock;
  struct stateattri const *pstate;
  struct authors const *pauthor;
  struct Revpairs const *prevision;
  int length;

  /* Only certain authors wanted.  */
  if ((pauthor = authorlist))
    while (strcmp (pauthor->login, pdelta->author) != 0)
      if (!(pauthor = pauthor->nextauthor))
        return false;
  /* Only certain states wanted.  */
  if ((pstate = statelist))
    while (strcmp (pstate->status, pdelta->state) != 0)
      if (!(pstate = pstate->nextstate))
        return false;
  /* Only locked revisions wanted.  */
  if (lockflag)
    for (plock = ADMIN (locks);; plock = plock->nextlock)
      if (!plock)
        return false;
      else if (plock->delta == pdelta)
        break;
  /* Only certain revs or branches wanted.  */
  if ((prevision = Revlst))
    for (;;)
      {
        length = prevision->numfld;
        if (countnumflds (pdelta->num) == length + (length & 1)
            && 0 <= compartial (pdelta->num, prevision->strtrev, length)
            && 0 <= compartial (prevision->endrev, pdelta->num, length))
          break;
        if (!(prevision = prevision->rnext))
          return false;
      }
  return true;
}

static void
exttree (struct hshentry *root)
/* Select revisions, starting with `root'.  */
{
  struct branchhead const *newbranch;

  if (!root)
    return;

  root->selector = extractdelta (root);
  root->log.string = NULL;
  exttree (root->next);

  newbranch = root->branches;
  while (newbranch)
    {
      exttree (newbranch->hsh);
      newbranch = newbranch->nextbranch;
    }
}

static void
getauthor (char *argv)
/* Get the author's name from command line and store in `authorlist'.  */
{
  register int c;
  struct authors *newauthor;

  argv--;
  while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
         || c == ';')
    continue;
  if (c == '\0')
    {
      authorlist = talloc (struct authors);
      authorlist->login = getusername (false);
      authorlist->nextauthor = NULL;
      return;
    }

  while (c != '\0')
    {
      newauthor = talloc (struct authors);
      newauthor->nextauthor = authorlist;
      newauthor->login = argv;
      authorlist = newauthor;
      while ((c = *++argv) && c != ',' && c != ' ' && c != '\t' && c != '\n'
             && c != ';')
        continue;
      *argv = '\0';
      if (c == '\0')
        return;
      while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
             || c == ';')
        continue;
    }
}

static void
getstate (char *argv)
/* Get the states of revisions from command line and store in `statelist'.  */
{
  register char c;
  struct stateattri *newstate;

  argv--;
  while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
         || c == ';')
    continue;
  if (c == '\0')
    {
      error ("missing state attributes after -s options");
      return;
    }

  while (c != '\0')
    {
      newstate = talloc (struct stateattri);
      newstate->nextstate = statelist;
      newstate->status = argv;
      statelist = newstate;
      while ((c = *++argv) && c != ',' && c != ' ' && c != '\t' && c != '\n'
             && c != ';')
        continue;
      *argv = '\0';
      if (c == '\0')
        return;
      while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
             || c == ';')
        continue;
    }
}

static void
trunclocks (void)
/* Truncate the list of locks to those that are held by the
   id's on` lockerlist'.  Do not truncate if `lockerlist' empty.  */
{
  struct rcslockers const *plocker;
  struct rcslock *p, **pp;

  if (!lockerlist)
    return;

  /* Shorten locks to those contained in `lockerlist'.  */
  for (pp = &ADMIN (locks); (p = *pp);)
    for (plocker = lockerlist;;)
      if (strcmp (plocker->login, p->login) == 0)
        {
          pp = &p->nextlock;
          break;
        }
      else if (!(plocker = plocker->lockerlink))
        {
          *pp = p->nextlock;
          break;
        }
}

static void
recentdate (struct hshentry const *root, struct Datepairs *pd)
/* Find the delta that is closest to the cutoff date `pd' among the
   revisions selected by `exttree'.  Successively narrow down the
   interval given by `pd', and set the `strtdate' of `pd' to the date
   of the selected delta.  */
{
  struct branchhead const *newbranch;

  if (!root)
    return;
  if (root->selector)
    {
      if (cmpdate (root->date, pd->strtdate) >= 0
          && cmpdate (root->date, pd->enddate) <= 0)
        {
          strncpy (pd->strtdate, root->date, datesize);
          pd->strtdate[datesize - 1] = '\0';
        }
    }

  recentdate (root->next, pd);
  newbranch = root->branches;
  while (newbranch)
    {
      recentdate (newbranch->hsh, pd);
      newbranch = newbranch->nextbranch;
    }
}

static int
extdate (struct hshentry *root)
/* Select revisions which are in the date range specified in `duelst'
   and `datelist', starting at `root'.  Return number of revisions
   selected, including those already selected.  */
{
  struct branchhead const *newbranch;
  struct Datepairs const *pdate;
  int revno, ne;

  if (!root)
    return 0;

  if (datelist || duelst)
    {
      pdate = datelist;
      while (pdate)
        {
          ne = pdate->ne_date;
          if ((!pdate->strtdate[0]
               || ne <= cmpdate (root->date, pdate->strtdate))
              &&
              (!pdate->enddate[0]
               || ne <= cmpdate (pdate->enddate, root->date)))
            break;
          pdate = pdate->dnext;
        }
      if (!pdate)
        {
          pdate = duelst;
          for (;;)
            {
              if (!pdate)
                {
                  root->selector = false;
                  break;
                }
              if (cmpdate (root->date, pdate->strtdate) == 0)
                break;
              pdate = pdate->dnext;
            }
        }
    }
  revno = root->selector + extdate (root->next);

  newbranch = root->branches;
  while (newbranch)
    {
      revno += extdate (newbranch->hsh);
      newbranch = newbranch->nextbranch;
    }
  return revno;
}

static void
getdatepair (char *argv)
/* Get time range from command line and store in `datelist' if
 a time range specified or in `duelst' if a time spot specified.  */
{
  register char c;
  struct Datepairs *nextdate;
  char const *rawdate;
  bool switchflag;

  argv--;
  while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
         || c == ';')
    continue;
  if (c == '\0')
    {
      error ("missing date/time after -d");
      return;
    }

  while (c != '\0')
    {
      switchflag = false;
      nextdate = talloc (struct Datepairs);
      if (c == '<')                     /* <DATE */
        {
          c = *++argv;
          if (!(nextdate->ne_date = c != '='))
            c = *++argv;
          (nextdate->strtdate)[0] = '\0';
        }
      else if (c == '>')                /* >DATE */
        {
          c = *++argv;
          if (!(nextdate->ne_date = c != '='))
            c = *++argv;
          (nextdate->enddate)[0] = '\0';
          switchflag = true;
        }
      else
        {
          rawdate = argv;
          while (c != '<' && c != '>' && c != ';' && c != '\0')
            c = *++argv;
          *argv = '\0';
          if (c == '>')
            switchflag = true;
          str2date (rawdate,
                    switchflag ? nextdate->enddate : nextdate->strtdate);
          if (c == ';' || c == '\0')    /* DATE */
            {
              strncpy (nextdate->enddate, nextdate->strtdate, datesize);
              nextdate->dnext = duelst;
              duelst = nextdate;
              goto end;
            }
          else                   /* DATE< or DATE> (see `switchflag') */
            {
              bool eq = argv[1] == '=';

              nextdate->ne_date = !eq;
              argv += eq;
              while ((c = *++argv) == ' ' || c == '\t' || c == '\n')
                continue;
              if (c == ';' || c == '\0')
                {
                  /* Second date missing.  */
                  if (switchflag)
                    *nextdate->strtdate = '\0';
                  else
                    *nextdate->enddate = '\0';
                  nextdate->dnext = datelist;
                  datelist = nextdate;
                  goto end;
                }
            }
        }
      rawdate = argv;
      while (c != '>' && c != '<' && c != ';' && c != '\0')
        c = *++argv;
      *argv = '\0';
      str2date (rawdate, switchflag ? nextdate->strtdate : nextdate->enddate);
      nextdate->dnext = datelist;
      datelist = nextdate;
    end:
      if (BE (version) < VERSION (5))
        nextdate->ne_date = 0;
      if (c == '\0')
        return;
      while ((c = *++argv) == ';' || c == ' ' || c == '\t' || c == '\n')
        continue;
    }
}

static bool
checkrevpair (char const *num1, char const *num2)
/* Check whether `num1', `num2' are a legal pair, i.e.
   only the last field differs and have same number of
   fields (if length <= 2, may be different if first field).  */
{
  int length = countnumflds (num1);

  if (countnumflds (num2) != length
      || (2 < length && compartial (num1, num2, length - 1) != 0))
    {
      rcserror ("invalid branch or revision pair %s : %s", num1, num2);
      return false;
    }
  return true;
}

#define KSTRCPY(to,kstr)  strncpy (to, kstr, sizeof kstr)

static bool
getnumericrev (void)
/* Get the numeric name of revisions stored in `revlist'; store
   them in `Revlst'.  If `branchflag', also add default branch.  */
{
  struct Revpairs *ptr, *pt;
  int n;
  struct buf s, e;
  char const *lrev;
  struct buf const *rstart, *rend;

  Revlst = NULL;
  ptr = revlist;
  bufautobegin (&s);
  bufautobegin (&e);
  while (ptr)
    {
      n = 0;
      rstart = &s;
      rend = &e;

      switch (ptr->numfld)
        {

        case 1:                         /* -rREV */
          if (!expandsym (ptr->strtrev, &s))
            goto freebufs;
          rend = &s;
          n = countnumflds (s.string);
          if (!n && (lrev = tiprev ()))
            {
              bufscpy (&s, lrev);
              n = countnumflds (lrev);
            }
          break;

        case 2:                         /* -rREV: */
          if (!expandsym (ptr->strtrev, &s))
            goto freebufs;
          bufscpy (&e, s.string);
          n = countnumflds (s.string);
          (n < 2 ? e.string : strrchr (e.string, '.'))[0] = 0;
          break;

        case 3:                         /* -r:REV */
          if (!expandsym (ptr->endrev, &e))
            goto freebufs;
          if ((n = countnumflds (e.string)) < 2)
            bufscpy (&s, ".0");
          else
            {
              bufscpy (&s, e.string);
              KSTRCPY (strrchr (s.string, '.'), ".0");
            }
          break;

        default:                        /* -rREV1:REV2 */
          if (!(expandsym (ptr->strtrev, &s)
                && expandsym (ptr->endrev, &e)
                && checkrevpair (s.string, e.string)))
            goto freebufs;
          n = countnumflds (s.string);
          /* Swap if out of order.  */
          if (compartial (s.string, e.string, n) > 0)
            {
              rstart = &e;
              rend = &s;
            }
          break;
        }

      if (n)
        {
          pt = ftalloc (struct Revpairs);
          pt->numfld = n;
          pt->strtrev = fbuf_save (rstart);
          pt->endrev = fbuf_save (rend);
          pt->rnext = Revlst;
          Revlst = pt;
        }
      ptr = ptr->rnext;
    }
  /* Now take care of `branchflag'.  */
  if (branchflag && (ADMIN (defbr) || ADMIN (head)))
    {
      pt = ftalloc (struct Revpairs);
      pt->strtrev = pt->endrev = ADMIN (defbr) ? ADMIN (defbr)
        : (partialno (&s, ADMIN (head)->num, 1),
           fbuf_save (&s),
           s.string);
      pt->rnext = Revlst;
      Revlst = pt;
      pt->numfld = countnumflds (pt->strtrev);
    }

freebufs:
  bufautoend (&s);
  bufautoend (&e);
  return !ptr;
}

static void
getrevpairs (register char *argv)
/* Get revision or branch range from command line; store in `revlist'.  */
{
  register char c;
  struct Revpairs *nextrevpair;
  int separator;

  c = *argv;

  /* Support old ambiguous '-' syntax; this will go away.  */
  if (strchr (argv, ':'))
    separator = ':';
  else
    {
      if (strchr (argv, '-') && VERSION (5) <= BE (version))
        warn ("`-' is obsolete in `-r%s'; use `:' instead", argv);
      separator = '-';
    }

  for (;;)
    {
      while (c == ' ' || c == '\t' || c == '\n')
        c = *++argv;
      nextrevpair = talloc (struct Revpairs);
      nextrevpair->rnext = revlist;
      revlist = nextrevpair;
      nextrevpair->numfld = 1;
      nextrevpair->strtrev = argv;
      for (;; c = *++argv)
        {
          switch (c)
            {
            default:
              continue;
            case '\0':
            case ' ':
            case '\t':
            case '\n':
            case ',':
            case ';':
              break;
            case ':':
            case '-':
              if (c == separator)
                break;
              continue;
            }
          break;
        }
      *argv = '\0';
      while (c == ' ' || c == '\t' || c == '\n')
        c = *++argv;
      if (c == separator)
        {
          while ((c = *++argv) == ' ' || c == '\t' || c == '\n')
            continue;
          nextrevpair->endrev = argv;
          for (;; c = *++argv)
            {
              switch (c)
                {
                default:
                  continue;
                case '\0':
                case ' ':
                case '\t':
                case '\n':
                case ',':
                case ';':
                  break;
                case ':':
                case '-':
                  if (c == separator)
                    break;
                  continue;
                }
              break;
            }
          *argv = '\0';
          while (c == ' ' || c == '\t' || c == '\n')
            c = *++argv;
          nextrevpair->numfld =
            (!nextrevpair->endrev[0]
             ? 2                        /* -rREV: */
             : (!nextrevpair->strtrev[0]
                ? 3                     /* -r:REV */
                : 4));                  /* -rREV1:REV2 */
        }
      if (!c)
        break;
      else if (c == ',' || c == ';')
        c = *++argv;
      else
        error ("missing `,' near `%c%s'", c, argv + 1);
    }
}

/*:help
[options] file ...
Options:
  -L            Ignore RCS files with no locks set.
  -R            Print only the name of the RCS file.
  -h            Print only the "header" information.
  -t            Like -h, but also include the description.
  -N            Omit symbolic names.
  -b            Select the default branch.
  -dDATES       Select revisions in the range DATES, with spec:
                  D      -- single revision D or earlier
                  D1<D2  -- between D1 and D2, exclusive
                  D2>D1  -- likewise
                  <D, D> -- before D
                  >D, D< -- after D
                Use <= or >= to make ranges inclusive; DATES
                may also be a list of semicolon-separated specs.
  -l[WHO]       Select revisions locked by WHO (comma-separated list)
                only, or by anyone if WHO is omitted.
  -r[REVS]      Select revisions in REVS, a comma-separated list of
                range specs, one of: REV, REV:, :REV, REV1:REV2
  -sSTATES      Select revisions with state in STATES (comma-separated list).
  -w[WHO]       Select revisions commited by WHO (comma-separated list),
                or by the user if WHO is omitted.
  -V            Like --version.
  -VN           Emulate RCS version N.
  -xSUFF        Specify SUFF as a slash-separated list of suffixes
                used to identify RCS file names.
  -zZONE        Specify date output format in keyword-substitution.
  -q            No effect, included for consistency with other commands.
*/

const struct program program =
  {
    .name = "rlog",
    .help = help,
    .exiterr = exiterr
  };

int
main (int argc, char **argv)
{
  register FILE *out;
  char *a, **newargv;
  struct Datepairs *currdate;
  char const *accessListString, *accessFormat;
  char const *headFormat, *symbolFormat;
  struct access const *curaccess;
  struct assoc const *curassoc;
  struct hshentry const *delta;
  struct rcslock const *currlock;
  bool descflag, selectflag;
  bool onlylockflag;                   /* print only files with locks */
  bool onlyRCSflag;                    /* print only RCS pathname */
  bool pre5;
  bool shownames;
  int revno;

  CHECK_HV ();

  descflag = selectflag = shownames = true;
  onlylockflag = onlyRCSflag = false;
  out = stdout;
  BE (pe) = X_DEFAULT;

  argc = getRCSINIT (argc, argv, &newargv);
  argv = newargv;
  while (a = *++argv, 0 < --argc && *a++ == '-')
    {
      switch (*a++)
        {

        case 'L':
          onlylockflag = true;
          break;

        case 'N':
          shownames = false;
          break;

        case 'R':
          onlyRCSflag = true;
          break;

        case 'l':
          lockflag = true;
          getlocker (a);
          break;

        case 'b':
          branchflag = true;
          break;

        case 'r':
          getrevpairs (a);
          break;

        case 'd':
          getdatepair (a);
          break;

        case 's':
          getstate (a);
          break;

        case 'w':
          getauthor (a);
          break;

        case 'h':
          descflag = false;
          break;

        case 't':
          selectflag = false;
          break;

        case 'q':
          /* This has no effect; it's here for consistency.  */
          BE (quiet) = true;
          break;

        case 'x':
          BE (pe) = a;
          break;

        case 'z':
          zone_set (a);
          break;

        case 'T':
          /* Ignore -T, so that RCSINIT can contain -T.  */
          if (*a)
            goto unknown;
          break;

        case 'V':
          setRCSversion (*argv);
          break;

        default:
        unknown:
          error ("unknown option: %s", *argv);

        };
    }
  /* (End of option processing.)  */

  if (!(descflag | selectflag))
    {
      warn ("-t overrides -h.");
      descflag = true;
    }

  pre5 = BE (version) < VERSION (5);
  if (pre5)
    {
      accessListString = "\naccess list:   ";
      accessFormat = "  %s";
      headFormat =
        "\nRCS file:        %s;   Working file:    %s\nhead:           %s%s\nbranch:         %s%s\nlocks:         ";
      insDelFormat = "  lines added/del: %ld/%ld";
      symbolFormat = "  %s: %s;";
    }
  else
    {
      accessListString = "\naccess list:";
      accessFormat = "\n\t%s";
      headFormat =
        "\nRCS file: %s\nWorking file: %s\nhead:%s%s\nbranch:%s%s\nlocks:%s";
      insDelFormat = "  lines: +%ld -%ld";
      symbolFormat = "\n\t%s: %s";
    }

  /* Now handle all pathnames.  */
  if (LEX (nerr))
    cleanup ();
  else if (argc < 1)
    faterror ("no input file");
  else
    for (; 0 < argc; cleanup (), ++argv, --argc)
      {
        ffree ();

        if (pairnames (argc, argv, rcsreadopen, true, false) <= 0)
          continue;

        /* `REPO (filename)' contains the name of the RCS file,
           and `FLOW (from)' the file descriptor;
           `MANI (filename)' contains the name of the working file.  */

        /* Keep only those locks given by `-l'.  */
        if (lockflag)
          trunclocks ();

        /* Do nothing if `-L' is given and there are no locks.  */
        if (onlylockflag && !ADMIN (locks))
          continue;

        if (onlyRCSflag)
          {
            aprintf (out, "%s\n", REPO (filename));
            continue;
          }

        gettree ();

        if (!getnumericrev ())
          continue;

        /* Print RCS pathname, working pathname and optional
           administrative information.  Could use `getfullRCSname'
           here, but that is very slow.  */
        aprintf (out, headFormat, REPO (filename), MANI (filename),
                 ADMIN (head) ? " " : "",
                 ADMIN (head) ? ADMIN (head)->num : "",
                 ADMIN (defbr) ? " " : "", ADMIN (defbr) ? ADMIN (defbr) : "",
                 BE (strictly_locking) ? " strict" : "");
        currlock = ADMIN (locks);
        while (currlock)
          {
            aprintf (out, symbolFormat, currlock->login,
                     currlock->delta->num);
            currlock = currlock->nextlock;
          }
        if (BE (strictly_locking) && pre5)
          aputs ("  ;  strict" + (ADMIN (locks) ? 3 : 0), out);

        /* Print access list.  */
        aputs (accessListString, out);
        curaccess = ADMIN (allowed);
        while (curaccess)
          {
            aprintf (out, accessFormat, curaccess->login);
            curaccess = curaccess->nextaccess;
          }

        if (shownames)
          {
            /* Print symbolic names.  */
            aputs ("\nsymbolic names:", out);
            for (curassoc = ADMIN (assocs);
                 curassoc;
                 curassoc = curassoc->nextassoc)
              aprintf (out, symbolFormat, curassoc->symbol, curassoc->num);
          }
        if (pre5)
          {
            aputs ("\ncomment leader:  \"", out);
            awrite (ADMIN (log_lead).string, ADMIN (log_lead).size, out);
            afputc ('\"', out);
          }
        if (!pre5 || BE (kws) != kwsub_kv)
          aprintf (out, "\nkeyword substitution: %s", kwsub_string (BE (kws)));

        aprintf (out, "\ntotal revisions: %d", REPO (ndelt));

        revno = 0;

        if (ADMIN (head) && selectflag & descflag)
          {

            exttree (ADMIN (head));

            /* Get most recently date of the dates pointed by `duelst'.  */
            currdate = duelst;
            while (currdate)
              {
                KSTRCPY (currdate->strtdate, "0.0.0.0.0.0");
                recentdate (ADMIN (head), currdate);
                currdate = currdate->dnext;
              }

            revno = extdate (ADMIN (head));

            aprintf (out, ";\tselected revisions: %d", revno);
          }

        afputc ('\n', out);
        if (descflag)
          {
            aputs ("description:\n", out);
            getdesc (true);
          }
        if (revno)
          {
            while (!(delta = readdeltalog ())->selector || --revno)
              continue;
            if (delta->next && countnumflds (delta->num) == 2)
              /* Read through `delta->next' to get its `insertlns'.  */
              while (readdeltalog () != delta->next)
                continue;
            putrunk ();
            putree (ADMIN (head));
          }
        aputs (equal_line, out);
      }
  Ofclose (out);
  return exitstatus;
}

/* rlog.c ends here */
