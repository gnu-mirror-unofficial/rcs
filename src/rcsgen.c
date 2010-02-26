/* Generate RCS revisions.

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

int interactiveflag;            /* Should we act as if stdin is a tty?  */
struct buf curlogbuf;           /* buffer for current log message */

enum stringwork
{ enter, copy, edit, expand, edit_expand };

static void putdelta (struct hshentry const *, FILE *);
static void scandeltatext (struct hshentry *, enum stringwork, int);

char const *
buildrevision (struct hshentries const *deltas, struct hshentry *target,
               FILE *outfile, int expandflag)
/* Function: Generates the revision given by target
 * by retrieving all deltas given by parameter deltas and combining them.
 * If outfile is set, the revision is output to it,
 * otherwise written into a temporary file.
 * Temporary files are allocated by maketemp().
 * if expandflag is set, keyword expansion is performed.
 * Return 0 if outfile is set, the name of the temporary file otherwise.
 *
 * Algorithm: Copy initial revision unchanged.  Then edit all revisions but
 * the last one into it, alternating input and output files (resultname and
 * editname). The last revision is then edited in, performing simultaneous
 * keyword substitution (this saves one extra pass).
 * All this simplifies if only one revision needs to be generated,
 * or no keyword expansion is necessary, or if output goes to stdout.
 */
{
  if (deltas->first == target)
    {
      /* only latest revision to generate */
      openfcopy (outfile);
      scandeltatext (target, expandflag ? expand : copy, true);
      if (outfile)
        return 0;
      else
        {
          Ozclose (&fcopy);
          return resultname;
        }
    }
  else
    {
      /* several revisions to generate */
      /* Get initial revision without keyword expansion.  */
      scandeltatext (deltas->first, enter, false);
      while ((deltas = deltas->rest)->rest)
        {
          /* do all deltas except last one */
          scandeltatext (deltas->first, edit, false);
        }
      if (expandflag || outfile)
        {
          /* first, get to beginning of file */
          finishedit ((struct hshentry *) 0, outfile, false);
        }
      scandeltatext (target, expandflag ? edit_expand : edit, true);
      finishedit (expandflag ? target : (struct hshentry *) 0, outfile, true);
      if (outfile)
        return 0;
      Ozclose (&fcopy);
      return resultname;
    }
}

static void
scandeltatext (struct hshentry *delta, enum stringwork func, int needlog)
/* Function: Scans delta text nodes up to and including the one given
 * by delta. For the one given by delta, the log message is saved into
 * delta->log if needlog is set; func specifies how to handle the text.
 * Similarly, if needlog, delta->igtext is set to the ignored phrases.
 * Assumes the initial lexeme must be read in first.
 * Does not advance nexttok after it is finished.
 */
{
  struct hshentry const *nextdelta;
  struct cbuf cb;

  for (;;)
    {
      if (eoflex ())
        fatserror ("can't find delta for revision %s", delta->num);
      nextlex ();
      if (!(nextdelta = getnum ()))
        {
          fatserror ("delta number corrupted");
        }
      getkeystring (Klog);
      if (needlog && delta == nextdelta)
        {
          cb = savestring (&curlogbuf);
          delta->log = cleanlogmsg (curlogbuf.string, cb.size);
          nextlex ();
          delta->igtext = getphrases (Ktext);
        }
      else
        {
          readstring ();
          ignorephrases (Ktext);
        }
      getkeystring (Ktext);

      if (delta == nextdelta)
        break;
      readstring ();            /* skip over it */

    }
  switch (func)
    {
    case enter:
      enterstring ();
      break;
    case copy:
      copystring ();
      break;
    case expand:
      xpandstring (delta);
      break;
    case edit:
      editstring ((struct hshentry *) 0);
      break;
    case edit_expand:
      editstring (delta);
      break;
    }
}

struct cbuf
cleanlogmsg (char *m, size_t s)
{
  register char *t = m;
  register char const *f = t;
  struct cbuf r;
  while (s)
    {
      --s;
      if ((*t++ = *f++) == '\n')
        while (m < --t)
          if (t[-1] != ' ' && t[-1] != '\t')
            {
              *t++ = '\n';
              break;
            }
    }
  while (m < t && (t[-1] == ' ' || t[-1] == '\t' || t[-1] == '\n'))
    --t;
  r.string = m;
  r.size = t - m;
  return r;
}

int
ttystdin (void)
{
  static int initialized;
  if (!initialized)
    {
      if (!interactiveflag)
        interactiveflag = isatty (STDIN_FILENO);
      initialized = true;
    }
  return interactiveflag;
}

int
getcstdin (void)
{
  register FILE *in;
  register int c;

  in = stdin;
  if (feof (in) && ttystdin ())
    clearerr (in);
  c = getc (in);
  if (c == EOF)
    {
      testIerror (in);
      if (feof (in) && ttystdin ())
        afputc ('\n', stderr);
    }
  return c;
}

int
yesorno (int default_answer, char const *question, ...)
{
  va_list args;
  register int c, r;
  if (!quietflag && ttystdin ())
    {
      oflush ();
      va_start (args, question);
      fvfprintf (stderr, question, args);
      va_end (args);
      eflush ();
      r = c = getcstdin ();
      while (c != '\n' && !feof (stdin))
        c = getcstdin ();
      if (r == 'y' || r == 'Y')
        return true;
      if (r == 'n' || r == 'N')
        return false;
    }
  return default_answer;
}

void
putdesc (int textflag, char *textfile)
/* Function: puts the descriptive text into file frewrite.
 * if finptr && !textflag, the text is copied from the old description.
 * Otherwise, if textfile, the text is read from that
 * file, or from stdin, if !textfile.
 * A textfile with a leading '-' is treated as a string, not a pathname.
 * If finptr, the old descriptive text is discarded.
 * Always clears foutptr.
 */
{
  static struct buf desc;
  static struct cbuf desclean;

  register FILE *txt;
  register int c;
  register FILE *frew;
  register char *p;
  register size_t s;
  char const *plim;

  frew = frewrite;
  if (finptr && !textflag)
    {
      /* copy old description */
      aprintf (frew, "\n\n%s%c", Kdesc, nextc);
      foutptr = frewrite;
      getdesc (false);
      foutptr = 0;
    }
  else
    {
      foutptr = 0;
      /* get new description */
      if (finptr)
        {
          /*skip old description */
          getdesc (false);
        }
      aprintf (frew, "\n\n%s\n%c", Kdesc, SDELIM);
      if (!textfile)
        desclean = getsstdin ("t-", "description",
                              "NOTE: This is NOT the log message!\n", &desc);
      else if (!desclean.string)
        {
          if (*textfile == '-')
            {
              p = textfile + 1;
              s = strlen (p);
            }
          else
            {
              if (!(txt = fopenSafer (textfile, "r")))
                efaterror (textfile);
              bufalloc (&desc, 1);
              p = desc.string;
              plim = p + desc.size;
              for (;;)
                {
                  if ((c = getc (txt)) == EOF)
                    {
                      testIerror (txt);
                      if (feof (txt))
                        break;
                    }
                  if (plim <= p)
                    p = bufenlarge (&desc, &plim);
                  *p++ = c;
                }
              if (fclose (txt) != 0)
                Ierror ();
              s = p - desc.string;
              p = desc.string;
            }
          desclean = cleanlogmsg (p, s);
        }
      putstring (frew, false, desclean, true);
      aputc_ ('\n', frew)
    }
}

struct cbuf
getsstdin (char const *option, char const *name,
           char const *note, struct buf *buf)
{
  register int c;
  register char *p;
  register size_t i;
  register int tty = ttystdin ();

  if (tty)
    {
      aprintf (stderr,
               "enter %s, terminated with single '.' or end of file:\n%s>> ",
               name, note);
      eflush ();
    }
  else if (feof (stdin))
    rcsfaterror ("can't reread redirected stdin for %s; use -%s<%s>",
                 name, option, name);

  for (i = 0, p = 0;
       c = getcstdin (), !feof (stdin);
       bufrealloc (buf, i + 1), p = buf->string, p[i++] = c)
    if (c == '\n')
      {
        if (i && p[i - 1] == '.' && (i == 1 || p[i - 2] == '\n'))
          {
            /* Remove trailing '.'.  */
            --i;
            break;
          }
        else if (tty)
          {
            aputs (">> ", stderr);
            eflush ();
          }
      }
  return cleanlogmsg (p, i);
}

void
putadmin (void)
/* Output the admin node.  */
{
  register FILE *fout;
  struct assoc const *curassoc;
  struct rcslock const *curlock;
  struct access const *curaccess;

  if (!(fout = frewrite))
    {
#		if bad_creat0
      ORCSclose ();
      fout = fopenSafer (makedirtemp (0), FOPEN_WB);
#		else
      int fo = fdlock;
      fdlock = -1;
      fout = fdopen (fo, FOPEN_WB);
#		endif

      if (!(frewrite = fout))
        efaterror (RCSname);
    }

  /*
   * Output the first character with putc, not printf.
   * Otherwise, an SVR4 stdio bug buffers output inefficiently.
   */
  aputc_ (*Khead, fout)
  aprintf (fout, "%s\t%s;\n", Khead + 1, Head ? Head->num : "");
  if (Dbranch && VERSION (4) <= RCSversion)
    aprintf (fout, "%s\t%s;\n", Kbranch, Dbranch);

  aputs (Kaccess, fout);
  curaccess = AccessList;
  while (curaccess)
    {
      aprintf (fout, "\n\t%s", curaccess->login);
      curaccess = curaccess->nextaccess;
    }
  aprintf (fout, ";\n%s", Ksymbols);
  curassoc = Symbols;
  while (curassoc)
    {
      aprintf (fout, "\n\t%s:%s", curassoc->symbol, curassoc->num);
      curassoc = curassoc->nextassoc;
    }
  aprintf (fout, ";\n%s", Klocks);
  curlock = Locks;
  while (curlock)
    {
      aprintf (fout, "\n\t%s:%s", curlock->login, curlock->delta->num);
      curlock = curlock->nextlock;
    }
  if (StrictLocks)
    aprintf (fout, "; %s", Kstrict);
  aprintf (fout, ";\n");
  if (Comment.size)
    {
      aprintf (fout, "%s\t", Kcomment);
      putstring (fout, true, Comment, false);
      aprintf (fout, ";\n");
    }
  if (Expand != KEYVAL_EXPAND)
    aprintf (fout, "%s\t%c%s%c;\n",
             Kexpand, SDELIM, expand_names[Expand], SDELIM);
  awrite (Ignored.string, Ignored.size, fout);
  aputc_ ('\n', fout)
}

static void
putdelta (register struct hshentry const *node, register FILE *fout)
/* Output the delta NODE to FOUT.  */
{
  struct branchhead const *nextbranch;

  if (!node)
    return;

  aprintf (fout, "\n%s\n%s\t%s;\t%s %s;\t%s %s;\nbranches",
           node->num,
           Kdate, node->date,
           Kauthor, node->author, Kstate, node->state ? node->state : "");
  nextbranch = node->branches;
  while (nextbranch)
    {
      aprintf (fout, "\n\t%s", nextbranch->hsh->num);
      nextbranch = nextbranch->nextbranch;
    }

  aprintf (fout, ";\n%s\t%s;\n", Knext, node->next ? node->next->num : "");
  awrite (node->ig.string, node->ig.size, fout);
}

void
puttree (struct hshentry const *root, register FILE *fout)
/* Output the delta tree with base ROOT in preorder to FOUT.  */
{
  struct branchhead const *nextbranch;

  if (!root)
    return;

  if (root->selector)
    putdelta (root, fout);

  puttree (root->next, fout);

  nextbranch = root->branches;
  while (nextbranch)
    {
      puttree (nextbranch->hsh, fout);
      nextbranch = nextbranch->nextbranch;
    }
}

int
putdtext (struct hshentry const *delta, char const *srcname,
          FILE *fout, int diffmt)
/*
 * Output a deltatext node with delta number DELTA->num, log message DELTA->log,
 * ignored phrases DELTA->igtext and text SRCNAME to FOUT.
 * Double up all SDELIMs in both the log and the text.
 * Make sure the log message ends in \n.
 * Return false on error.
 * If DIFFMT, also check that the text is valid diff -n output.
 */
{
  RILE *fin;
  if (!(fin = Iopen (srcname, "r", (struct stat *) 0)))
    {
      eerror (srcname);
      return false;
    }
  putdftext (delta, fin, fout, diffmt);
  Ifclose (fin);
  return true;
}

void
putstring (register FILE *out, int delim, struct cbuf s, int log)
/*
 * Output to OUT one SDELIM if DELIM, then the string S with SDELIMs doubled.
 * If LOG is set then S is a log string; append a newline if S is nonempty.
 */
{
  register char const *sp;
  register size_t ss;

  if (delim)
    aputc_ (SDELIM, out)
  sp = s.string;
  for (ss = s.size; ss; --ss)
    {
      if (*sp == SDELIM)
        aputc_ (SDELIM, out)
      aputc_ (*sp++, out)
    }
  if (s.size && log)
    aputc_ ('\n', out)
  aputc_ (SDELIM, out)
}

void
putdftext (struct hshentry const *delta, RILE *finfile,
           FILE *foutfile, int diffmt)
/* like putdtext(), except the source file is already open */
{
  declarecache;
  register FILE *fout;
  register int c;
  register RILE *fin;
  int ed;
  struct diffcmd dc;

  fout = foutfile;
  aprintf (fout, DELNUMFORM, delta->num, Klog);

  /* put log */
  putstring (fout, true, delta->log, true);
  aputc_ ('\n', fout)
  /* put ignored phrases */
  awrite (delta->igtext.string, delta->igtext.size, fout);

  /* put text */
  aprintf (fout, "%s\n%c", Ktext, SDELIM);

  fin = finfile;
  setupcache (fin);
  if (!diffmt)
    {
      /* Copy the file */
      cache (fin);
      for (;;)
        {
          cachegeteof_ (c, break; )
          if (c == SDELIM)
            aputc_ (SDELIM, fout)       /*double up SDELIM */
          aputc_ (c, fout)
        }
    }
  else
    {
      initdiffcmd (&dc);
      while (0 <= (ed = getdiffcmd (fin, false, fout, &dc)))
        if (ed)
          {
            cache (fin);
            while (dc.nlines--)
              do
                {
                  cachegeteof_ (c,
                                {
                                  if (!dc.nlines)
                                    goto OK_EOF;
                                  unexpected_EOF ();
                                })
                  if (c == SDELIM)
                    aputc_ (SDELIM, fout)
                  aputc_ (c, fout)
                }
              while (c != '\n');
            uncache (fin);
          }
    }
 OK_EOF:
  aprintf (fout, "%c\n", SDELIM);
}
