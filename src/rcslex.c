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

/******************************************************************************
 *                     Lexical Analysis.
 *                     hashtable, Lexinit, nextlex, getlex, getkey,
 *                     getid, getnum, readstring, printstring, savestring,
 *                     checkid, fatserror, error, faterror, warn, diagnose
 *                     Testprogram: define LEXDB
 ******************************************************************************
 */


/*
#define LEXDB
*/
/* version LEXDB is for testing the lexical analyzer. The testprogram
 * reads a stream of lexemes, enters the revision numbers into the
 * hashtable, and prints the recognized tokens. Keywords are recognized
 * as identifiers.
 */

#include "rcsbase.h"

static char *checkidentifier (char *, int, int);
static void errsay (char const *);
static void fatsay (char const *);
static void lookup (char const *);
static void startsay (const char *, const char *);
static void warnsay (char const *);

static struct hshentry *nexthsh;        /*pointer to next hash entry, set by lookup */

enum tokens nexttok;            /*next token, set by nextlex                    */

int hshenter;                   /*if true, next suitable lexeme will be entered */
                            /*into the symbol table. Handle with care.      */
int nextc;                      /*next input character, initialized by Lexinit  */

long rcsline;                   /*current line-number of input                  */
int nerror;                     /*counter for errors                            */
int quietflag;                  /*indicates quiet mode                          */
RILE *finptr;                   /*input file descriptor                         */

FILE *frewrite;                 /*file descriptor for echoing input             */

FILE *foutptr;                  /* copy of frewrite, but 0 to suppress echo  */

static struct buf tokbuf;       /* token buffer                                 */

char const *NextString;         /* next token                                   */

/*
 * Our hash algorithm is h[0] = 0, h[i+1] = 4*h[i] + c,
 * so hshsize should be odd.
 * See B J McKenzie, R Harries & T Bell, Selecting a hashing algorithm,
 * Software--practice & experience 20, 2 (Feb 1990), 209-224.
 */
#ifndef hshsize
#	define hshsize 511
#endif

static struct hshentry *hshtab[hshsize];        /*hashtable                        */

static int ignored_phrases;     /* have we ignored phrases in this RCS file? */

void
warnignore (void)
{
  if (!ignored_phrases)
    {
      ignored_phrases = true;
      rcswarn ("Unknown phrases like `%s ...;' are present.", NextString);
    }
}

static void
lookup (char const *str)
/* Function: Looks up the character string pointed to by str in the
 * hashtable. If the string is not present, a new entry for it is created.
 * In any case, the address of the corresponding hashtable entry is placed
 * into nexthsh.
 */
{
  register unsigned ihash;      /* index into hashtable */
  register char const *sp;
  register struct hshentry *n, **p;

  /* calculate hash code */
  sp = str;
  ihash = 0;
  while (*sp)
    ihash = (ihash << 2) + *sp++;
  ihash %= hshsize;

  for (p = &hshtab[ihash];; p = &n->nexthsh)
    if (!(n = *p))
      {
        /* empty slot found */
        *p = n = ftalloc (struct hshentry);
        n->num = fstr_save (str);
        n->nexthsh = 0;
#			ifdef LEXDB
        printf ("\nEntered: %s at %u ", str, ihash);
#			endif
        break;
      }
    else if (strcmp (str, n->num) == 0)
      /* match found */
      break;
  nexthsh = n;
  NextString = n->num;
}

void
Lexinit (void)
/* Function: Initialization of lexical analyzer:
 * initializes the hashtable,
 * initializes nextc, nexttok if finptr != 0
 */
{
  register int c;

  for (c = hshsize; 0 <= --c;)
    {
      hshtab[c] = 0;
    }

  nerror = 0;
  if (finptr)
    {
      foutptr = 0;
      hshenter = true;
      ignored_phrases = false;
      rcsline = 1;
      bufrealloc (&tokbuf, 2);
      Iget_ (finptr, nextc)
      nextlex (); /*initial token */
    }
}

void
nextlex (void)
/* Function: Reads the next token and sets nexttok to the next token code.
 * Only if hshenter is set, a revision number is entered into the
 * hashtable and a pointer to it is placed into nexthsh.
 * This is useful for avoiding that dates are placed into the hashtable.
 * For ID's and NUM's, NextString is set to the character string.
 * Assumption: nextc contains the next character.
 */
{
  register int c;
  declarecache;
  register FILE *frew;
  register char *sp;
  char const *limit;
  register enum tokens d;
  register RILE *fin;

  fin = finptr;
  frew = foutptr;
  setupcache (fin);
  cache (fin);
  c = nextc;

  for (;;)
    {
      switch ((d = ctab[c]))
        {

        default:
          fatserror ("unknown character `%c'", c);
         /*NOTREACHED*/ case NEWLN:
          ++rcsline;
#               ifdef LEXDB
          afputc ('\n', stdout);
#               endif
          /* Note: falls into next case */

        case SPACE:
          GETC_ (frew, c)
          continue;

        case IDCHAR:
        case LETTER:
        case Letter:
          d = ID;
          /* fall into */
        case DIGIT:
        case PERIOD:
          sp = tokbuf.string;
          limit = sp + tokbuf.size;
          *sp++ = c;
          for (;;)
            {
              GETC_ (frew, c)
              switch (ctab[c])
                {
                case IDCHAR:
                case LETTER:
                case Letter:
                  d = ID;
                  /* fall into */
                case DIGIT:
                case PERIOD:
                  *sp++ = c;
                  if (limit <= sp)
                    sp = bufenlarge (&tokbuf, &limit);
                  continue;

                default:
                  break;
                }
              break;
            }
          *sp = 0;
          if (d == DIGIT || d == PERIOD)
            {
              d = NUM;
              if (hshenter)
                {
                  lookup (tokbuf.string);
                  break;
                }
            }
          NextString = fstr_save (tokbuf.string);
          break;

        case SBEGIN:           /* long string */
          d = STRING;
          /* note: only the initial SBEGIN has been read */
          /* read the string, and reset nextc afterwards */
          break;

        case COLON:
        case SEMI:
          GETC_ (frew, c)
          break;
        }
      break;
    }
  nextc = c;
  nexttok = d;
  uncache (fin);
}

int
eoflex (void)
/*
 * Yield true if we look ahead to the end of the input, false otherwise.
 * nextc becomes undefined at end of file.
 */
{
  register int c;
  declarecache;
  register FILE *fout;
  register RILE *fin;

  c = nextc;
  fin = finptr;
  fout = foutptr;
  setupcache (fin);
  cache (fin);

  for (;;)
    {
      switch (ctab[c])
        {
        default:
          nextc = c;
          uncache (fin);
          return false;

        case NEWLN:
          ++rcsline;
          /* fall into */
        case SPACE:
          cachegeteof_ (c,
                        {
                        uncache (fin);
                        return true;
                        })
          break;
        }
      if (fout)
        aputc_ (c, fout)
    }
}

int
getlex (enum tokens token)
/* Function: Checks if nexttok is the same as token. If so,
 * advances the input by calling nextlex and returns true.
 * otherwise returns false.
 * Doesn't work for strings and keywords; loses the character string for ids.
 */
{
  if (nexttok == token)
    {
      nextlex ();
      return (true);
    }
  else
    return (false);
}

int
getkeyopt (char const *key)
/* Function: If the current token is a keyword identical to key,
 * advances the input by calling nextlex and returns true;
 * otherwise returns false.
 */
{
  if (nexttok == ID && strcmp (key, NextString) == 0)
    {
      /* match found */
      ffree1 (NextString);
      nextlex ();
      return (true);
    }
  return (false);
}

void
getkey (char const *key)
/* Check that the current input token is a keyword identical to key,
 * and advance the input by calling nextlex.
 */
{
  if (!getkeyopt (key))
    fatserror ("missing '%s' keyword", key);
}

void
getkeystring (char const *key)
/* Check that the current input token is a keyword identical to key,
 * and advance the input by calling nextlex; then look ahead for a string.
 */
{
  getkey (key);
  if (nexttok != STRING)
    fatserror ("missing string after '%s' keyword", key);
}

char const *
getid (void)
/* Function: Checks if nexttok is an identifier. If so,
 * advances the input by calling nextlex and returns a pointer
 * to the identifier; otherwise returns 0.
 * Treats keywords as identifiers.
 */
{
  register char const *name;
  if (nexttok == ID)
    {
      name = NextString;
      nextlex ();
      return name;
    }
  else
    return 0;
}

struct hshentry *
getnum (void)
/* Function: Checks if nexttok is a number. If so,
 * advances the input by calling nextlex and returns a pointer
 * to the hashtable entry.  Otherwise returns 0.
 * Doesn't work if hshenter is false.
 */
{
  register struct hshentry *num;
  if (nexttok == NUM)
    {
      num = nexthsh;
      nextlex ();
      return num;
    }
  else
    return 0;
}

struct cbuf
getphrases (char const *key)
/*
* Get a series of phrases that do not start with KEY.  Yield resulting buffer.
* Stop when the next phrase starts with a token that is not an identifier,
* or is KEY.  Copy input to foutptr if it is set.  Unlike ignorephrases(),
* this routine assumes nextlex() has already been invoked before we start.
*/
{
  declarecache;
  register int c;
  register char const *kn;
  struct cbuf r;
  register RILE *fin;
  register FILE *frew;
#   if large_memory
#	define savech_(c) ;
#   else
  register char *p;
  char const *limit;
  struct buf b;
#	define savech_(c) {if (limit<=p)p=bufenlarge(&b,&limit); *p++ =(c);}
#   endif

  if (nexttok != ID || strcmp (NextString, key) == 0)
    clear_buf (&r);
  else
    {
      warnignore ();
      fin = finptr;
      frew = foutptr;
      setupcache (fin);
      cache (fin);
#	if large_memory
      r.string = (char const *) cacheptr () - strlen (NextString) - 1;
#	else
      bufautobegin (&b);
      bufscpy (&b, NextString);
      p = b.string + strlen (b.string);
      limit = b.string + b.size;
#	endif
      ffree1 (NextString);
      c = nextc;
      for (;;)
        {
          for (;;)
            {
              savech_ (c)
              switch (ctab[c])
                {
                default:
                  fatserror ("unknown character `%c'", c);
                  /*NOTREACHED*/ case NEWLN:
                  ++rcsline;
                  /* fall into */
                case COLON:
                case DIGIT:
                case LETTER:
                case Letter:
                case PERIOD:
                case SPACE:
                  GETC_ (frew, c)
                  continue;
                case SBEGIN:     /* long string */
                  for (;;)
                    {
                      for (;;)
                        {
                          GETC_ (frew, c)
                          savech_ (c)
                          switch (c)
                            {
                            case '\n':
                              ++rcsline;
                              /* fall into */
                            default:
                              continue;

                            case SDELIM:
                              break;
                            }
                          break;
                        }
                      GETC_ (frew, c)
                      if (c != SDELIM)
                        break;
                      savech_ (c)
                    }
                  continue;
                case SEMI:
                  cacheget_ (c)
                  if (ctab[c] == NEWLN)
                    {
                      if (frew)
                        aputc_ (c, frew)
                      ++ rcsline;
                      savech_ (c)
                      cacheget_ (c)
                    }
#			if large_memory
                  r.size = (char const *) cacheptr () - 1 - r.string;
#			endif
                  for (;;)
                    {
                      switch (ctab[c])
                        {
                        case NEWLN:
                          ++rcsline;
                          /* fall into */
                        case SPACE:
                          cacheget_ (c)
                          continue;

                        default:
                          break;
                        }
                      break;
                    }
                  if (frew)
                    aputc_ (c, frew)
                  break;
                }
              break;
            }
          if (ctab[c] == Letter)
            {
              for (kn = key; c && *kn == c; kn++)
                GETC_ (frew, c)
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
                    nextc = c;
                    NextString = fstr_save (key);
                    nexttok = ID;
                    uncache (fin);
                    goto returnit;
                  }
#		    if !large_memory
              {
                register char const *ki;
                for (ki = key; ki < kn;)
                  savech_ (*ki++)
              }
#		    endif
            }
          else
            {
              nextc = c;
              uncache (fin);
              nextlex ();
              break;
            }
        }
    returnit:;
#	if !large_memory
      return bufremember (&b, (size_t) (p - b.string));
#	endif
    }
  return r;
}

void
readstring (void)
/* skip over characters until terminating single SDELIM        */
/* If foutptr is set, copy every character read to foutptr.    */
/* Does not advance nextlex at the end.                        */
{
  register int c;
  declarecache;
  register FILE *frew;
  register RILE *fin;
  fin = finptr;
  frew = foutptr;
  setupcache (fin);
  cache (fin);
  for (;;)
    {
      GETC_ (frew, c)
      switch (c)
        {
        case '\n':
          ++rcsline;
          break;

        case SDELIM:
          GETC_ (frew, c)
          if (c != SDELIM)
            {
              /* end of string */
              nextc = c;
              uncache (fin);
              return;
            }
          break;
        }
    }
}

void
printstring (void)
/* Function: copy a string to stdout, until terminated with a single SDELIM.
 * Does not advance nextlex at the end.
 */
{
  register int c;
  declarecache;
  register FILE *fout;
  register RILE *fin;
  fin = finptr;
  fout = stdout;
  setupcache (fin);
  cache (fin);
  for (;;)
    {
      cacheget_ (c)
      switch (c)
        {
        case '\n':
          ++rcsline;
          break;
        case SDELIM:
          cacheget_ (c)
          if (c != SDELIM)
            {
              nextc = c;
              uncache (fin);
              return;
            }
          break;
        }
      aputc_ (c, fout)
    }
}

struct cbuf
savestring (struct buf *target)
/* Copies a string terminated with SDELIM from file finptr to buffer target.
 * Double SDELIM is replaced with SDELIM.
 * If foutptr is set, the string is also copied unchanged to foutptr.
 * Does not advance nextlex at the end.
 * Yield a copy of *TARGET, except with exact length.
 */
{
  register int c;
  declarecache;
  register FILE *frew;
  register char *tp;
  register RILE *fin;
  char const *limit;
  struct cbuf r;

  fin = finptr;
  frew = foutptr;
  setupcache (fin);
  cache (fin);
  tp = target->string;
  limit = tp + target->size;
  for (;;)
    {
      GETC_ (frew, c)
      switch (c)
        {
        case '\n':
          ++rcsline;
          break;
        case SDELIM:
          GETC_ (frew, c)
          if (c != SDELIM)
            {
              /* end of string */
              nextc = c;
              r.string = target->string;
              r.size = tp - r.string;
              uncache (fin);
              return r;
            }
          break;
        }
      if (tp == limit)
        tp = bufenlarge (target, &limit);
      *tp++ = c;
    }
}

static char *
checkidentifier (register char *id, int delimiter, register int dotok)
/*   Function:  check whether the string starting at id is an   */
/*		identifier and return a pointer to the delimiter*/
/*		after the identifier.  White space, delim and 0 */
/*              are legal delimiters.  Aborts the program if not*/
/*              a legal identifier. Useful for checking commands*/
/*		If !delim, the only delimiter is 0.		*/
/*		Allow '.' in identifier only if DOTOK is set.   */
{
  register char *temp;
  register char c;
  register char delim = delimiter;
  int isid = false;

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
  if (!isid
      || (c
          && (!delim
              || (c != delim && c != ' ' && c != '\t'
                  && c != '\n'))))
    {
      /* append \0 to end of id before error message */
      while ((c = *id) && c != ' ' && c != '\t' && c != '\n'
             && c != delim)
        id++;
      *id = '\0';
      faterror ("invalid %s `%s'", dotok ? "identifier" : "symbol", temp);
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
/* Check whether the string ID is an identifier.  */
{
  checkid (id, 0);
}

void
checkssym (char *sym)
{
  checksym (sym, 0);
}

#if !large_memory
#   define Iclose(f) fclose(f)
#else
# if !maps_memory
static int Iclose (RILE *);
static int
Iclose (register RILE *f)
{
  tfree (f->base);
  f->base = 0;
  return fclose (f->stream);
}
# else
static int Iclose (RILE *);
static int
Iclose (register RILE *f)
{
  (*f->deallocate) (f);
  f->base = 0;
  return close (f->fd);
}

#   if has_map_fd
static void
map_fd_deallocate (RILE *);
static void
map_fd_deallocate (register RILE *f)
{
  if (vm_deallocate (task_self (),
                     (vm_address_t) f->base,
                     (vm_size_t) (f->lim - f->base)) !=
      KERN_SUCCESS)
    efaterror ("vm_deallocate");
}
#   endif
#   if has_mmap
static void mmap_deallocate (RILE *);
static void
mmap_deallocate (register RILE *f)
{
  if (munmap ((char *) f->base, (size_t) (f->lim - f->base)) != 0)
    efaterror ("munmap");
}
#   endif
static void read_deallocate (RILE *);
static void
read_deallocate (RILE *f)
{
  tfree (f->base);
}

static void nothing_to_deallocate (RILE *);
static void
nothing_to_deallocate (RILE *f)
{
}
# endif
#endif

#if large_memory && maps_memory
static RILE *fd2_RILE (int, char const *, struct stat *);
static RILE *
fd2_RILE (int fd, char const *name, register struct stat *status)
#else
static RILE * fd2RILE (int, char const *, char const *, struct stat *);
static RILE *
fd2RILE (int fd, char const *name, char const *type,
         register struct stat *status)
#endif
{
  struct stat st;

  if (!status)
    status = &st;
  if (fstat (fd, status) != 0)
    efaterror (name);
  if (!S_ISREG (status->st_mode))
    {
      error ("`%s' is not a regular file", name);
      close (fd);
      errno = EINVAL;
      return 0;
    }
  else
    {

#	    if !(large_memory && maps_memory)
      FILE *stream;
      if (!(stream = fdopen (fd, type)))
        efaterror (name);
#	    endif

#	    if !large_memory
      return stream;
#	    else
#		define RILES 3
      {
        static RILE rilebuf[RILES];

        register RILE *f;
        size_t s = status->st_size;

        if (s != status->st_size)
          faterror ("%s: too large", name);
        for (f = rilebuf; f->base; f++)
          if (f == rilebuf + RILES)
            faterror ("too many RILEs");
#		if maps_memory
        f->deallocate = nothing_to_deallocate;
#		endif
        if (!s)
          {
            static unsigned char nothing;
            f->base = &nothing;     /* Any nonzero address will do.  */
          }
        else
          {
            f->base = 0;
#		    if has_map_fd
            map_fd (fd, (vm_offset_t) 0,
                    (vm_address_t *) & f->base, TRUE,
                    (vm_size_t) s);
            f->deallocate = map_fd_deallocate;
#		    endif
#		    if has_mmap
            if (!f->base)
              {
                catchmmapints ();
                f->base = (unsigned char *) mmap ((char *) 0, s,
                                                  PROT_READ,
                                                  MAP_SHARED, fd,
                                                  (off_t) 0);
#			    ifndef MAP_FAILED
#			    define MAP_FAILED (-1)
#			    endif
                if (f->base == (unsigned char *) MAP_FAILED)
                  f->base = 0;
                else
                  {
#				if has_NFS && mmap_signal
                    /*
                     * On many hosts, the superuser
                     * can mmap an NFS file it can't read.
                     * So access the first page now, and print
                     * a nice message if a bus error occurs.
                     */
                    readAccessFilenameBuffer (name, f->base);
#				endif
                  }
                f->deallocate = mmap_deallocate;
              }
#		    endif
            if (!f->base)
              {
                f->base = tnalloc (unsigned char, s);
#			if maps_memory
                {
                  /*
                   * We can't map the file into memory for some reason.
                   * Read it into main memory all at once; this is
                   * the simplest substitute for memory mapping.
                   */
                  char *bufptr = (char *) f->base;
                  size_t bufsiz = s;
                  do
                    {
                      ssize_t r = read (fd, bufptr, bufsiz);
                      switch (r)
                        {
                        case -1:
                          efaterror (name);

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
                    efaterror (name);
                  f->deallocate = read_deallocate;
                }
#			endif
              }
          }
        f->ptr = f->base;
        f->lim = f->base + s;
        f->fd = fd;
#		if !maps_memory
        f->readlim = f->base;
        f->stream = stream;
#		endif
        if_advise_access (s, f, MADV_SEQUENTIAL);
        return f;
      }
#	    endif
    }
}

#if !maps_memory && large_memory
int
Igetmore (register RILE *f)
{
  register fread_type r;
  register size_t s = f->lim - f->readlim;

  if (BUFSIZ < s)
    s = BUFSIZ;
  if (! (r = Fread (f->readlim, sizeof (*f->readlim), s, f->stream)))
    {
      testIerror (f->stream);
      f->lim = f->readlim;  /* The file might have shrunk!  */
      return 0;
    }
  f->readlim += r;
  return 1;
}
#endif

#if has_madvise && has_mmap && large_memory
void
advise_access (register RILE *f, int advice)
{
  if (f->deallocate == mmap_deallocate)
    madvise ((char *) f->base, (size_t) (f->lim - f->base), advice);
  /* Don't worry if madvise fails; it's only advisory.  */
}
#endif

RILE *
#if large_memory && maps_memory
I_open (char const *name, struct stat *status)
#else
Iopen (char const *name, char const *type, struct stat *status)

#endif
/* Open NAME for reading, yield its descriptor, and set *STATUS.  */
{
  int fd = fdSafer (open (name, O_RDONLY
#		if OPEN_O_BINARY
                          | (strchr (type, 'b') ? OPEN_O_BINARY :
                             0)
#		endif
                          ));

  if (fd < 0)
    return 0;
#	if large_memory && maps_memory
  return fd2_RILE (fd, name, status);
#	else
  return fd2RILE (fd, name, type, status);
#	endif
}

static int Oerrloop;

void
Oerror (void)
{
  if (Oerrloop)
    exiterr ();
  Oerrloop = true;
  efaterror ("output error");
}

void
Ieof (void)
{
  fatserror ("unexpected end of file");
}
void
Ierror (void)
{
  efaterror ("input error");
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
  *p = 0;
}
void
Ozclose (FILE **p)
{
  Ofclose (*p);
  *p = 0;
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
#endif

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
eflush (void)
{
  if (fflush (stderr) != 0 && !Oerrloop)
    Oerror ();
}
void
oflush (void)
{
  if (fflush (workstdout ? workstdout : stdout) != 0 && !Oerrloop)
    Oerror ();
}

void
fatcleanup (int already_newline)
{
  fprintf (stderr, already_newline + "\n%s aborted\n", cmdid);
  exiterr ();
}

static void
startsay (const char *s, const char *t)
{
  oflush ();
  if (s)
    aprintf (stderr, "%s: %s: %s", cmdid, s, t);
  else
    aprintf (stderr, "%s: %s", cmdid, t);
}

static void
fatsay (char const *s)
{
  startsay (s, "");
}

static void
errsay (char const *s)
{
  fatsay (s);
  nerror++;
}

static void
warnsay (char const *s)
{
  startsay (s, "warning: ");
}

void
eerror (char const *s)
{
  enerror (errno, s);
}

void
enerror (int e, char const *s)
{
  errsay ((char const *) 0);
  errno = e;
  perror (s);
  eflush ();
}

void
efaterror (char const *s)
{
  enfaterror (errno, s);
}

void
enfaterror (int e, char const *s)
{
  fatsay ((char const *) 0);
  errno = e;
  perror (s);
  fatcleanup (true);
}

void
error (char const *format, ...)
/* non-fatal error */
{
  va_list args;
  errsay ((char const *) 0);
  va_start (args, format);
  fvfprintf (stderr, format, args);
  va_end (args);
  afputc ('\n', stderr);
  eflush ();
}

void
rcserror (char const *format, ...)
/* non-fatal RCS file error */
{
  va_list args;
  errsay (RCSname);
  va_start (args, format);
  fvfprintf (stderr, format, args);
  va_end (args);
  afputc ('\n', stderr);
  eflush ();
}

void
workerror (char const *format, ...)
/* non-fatal working file error */
{
  va_list args;
  errsay (workname);
  va_start (args, format);
  fvfprintf (stderr, format, args);
  va_end (args);
  afputc ('\n', stderr);
  eflush ();
}

void
fatserror (char const *format, ...)
/* fatal RCS file syntax error */
{
  va_list args;
  oflush ();
  fprintf (stderr, "%s: %s:%ld: ", cmdid, RCSname, rcsline);
  va_start (args, format);
  fvfprintf (stderr, format, args);
  va_end (args);
  fatcleanup (false);
}

void
faterror (char const *format, ...)
/* fatal error, terminates program after cleanup */
{
  va_list args;
  fatsay ((char const *) 0);
  va_start (args, format);
  fvfprintf (stderr, format, args);
  va_end (args);
  fatcleanup (false);
}

void
rcsfaterror (char const *format, ...)
/* fatal RCS file error, terminates program after cleanup */
{
  va_list args;
  fatsay (RCSname);
  va_start (args, format);
  fvfprintf (stderr, format, args);
  va_end (args);
  fatcleanup (false);
}

void
warn (char const *format, ...)
/* warning */
{
  va_list args;
  if (!quietflag)
    {
      warnsay ((char *) 0);
      va_start (args, format);
      fvfprintf (stderr, format, args);
      va_end (args);
      afputc ('\n', stderr);
      eflush ();
    }
}

void
rcswarn (char const *format, ...)
/* RCS file warning */
{
  va_list args;
  if (!quietflag)
    {
      warnsay (RCSname);
      va_start (args, format);
      fvfprintf (stderr, format, args);
      va_end (args);
      afputc ('\n', stderr);
      eflush ();
    }
}

void
workwarn (char const *format, ...)
/* working file warning */
{
  va_list args;
  if (!quietflag)
    {
      warnsay (workname);
      va_start (args, format);
      fvfprintf (stderr, format, args);
      va_end (args);
      afputc ('\n', stderr);
      eflush ();
    }
}

void
redefined (int c)
{
  warn ("redefinition of -%c option", c);
}

void
diagnose (char const *format, ...)
/* prints a diagnostic message */
/* Unlike the other routines, it does not append a newline. */
/* This lets some callers suppress the newline, and is faster */
/* in implementations that flush stderr just at the end of each printf. */
{
  va_list args;
  if (!quietflag)
    {
      oflush ();
      va_start (args, format);
      fvfprintf (stderr, format, args);
      va_end (args);
      eflush ();
    }
}

void
afputc (int c, register FILE *f)
/* afputc(c,f); acts like aputc_(c,f) but is smaller and slower.  */
{
  aputc_ (c, f)
}

void
aputs (char const *s, FILE *iop)
/* Function: Put string s on file iop, abort on error.
 */
{
  if (fputs (s, iop) < 0)
    Oerror ();
}

void
fvfprintf (FILE * stream, char const *format, va_list args)
/* like vfprintf, except abort program on error */
{
  if (vfprintf (stream, format, args) < 0)
    Oerror ();
}

void
aprintf (FILE * iop, char const *fmt, ...)
/* Function: formatted output. Same as fprintf in stdio,
 * but aborts program on error
 */
{
  va_list ap;
  va_start (ap, fmt);
  fvfprintf (iop, fmt, ap);
  va_end (ap);
}

#ifdef LEXDB
/* test program reading a stream of lexemes and printing the tokens.
 */

int
main (int argc, char *argv[])
{
  cmdid = "lextest";
  if (argc < 2)
    {
      aputs ("No input file\n", stderr);
      return EXIT_FAILURE;
    }
  if (!(finptr = Iopen (argv[1], FOPEN_R, (struct stat *) 0)))
    {
      faterror ("can't open input file %s", argv[1]);
    }
  Lexinit ();
  while (!eoflex ())
    {
      switch (nexttok)
        {

        case ID:
          printf ("ID: %s", NextString);
          break;

        case NUM:
          if (hshenter)
            printf ("NUM: %s, index: %d", nexthsh->num,
                    nexthsh - hshtab);
          else
            printf ("NUM, unentered: %s", NextString);
          hshenter = !hshenter;     /*alternate between dates and numbers */
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

void
exiterr (void)
{
  _exit (EXIT_FAILURE);
}

#endif
