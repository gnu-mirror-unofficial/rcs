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

#include "base.h"
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include "b-complain.h"

enum stringwork
{ enter, copy, edit, expand, edit_expand };

static void
scandeltatext (struct hshentry *delta, enum stringwork func, bool needlog)
/* Scan delta text nodes up to and including the one given by `delta'.
   For the one given by `delta', the log message is saved into
   `delta->log' if `needlog' is set; `func' specifies how to handle the
   text.  Similarly, if `needlog', `delta->igtext' is set to the ignored
   phrases.  Assume the initial lexeme must be read in first.  Does not
   advance `NEXT (tok)' after it is finished.  */
{
  struct hshentry const *nextdelta;
  struct cbuf cb;

  for (;;)
    {
      if (eoflex ())
        fatal_syntax ("can't find delta for revision %s", delta->num);
      nextlex ();
      if (!(nextdelta = getnum ()))
        {
          fatal_syntax ("delta number corrupted");
        }
      getkeystring (Klog);
      if (needlog && delta == nextdelta)
        {
          cb = savestring (&MANI (log));
          delta->log = cleanlogmsg (MANI (log).string, cb.size);
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
      /* Skip over it.  */
      readstring ();
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
      editstring (NULL);
      break;
    case edit_expand:
      editstring (delta);
      break;
    }
}

char const *
buildrevision (struct hshentries const *deltas, struct hshentry *target,
               FILE *outfile, bool expandflag)
/* Generate the revision given by `target' by retrieving all deltas given
   by parameter `deltas' and combining them.  If `outfile' is set, the
   revision is output to it, otherwise write into a temporary file.
   Temporary files are allocated by `maketemp'.  If `expandflag' is set,
   keyword expansion is performed.  Return NULL if `outfile' is set, the
   name of the temporary file otherwise.

   Algorithm: Copy initial revision unchanged.  Then edit all revisions
   but the last one into it, alternating input and output files
   (`FLOW (result)' and `editname').  The last revision is then edited in,
   performing simultaneous keyword substitution (this saves one extra
   pass).  All this simplifies if only one revision needs to be generated,
   or no keyword expansion is necessary, or if output goes to stdout.  */
{
  if (deltas->first == target)
    {
      /* Only latest revision to generate.  */
      openfcopy (outfile);
      scandeltatext (target, expandflag ? expand : copy, true);
      if (outfile)
        return NULL;
      else
        {
          Ozclose (&FLOW (res));
          return FLOW (result);
        }
    }
  else
    {
      /* Several revisions to generate.
         Get initial revision without keyword expansion.  */
      scandeltatext (deltas->first, enter, false);
      while ((deltas = deltas->rest)->rest)
        {
          /* Do all deltas except last one.  */
          scandeltatext (deltas->first, edit, false);
        }
      if (expandflag || outfile)
        {
          /* First, get to beginning of file.  */
          finishedit (NULL, outfile, false);
        }
      scandeltatext (target, expandflag ? edit_expand : edit, true);
      finishedit (expandflag ? target : NULL, outfile, true);
      if (outfile)
        return NULL;
      Ozclose (&FLOW (res));
      return FLOW (result);
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

bool
ttystdin (void)
{
  if (!BE (interactive_valid))
    {
      if (!BE (interactive))
        BE (interactive) = isatty (STDIN_FILENO);
      BE (interactive_valid) = true;
    }
  return BE (interactive);
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
        complain ("\n");
    }
  return c;
}

bool
yesorno (bool default_answer, char const *question, ...)
{
  va_list args;
  register int c, r;

  if (!BE (quiet) && ttystdin ())
    {
      oflush ();
      va_start (args, question);
      vcomplain (question, args);
      va_end (args);
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
putdesc (bool textflag, char *textfile)
/* Put the descriptive text into file `FLOW (rewr)'.
   If `FLOW (from) && !textflag', the text is copied from the old description.
   Otherwise, if `textfile', the text is read from that file, or from
   stdin, if `!textfile'.  A `textfile' with a leading '-' is treated as a
   string, not a pathname.  If `FLOW (from)', the old descriptive text is
   discarded.  Always clear `FLOW (to)'.  */
{
  static struct buf desc;
  static struct cbuf desclean;

  register FILE *txt;
  register int c;
  register FILE *frew;
  register char *p;
  register size_t s;
  char const *plim;

  frew = FLOW (rewr);
  if (FLOW (from) && !textflag)
    {
      /* Copy old description.  */
      aprintf (frew, "\n\n%s%c", Kdesc, NEXT (c));
      FLOW (to) = FLOW (rewr);
      getdesc (false);
      FLOW (to) = NULL;
    }
  else
    {
      FLOW (to) = NULL;
      /* Get new description.  */
      if (FLOW (from))
        {
          /* Skip old description.  */
          getdesc (false);
        }
      aprintf (frew, "\n\n%s\n%c", Kdesc, SDELIM);
      if (!textfile)
        desclean = getsstdin ("t-", "description",
                              "NOTE: This is NOT the log message!\n",
                              &desc);
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
                fatal_sys (textfile);
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
      aputc ('\n', frew);
    }
}

struct cbuf
getsstdin (char const *option, char const *name,
           char const *note, struct buf *buf)
{
  register int c;
  register char *p;
  register size_t i;
  register bool tty = ttystdin ();

#define prompt  complain
  if (tty)
    prompt ("enter %s, terminated with single '.' or end of file:\n%s>> ",
            name, note);
  else if (feof (stdin))
    RFATAL ("can't reread redirected stdin for %s; use -%s<%s>",
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
          prompt (">> ");
      }
#undef prompt
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

  if (!(fout = FLOW (rewr)))
    {
#if BAD_CREAT0
      ORCSclose ();
      fout = fopenSafer (makedirtemp (0), FOPEN_WB);
#else  /* !BAD_CREAT0 */
      int fo = REPO (fd_lock);

      REPO (fd_lock) = -1;
      fout = fdopen (fo, FOPEN_WB);
#endif  /* !BAD_CREAT0 */

      if (!(FLOW (rewr) = fout))
        fatal_sys (REPO (filename));
    }

  aprintf (fout, "%s\t%s;\n", Khead,
           ADMIN (head) ? ADMIN (head)->num : "");
  if (ADMIN (defbr) && VERSION (4) <= BE (version))
    aprintf (fout, "%s\t%s;\n", Kbranch, ADMIN (defbr));

  aputs (Kaccess, fout);
  curaccess = ADMIN (allowed);
  while (curaccess)
    {
      aprintf (fout, "\n\t%s", curaccess->login);
      curaccess = curaccess->nextaccess;
    }
  aprintf (fout, ";\n%s", Ksymbols);
  curassoc = ADMIN (assocs);
  while (curassoc)
    {
      aprintf (fout, "\n\t%s:%s", curassoc->symbol, curassoc->num);
      curassoc = curassoc->nextassoc;
    }
  aprintf (fout, ";\n%s", Klocks);
  curlock = ADMIN (locks);
  while (curlock)
    {
      aprintf (fout, "\n\t%s:%s", curlock->login, curlock->delta->num);
      curlock = curlock->nextlock;
    }
  if (BE (strictly_locking))
    aprintf (fout, "; %s", Kstrict);
  aprintf (fout, ";\n");
  if (ADMIN (log_lead).size)
    {
      aprintf (fout, "%s\t", Kcomment);
      putstring (fout, true, ADMIN (log_lead), false);
      aprintf (fout, ";\n");
    }
  if (BE (kws) != kwsub_kv)
    aprintf (fout, "%s\t%c%s%c;\n",
             Kexpand, SDELIM, kwsub_string (BE (kws)), SDELIM);
  awrite (ADMIN (description).string, ADMIN (description).size, fout);
  aputc ('\n', fout);
}

static void
putdelta (register struct hshentry const *node, register FILE *fout)
/* Output the delta `node' to `fout'.  */
{
  struct branchhead const *nextbranch;

  if (!node)
    return;

  aprintf (fout, "\n%s\n%s\t%s;\t%s %s;\t%s %s;\nbranches",
           node->num, Kdate, node->date, Kauthor, node->author,
           Kstate, node->state ? node->state : "");
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
/* Output the delta tree with base `root' in preorder to `fout'.  */
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

bool
putdtext (struct hshentry const *delta, char const *srcname,
          FILE *fout, bool diffmt)
/* Output a deltatext node with delta number `delta->num', log message
   `delta->log', ignored phrases `delta->igtext' and text `srcname' to
   `fout'.  Double up all `SDELIM's in both the log and the text.  Make
   sure the log message ends in '\n'.  Return false on error.  If
   `diffmt', also check that the text is valid "diff -n" output.  */
{
  RILE *fin;

  if (!(fin = Iopen (srcname, "r", NULL)))
    {
      syserror_errno (srcname);
      return false;
    }
  putdftext (delta, fin, fout, diffmt);
  Ifclose (fin);
  return true;
}

void
putstring (register FILE *out, bool delim, struct cbuf s, bool log)
/* Output to `out' one `SDELIM' if `delim', then the string `s' with
   `SDELIM's doubled.  If `log' is set then `s' is a log string; append
   a newline if `s' is nonempty.  */
{
  register char const *sp;
  register size_t ss;

  if (delim)
    aputc (SDELIM, out);
  sp = s.string;
  for (ss = s.size; ss; --ss)
    {
      if (*sp == SDELIM)
        aputc (SDELIM, out);
      aputc (*sp++, out);
    }
  if (s.size && log)
    aputc ('\n', out);
  aputc (SDELIM, out);
}

void
putdftext (struct hshentry const *delta, RILE *finfile,
           FILE *foutfile, bool diffmt)
/* Like `putdtext', except the source file is already open.  */
{
  declarecache;
  register FILE *fout;
  register int c;
  register RILE *fin;
  int ed;
  struct diffcmd dc;

  fout = foutfile;
  aprintf (fout, DELNUMFORM, delta->num, Klog);

  /* Put log.  */
  putstring (fout, true, delta->log, true);
  aputc ('\n', fout);
  /* Put ignored phrases.  */
  awrite (delta->igtext.string, delta->igtext.size, fout);

  /* Put text.  */
  aprintf (fout, "%s\n%c", Ktext, SDELIM);

  fin = finfile;
  setupcache (fin);
  if (!diffmt)
    {
      /* Copy the file.  */
      cache (fin);
      for (;;)
        {
          cachegeteof (c, goto done);
          if (c == SDELIM)
            /* Double up `SDELIM'.  */
            aputc (SDELIM, fout);
          aputc (c, fout);
        }
    done:
      ;
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
                  cachegeteof (c,
                               {
                                 if (!dc.nlines)
                                   goto OK_EOF;
                                 unexpected_EOF ();
                               });
                  if (c == SDELIM)
                    aputc (SDELIM, fout);
                  aputc (c, fout);
                }
              while (c != '\n');
            uncache (fin);
          }
    }
 OK_EOF:
  aprintf (fout, "%c\n", SDELIM);
}

/* rcsgen.c ends here */
