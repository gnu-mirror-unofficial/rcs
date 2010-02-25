/* RCS common definitions and data structures

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

#include "conf.h"


#define EXIT_TROUBLE DIFF_TROUBLE

#ifdef _POSIX_PATH_MAX
#	define SIZEABLE_PATH _POSIX_PATH_MAX
#else
#	define SIZEABLE_PATH 255 /* size of a large path; not a hard limit */
#endif

/* for traditional C hosts with unusual size arguments */
#define Fread(p,s,n,f)  fread(p, (freadarg_type)(s), (freadarg_type)(n), f)
#define Fwrite(p,s,n,f)  fwrite(p, (freadarg_type)(s), (freadarg_type)(n), f)


/*
 * Parameters
 */

/* backwards compatibility with old versions of RCS */
#define VERSION_min 3		/* old output RCS format supported */
#define VERSION_max 5		/* newest output RCS format supported */
#ifndef VERSION_DEFAULT		/* default RCS output format */
#	define VERSION_DEFAULT VERSION_max
#endif
#define VERSION(n) ((n) - VERSION_DEFAULT) /* internally, 0 is the default */

#ifndef STRICT_LOCKING
#define STRICT_LOCKING 1
#endif
			      /* 0 sets the default locking to non-strict;  */
                              /* used in experimental environments.         */
                              /* 1 sets the default locking to strict;      */
                              /* used in production environments.           */

#define yearlength	   16 /* (good through AD 9,999,999,999,999,999)    */
#define datesize (yearlength+16)	/* size of output of time2date */
#define RCSTMPPREFIX '_' /* prefix for temp files in working dir  */
#define KDELIM            '$' /* delimiter for keywords                     */
#define VDELIM            ':' /* separates keywords from values             */
#define DEFAULTSTATE    "Exp" /* default state of revisions                 */



#define true     1
#define false    0


/*
 * RILE - readonly file
 * declarecache; - declares local cache for RILE variable(s)
 * setupcache - sets up the local RILE cache, but does not initialize it
 * cache, uncache - caches and uncaches the local RILE;
 *	(uncache,cache) is needed around functions that advance the RILE pointer
 * Igeteof_(f,c,s) - get a char c from f, executing statement s at EOF
 * cachegeteof_(c,s) - Igeteof_ applied to the local RILE
 * Iget_(f,c) - like Igeteof_, except EOF is an error
 * cacheget_(c) - Iget_ applied to the local RILE
 * cacheunget_(f,c,s) - read c backwards from cached f, executing s at BOF
 * Ifileno, Ioffset_type, Irewind, Itell - analogs to stdio routines
 *
 * By conventions, macros whose names end in _ are statements, not expressions.
 * Following such macros with `; else' results in a syntax error.
 */

#define maps_memory (has_map_fd || has_mmap)

#if large_memory
	typedef unsigned char const *Iptr_type;
	typedef struct RILE {
		Iptr_type ptr, lim;
		unsigned char *base; /* not Iptr_type for lint's sake */
		unsigned char *readlim;
		int fd;
#		if maps_memory
			void (*deallocate) P((struct RILE *));
#		else
			FILE *stream;
#		endif
	} RILE;
#	if maps_memory
#		define declarecache register Iptr_type ptr, lim
#		define setupcache(f) (lim = (f)->lim)
#		define Igeteof_(f,c,s) if ((f)->ptr==(f)->lim) s else (c)= *(f)->ptr++;
#		define cachegeteof_(c,s) if (ptr==lim) s else (c)= *ptr++;
#	else
		int Igetmore P((RILE*));
#		define declarecache register Iptr_type ptr; register RILE *rRILE
#		define setupcache(f) (rRILE = (f))
#		define Igeteof_(f,c,s) if ((f)->ptr==(f)->readlim && !Igetmore(f)) s else (c)= *(f)->ptr++;
#		define cachegeteof_(c,s) if (ptr==rRILE->readlim && !Igetmore(rRILE)) s else (c)= *ptr++;
#	endif
#	define uncache(f) ((f)->ptr = ptr)
#	define cache(f) (ptr = (f)->ptr)
#	define Iget_(f,c) Igeteof_(f,c,Ieof();)
#	define cacheget_(c) cachegeteof_(c,Ieof();)
#	define cacheunget_(f,c) (c)=(--ptr)[-1];
#	define Ioffset_type size_t
#	define Itell(f) ((f)->ptr - (f)->base)
#	define Irewind(f) ((f)->ptr = (f)->base)
#	define cacheptr() ptr
#	define Ifileno(f) ((f)->fd)
#else
#	define RILE FILE
#	define declarecache register FILE *ptr
#	define setupcache(f) (ptr = (f))
#	define uncache(f)
#	define cache(f)
#	define Igeteof_(f,c,s) {if(((c)=getc(f))==EOF){testIerror(f);if(feof(f))s}}
#	define cachegeteof_(c,s) Igeteof_(ptr,c,s)
#	define Iget_(f,c) { if (((c)=getc(f))==EOF) testIeof(f); }
#	define cacheget_(c) Iget_(ptr,c)
#	define cacheunget_(f,c) if(fseek(ptr,-2L,SEEK_CUR))Ierror();else cacheget_(c)
#	define Ioffset_type long
#	define Itell(f) ftell(f)
#	define Ifileno(f) fileno(f)
#endif

/* Print a char, but abort on write error.  */
#define aputc_(c,o) { if (putc(c,o)==EOF) testOerror(o); }

/* Get a character from an RCS file, perhaps copying to a new RCS file.  */
#define GETCeof_(o,c,s) { cachegeteof_(c,s) if (o) aputc_(c,o) }
#define GETC_(o,c) { cacheget_(c) if (o) aputc_(c,o) }


#define WORKMODE(RCSmode, writable) (((RCSmode)&(mode_t)~(S_IWUSR|S_IWGRP|S_IWOTH)) | ((writable)?S_IWUSR:0))
/* computes mode of working file: same as RCSmode, but write permission     */
/* determined by writable */


/* character classes and token codes */
enum tokens {
/* classes */	DELIM,	DIGIT,	IDCHAR,	NEWLN,	LETTER,	Letter,
		PERIOD,	SBEGIN,	SPACE,	UNKN,
/* tokens */	COLON,	ID,	NUM,	SEMI,	STRING
};

#define SDELIM  '@'     /* the actual character is needed for string handling*/
/* SDELIM must be consistent with ctab[], so that ctab[SDELIM]==SBEGIN.
 * there should be no overlap among SDELIM, KDELIM, and VDELIM
 */

#define isdigit(c) (((unsigned)(c)-'0') <= 9) /* faster than ctab[c]==DIGIT */





/***************************************
 * Data structures for the symbol table
 ***************************************/

/* Buffer of arbitrary data */
struct buf {
	char *string;
	size_t size;
};
struct cbuf {
	char const *string;
	size_t size;
};

/* Hash table entry */
struct hshentry {
	char const	  * num;      /* pointer to revision number (ASCIZ) */
	char const	  * date;     /* pointer to date of checkin	    */
	char const	  * author;   /* login of person checking in	    */
	char const	  * lockedby; /* who locks the revision		    */
	char const	  * state;    /* state of revision (Exp by default) */
	char const	  * name;     /* name (if any) by which retrieved   */
	struct cbuf	    log;      /* log message requested at checkin   */
        struct branchhead * branches; /* list of first revisions on branches*/
	struct cbuf	    ig;	      /* ignored phrases in admin part	    */
	struct cbuf	    igtext;   /* ignored phrases in deltatext part  */
        struct hshentry   * next;     /* next revision on same branch       */
	struct hshentry   * nexthsh;  /* next revision with same hash value */
	long		    insertlns;/* lines inserted (computed by rlog)  */
	long		    deletelns;/* lines deleted  (computed by rlog)  */
	char		    selector; /* true if selected, false if deleted */
};

/* list of hash entries */
struct hshentries {
	struct hshentries *rest;
	struct hshentry *first;
};

/* list element for branch lists */
struct branchhead {
        struct hshentry   * hsh;
        struct branchhead * nextbranch;
};

/* accesslist element */
struct access {
	char const	  * login;
        struct access     * nextaccess;
};

/* list element for locks  */
struct rcslock {
	char const	  * login;
        struct hshentry   * delta;
	struct rcslock    * nextlock;
};

/* list element for symbolic names */
struct assoc {
	char const	  * symbol;
	char const	  * num;
        struct assoc      * nextassoc;
};


#define mainArgs (argc,argv) int argc; char **argv;

#if RCS_lint
#	define mainProg(name,cmd) int name mainArgs
#else
#	define mainProg(n,c) char const Copyright[] = "Copyright 1982,1988,1989 Walter F. Tichy, Purdue CS\nCopyright 1990,1991,1992,1993,1994,1995 Paul Eggert", cmdid[] = c; int main P((int,char**)); int main mainArgs
#endif

/*
 * Markers for keyword expansion (used in co and ident)
 *	Every byte must have class LETTER or Letter.
 */
#define AUTHOR          "Author"
#define DATE            "Date"
#define HEADER          "Header"
#define IDH             "Id"
#define LOCKER          "Locker"
#define LOG             "Log"
#define NAME		"Name"
#define RCSFILE         "RCSfile"
#define REVISION        "Revision"
#define SOURCE          "Source"
#define STATE           "State"
#define keylength 8 /* max length of any of the above keywords */

enum markers { Nomatch, Author, Date, Header, Id,
	       Locker, Log, Name, RCSfile, Revision, Source, State };
	/* This must be in the same order as rcskeys.c's Keyword[] array. */

#define DELNUMFORM      "\n\n%s\n%s\n"
/* used by putdtext and scanlogtext */

#define EMPTYLOG "*** empty log message ***" /* used by ci and rlog */

/* main program */
extern char const cmdid[];
void exiterr P((void)) exiting;

/* merge */
int merge P((int,char const*,char const*const[3],char const*const[3]));

/* rcsedit */
#define ciklogsize 23 /* sizeof("checked in with -k by ") */
extern FILE *fcopy;
extern char const *resultname;
extern char const ciklog[ciklogsize];
extern int locker_expansion;
RILE *rcswriteopen P((struct buf*,struct stat*,int));
char const *makedirtemp P((int));
char const *getcaller P((void));
int addlock P((struct hshentry*,int));
int addsymbol P((char const*,char const*,int));
int checkaccesslist P((void));
int chnamemod P((FILE**,char const*,char const*,int,mode_t,time_t));
int donerewrite P((int,time_t));
int dorewrite P((int,int));
int expandline P((RILE*,FILE*,struct hshentry const*,int,FILE*,int));
int findlock P((int,struct hshentry**));
int setmtime P((char const*,time_t));
void ORCSclose P((void));
void ORCSerror P((void));
void copystring P((void));
void dirtempunlink P((void));
void enterstring P((void));
void finishedit P((struct hshentry const*,FILE*,int));
void keepdirtemp P((char const*));
void openfcopy P((FILE*));
void snapshotedit P((FILE*));
void xpandstring P((struct hshentry const*));
#if has_NFS || bad_unlink
	int un_link P((char const*));
#else
#	define un_link(s) unlink(s)
#endif
#if large_memory
	void edit_string P((void));
#	define editstring(delta) edit_string()
#else
	void editstring P((struct hshentry const*));
#endif

/* rcsfcmp */
int rcsfcmp P((RILE*,struct stat const*,char const*,struct hshentry const*));

/* rcsfnms */
#define bufautobegin(b) clear_buf(b)
#define clear_buf(b) (((b)->string = 0, (b)->size = 0))
extern FILE *workstdout;
extern char *workname;
extern char const *RCSname;
extern char const *suffixes;
extern int fdlock;
extern struct stat RCSstat;
RILE *rcsreadopen P((struct buf*,struct stat*,int));
char *bufenlarge P((struct buf*,char const**));
char const *basefilename P((char const*));
char const *getfullRCSname P((void));
char const *maketemp P((int));
char const *rcssuffix P((char const*));
int pairnames P((int,char**,RILE*(*)P((struct buf*,struct stat*,int)),int,int));
struct cbuf bufremember P((struct buf*,size_t));
void bufalloc P((struct buf*,size_t));
void bufautoend P((struct buf*));
void bufrealloc P((struct buf*,size_t));
void bufscat P((struct buf*,char const*));
void bufscpy P((struct buf*,char const*));
void tempunlink P((void));

/* rcsgen */
extern int interactiveflag;
extern struct buf curlogbuf;
char const *buildrevision P((struct hshentries const*,struct hshentry*,FILE*,int));
int getcstdin P((void));
int putdtext P((struct hshentry const*,char const*,FILE*,int));
int ttystdin P((void));
int yesorno P((int,char const*,...)) printf_string(2,3);
struct cbuf cleanlogmsg P((char*,size_t));
struct cbuf getsstdin P((char const*,char const*,char const*,struct buf*));
void putdesc P((int,char*));
void putdftext P((struct hshentry const*,RILE*,FILE*,int));

/* rcskeep */
extern int prevkeys;
extern struct buf prevauthor, prevdate, prevname, prevrev, prevstate;
int getoldkeys P((RILE*));

/* rcskeys */
extern char const *const Keyword[];
enum markers trymatch P((char const*));

/* rcslex */
extern FILE *foutptr;
extern FILE *frewrite;
extern RILE *finptr;
extern char const *NextString;
extern enum tokens nexttok;
extern int hshenter;
extern int nerror;
extern int nextc;
extern int quietflag;
extern long rcsline;
char const *getid P((void));
void efaterror P((char const*)) exiting;
void enfaterror P((int,char const*)) exiting;
void fatcleanup P((int)) exiting;
void faterror P((char const*,...)) printf_string_exiting(1,2);
void fatserror P((char const*,...)) printf_string_exiting(1,2);
void rcsfaterror P((char const*,...)) printf_string_exiting(1,2);
void Ieof P((void)) exiting;
void Ierror P((void)) exiting;
void Oerror P((void)) exiting;
char *checkid P((char*,int));
char *checksym P((char*,int));
int eoflex P((void));
int getkeyopt P((char const*));
int getlex P((enum tokens));
struct cbuf getphrases P((char const*));
struct cbuf savestring P((struct buf*));
struct hshentry *getnum P((void));
void Ifclose P((RILE*));
void Izclose P((RILE**));
void Lexinit P((void));
void Ofclose P((FILE*));
void Orewind P((FILE*));
void Ozclose P((FILE**));
void aflush P((FILE*));
void afputc P((int,FILE*));
void aprintf P((FILE*,char const*,...)) printf_string(2,3);
void aputs P((char const*,FILE*));
void checksid P((char*));
void checkssym P((char*));
void diagnose P((char const*,...)) printf_string(1,2);
void eerror P((char const*));
void eflush P((void));
void enerror P((int,char const*));
void error P((char const*,...)) printf_string(1,2);
void fvfprintf P((FILE*,char const*,va_list));
void getkey P((char const*));
void getkeystring P((char const*));
void nextlex P((void));
void oflush P((void));
void printstring P((void));
void readstring P((void));
void redefined P((int));
void rcserror P((char const*,...)) printf_string(1,2);
void rcswarn P((char const*,...)) printf_string(1,2);
void testIerror P((FILE*));
void testOerror P((FILE*));
void warn P((char const*,...)) printf_string(1,2);
void warnignore P((void));
void workerror P((char const*,...)) printf_string(1,2);
void workwarn P((char const*,...)) printf_string(1,2);
#if has_madvise && has_mmap && large_memory
	void advise_access P((RILE*,int));
#	define if_advise_access(p,f,advice) if (p) advise_access(f,advice)
#else
#	define advise_access(f,advice)
#	define if_advise_access(p,f,advice)
#endif
#if large_memory && maps_memory
	RILE *I_open P((char const*,struct stat*));
#	define Iopen(f,m,s) I_open(f,s)
#else
	RILE *Iopen P((char const*,char const*,struct stat*));
#endif
#if !large_memory
	void testIeof P((FILE*));
	void Irewind P((RILE*));
#endif

/* rcsmap */
extern enum tokens const ctab[];

/* rcsrev */
char *partialno P((struct buf*,char const*,int));
char const *namedrev P((char const*,struct hshentry*));
char const *tiprev P((void));
int cmpdate P((char const*,char const*));
int cmpnum P((char const*,char const*));
int cmpnumfld P((char const*,char const*,int));
int compartial P((char const*,char const*,int));
int expandsym P((char const*,struct buf*));
int fexpandsym P((char const*,struct buf*,RILE*));
struct hshentry *genrevs P((char const*,char const*,char const*,char const*,struct hshentries**));
int countnumflds P((char const*));
void getbranchno P((char const*,struct buf*));

/* rcssyn */
/* These expand modes must agree with Expand_names[] in rcssyn.c.  */
#define KEYVAL_EXPAND 0 /* -kkv `$Keyword: value $' */
#define KEYVALLOCK_EXPAND 1 /* -kkvl `$Keyword: value locker $' */
#define KEY_EXPAND 2 /* -kk `$Keyword$' */
#define VAL_EXPAND 3 /* -kv `value' */
#define OLD_EXPAND 4 /* -ko use old string, omitting expansion */
#define BINARY_EXPAND 5 /* -kb like -ko, but use binary mode I/O */
#define MIN_UNEXPAND OLD_EXPAND /* min value for no logical expansion */
#define MIN_UNCHANGED_EXPAND (OPEN_O_BINARY ? BINARY_EXPAND : OLD_EXPAND)
			/* min value guaranteed to yield an identical file */
struct diffcmd {
	long
		line1, /* number of first line */
		nlines, /* number of lines affected */
		adprev, /* previous 'a' line1+1 or 'd' line1 */
		dafter; /* sum of previous 'd' line1 and previous 'd' nlines */
};
extern char const      * Dbranch;
extern struct access   * AccessList;
extern struct assoc    * Symbols;
extern struct cbuf Comment;
extern struct cbuf Ignored;
extern struct rcslock *Locks;
extern struct hshentry * Head;
extern int		 Expand;
extern int               StrictLocks;
extern int               TotalDeltas;
extern char const *const expand_names[];
extern char const
	Kaccess[], Kauthor[], Kbranch[], Kcomment[],
	Kdate[], Kdesc[], Kexpand[], Khead[], Klocks[], Klog[],
	Knext[], Kstate[], Kstrict[], Ksymbols[], Ktext[];
void unexpected_EOF P((void)) exiting;
int getdiffcmd P((RILE*,int,FILE*,struct diffcmd*));
int str2expmode P((char const*));
void getadmin P((void));
void getdesc P((int));
void gettree P((void));
void ignorephrases P((char const*));
void initdiffcmd P((struct diffcmd*));
void putadmin P((void));
void putstring P((FILE*,int,struct cbuf,int));
void puttree P((struct hshentry const*,FILE*));

/* rcstime */
#define zonelenmax 9 /* maxiumum length of time zone string, e.g. "+12:34:56" */
char const *date2str P((char const[datesize],char[datesize + zonelenmax]));
time_t date2time P((char const[datesize]));
void str2date P((char const*,char[datesize]));
void time2date P((time_t,char[datesize]));
void zone_set P((char const*));

/* rcsutil */
extern int RCSversion;
FILE *fopenSafer P((char const*,char const*));
char *cgetenv P((char const*));
char *fstr_save P((char const*));
char *str_save P((char const*));
char const *getusername P((int));
int fdSafer P((int));
int getRCSINIT P((int,char**,char***));
int run P((int,char const*,...));
int runv P((int,char const*,char const**));
malloc_type fremember P((malloc_type));
malloc_type ftestalloc P((size_t));
malloc_type testalloc P((size_t));
malloc_type testrealloc P((malloc_type,size_t));
#define ftalloc(T) ftnalloc(T,1)
#define talloc(T) tnalloc(T,1)
#if RCS_lint
	extern malloc_type lintalloc;
#	define ftnalloc(T,n) (lintalloc = ftestalloc(sizeof(T)*(n)), (T*)0)
#	define tnalloc(T,n) (lintalloc = testalloc(sizeof(T)*(n)), (T*)0)
#	define trealloc(T,p,n) (lintalloc = testrealloc((malloc_type)0, sizeof(T)*(n)), p)
#	define tfree(p)
#else
#	define ftnalloc(T,n) ((T*) ftestalloc(sizeof(T)*(n)))
#	define tnalloc(T,n) ((T*) testalloc(sizeof(T)*(n)))
#	define trealloc(T,p,n) ((T*) testrealloc((malloc_type)(p), sizeof(T)*(n)))
#	define tfree(p) free((malloc_type)(p))
#endif
time_t now P((void));
void awrite P((char const*,size_t,FILE*));
void fastcopy P((RILE*,FILE*));
void ffree P((void));
void ffree1 P((char const*));
void setRCSversion P((char const*));
#if has_signal
	void catchints P((void));
	void ignoreints P((void));
	void restoreints P((void));
#else
#	define catchints()
#	define ignoreints()
#	define restoreints()
#endif
#if has_mmap && large_memory
#   if has_NFS && mmap_signal
	void catchmmapints P((void));
	void readAccessFilenameBuffer P((char const*,unsigned char const*));
#   else
#	define catchmmapints()
#   endif
#endif
#if has_getuid
	uid_t ruid P((void));
#	define myself(u) ((u) == ruid())
#else
#	define myself(u) true
#endif
#if has_setuid
	uid_t euid P((void));
	void nosetid P((void));
	void seteid P((void));
	void setrid P((void));
#else
#	define nosetid()
#	define seteid()
#	define setrid()
#endif

/* version */
extern char const RCS_version_string[];
