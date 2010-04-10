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
#include "b-complain.h"
#include "b-divvy.h"
#include "b-isr.h"

/* Pointer to next hash entry, set by `lookup'.  */
static struct hshentry *nexthsh;

/* Our hash algorithm is h[0] = 0, h[i+1] = 4*h[i] + c,
   so hshsize should be odd.

   See B J McKenzie, R Harries & T Bell, Selecting a hashing algorithm,
   Software--practice & experience 20, 2 (Feb 1990), 209-224.  */
#ifndef hshsize
#define hshsize 511
#endif

/* Hashtable.  */
static struct hshentry *hshtab[hshsize];

/* Have we ignored phrases in this RCS file?  */
static bool ignored_phrases;

void
warnignore (void)
{
  if (!ignored_phrases)
    {
      ignored_phrases = true;
      RWARN ("Unknown phrases like `%s ...;' are present.", NEXT (str));
    }
}

static void
lookup (char const *str)
/* Look up the character string `str' in the hashtable.
   If the string is not present, a new entry for it is created.  In any
   case, the address of the corresponding hashtable entry is placed into
   `nexthsh'.  */
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

  for (p = &hshtab[ihash];; p = &n->nexthsh)
    if (!(n = *p))
      {
        /* Empty slot found.  */
        *p = n = ftalloc (struct hshentry);
        n->num = intern0 (single, str);
        n->nexthsh = NULL;
#ifdef LEXDB
        printf ("\nEntered: %s at %u ", str, ihash);
#endif
        break;
      }
    else if (strcmp (str, n->num) == 0)
      /* Match found.  */
      break;
  nexthsh = n;
  NEXT (str) = n->num;
}

void
Lexinit (void)
/* Initialize lexical analyzer: initialize the hashtable;
   initialize `NEXT (c)', `NEXT (tok)' if `FLOW (from)' != NULL.  */
{
  register int c;

  for (c = hshsize; 0 <= --c;)
    {
      hshtab[c] = NULL;
    }

  LEX (erroneousp) = false;
  if (FLOW (from))
    {
      FLOW (to) = NULL;
      BE (receptive_to_next_hash_key) = true;
      ignored_phrases = false;
      LEX (lno) = 1;
      if (LEX (tokbuf))
        forget (LEX (tokbuf));
      else
        LEX (tokbuf) = make_space ("tokbuf"); /* TODO: smaller chunk size */
      Iget (FLOW (from), NEXT (c));
      /* Initial token.  */
      nextlex ();
    }
}

void
nextlex (void)
/* Read the next token and set `NEXT (tok)' to the next token code.
   Only if `receptive_to_next_hash_key', a revision number is entered
   into the hashtable and a pointer to it is placed into `nexthsh'.
   This is useful for avoiding that dates are placed into the hashtable.
   For ID's and NUM's, `NEXT (str)' is set to the character string.
   Assumption: `NEXT (c)' contains the next character.  */
{
  register int c;
  declarecache;
  register FILE *frew;
  register char *sp;
  register enum tokens d;
  register RILE *fin;
  size_t len;

  fin = FLOW (from);
  frew = FLOW (to);
  setupcache (fin);
  cache (fin);
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
          GETC (frew, c);
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
              GETC (frew, c);
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
          NEXT (str) = intern (single, sp, len);
          break;

        case SBEGIN:           /* long string */
          d = STRING;
          /* Note: Only the initial SBEGIN has been read.
             Read the string, and reset `NEXT (c)' afterwards.  */
          break;

        case COLON:
        case SEMI:
          GETC (frew, c);
          break;
        }
      break;
    }
  NEXT (c) = c;
  NEXT (tok) = d;
  uncache (fin);
}

bool
eoflex (void)
/* Return true if we look ahead to the end of the input, false otherwise.
   `NEXT (c)' becomes undefined at end of file.  */
{
  register int c;
  declarecache;
  register FILE *fout;
  register RILE *fin;

  c = NEXT (c);
  fin = FLOW (from);
  fout = FLOW (to);
  setupcache (fin);
  cache (fin);

  for (;;)
    {
      switch (ctab[c])
        {
        default:
          NEXT (c) = c;
          uncache (fin);
          return false;

        case NEWLN:
          ++LEX (lno);
          /* fall into */
        case SPACE:
          cachegeteof (c,
                       {
                         uncache (fin);
                         return true;
                       });
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
      num = nexthsh;
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
  declarecache;
  register int c;
  register char const *kn;
  struct cbuf r;
  register RILE *fin;
  register FILE *frew;
#if large_memory
#define savech_(c) ;
#else  /* !large_memory */
  register char *p;
  char const *limit;
  struct buf b;
#define savech_(c)  { if (limit <= p) p = bufenlarge (&b, &limit); *p++ = (c); }
#endif  /* !large_memory */

  if (NEXT (tok) != ID || strcmp (NEXT (str), key) == 0)
    clear_buf (&r);
  else
    {
      warnignore ();
      fin = FLOW (from);
      frew = FLOW (to);
      setupcache (fin);
      cache (fin);
#if large_memory
      r.string = (char const *) cacheptr () - strlen (NEXT (str)) - 1;
#else  /* !large_memory */
      bufautobegin (&b);
      bufscpy (&b, NEXT (str));
      p = b.string + strlen (b.string);
      limit = b.string + b.size;
#endif  /* !large_memory */
      free_NEXT_str ();
      c = NEXT (c);
      for (;;)
        {
          for (;;)
            {
              savech_ (c)
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
                  GETC (frew, c);
                  continue;
                case SBEGIN:     /* long string */
                  for (;;)
                    {
                      for (;;)
                        {
                          GETC (frew, c);
                          savech_ (c)
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
                      GETC (frew, c);
                      if (c != SDELIM)
                        break;
                      savech_ (c)
                    }
                  continue;
                case SEMI:
                  cacheget (c);
                  if (ctab[c] == NEWLN)
                    {
                      if (frew)
                        aputc (c, frew);
                      ++ LEX (lno);
                      savech_ (c)
                      cacheget (c);
                    }
#if large_memory
                  r.size = (char const *) cacheptr () - 1 - r.string;
#endif
                  for (;;)
                    {
                      switch (ctab[c])
                        {
                        case NEWLN:
                          ++LEX (lno);
                          /* fall into */
                        case SPACE:
                          cacheget (c);
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
              for (kn = key; c && *kn == c; kn++)
                GETC (frew, c);
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
                    NEXT (str) = intern0 (single, key);
                    NEXT (tok) = ID;
                    uncache (fin);
                    goto returnit;
                  }
#if !large_memory
              {
                register char const *ki;

                for (ki = key; ki < kn;)
                  savech_ (*ki++)
              }
#endif
            }
          else
            {
              NEXT (c) = c;
              uncache (fin);
              nextlex ();
              break;
            }
        }
    returnit:;
#if !large_memory
      r.string = (r.size = p - b.string)
        ? intern (single, b.string, r.size)
        : "";
      bufautoend (&b);
#endif
    }
  return r;
}

void
readstring (void)
/* Skip over characters until terminating single `SDELIM'.
   If `FLOW (to)' is set, copy every character read to `FLOW (to)'.
   Do not advance `nextlex' at the end.  */
{
  register int c;
  declarecache;
  register FILE *frew;
  register RILE *fin;

  fin = FLOW (from);
  frew = FLOW (to);
  setupcache (fin);
  cache (fin);
  for (;;)
    {
      GETC (frew, c);
      switch (c)
        {
        case '\n':
          ++LEX (lno);
          break;

        case SDELIM:
          GETC (frew, c);
          if (c != SDELIM)
            {
              /* End of string.  */
              NEXT (c) = c;
              uncache (fin);
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
  register int c;
  declarecache;
  register FILE *fout;
  register RILE *fin;

  fin = FLOW (from);
  fout = stdout;
  setupcache (fin);
  cache (fin);
  for (;;)
    {
      cacheget (c);
      switch (c)
        {
        case '\n':
          ++LEX (lno);
          break;
        case SDELIM:
          cacheget (c);
          if (c != SDELIM)
            {
              NEXT (c) = c;
              uncache (fin);
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
  register int c;
  declarecache;
  register FILE *frew;
  register RILE *fin;
  struct cbuf r;

  fin = FLOW (from);
  frew = FLOW (to);
  setupcache (fin);
  cache (fin);
  for (;;)
    {
      GETC (frew, c);
      switch (c)
        {
        case '\n':
          ++LEX (lno);
          break;
        case SDELIM:
          GETC (frew, c);
          if (c != SDELIM)
            {
              /* End of string.  */
              NEXT (c) = c;
              r.string = finish_string (single, &r.size);
              uncache (fin);
              return r;
            }
          break;
        }
      accumulate_byte (single, c);
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

#if !large_memory
#define Iclose(f) fclose(f)
#else  /* large_memory */
#if !maps_memory
static int
Iclose (register RILE *f)
{
  tfree (f->base);
  f->base = NULL;
  return fclose (f->stream);
}
#else  /* maps_memory */
static int
Iclose (register RILE *f)
{
  (*f->deallocate) (f);
  f->base = NULL;
  return close (f->fd);
}

#if defined HAVE_MMAP
static void
mmap_deallocate (register RILE *f)
{
  if (munmap ((char *) f->base, (size_t) (f->lim - f->base)) != 0)
    fatal_sys ("munmap");
}
#endif  /* defined HAVE_MMAP */

static void
read_deallocate (RILE *f)
{
  tfree (f->base);
}

static void
nothing_to_deallocate (RILE *f RCS_UNUSED)
{
}
#endif  /* maps_memory */
#endif  /* large_memory */

void
hey_trundling (bool sequentialp, RILE *f)
/* Advise the mmap machinery (if applicable) that access to `f'
   is sequential if `sequentialp', otherwise normal.  */
{
#if defined HAVE_MADVISE && defined HAVE_MMAP && large_memory
  if (f->deallocate == mmap_deallocate)
    madvise ((char *) f->base, (size_t) (f->lim - f->base),
             sequentialp ? MADV_SEQUENTIAL : MADV_NORMAL);
#endif
}

#if large_memory && maps_memory
static RILE *
fd2_RILE (int fd, char const *name, register struct stat *status)
#else
static RILE *
fd2RILE (int fd, char const *name, char const *type,
         register struct stat *status)
#endif
{
  struct stat st;

  if (!status)
    status = &st;
  if (fstat (fd, status) != 0)
    fatal_sys (name);
  if (!S_ISREG (status->st_mode))
    {
      PERR ("`%s' is not a regular file", name);
      close (fd);
      errno = EINVAL;
      return NULL;
    }
  else
    {
#if !(large_memory && maps_memory)
      FILE *stream;

      if (!(stream = fdopen (fd, type)))
        fatal_sys (name);
#endif  /* !(large_memory && maps_memory) */

#if !large_memory
      return stream;
#else  /* large_memory */
#define RILES 3
      {
        static RILE rilebuf[RILES];

        register RILE *f;
        off_t s = status->st_size;

        if (s != status->st_size)
          PFATAL ("%s: too large", name);
        for (f = rilebuf; f->base; f++)
          if (f == rilebuf + RILES)
            PFATAL ("too many RILEs");
#if maps_memory
        f->deallocate = nothing_to_deallocate;
#endif
        if (!s)
          f->base = (void *) &FLOW (to); /* Any nonzero address will do.  */
        else
          {
            f->base = NULL;
#if defined HAVE_MMAP
            if (!f->base)
              {
                ISR_DO (CATCHMMAPINTS);
                f->base = (unsigned char *) mmap (NULL, s,
                                                  PROT_READ,
                                                  MAP_SHARED, fd,
                                                  (off_t) 0);
#ifndef MAP_FAILED
#define MAP_FAILED (-1)
#endif
                if (f->base == (unsigned char *) MAP_FAILED)
                  f->base = NULL;
                else
                  {
#if has_NFS && MMAP_SIGNAL
                    /* On many hosts, the superuser can mmap an NFS file
                       it can't read.  So access the first page now, and
                       print a nice message if a bus error occurs.  */
                    access_page (ISR_SCRATCH, name, f->base);
#endif  /* has_NFS && MMAP_SIGNAL */
                  }
                f->deallocate = mmap_deallocate;
              }
#endif  /* defined HAVE_MMAP */
            if (!f->base)
              {
                f->base = testalloc (s);
#if maps_memory
                {
                  /* We can't map the file into memory for some reason.
                     Read it into main memory all at once; this is
                     the simplest substitute for memory mapping.  */
                  char *bufptr = (char *) f->base;
                  size_t bufsiz = s;

                  do
                    {
                      ssize_t r = read (fd, bufptr, bufsiz);

                      switch (r)
                        {
                        case -1:
                          fatal_sys (name);

                        case 0:
                          /* The file must have shrunk!  */
                          status->st_size = s -= bufsiz;
                          bufsiz = 0;
                          break;

                        default:
                          bufptr += r;
                          bufsiz -= r;
                          break;
                        }
                    }
                  while (bufsiz);
                  if (lseek (fd, (off_t) 0, SEEK_SET) == -1)
                    fatal_sys (name);
                  f->deallocate = read_deallocate;
                }
#endif  /* maps_memory */
              }
          }
        f->ptr = f->base;
        f->lim = f->base + s;
        f->fd = fd;
#if !maps_memory
        f->readlim = f->base;
        f->stream = stream;
#endif  /* !maps_memory */
        if (s)
          hey_trundling (true, f);
        return f;
      }
#endif  /* large_memory */
    }
}

#if !maps_memory && large_memory
bool
Igetmore (register RILE *f)
{
  register size_t r;
  register size_t s = f->lim - f->readlim;

  if (BUFSIZ < s)
    s = BUFSIZ;
  if (! (r = fread (f->readlim, sizeof (*f->readlim), s, f->stream)))
    {
      testIerror (f->stream);
      /* The file might have shrunk!  */
      f->lim = f->readlim;
      return false;
    }
  f->readlim += r;
  return true;
}
#endif  /* !maps_memory && large_memory */

RILE *
#if large_memory && maps_memory
I_open (char const *name, struct stat *status)
#else
Iopen (char const *name, char const *type, struct stat *status)
#endif
/* Open `name' for reading, return its descriptor, and set `*status'.  */
{
  int fd = fdSafer (open (name, O_RDONLY
#if OPEN_O_BINARY
                          | (strchr (type, 'b') ? OPEN_O_BINARY :
                             0)
#endif
                          ));

  if (fd < 0)
    return NULL;
#if large_memory && maps_memory
  return fd2_RILE (fd, name, status);
#else
  return fd2RILE (fd, name, type, status);
#endif
}

static bool Oerrloop;

void
Oerror (void)
{
  if (Oerrloop)
    PROGRAM (exiterr) ();
  Oerrloop = true;
  fatal_sys ("output error");
}

void
Ieof (void)
{
  fatal_syntax ("unexpected end of file");
}

void
Ierror (void)
{
  fatal_sys ("input error");
}

void
testIerror (FILE *f)
{
  if (ferror (f))
    Ierror ();
}

void
testOerror (FILE *o)
{
  if (ferror (o))
    Oerror ();
}

void
Ifclose (RILE *f)
{
  if (f && Iclose (f) != 0)
    Ierror ();
}

void
Ofclose (FILE *f)
{
  if (f && fclose (f) != 0)
    Oerror ();
}

void
Izclose (RILE **p)
{
  Ifclose (*p);
  *p = NULL;
}

void
Ozclose (FILE **p)
{
  Ofclose (*p);
  *p = NULL;
}

#if !large_memory
void
testIeof (FILE *f)
{
  testIerror (f);
  if (feof (f))
    Ieof ();
}

void
Irewind (FILE *f)
{
  if (fseek (f, 0L, SEEK_SET) != 0)
    Ierror ();
}
#endif  /* !large_memory */

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
      != 0 && !Oerrloop)
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
  if (!(FLOW (from) = Iopen (argv[1], FOPEN_R, NULL)))
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
            printf ("NUM: %s, index: %d", nexthsh->num,
                    nexthsh - hshtab);
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
