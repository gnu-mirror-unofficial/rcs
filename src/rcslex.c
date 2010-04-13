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

void
warnignore (void)
{
  if (!LEX (ignore))
    {
      /* This used to be a simple boolean, but we overload it now as a
         means to avoid the most infelicitous `NEXT (str)' clobbering.
         If/when that goes away, this can happily resvelten.  */
      LEX (ignore) = make_space ("ignore");
      RWARN ("Unknown phrases like `%s ...;' are present.", NEXT (str));
    }
}

static void
lookup (char const *str)
/* Look up the character string `str' in the hashtable.
   If the string is not present, a new entry for it is created.  In any
   case, the address of the corresponding hashtable entry is placed into
   `NEXT (hsh)'.  */
{
  register unsigned ihash;      /* index into hashtable */
  register char const *sp;
  register struct hshentry *n, **p;

  /* Calculate hash code.  */
  sp = str;
  ihash = 0;
  while (*sp)
    ihash = (ihash << 2) + *sp++;
  ihash %= hshsize;

  for (p = &LEX (hshtab)[ihash];; p = &n->nexthsh)
    if (!(n = *p))
      {
        /* Empty slot found.  */
        *p = n = FALLOC (struct hshentry);
        n->num = intern0 (SINGLE, str);
        n->nexthsh = NULL;
#ifdef LEXDB
        printf ("\nEntered: %s at %u ", str, ihash);
#endif
        break;
      }
    else if (strcmp (str, n->num) == 0)
      /* Match found.  */
      break;
  NEXT (hsh) = n;
  NEXT (str) = n->num;
}

void
Lexinit (void)
/* Initialize lexical analyzer: initialize the hashtable;
   initialize `NEXT (c)', `NEXT (tok)' if `FLOW (from)' != NULL.  */
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
      if (LEX (ignore))
        {
          close_space (LEX (ignore));
          LEX (ignore) = NULL;
        }
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
/* Read the next token and set `NEXT (tok)' to the next token code.
   Only if `receptive_to_next_hash_key', a revision number is entered
   into the hashtable and a pointer to it is placed into `NEXT (hsh)'.
   This is useful for avoiding that dates are placed into the hashtable.
   For ID's and NUM's, `NEXT (str)' is set to the character string.
   Assumption: `NEXT (c)' contains the next character.  */
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
#ifdef LEXDB
          afputc ('\n', stdout);
#endif
          /* Note: falls into next case */

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
             TODO: Make `nextlex' accept "disposition".  */
          NEXT (str) = intern (SINGLE, sp, len);
          break;

        case SBEGIN:           /* long string */
          d = STRING;
          /* Note: Only the initial SBEGIN has been read.
             Read the string, and reset `NEXT (c)' afterwards.  */
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
   `NEXT (c)' becomes undefined at end of file.  */
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
/* Check if `NEXT (tok)' is the same as `token'.  If so, advance the input
   by calling `nextlex' and return true.  Otherwise return false.
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
getkeyopt (char const *key)
/* If the current token is a keyword identical to `key',
   advance the input by calling `nextlex' and return true;
   otherwise return false.  */
{
  if (NEXT (tok) == ID && strcmp (key, NEXT (str)) == 0)
    {
      /* Match found.  */
      free_NEXT_str ();
      nextlex ();
      return true;
    }
  return false;
}

void
getkey (char const *key)
/* Check that the current input token is a keyword identical to `key',
   and advance the input by calling `nextlex'.  */
{
  if (!getkeyopt (key))
    fatal_syntax ("missing '%s' keyword", key);
}

void
getkeystring (char const *key)
/* Check that the current input token is a keyword identical to `key',
   and advance the input by calling `nextlex'; then look ahead for a
   string.  */
{
  getkey (key);
  if (NEXT (tok) != STRING)
    fatal_syntax ("missing string after '%s' keyword", key);
}

char const *
getid (void)
/* Check if `NEXT (tok)' is an identifier.  If so, advance the input by
   calling `nextlex' and return a pointer to the identifier; otherwise
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
/* Check if `NEXT (tok)' is a number.  If so, advance the input by calling
   `nextlex' and return a pointer to the hashtable entry.  Otherwise
   returns NULL.  Doesn't work if not `receptive_to_next_hash_key'.  */
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

struct cbuf
getphrases (char const *key)
/* Get a series of phrases that do not start with `key'.  Return
   resulting buffer.  Stop when the next phrase starts with a token that
   is not an identifier, or is `key'.  Copy input to `FLOW (to)' if it is
   set.  Unlike `ignorephrases', this routine assumes `nextlex' has
   already been invoked before we start.  */
{
  int c;
  register char const *kn;
  struct cbuf r;
  register struct fro *fin;
  register FILE *frew;

  if (NEXT (tok) != ID || strcmp (NEXT (str), key) == 0)
    clear_buf (&r);
  else
    {
      warnignore ();
      fin = FLOW (from);
      frew = FLOW (to);
      accumulate_nonzero_bytes (LEX (ignore), NEXT (str));
      free_NEXT_str ();
      c = NEXT (c);
      for (;;)
        {
#define SAVECH(c)  accumulate_byte (LEX (ignore), c)
          for (;;)
            {
              SAVECH (c);
              switch (ctab[c])
                {
                default:
                  fatal_syntax ("unknown character `%c'", c);
                case NEWLN:
                  ++LEX (lno);
                  /* fall into */
                case COLON:
                case DIGIT:
                case LETTER:
                case Letter:
                case PERIOD:
                case SPACE:
                  TEECHAR ();
                  continue;
                case SBEGIN:     /* long string */
                  for (;;)
                    {
                      for (;;)
                        {
                          TEECHAR ();
                          SAVECH (c);
                          switch (c)
                            {
                            case '\n':
                              ++LEX (lno);
                              /* fall into */
                            default:
                              continue;

                            case SDELIM:
                              break;
                            }
                          break;
                        }
                      TEECHAR ();
                      if (c != SDELIM)
                        break;
                      SAVECH (c);
                    }
                  continue;
                case SEMI:
                  GETCHAR (c, fin);
                  if (ctab[c] == NEWLN)
                    {
                      if (frew)
                        aputc (c, frew);
                      ++ LEX (lno);
                      SAVECH (c);
                      GETCHAR (c, fin);
                    }
                  for (;;)
                    {
                      switch (ctab[c])
                        {
                        case NEWLN:
                          ++LEX (lno);
                          /* fall into */
                        case SPACE:
                          GETCHAR (c, fin);
                          continue;

                        default:
                          break;
                        }
                      break;
                    }
                  if (frew)
                    aputc (c, frew);
                  break;
                }
              break;
            }
          if (ctab[c] == Letter)
            {
              register char const *ki;

              for (kn = key; c && *kn == c; kn++)
                TEECHAR ();
              if (!*kn)
                switch (ctab[c])
                  {
                  case DIGIT:
                  case LETTER:
                  case Letter:
                  case IDCHAR:
                  case PERIOD:
                    break;
                  default:
                    NEXT (c) = c;
                    /* FIXME: {Re-}move redundant `strlen' call.
                       All callers are key = Kfoo (constant strings).  */
                    NEXT (str) = intern0 (SINGLE, key);
                    NEXT (tok) = ID;
                    goto returnit;
                  }
              for (ki = key; ki < kn;)
                SAVECH (*ki++);
            }
          else
            {
              NEXT (c) = c;
              nextlex ();
              break;
            }
#undef SAVECH
        }
    returnit:;
      r.string = finish_string (LEX (ignore), &r.size);
    }
  return r;
}

void
readstring (void)
/* Skip over characters until terminating single `SDELIM'.
   If `FLOW (to)' is set, copy every character read to `FLOW (to)'.
   Do not advance `nextlex' at the end.  */
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
/* Copy a string to stdout, until terminated with a single `SDELIM'.
   Do not advance `nextlex' at the end.  */
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
/* Return a `SDELIM'-terminated cbuf read from file `FLOW (from)',
   replacing double `SDELIM' is with `SDELIM'.  If `FLOW (to)' is set,
   also copy (unsubstituted) to `FLOW (to)'.  Do not advance `nextlex'
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

static char *
checkidentifier (register char *id, int delimiter, register bool dotok)
/* Check whether the string starting at `id' is an identifier and return
   a pointer to the delimiter after the identifier.  White space,
   `delimiter' and 0 are legal delimiters.  Abort the program if not a
   legal identifier.  Useful for checking commands.  If `!delimiter',
   the only delimiter is 0.  Allow '.' in identifier only if `dotok' is
   set.  */
{
  register char *temp;
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
      /* Append '\0' to end of `id' before error message.  */
      while ((c = *id) && c != ' ' && c != '\t' && c != '\n'
             && c != delim)
        id++;
      *id = '\0';
      PFATAL ("invalid %s `%s'", dotok ? "identifier" : "symbol", temp);
    }
  return id;
}

char *
checkid (char *id, int delimiter)
{
  return checkidentifier (id, delimiter, true);
}

char *
checksym (char *sym, int delimiter)
{
  return checkidentifier (sym, delimiter, false);
}

void
checksid (char *id)
/* Check whether the string `id' is an identifier.  */
{
  checkid (id, 0);
}

void
checkssym (char *sym)
{
  checksym (sym, 0);
}

void
Ofclose (FILE *f)
{
  if (f && fclose (f) != 0)
    Oerror ();
}

void
Ozclose (FILE **p)
{
  Ofclose (*p);
  *p = NULL;
}

void
Orewind (FILE *f)
{
  if (fseek (f, 0L, SEEK_SET) != 0)
    Oerror ();
}

void
aflush (FILE *f)
{
  if (fflush (f) != 0)
    Oerror ();
}

void
oflush (void)
{
  if (fflush (MANI (standard_output)
              ? MANI (standard_output)
              : stdout)
      != 0 && !BE (Oerrloop))
    Oerror ();
}

void
redefined (int c)
{
  PWARN ("redefinition of -%c option", c);
}

void
afputc (int c, register FILE *f)
/* `afputc (c, f)' acts like `aputc (c, f)' but is smaller and slower.  */
{
  aputc (c, f);
}

void
aputs (char const *s, FILE *iop)
/* Put string `s' on file `iop', abort on error.  */
{
  if (fputs (s, iop) < 0)
    Oerror ();
}

void
aprintf (FILE * iop, char const *fmt, ...)
/* Formatted output.  Same as `fprintf' in <stdio.h>,
   but abort program on error.  */
{
  va_list ap;

  va_start (ap, fmt);
  if (0 > vfprintf (iop, fmt, ap))
    Oerror ();
  va_end (ap);
}

#ifdef LEXDB
/* The test program reads a stream of lexemes, enters the revision numbers
   into the hashtable, and prints the recognized tokens.  Keywords are
   recognized as identifiers.  */

static void
exiterr (void)
{
  _Exit (EXIT_FAILURE);
}

const struct program program =
  {
    .name = "lextest",
    .exiterr = exiterr
  };

int
main (int argc, char *argv[])
{
  if (argc < 2)
    {
      complain ("No input file\n");
      return EXIT_FAILURE;
    }
  if (!(FLOW (from) = fro_open (argv[1], FOPEN_R, NULL)))
    {
      PFATAL ("can't open input file %s", argv[1]);
    }
  Lexinit ();
  while (!eoflex ())
    {
      switch (NEXT (tok))
        {

        case ID:
          printf ("ID: %s", NEXT (str));
          break;

        case NUM:
          if (BE (receptive_to_next_hash_key))
            printf ("NUM: %s, index: %d", NEXT (hsh)->num,
                    NEXT (hsh) - LEX (hshtab));
          else
            printf ("NUM, unentered: %s", NEXT (str));
          /* Alternate between dates and numbers.  */
          BE (receptive_to_next_hash_key) = !BE (receptive_to_next_hash_key);
          break;

        case COLON:
          printf ("COLON");
          break;

        case SEMI:
          printf ("SEMI");
          break;

        case STRING:
          readstring ();
          printf ("STRING");
          break;

        case UNKN:
          printf ("UNKN");
          break;

        default:
          printf ("DEFAULT");
          break;
        }
      printf (" | ");
      nextlex ();
    }
  return EXIT_SUCCESS;
}

#endif  /* defined LEXDB */

/* rcslex.c ends here */
