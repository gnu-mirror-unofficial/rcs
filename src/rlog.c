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
#include <string.h>
#include <stdlib.h>
#include "rlog.help"
#include "b-complain.h"
#include "b-divvy.h"
#include "b-esds.h"
#include "b-fb.h"
#include "b-fro.h"

struct top *top;

struct revrange
{
  char const *beg;
  char const *end;
  int nfield;
};

struct daterange
{
  char beg[datesize];
  char end[datesize];
  bool oep;                             /* open end? */
};

/* A version-specific format string.  */
static char const *insDelFormat;
/* The ‘-b’ option.  */
static bool branchflag;
/* The ‘-l’ option.  */
static bool lockflag;

/* Date range in ‘-d’ option.  */
static struct link *datelist, *duelst;

/* Revision or branch range in ‘-r’ option.
   On the first pass (option processing), push onto ‘revlist’.
   After ‘gettree’, walk ‘revlist’ and push onto ‘Revlst’.  */
static struct link *revlist, *Revlst;

/* Login names in author option.  */
static struct link *authorlist;

/* Lockers in locker option.  */
static struct link *lockerlist;

/* States in ‘-s’ option.  */
static struct link *statelist;

static int exitstatus;

static void
cleanup (void)
{
  if (FLOW (erroneousp))
    exitstatus = EXIT_FAILURE;
  fro_zclose (&FLOW (from));
}

static exiting void
exiterr (void)
{
  _Exit (EXIT_FAILURE);
}

static void
getlocker (char *argv)
/* Get the login names of lockers from command line
   and store in ‘lockerlist’.  */
{
  register char c;
  struct link fake, *tp;

  argv--;
  while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
         || c == ';')
    continue;
  if (c == '\0')
    {
      lockerlist = NULL;
      return;
    }

  fake.next = lockerlist;
  tp = &fake;
  while (c != '\0')
    {
      tp = extend (tp, argv, SHARED);
      while ((c = *++argv) && c != ',' && c != ' ' && c != '\t' && c != '\n'
             && c != ';')
        continue;
      *argv = '\0';
      if (c == '\0')
        {
          lockerlist = fake.next;
          return;
        }
      while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
             || c == ';')
        continue;
    }
}

static void
putadelta (register struct hshentry const *node,
           register struct hshentry const *editscript,
           bool trunk)
/* Print delta ‘node’ if ‘node->selector’ is set.
   ‘editscript’ indicates where the editscript is stored;
   ‘trunk’ !false indicates this node is in trunk.  */
{
  register FILE *out;
  char const *s;
  size_t n;
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
      long a, d;

      a = trunk ? editscript->deletelns : editscript->insertlns;
      d = trunk ? editscript->insertlns : editscript->deletelns;
      aprintf (out, insDelFormat, a, d);
    }

  if (node->branches)
    {
      aputs ("\nbranches:", out);
      for (struct wlink *ls = node->branches; ls; ls = ls->next)
        {
          struct hshentry *delta = ls->entry;

          aprintf (out, "  %s;", BRANCHNO (delta->num));
        }
    }

  if (node->commitid)
    aprintf (out, "%s commitid: %s", editscript ? ";" : "", node->commitid);

  afputc ('\n', out);
  s = node->pretty_log.string;
  if (!(n = node->pretty_log.size))
    {
      s = EMPTYLOG;
      n = sizeof (EMPTYLOG) - 1;
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

  for (ptr = REPO (tip); ptr; ptr = ptr->next)
    putadelta (ptr, ptr->next, true);
}

static void putforest (struct wlink const *);

static void
putree (struct hshentry const *root)
/* Print delta tree from ‘root’ (not including trunk)
   in reverse order on each branch.  */
{
  if (!root)
    return;
  putree (root->next);
  putforest (root->branches);
}

static void
putabranch (struct hshentry const *root)
/* Print one branch from ‘root’.  */
{
  if (!root)
    return;
  putabranch (root->next);
  putadelta (root, root, false);
}

static void
putforest (struct wlink const *branchroot)
/* Print branches that have the same direct ancestor ‘branchroot’.  */
{
  if (!branchroot)
    return;
  putforest (branchroot->next);
  putabranch (branchroot->entry);
  putree (branchroot->entry);
}

static void
getscript (struct hshentry *Delta)
/* Read edit script of ‘Delta’ and count
   how many lines added and deleted in the script.  */
{
  int ed;                               /* editor command */
  register struct fro *fin;
  int c;
  register long i;
  struct diffcmd dc;

  fin = FLOW (from);
  initdiffcmd (&dc);
  while (0 <= (ed = getdiffcmd (fin, true, NULL, &dc)))
    if (!ed)
      Delta->deletelns += dc.nlines;
    else
      {
        /* Skip scripted lines.  */
        i = dc.nlines;
        Delta->insertlns += i;
        do
          {
            for (;;)
              {
                GETCHAR (c, fin);
                switch (c)
                  {
                  default:
                    continue;
                  case SDELIM:
                    GETCHAR (c, fin);
                    if (c == SDELIM)
                      continue;
                    if (--i)
                      unexpected_EOF ();
                    NEXT (c) = c;
                    return;
                  case '\n':
                    break;
                  }
                break;
              }
            ++LEX (lno);
          }
        while (--i);
      }
}

static struct hshentry const *
readdeltalog (void)
/* Get the log message and skip the text of a deltatext node.
   Return the delta found.  Assume the current lexeme is not
   yet in ‘NEXT (tok)’; do not advance ‘NEXT (tok)’.  */
{
  register struct hshentry *Delta;

  if (eoflex ())
    SYNTAX_ERROR ("missing delta log");
  Delta = must_get_delta_num ();
  getkeystring (&TINY (log));
  if (Delta->pretty_log.string)
    SYNTAX_ERROR ("duplicate delta log");
  Delta->pretty_log = savestring ();

  nextlex ();
  getkeystring (&TINY (text));
  Delta->insertlns = Delta->deletelns = 0;
  if (Delta != REPO (tip))
    getscript (Delta);
  else
    readstring ();
  return Delta;
}

static char
extractdelta (struct hshentry const *pdelta)
/* Return true if ‘pdelta’ matches the selection critera.  */
{
  struct link const *pstate;
  struct link const *pauthor;
  int length;

  /* Only certain authors wanted.  */
  if ((pauthor = authorlist))
    while (STR_DIFF (pauthor->entry, pdelta->author))
      if (!(pauthor = pauthor->next))
        return false;
  /* Only certain states wanted.  */
  if ((pstate = statelist))
    while (STR_DIFF (pstate->entry, pdelta->state))
      if (!(pstate = pstate->next))
        return false;
  /* Only locked revisions wanted.  */
  if (lockflag)
    for (struct link *ls = ADMIN (locks);; ls = ls->next)
      {
        struct rcslock const *rl;

        if (!ls)
          return false;
        rl = ls->entry;
        if (rl->delta == pdelta)
          break;
      }
  /* Only certain revs or branches wanted.  */
  for (struct link *ls = Revlst; ls;)
    {
      struct revrange const *rr = ls->entry;

      length = rr->nfield;
      if (countnumflds (pdelta->num) == length + (length & 1)
          && 0 <= compartial (pdelta->num, rr->beg, length)
          && 0 <= compartial (rr->end, pdelta->num, length))
        break;
      if (! (ls = ls->next))
        return false;
    }
  return true;
}

static void
exttree (struct hshentry *root)
/* Select revisions, starting with ‘root’.  */
{
  if (!root)
    return;

  root->selector = extractdelta (root);
  root->pretty_log.string = NULL;
  exttree (root->next);

  for (struct wlink *ls = root->branches; ls; ls = ls->next)
    exttree (ls->entry);
}

static void
getauthor (char *argv)
/* Get the author's name from command line and store in ‘authorlist’.  */
{
  register int c;
  struct link fake, *tp;

  argv--;
  while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
         || c == ';')
    continue;
  fake.next = authorlist;
  tp = &fake;
  if (c == '\0')
    {
      tp = extend (tp, getusername (false), SHARED);
      return;
    }

  while (c != '\0')
    {
      tp = extend (tp, argv, SHARED);
      while ((c = *++argv) && c != ',' && c != ' ' && c != '\t' && c != '\n'
             && c != ';')
        continue;
      *argv = '\0';
      if (c == '\0')
        {
          authorlist = fake.next;
          return;
        }
      while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
             || c == ';')
        continue;
    }
}

static void
getstate (char *argv)
/* Get the states of revisions from command line and store in ‘statelist’.  */
{
  register char c;
  struct link fake, *tp;

  argv--;
  while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
         || c == ';')
    continue;
  if (c == '\0')
    {
      PERR ("missing state attributes after -s options");
      return;
    }

  fake.next = statelist;
  tp = &fake;
  while (c != '\0')
    {
      tp = extend (tp, argv, SHARED);
      while ((c = *++argv) && c != ',' && c != ' ' && c != '\t' && c != '\n'
             && c != ';')
        continue;
      *argv = '\0';
      if (c == '\0')
        {
          statelist = fake.next;
          return;
        }
      while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
             || c == ';')
        continue;
    }
}

static void
trunclocks (void)
/* Truncate the list of locks to those that are held by the
   id's on ‘lockerlist’.  Do not truncate if ‘lockerlist’ empty.  */
{
  struct link const *plocker;
  struct link fake, *tp;

  if (!lockerlist)
    return;

  /* Shorten locks to those contained in ‘lockerlist’.  */
  for (fake.next = ADMIN (locks), tp = &fake; tp->next;)
    {
      struct rcslock const *rl = tp->next->entry;

      for (plocker = lockerlist;;)
        if (STR_SAME (plocker->entry, rl->login))
          {
            tp = tp->next;
            break;
          }
        else if (!(plocker = plocker->next))
          {
            tp->next = tp->next->next;
            ADMIN (locks) = fake.next;
            break;
          }
    }
}

static void
recentdate (struct hshentry const *root, struct daterange *r)
/* Find the delta that is closest to the cutoff date ‘pd’ among the
   revisions selected by ‘exttree’.  Successively narrow down the
   interval given by ‘pd’, and set the ‘strtdate’ of ‘pd’ to the date
   of the selected delta.  */
{
  if (!root)
    return;
  if (root->selector)
    {
      if (0 <= cmpdate (root->date, r->beg)
          && 0 >= cmpdate (root->date, r->end))
        {
          strncpy (r->beg, root->date, datesize);
          r->beg[datesize - 1] = '\0';
        }
    }

  recentdate (root->next, r);
  for (struct wlink *ls = root->branches; ls; ls = ls->next)
    recentdate (ls->entry, r);
}

static int
extdate (struct hshentry *root)
/* Select revisions which are in the date range specified in ‘duelst’
   and ‘datelist’, starting at ‘root’.  Return number of revisions
   selected, including those already selected.  */
{
  int revno;

  if (!root)
    return 0;

  if (datelist || duelst)
    {
      struct daterange const *r;
      bool oep, sel = false;

      for (struct link *ls = datelist; ls; ls = ls->next)
        {
          r = ls->entry;
          oep = r->oep;
          if ((sel = ((!r->beg[0]
                       || oep <= cmpdate (root->date, r->beg))
                      &&
                      (!r->end[0]
                       || oep <= cmpdate (r->end, root->date)))))
            break;
        }
      if (!sel)
        {
          for (struct link *ls = duelst; ls; ls = ls->next)
            {
              r = ls->entry;
              if ((sel = !cmpdate (root->date, r->beg)))
                break;
            }
          if (!sel)
            root->selector = false;
        }
    }
  revno = root->selector + extdate (root->next);

  for (struct wlink *ls = root->branches; ls; ls = ls->next)
    revno += extdate (ls->entry);
  return revno;
}

#define PUSH(x,ls)  ls = prepend (x, ls, SHARED)

static void
getdatepair (char *argv)
/* Get time range from command line and store in ‘datelist’ if
 a time range specified or in ‘duelst’ if a time spot specified.  */
{
  register char c;
  struct daterange *r;
  char const *rawdate;
  bool switchflag;

  argv--;
  while ((c = *++argv) == ',' || c == ' ' || c == '\t' || c == '\n'
         || c == ';')
    continue;
  if (c == '\0')
    {
      PERR ("missing date/time after -d");
      return;
    }

  while (c != '\0')
    {
      switchflag = false;
      r = ZLLOC (1, struct daterange);
      if (c == '<')                     /* <DATE */
        {
          c = *++argv;
          if (!(r->oep = c != '='))
            c = *++argv;
          r->beg[0] = '\0';
        }
      else if (c == '>')                /* >DATE */
        {
          c = *++argv;
          if (!(r->oep = c != '='))
            c = *++argv;
          r->end[0] = '\0';
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
          str2date (rawdate, switchflag ? r->end : r->beg);
          if (c == ';' || c == '\0')    /* DATE */
            {
              strncpy (r->end, r->beg, datesize);
              PUSH (r, duelst);
              goto end;
            }
          else                   /* DATE< or DATE> (see ‘switchflag’) */
            {
              bool eq = argv[1] == '=';

              r->oep = !eq;
              argv += eq;
              while ((c = *++argv) == ' ' || c == '\t' || c == '\n')
                continue;
              if (c == ';' || c == '\0')
                {
                  /* Second date missing.  */
                  (switchflag ? r->beg : r->end)[0] = '\0';
                  PUSH (r, datelist);
                  goto end;
                }
            }
        }
      rawdate = argv;
      while (c != '>' && c != '<' && c != ';' && c != '\0')
        c = *++argv;
      *argv = '\0';
      str2date (rawdate, switchflag ? r->beg : r->end);
      PUSH (r, datelist);
    end:
      if (BE (version) < VERSION (5))
        r->oep = false;
      if (c == '\0')
        return;
      while ((c = *++argv) == ';' || c == ' ' || c == '\t' || c == '\n')
        continue;
    }
}

static bool
checkrevpair (char const *num1, char const *num2)
/* Check whether ‘num1’, ‘num2’ are a legal pair, i.e.
   only the last field differs and have same number of
   fields (if length <= 2, may be different if first field).  */
{
  int length = countnumflds (num1);

  if (countnumflds (num2) != length
      || (2 < length && compartial (num1, num2, length - 1) != 0))
    {
      RERR ("invalid branch or revision pair %s : %s", num1, num2);
      return false;
    }
  return true;
}

#define KSTRCPY(to,kstr)  strncpy (to, kstr, sizeof kstr)

static bool
getnumericrev (void)
/* Get the numeric name of revisions stored in ‘revlist’; store
   them in ‘Revlst’.  If ‘branchflag’, also add default branch.  */
{
  struct link *ls;
  struct revrange *rr;
  int n;
  struct cbuf s, e;
  char const *lrev;
  struct cbuf const *rstart, *rend;

  Revlst = NULL;
  for (ls = revlist; ls; ls = ls->next)
    {
      struct revrange const *from = ls->entry;

      n = 0;
      rstart = &s;
      rend = &e;

      switch (from->nfield)
        {

        case 1:                         /* -rREV */
          if (!fully_numeric_no_k (&s, from->beg))
            goto freebufs;
          rend = &s;
          n = countnumflds (s.string);
          if (!n && (lrev = tiprev ()))
            {
              s.string = lrev;
              n = countnumflds (lrev);
            }
          break;

        case 2:                         /* -rREV: */
          if (!fully_numeric_no_k (&s, from->beg))
            goto freebufs;
          if (2 > (n = countnumflds (s.string)))
            e.string = "";
          else
            {
              accumulate_range (SHARED, s.string, strrchr (s.string, '.'));
              e.string = finish_string (SHARED, &e.size);
            }
          break;

        case 3:                         /* -r:REV */
          if (!fully_numeric_no_k (&e, from->end))
            goto freebufs;
          if ((n = countnumflds (e.string)) < 2)
            s.string = ".0";
          else
            {
              accumulate_range (SHARED, e.string, strrchr (e.string, '.'));
              accf (SHARED, ".0");
              s.string = finish_string (SHARED, &s.size);
            }
          break;

        default:                        /* -rREV1:REV2 */
          if (!(fully_numeric_no_k (&s, from->beg)
                && fully_numeric_no_k (&e, from->end)
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
          rr = FALLOC (struct revrange);
          rr->nfield = n;
          rr->beg = rstart->string;
          rr->end = rend->string;
          PUSH (rr, Revlst);
        }
    }
  /* Now take care of ‘branchflag’.  */
  if (branchflag && (ADMIN (defbr) || REPO (tip)))
    {
      rr = FALLOC (struct revrange);
      rr->beg = rr->end = ADMIN (defbr)
        ? ADMIN (defbr)
        : TAKE (1, REPO (tip)->num);
      rr->nfield = countnumflds (rr->beg);
      PUSH (rr, Revlst);
    }

freebufs:
  return !ls;
}

static void
putrevpairs (char const *b, char const *e, bool sawsep)
/* Store a revision or branch range into ‘revlist’.  */
{
  struct revrange *rr = ZLLOC (1, struct revrange);

  rr->beg = b;
  rr->end = e;
  rr->nfield = (!sawsep
                ? 1                     /* -rREV */
                : (!e[0]
                   ? 2                  /* -rREV: */
                   : (!b[0]
                      ? 3               /* -r:REV */
                      : 4)));           /* -rREV1:REV2 */
  PUSH (rr, revlist);
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
  FILE *out;
  char *a, **newargv;
  char const *accessListString, *accessFormat;
  char const *headFormat, *symbolFormat;
  struct link const *curaccess;
  struct hshentry const *delta;
  bool descflag, selectflag;
  bool onlylockflag;                   /* print only files with locks */
  bool onlyRCSflag;                    /* print only RCS filename */
  bool pre5;
  bool shownames;
  int revno;

  CHECK_HV ();
  gnurcs_init ();

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
          parse_revpairs ('r', a, putrevpairs);
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
          bad_option (*argv);
        };
    }
  /* (End of option processing.)  */

  if (!(descflag | selectflag))
    {
      PWARN ("-t overrides -h.");
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

  /* Now handle all filenames.  */
  if (FLOW (erroneousp))
    cleanup ();
  else if (argc < 1)
    PFATAL ("no input file");
  else
    for (; 0 < argc; cleanup (), ++argv, --argc)
      {
        ffree ();

        if (pairnames (argc, argv, rcsreadopen, true, false) <= 0)
          continue;

        /* ‘REPO (filename)’ contains the name of the RCS file,
           and ‘FLOW (from)’ the file descriptor;
           ‘MANI (filename)’ contains the name of the working file.  */

        /* Keep only those locks given by ‘-l’.  */
        if (lockflag)
          trunclocks ();

        /* Do nothing if ‘-L’ is given and there are no locks.  */
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

        /* Print RCS filename, working filename and optional
           administrative information.  Could use ‘getfullRCSname’
           here, but that is very slow.  */
        aprintf (out, headFormat, REPO (filename), MANI (filename),
                 REPO (tip) ? " " : "",
                 REPO (tip) ? REPO (tip)->num : "",
                 ADMIN (defbr) ? " " : "", ADMIN (defbr) ? ADMIN (defbr) : "",
                 BE (strictly_locking) ? " strict" : "");
        for (struct link *ls = ADMIN (locks); ls; ls = ls->next)
          {
            struct rcslock const *rl = ls->entry;

            aprintf (out, symbolFormat, rl->login, rl->delta->num);
          }
        if (BE (strictly_locking) && pre5)
          aputs ("  ;  strict" + (ADMIN (locks) ? 3 : 0), out);

        /* Print access list.  */
        aputs (accessListString, out);
        curaccess = ADMIN (allowed);
        while (curaccess)
          {
            aprintf (out, accessFormat, curaccess->entry);
            curaccess = curaccess->next;
          }

        if (shownames)
          {
            /* Print symbolic names.  */
            aputs ("\nsymbolic names:", out);
            format_assocs (out, symbolFormat);
          }
        if (pre5)
          {
            aputs ("\ncomment leader:  \"", out);
            awrite (REPO (log_lead).string, REPO (log_lead).size, out);
            afputc ('\"', out);
          }
        if (!pre5 || BE (kws) != kwsub_kv)
          aprintf (out, "\nkeyword substitution: %s", kwsub_string (BE (kws)));

        aprintf (out, "\ntotal revisions: %d", REPO (ndelt));

        revno = 0;

        if (REPO (tip) && selectflag & descflag)
          {
            exttree (REPO (tip));

            /* Get most recently date of the dates pointed by ‘duelst’.  */
            for (struct link *ls = duelst; ls; ls = ls->next)
              {
                struct daterange const *incomplete = ls->entry;
                struct daterange *r = ZLLOC (1, struct daterange);

                /* Couldn't this have been done before?  --ttn  */
                *r = *incomplete;
                KSTRCPY (r->beg, "0.0.0.0.0.0");
                ls->entry = r;
                recentdate (REPO (tip), r);
              }

            revno = extdate (REPO (tip));

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
              /* Read through ‘delta->next’ to get its ‘insertlns’.  */
              while (readdeltalog () != delta->next)
                continue;
            putrunk ();
            putree (REPO (tip));
          }
        aputs (equal_line, out);
      }
  Ozclose (&out);
  return exitstatus;
}

/* rlog.c ends here */
