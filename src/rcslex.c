/* lexical analysis of RCS files

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
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "unistd-safer.h"
#include "b-complain.h"
#include "b-divvy.h"
#include "b-esds.h"
#include "b-fb.h"
#include "b-fro.h"
#include "b-isr.h"

/* Our hash algorithm is h[0] = 0, h[i+1] = 4*h[i] + c,
   so hshsize should be odd.

   See B J McKenzie, R Harries & T Bell, Selecting a hashing algorithm,
   Software--practice & experience 20, 2 (Feb 1990), 209-224.  */
#ifndef hshsize
#define hshsize 511
#endif

static void
lookup (char const *str)
/* Look up the character string ‘str’ in the hashtable.
   If the string is not present, a new entry for it is created.  In any
   case, the address of the corresponding hashtable entry is placed into
   ‘NEXT (hsh)’.  */
{
  register unsigned ihash;      /* index into hashtable */
  register char const *sp;
  register struct hshentry *n;

  /* Calculate hash code.  */
  sp = str;
  ihash = 0;
  while (*sp)
    ihash = (ihash << 2) + *sp++;
  ihash %= hshsize;

  for (struct wlink *ls = LEX (hshtab)[ihash]; ; ls = ls->next)
    if (!ls)
      {
        /* Empty slot found.  */
        n = FALLOC (struct hshentry);
        n->num = intern (SINGLE, str, sp - str);
        LEX (hshtab)[ihash] = wprepend (n, LEX (hshtab)[ihash], SINGLE);
        break;
      }
    else if (n = ls->entry,
             STR_SAME (str, n->num))
      /* Match found.  */
      break;
  NEXT (hsh) = n;
  NEXT (str) = n->num;
}

void
Lexinit (void)
/* Initialize lexical analyzer: initialize the hashtable;
   initialize ‘NEXT (c)’, ‘NEXT (tok)’ if ‘FLOW (from)’ != NULL.  */
{
  if (! LEX (hshtab))
    LEX (hshtab) = pointer_array (SHARED, hshsize);
  for (int i = 0; i < hshsize; i++)
    LEX (hshtab)[i] = NULL;

  LEX (erroneousp) = false;
  BE (Oerrloop) = false;
  if (FLOW (from))
    {
      FLOW (to) = NULL;
      BE (receptive_to_next_hash_key) = true;
      LEX (lno) = 1;
      if (LEX (tokbuf))
        forget (LEX (tokbuf));
      else
        LEX (tokbuf) = make_space ("tokbuf"); /* TODO: smaller chunk size */
      GETCHAR (NEXT (c), FLOW (from));
      /* Initial token.  */
      nextlex ();
    }
}

void
nextlex (void)
/* Read the next token and set ‘NEXT (tok)’ to the next token code.
   Only if ‘receptive_to_next_hash_key’, a revision number is entered
   into the hashtable and a pointer to it is placed into ‘NEXT (hsh)’.
   This is useful for avoiding that dates are placed into the hashtable.
   For ID's and NUM's, ‘NEXT (str)’ is set to the character string.
   Assumption: ‘NEXT (c)’ contains the next character.  */
{
  int c;
  register FILE *frew;
  register char *sp;
  register enum tokens d;
  register struct fro *fin;
  size_t len;

  fin = FLOW (from);
  frew = FLOW (to);
  c = NEXT (c);

  for (;;)
    {
      switch ((d = ctab[c]))
        {
        default:
          fatal_syntax ("unknown character `%c'", c);
        case NEWLN:
          ++LEX (lno);
          /* fall into */
        case SPACE:
          TEECHAR ();
          continue;

        case IDCHAR:
        case LETTER:
        case Letter:
          d = ID;
          /* fall into */
        case DIGIT:
        case PERIOD:
          forget (LEX (tokbuf));
          accumulate_byte (LEX (tokbuf), c);
          for (;;)
            {
              TEECHAR ();
              switch (ctab[c])
                {
                case IDCHAR:
                case LETTER:
                case Letter:
                  d = ID;
                  /* fall into */
                case DIGIT:
                case PERIOD:
                  accumulate_byte (LEX (tokbuf), c);
                  continue;

                default:
                  break;
                }
              break;
            }
          sp = finish_string (LEX (tokbuf), &len);
          if (d == DIGIT || d == PERIOD)
            {
              d = NUM;
              if (BE (receptive_to_next_hash_key))
                {
                  lookup (sp);
                  break;
                }
            }
          /* This extra copy is almost unbearably bletcherous.
             TODO: Make ‘nextlex’ accept "disposition".  */
          NEXT (str) = intern (SINGLE, sp, len);
          break;

        case SBEGIN:           /* long string */
          d = STRING;
          /* Note: Only the initial SBEGIN has been read.
             Read the string, and reset ‘NEXT (c)’ afterwards.  */
          break;

        case COLON:
        case SEMI:
          TEECHAR ();
          break;
        }
      break;
    }
  NEXT (c) = c;
  NEXT (tok) = d;
}

bool
eoflex (void)
/* Return true if we look ahead to the end of the input, false otherwise.
   ‘NEXT (c)’ becomes undefined at end of file.  */
{
  int c;
  register FILE *fout;
  register struct fro *fin;

  c = NEXT (c);
  fin = FLOW (from);
  fout = FLOW (to);

  for (;;)
    {
      switch (ctab[c])
        {
        default:
          NEXT (c) = c;
          return false;

        case NEWLN:
          ++LEX (lno);
          /* fall into */
        case SPACE:
          GETCHAR_OR (c, fin, return true);
          break;
        }
      if (fout)
        aputc (c, fout);
    }
}

bool
getlex (enum tokens token)
/* Check if ‘NEXT (tok)’ is the same as ‘token’.  If so, advance the input
   by calling ‘nextlex’ and return true.  Otherwise return false.
   Doesn't work for strings and keywords; loses the character string for
   ids.  */
{
  if (NEXT (tok) == token)
    {
      nextlex ();
      return (true);
    }
  else
    return (false);
}

bool
getkeyopt (struct tinysym const *key)
/* If the current token is a keyword identical to ‘key’,
   advance the input by calling ‘nextlex’ and return true;
   otherwise return false.  */
{
  if (NEXT (tok) == ID && looking_at (key, NEXT (str)))
    {
      /* Match found.  */
      free_NEXT_str ();
      nextlex ();
      return true;
    }
  return false;
}

void
getkey (struct tinysym const *key)
/* Check that the current input token is a keyword identical to ‘key’,
   and advance the input by calling ‘nextlex’.  */
{
  if (!getkeyopt (key))
    fatal_syntax ("missing '%s' keyword", TINYS (key));
}

void
getkeystring (struct tinysym const *key)
/* Check that the current input token is a keyword identical to ‘key’,
   and advance the input by calling ‘nextlex’; then look ahead for a
   string.  */
{
  getkey (key);
  if (NEXT (tok) != STRING)
    fatal_syntax ("missing string after '%s' keyword", TINYS (key));
}

char const *
getid (void)
/* Check if ‘NEXT (tok)’ is an identifier.  If so, advance the input by
   calling ‘nextlex’ and return a pointer to the identifier; otherwise
   returns NULL.  Treat keywords as identifiers.  */
{
  register char const *name;

  if (NEXT (tok) == ID)
    {
      name = NEXT (str);
      nextlex ();
      return name;
    }
  else
    return NULL;
}

struct hshentry *
getnum (void)
/* Check if ‘NEXT (tok)’ is a number.  If so, advance the input by calling
   ‘nextlex’ and return a pointer to the hashtable entry.  Otherwise
   returns NULL.  Doesn't work if not ‘receptive_to_next_hash_key’.  */
{
  register struct hshentry *num;

  if (NEXT (tok) == NUM)
    {
      num = NEXT (hsh);
      nextlex ();
      return num;
    }
  else
    return NULL;
}

struct hshentry *
must_get_delta_num (void)
{
  struct hshentry *rv;

  nextlex ();
  if (!(rv = getnum ()))
    fatal_syntax ("delta number corrupted");

  return rv;
}

void
readstring (void)
/* Skip over characters until terminating single ‘SDELIM’.
   If ‘FLOW (to)’ is set, copy every character read to ‘FLOW (to)’.
   Do not advance ‘nextlex’ at the end.  */
{
  int c;
  register FILE *frew;
  register struct fro *fin;

  fin = FLOW (from);
  frew = FLOW (to);
  for (;;)
    {
      TEECHAR ();
      switch (c)
        {
        case '\n':
          ++LEX (lno);
          break;

        case SDELIM:
          TEECHAR ();
          if (c != SDELIM)
            {
              /* End of string.  */
              NEXT (c) = c;
              return;
            }
          break;
        }
    }
}

void
printstring (void)
/* Copy a string to stdout, until terminated with a single ‘SDELIM’.
   Do not advance ‘nextlex’ at the end.  */
{
  int c;
  register FILE *fout;
  register struct fro *fin;

  fin = FLOW (from);
  fout = stdout;
  for (;;)
    {
      GETCHAR (c, fin);
      switch (c)
        {
        case '\n':
          ++LEX (lno);
          break;
        case SDELIM:
          GETCHAR (c, fin);
          if (c != SDELIM)
            {
              NEXT (c) = c;
              return;
            }
          break;
        }
      aputc (c, fout);
    }
}

struct cbuf
savestring (void)
/* Return a ‘SDELIM’-terminated cbuf read from file ‘FLOW (from)’,
   replacing double ‘SDELIM’ is with ‘SDELIM’.  If ‘FLOW (to)’ is set,
   also copy (unsubstituted) to ‘FLOW (to)’.  Do not advance ‘nextlex’
   at the end.  The returned cbuf has exact length.  */
{
  int c;
  register FILE *frew;
  register struct fro *fin;
  struct cbuf r;

  fin = FLOW (from);
  frew = FLOW (to);
  for (;;)
    {
      TEECHAR ();
      switch (c)
        {
        case '\n':
          ++LEX (lno);
          break;
        case SDELIM:
          TEECHAR ();
          if (c != SDELIM)
            {
              /* End of string.  */
              NEXT (c) = c;
              r.string = finish_string (SINGLE, &r.size);
              return r;
            }
          break;
        }
      accumulate_byte (SINGLE, c);
    }
}

static char const *
checkidentifier (char const *id, int delimiter, register bool dotok)
/* Check whether the string starting at ‘id’ is an identifier and return
   a pointer to the delimiter after the identifier.  White space,
   ‘delimiter’ and 0 are legal delimiters.  Abort the program if not a
   legal identifier.  Useful for checking commands.  If ‘!delimiter’,
   the only delimiter is 0.  Allow '.' in identifier only if ‘dotok’ is
   set.  */
{
  register char const *temp;
  register char c;
  register char delim = delimiter;
  bool isid = false;

  temp = id;
  for (;; id++)
    {
      switch (ctab[(unsigned char) (c = *id)])
        {
        case IDCHAR:
        case LETTER:
        case Letter:
          isid = true;
          continue;

        case DIGIT:
          continue;

        case PERIOD:
          if (dotok)
            continue;
          break;

        default:
          break;
        }
      break;
    }
  if (!isid || (c && (!delim || (c != delim
                                 && c != ' '
                                 && c != '\t'
                                 && c != '\n'))))
    {
      while ((c = *id) && c != ' ' && c != '\t' && c != '\n'
             && c != delim)
        id++;
      PFATAL ("invalid %s `%.*s'", dotok ? "identifier" : "symbol",
              id - temp, temp);
    }
  return id;
}

char const *
checkid (char const *id, int delimiter)
{
  return checkidentifier (id, delimiter, true);
}

char const *
checksym (char const *sym, int delimiter)
{
  return checkidentifier (sym, delimiter, false);
}

void
checksid (char const *id)
/* Check whether the string ‘id’ is an identifier.  */
{
  checkid (id, 0);
}

void
checkssym (char const *sym)
{
  checksym (sym, 0);
}

/* rcslex.c ends here */
