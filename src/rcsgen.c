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
#include "b-divvy.h"
#include "b-esds.h"
#include "b-fb.h"
#include "b-fro.h"
#include "b-kwxout.h"

enum stringwork
{ enter, copy, edit, expand, edit_expand };

static void
scandeltatext (struct editstuff *es, struct hshentry *delta,
               enum stringwork func, bool needlog)
/* Scan delta text nodes up to and including the one given by ‘delta’.
   For the one given by ‘delta’, the log message is saved into
   ‘delta->log’ if ‘needlog’ is set; ‘func’ specifies how to handle the
   text.  Assume the initial lexeme must be read in first.  Does not
   advance ‘NEXT (tok)’ after it is finished.  */
{
  struct hshentry const *nextdelta;
  struct cbuf cb;

  for (;;)
    {
      if (eoflex ())
        SYNTAX_ERROR ("can't find delta for revision %s", delta->num);
      nextdelta = must_get_delta_num ();
      getkeystring (&TINY (log));
      if (needlog && delta == nextdelta)
        {
          cb = savestring ();
          delta->pretty_log = cleanlogmsg (cb.string, cb.size);
        }
      else
        readstring ();
      nextlex ();
      getkeystring (&TINY (text));

      if (delta == nextdelta)
        break;
      /* Skip over it.  */
      readstring ();
    }
  switch (func)
    {
    case enter:
      enterstring (es);
      break;
    case copy:
      copystring (es);
      break;
    case expand:
      /* Read a string terminated by ‘SDELIM’ from ‘FLOW (from)’ and
         write it to ‘FLOW (res)’.  Double ‘SDELIM’ is replaced with
         single ‘SDELIM’.  Keyword expansion is performed with data
         from ‘delta’.  If ‘FLOW (to)’ is non-NULL, the string is
         also copied unchanged to ‘FLOW (to)’.  */
      {
        struct expctx ctx = EXPCTX (FLOW (res), FLOW (to),
                                    FLOW (from), true, true);

        while (1 < expandline (&ctx))
          continue;
      }
      break;
    case edit:
      editstring (es, NULL);
      break;
    case edit_expand:
      editstring (es, delta);
      break;
    }
}

char const *
buildrevision (struct hshentries const *deltas, struct hshentry *target,
               FILE *outfile, bool expandflag)
/* Generate the revision given by ‘target’ by retrieving all deltas given
   by parameter ‘deltas’ and combining them.  If ‘outfile’ is set, the
   revision is output to it, otherwise write into a temporary file.
   Temporary files are allocated by ‘maketemp’.  If ‘expandflag’ is set,
   keyword expansion is performed.  Return NULL if ‘outfile’ is set, the
   name of the temporary file otherwise.

   Algorithm: Copy initial revision unchanged.  Then edit all revisions
   but the last one into it, alternating input and output files
   (‘FLOW (result)’ and ‘editname’).  The last revision is then edited in,
   performing simultaneous keyword substitution (this saves one extra
   pass).  All this simplifies if only one revision needs to be generated,
   or no keyword expansion is necessary, or if output goes to stdout.  */
{
  struct editstuff *es = make_editstuff ();

  if (deltas->first == target)
    {
      /* Only latest revision to generate.  */
      openfcopy (outfile);
      scandeltatext (es, target, expandflag ? expand : copy, true);
    }
  else
    {
      /* Several revisions to generate.
         Get initial revision without keyword expansion.  */
      scandeltatext (es, deltas->first, enter, false);
      while ((deltas = deltas->rest)->rest)
        {
          /* Do all deltas except last one.  */
          scandeltatext (es, deltas->first, edit, false);
        }
      if (expandflag || outfile)
        {
          /* First, get to beginning of file.  */
          finishedit (es, NULL, outfile, false);
        }
      scandeltatext (es, target, expandflag ? edit_expand : edit, true);
      finishedit (es, expandflag ? target : NULL, outfile, true);
    }
  unmake_editstuff (es);
  if (outfile)
    return NULL;
  Ozclose (&FLOW (res));
  return FLOW (result);
}

struct cbuf
cleanlogmsg (char const *m, size_t s)
{
  struct cbuf r;

#define WHITESPACEP(c)  (' ' == c || '\t' == c || '\n' == c)
  while (s && WHITESPACEP (*m))
    s--, m++;
  while (s && WHITESPACEP (m[s - 1]))
    s--;
#undef WHITESPACEP

  r.string = m;
  r.size = s;
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
putdesc (struct cbuf *cb, bool textflag, char *textfile)
/* Put the descriptive text into file ‘FLOW (rewr)’.
   Also, save the description text into ‘cb’.
   If ‘FLOW (from) && !textflag’, the text is copied from the old description.
   Otherwise, if ‘textfile’, the text is read from that file, or from
   stdin, if ‘!textfile’.  A ‘textfile’ with a leading '-' is treated as a
   string, not a filename.  If ‘FLOW (from)’, the old descriptive text is
   discarded.  Always clear ‘FLOW (to)’.  */
{
  register FILE *txt;
  register int c;
  register FILE *frew;
  register char *p;
  size_t s;

  frew = FLOW (rewr);
  if (FLOW (from) && !textflag)
    {
      /* Copy old description.  */
      aprintf (frew, "\n\n%s%c", TINYKS (desc), NEXT (c));
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
      aprintf (frew, "\n\n%s\n%c", TINYKS (desc), SDELIM);
      if (!textfile)
        *cb = getsstdin ("t-", "description",
                         "NOTE: This is NOT the log message!\n");
      else if (!cb->string)
        {
          if (*textfile == '-')
            {
              p = textfile + 1;
              s = strlen (p);
            }
          else
            {
              if (!(txt = fopen_safer (textfile, "r")))
                fatal_sys (textfile);
              for (;;)
                {
                  if ((c = getc (txt)) == EOF)
                    {
                      testIerror (txt);
                      if (feof (txt))
                        break;
                    }
                  accumulate_byte (SHARED, c);
                }
              if (fclose (txt) != 0)
                Ierror ();
              p = finish_string (SHARED, &s);
            }
          *cb = cleanlogmsg (p, s);
        }
      putstring (frew, false, *cb, true);
      aputc ('\n', frew);
    }
}

struct cbuf
getsstdin (char const *option, char const *name, char const *note)
{
  register int c;
  register char *p;
  register bool tty = ttystdin ();
  size_t len, column = 0;
  bool dot_in_first_column_p = false, discardp = false;

#define prompt  complain
  if (tty)
    prompt ("enter %s, terminated with single '.' or end of file:\n%s>> ",
            name, note);
  else if (feof (stdin))
    RFATAL ("can't reread redirected stdin for %s; use -%s<%s>",
            name, option, name);

  while (c = getcstdin (), !feof (stdin))
    {
      if (!column)
        dot_in_first_column_p = ('.' == c);
      if (c == '\n')
        {
          if (1 == column && dot_in_first_column_p)
            {
              discardp = true;
              break;
            }
          else if (tty)
            prompt (">> ");
          column = 0;
        }
      else
        column++;
      accumulate_byte (SHARED, c);
    }
  p = finish_string (SHARED, &len);
#undef prompt
  return cleanlogmsg (p, len - (discardp ? 1 : 0));
}

void
format_assocs (FILE *out, char const *fmt)
{
  for (struct link *ls = ADMIN (assocs); ls; ls = ls->next)
    {
      struct symdef const *d = ls->entry;

      aprintf (out, fmt, d->meaningful, d->underlying);
    }
}

void
putadmin (void)
/* Output the admin node.  */
{
  register FILE *fout;
  struct link const *curaccess;

  if (!(fout = FLOW (rewr)))
    {
#if BAD_CREAT0
      ORCSclose ();
      fout = fopen_safer (makedirtemp (false), FOPEN_WB);
#else  /* !BAD_CREAT0 */
      int fo = REPO (fd_lock);

      REPO (fd_lock) = -1;
      fout = fdopen (fo, FOPEN_WB);
#endif  /* !BAD_CREAT0 */

      if (!(FLOW (rewr) = fout))
        fatal_sys (REPO (filename));
    }

  aprintf (fout, "%s\t%s;\n", TINYKS (head),
           REPO (tip) ? REPO (tip)->num : "");
  if (ADMIN (defbr) && VERSION (4) <= BE (version))
    aprintf (fout, "%s\t%s;\n", TINYKS (branch), ADMIN (defbr));

  aputs (TINYKS (access), fout);
  curaccess = ADMIN (allowed);
  while (curaccess)
    {
      aprintf (fout, "\n\t%s", (char const *)curaccess->entry);
      curaccess = curaccess->next;
    }
  aprintf (fout, ";\n%s", TINYKS (symbols));
  format_assocs (fout, "\n\t%s:%s");
  aprintf (fout, ";\n%s", TINYKS (locks));
  for (struct link *ls = ADMIN (locks); ls; ls = ls->next)
    {
      struct rcslock const *rl = ls->entry;

      aprintf (fout, "\n\t%s:%s", rl->login, rl->delta->num);
    }
  if (BE (strictly_locking))
    aprintf (fout, "; %s", TINYKS (strict));
  aprintf (fout, ";\n");
  if (ADMIN (log_lead).size)
    {
      aprintf (fout, "%s\t", TINYKS (comment));
      putstring (fout, true, ADMIN (log_lead), false);
      aprintf (fout, ";\n");
    }
  if (BE (kws) != kwsub_kv)
    aprintf (fout, "%s\t%c%s%c;\n",
             TINYKS (expand), SDELIM, kwsub_string (BE (kws)), SDELIM);
}

static void
putdelta (register struct hshentry const *node, register FILE *fout)
/* Output the delta ‘node’ to ‘fout’.  */
{
  if (!node)
    return;

  aprintf (fout, "\n%s\n%s\t%s;\t%s %s;\t%s %s;\nbranches",
           node->num, TINYKS (date), node->date, TINYKS (author), node->author,
           TINYKS (state), node->state ? node->state : "");
  for (struct wlink *ls = node->branches; ls; ls = ls->next)
    {
      struct hshentry *delta = ls->entry;

      aprintf (fout, "\n\t%s", delta->num);
    }

  aprintf (fout, ";\n%s\t%s;\n", TINYKS (next), node->next ? node->next->num : "");
  if (node->commitid)
    aprintf (fout, "%s\t%s;\n", TINYKS (commitid), node->commitid);
}

void
puttree (struct hshentry const *root, register FILE *fout)
/* Output the delta tree with base ‘root’ in preorder to ‘fout’.  */
{
  if (!root)
    return;

  if (root->selector)
    putdelta (root, fout);

  puttree (root->next, fout);

  for (struct wlink *ls = root->branches; ls; ls = ls->next)
    puttree (ls->entry, fout);
}

bool
putdtext (struct hshentry const *delta, char const *srcname,
          FILE *fout, bool diffmt)
/* Output a deltatext node with delta number ‘delta->num’, log message
   ‘delta->pretty_log’, and text ‘srcname’ to ‘fout’.  Double up all ‘SDELIM’s
   in both the log and the text.  Make sure the log message ends in '\n'.
   Return false on error.  If ‘diffmt’, also check that the text is valid
   "diff -n" output.  */
{
  struct fro *fin;

  if (!(fin = fro_open (srcname, "r", NULL)))
    {
      syserror_errno (srcname);
      return false;
    }
  putdftext (delta, fin, fout, diffmt);
  fro_close (fin);
  return true;
}

void
putstring (register FILE *out, bool delim, struct cbuf s, bool log)
/* Output to ‘out’ one ‘SDELIM’ if ‘delim’, then the string ‘s’ with
   ‘SDELIM’s doubled.  If ‘log’ is set then ‘s’ is a log string; append
   a newline if ‘s’ is nonempty.  */
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
putdftext (struct hshentry const *delta, struct fro *finfile,
           FILE *foutfile, bool diffmt)
/* Like ‘putdtext’, except the source file is already open.  */
{
  register FILE *fout;
  int c;
  register struct fro *fin;
  int ed;
  struct diffcmd dc;

  fout = foutfile;
  aprintf (fout, DELNUMFORM, delta->num, TINYKS (log));

  /* Put log.  */
  putstring (fout, true, delta->pretty_log, true);
  aputc ('\n', fout);
  /* Put text.  */
  aprintf (fout, "%s\n%c", TINYKS (text), SDELIM);

  fin = finfile;
  if (!diffmt)
    {
      /* Copy the file.  */
      for (;;)
        {
          GETCHAR_OR (c, fin, goto done);
          if (c == SDELIM)
            /* Double up ‘SDELIM’.  */
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
            while (dc.nlines--)
              do
                {
                  GETCHAR_OR (c, fin,
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
          }
    }
 OK_EOF:
  aprintf (fout, "%c\n", SDELIM);
}

/* rcsgen.c ends here */
