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

#include "rcsbase.h"

struct rcslockers {                   /* lockers in locker option; stored   */
     char const		* login;      /* lockerlist			    */
     struct rcslockers  * lockerlink;
     }  ;

struct  stateattri {                  /* states in state option; stored in  */
     char const		* status;     /* statelist			    */
     struct  stateattri * nextstate;
     }  ;

struct  authors {                     /* login names in author option;      */
     char const		* login;      /* stored in authorlist		    */
     struct     authors * nextauthor;
     }  ;

struct Revpairs{                      /* revision or branch range in -r     */
     int		  numfld;     /* option; stored in revlist	    */
     char const		* strtrev;
     char const		* endrev;
     struct  Revpairs   * rnext;
     } ;

struct Datepairs{                     /* date range in -d option; stored in */
     struct Datepairs *dnext;
     char               strtdate[datesize];   /* duelst and datelist      */
     char               enddate[datesize];
     char ne_date; /* datelist only; distinguishes < from <= */
     };

static char extractdelta P((struct hshentry const*));
static int checkrevpair P((char const*,char const*));
static int extdate P((struct hshentry*));
static int getnumericrev P((void));
static struct hshentry const *readdeltalog P((void));
static void cleanup P((void));
static void exttree P((struct hshentry*));
static void getauthor P((char*));
static void getdatepair P((char*));
static void getlocker P((char*));
static void getrevpairs P((char*));
static void getscript P((struct hshentry*));
static void getstate P((char*));
static void putabranch P((struct hshentry const*));
static void putadelta P((struct hshentry const*,struct hshentry const*,int));
static void putforest P((struct branchhead const*));
static void putree P((struct hshentry const*));
static void putrunk P((void));
static void recentdate P((struct hshentry const*,struct Datepairs*));
static void trunclocks P((void));

static char const *insDelFormat;
static int branchflag;	/*set on -b */
static int exitstatus;
static int lockflag;
static struct Datepairs *datelist, *duelst;
static struct Revpairs *revlist, *Revlst;
static struct authors *authorlist;
static struct rcslockers *lockerlist;
static struct stateattri *statelist;


mainProg(rlogId, "rlog")
{
	static char const cmdusage[] =
		"\nrlog usage: rlog -{bhLNRt} -ddates -l[lockers] -r[revs] -sstates -Vn -w[logins] -xsuff -zzone file ...";

	register FILE *out;
	char *a, **newargv;
	struct Datepairs *currdate;
	char const *accessListString, *accessFormat;
	char const *headFormat, *symbolFormat;
	struct access const *curaccess;
	struct assoc const *curassoc;
	struct hshentry const *delta;
	struct rcslock const *currlock;
	int descflag, selectflag;
	int onlylockflag;  /* print only files with locks */
	int onlyRCSflag;  /* print only RCS pathname */
	int pre5;
	int shownames;
	int revno;

        descflag = selectflag = shownames = true;
	onlylockflag = onlyRCSflag = false;
	out = stdout;
	suffixes = X_DEFAULT;

	argc = getRCSINIT(argc, argv, &newargv);
	argv = newargv;
	while (a = *++argv,  0<--argc && *a++=='-') {
		switch (*a++) {

		case 'L':
			onlylockflag = true;
			break;

		case 'N':
			shownames = false;
			break;

		case 'R':
			onlyRCSflag =true;
			break;

                case 'l':
                        lockflag = true;
			getlocker(a);
                        break;

                case 'b':
                        branchflag = true;
                        break;

                case 'r':
			getrevpairs(a);
                        break;

                case 'd':
			getdatepair(a);
                        break;

                case 's':
			getstate(a);
                        break;

                case 'w':
			getauthor(a);
                        break;

                case 'h':
			descflag = false;
                        break;

                case 't':
                        selectflag = false;
                        break;

		case 'q':
			/* This has no effect; it's here for consistency.  */
			quietflag = true;
			break;

		case 'x':
			suffixes = a;
			break;

		case 'z':
			zone_set(a);
			break;

		case 'T':
			/* Ignore -T, so that RCSINIT can contain -T.  */
			if (*a)
				goto unknown;
			break;

		case 'V':
			setRCSversion(*argv);
			break;

                default:
		unknown:
			error("unknown option: %s%s", *argv, cmdusage);

                };
        } /* end of option processing */

	if (! (descflag|selectflag)) {
		warn("-t overrides -h.");
		descflag = true;
	}

	pre5 = RCSversion < VERSION(5);
	if (pre5) {
	    accessListString = "\naccess list:   ";
	    accessFormat = "  %s";
	    headFormat = "RCS file:        %s;   Working file:    %s\nhead:           %s%s\nbranch:         %s%s\nlocks:         ";
	    insDelFormat = "  lines added/del: %ld/%ld";
	    symbolFormat = "  %s: %s;";
	} else {
	    accessListString = "\naccess list:";
	    accessFormat = "\n\t%s";
	    headFormat = "RCS file: %s\nWorking file: %s\nhead:%s%s\nbranch:%s%s\nlocks:%s";
	    insDelFormat = "  lines: +%ld -%ld";
	    symbolFormat = "\n\t%s: %s";
	}

	/* Now handle all pathnames.  */
	if (nerror)
	  cleanup();
	else if (argc < 1)
	  faterror("no input file%s", cmdusage);
	else
	  for (;  0 < argc;  cleanup(), ++argv, --argc) {
	    ffree();

	    if (pairnames(argc, argv, rcsreadopen, true, false)  <=  0)
		continue;

	    /*
	     * RCSname contains the name of the RCS file,
	     * and finptr the file descriptor;
	     * workname contains the name of the working file.
             */

	    /* Keep only those locks given by -l.  */
	    if (lockflag)
		trunclocks();

            /* do nothing if -L is given and there are no locks*/
	    if (onlylockflag && !Locks)
		continue;

	    if ( onlyRCSflag ) {
		aprintf(out, "%s\n", RCSname);
		continue;
	    }

	    gettree();

	    if (!getnumericrev())
		continue;

	    /*
	    * Output the first character with putc, not printf.
	    * Otherwise, an SVR4 stdio bug buffers output inefficiently.
	    */
	    aputc_('\n', out)

	    /*   print RCS pathname, working pathname and optional
                 administrative information                         */
            /* could use getfullRCSname() here, but that is very slow */
	    aprintf(out, headFormat, RCSname, workname,
		    Head ? " " : "",  Head ? Head->num : "",
		    Dbranch ? " " : "",  Dbranch ? Dbranch : "",
		    StrictLocks ? " strict" : ""
	    );
            currlock = Locks;
            while( currlock ) {
		aprintf(out, symbolFormat, currlock->login,
                                currlock->delta->num);
                currlock = currlock->nextlock;
            }
            if (StrictLocks && pre5)
                aputs("  ;  strict" + (Locks?3:0), out);

	    aputs(accessListString, out);      /*  print access list  */
            curaccess = AccessList;
            while(curaccess) {
		aprintf(out, accessFormat, curaccess->login);
                curaccess = curaccess->nextaccess;
            }

	    if (shownames) {
		aputs("\nsymbolic names:", out);   /*  print symbolic names   */
		for (curassoc=Symbols; curassoc; curassoc=curassoc->nextassoc)
		    aprintf(out, symbolFormat, curassoc->symbol, curassoc->num);
	    }
	    if (pre5) {
		aputs("\ncomment leader:  \"", out);
		awrite(Comment.string, Comment.size, out);
		afputc('\"', out);
	    }
	    if (!pre5  ||  Expand != KEYVAL_EXPAND)
		aprintf(out, "\nkeyword substitution: %s",
			expand_names[Expand]
		);

	    aprintf(out, "\ntotal revisions: %d", TotalDeltas);

	    revno = 0;

	    if (Head  &&  selectflag & descflag) {

		exttree(Head);

		/*  get most recently date of the dates pointed by duelst  */
		currdate = duelst;
		while( currdate) {
		    VOID strcpy(currdate->strtdate, "0.0.0.0.0.0");
		    recentdate(Head, currdate);
		    currdate = currdate->dnext;
		}

		revno = extdate(Head);

		aprintf(out, ";\tselected revisions: %d", revno);
	    }

	    afputc('\n',out);
	    if (descflag) {
		aputs("description:\n", out);
		getdesc(true);
	    }
	    if (revno) {
		while (! (delta = readdeltalog())->selector  ||  --revno)
		    continue;
		if (delta->next && countnumflds(delta->num)==2)
		    /* Read through delta->next to get its insertlns.  */
		    while (readdeltalog() != delta->next)
			continue;
		putrunk();
		putree(Head);
	    }
	    aputs("=============================================================================\n",out);
	  }
	Ofclose(out);
	exitmain(exitstatus);
}

	static void
cleanup()
{
	if (nerror) exitstatus = EXIT_FAILURE;
	Izclose(&finptr);
}

#if RCS_lint
#	define exiterr rlogExit
#endif
	void
exiterr()
{
	_exit(EXIT_FAILURE);
}



	static void
putrunk()
/*  function:  print revisions chosen, which are in trunk      */

{
	register struct hshentry const *ptr;

	for (ptr = Head;  ptr;  ptr = ptr->next)
		putadelta(ptr, ptr->next, true);
}



	static void
putree(root)
	struct hshentry const *root;
/*   function: print delta tree (not including trunk) in reverse
               order on each branch                                        */

{
	if (!root) return;

        putree(root->next);

        putforest(root->branches);
}




	static void
putforest(branchroot)
	struct branchhead const *branchroot;
/*   function:  print branches that has the same direct ancestor    */
{
	if (!branchroot) return;

        putforest(branchroot->nextbranch);

        putabranch(branchroot->hsh);
        putree(branchroot->hsh);
}




	static void
putabranch(root)
	struct hshentry const *root;
/*   function  :  print one branch     */

{
	if (!root) return;

        putabranch(root->next);

        putadelta(root, root, false);
}





	static void
putadelta(node,editscript,trunk)
	register struct hshentry const *node, *editscript;
	int trunk;
/*  function: Print delta node if node->selector is set.        */
/*      editscript indicates where the editscript is stored     */
/*      trunk indicated whether this node is in trunk           */
{
	static char emptych[] = EMPTYLOG;

	register FILE *out;
	char const *s;
	size_t n;
	struct branchhead const *newbranch;
	struct buf branchnum;
	char datebuf[datesize + zonelenmax];
	int pre5 = RCSversion < VERSION(5);

	if (!node->selector)
            return;

	out = stdout;
	aprintf(out,
		"----------------------------\nrevision %s%s",
		node->num,  pre5 ? "        " : ""
	);
        if ( node->lockedby )
	    aprintf(out, pre5+"\tlocked by: %s;", node->lockedby);

	aprintf(out, "\ndate: %s;  author: %s;  state: %s;",
		date2str(node->date, datebuf),
		node->author, node->state
	);

        if ( editscript )
           if(trunk)
	      aprintf(out, insDelFormat,
                             editscript->deletelns, editscript->insertlns);
           else
	      aprintf(out, insDelFormat,
                             editscript->insertlns, editscript->deletelns);

        newbranch = node->branches;
        if ( newbranch ) {
	   bufautobegin(&branchnum);
	   aputs("\nbranches:", out);
           while( newbranch ) {
		getbranchno(newbranch->hsh->num, &branchnum);
		aprintf(out, "  %s;", branchnum.string);
                newbranch = newbranch->nextbranch;
           }
	   bufautoend(&branchnum);
        }

	afputc('\n', out);
	s = node->log.string;
	if (!(n = node->log.size)) {
		s = emptych;
		n = sizeof(emptych)-1;
	}
	awrite(s, n, out);
	if (s[n-1] != '\n')
		afputc('\n', out);
}


	static struct hshentry const *
readdeltalog()
/*  Function : get the log message and skip the text of a deltatext node.
 *	       Return the delta found.
 *             Assumes the current lexeme is not yet in nexttok; does not
 *             advance nexttok.
 */
{
        register struct  hshentry  * Delta;
	struct buf logbuf;
	struct cbuf cb;

	if (eoflex())
		fatserror("missing delta log");
        nextlex();
	if (!(Delta = getnum()))
		fatserror("delta number corrupted");
	getkeystring(Klog);
	if (Delta->log.string)
		fatserror("duplicate delta log");
	bufautobegin(&logbuf);
	cb = savestring(&logbuf);
	Delta->log = bufremember(&logbuf, cb.size);

	ignorephrases(Ktext);
	getkeystring(Ktext);
        Delta->insertlns = Delta->deletelns = 0;
        if ( Delta != Head)
                getscript(Delta);
        else
                readstring();
	return Delta;
}


	static void
getscript(Delta)
struct    hshentry   * Delta;
/*   function:  read edit script of Delta and count how many lines added  */
/*              and deleted in the script                                 */

{
        int ed;   /*  editor command  */
	declarecache;
	register RILE *fin;
        register  int   c;
	register long i;
	struct diffcmd dc;

	fin = finptr;
	setupcache(fin);
	initdiffcmd(&dc);
	while (0  <=  (ed = getdiffcmd(fin,true,(FILE *)0,&dc)))
	    if (!ed)
                 Delta->deletelns += dc.nlines;
	    else {
                 /*  skip scripted lines  */
		 i = dc.nlines;
		 Delta->insertlns += i;
		 cache(fin);
		 do {
		     for (;;) {
			cacheget_(c)
			switch (c) {
			    default:
				continue;
			    case SDELIM:
				cacheget_(c)
				if (c == SDELIM)
				    continue;
				if (--i)
					unexpected_EOF();
				nextc = c;
				uncache(fin);
				return;
			    case '\n':
				break;
			}
			break;
		     }
		     ++rcsline;
		 } while (--i);
		 uncache(fin);
            }
}







	static void
exttree(root)
struct hshentry  *root;
/*  function: select revisions , starting with root             */

{
	struct branchhead const *newbranch;

	if (!root) return;

	root->selector = extractdelta(root);
	root->log.string = 0;
        exttree(root->next);

        newbranch = root->branches;
        while( newbranch ) {
            exttree(newbranch->hsh);
            newbranch = newbranch->nextbranch;
        }
}




	static void
getlocker(argv)
char    * argv;
/*   function : get the login names of lockers from command line   */
/*              and store in lockerlist.                           */

{
        register char c;
	struct rcslockers *newlocker;
        argv--;
	while ((c = *++argv)==',' || c==' ' || c=='\t' || c=='\n' || c==';')
	    continue;
        if (  c == '\0') {
	    lockerlist = 0;
            return;
        }

        while( c != '\0' ) {
	    newlocker = talloc(struct rcslockers);
            newlocker->lockerlink = lockerlist;
            newlocker->login = argv;
            lockerlist = newlocker;
	    while ((c = *++argv) && c!=',' && c!=' ' && c!='\t' && c!='\n' && c!=';')
		continue;
            *argv = '\0';
            if ( c == '\0' ) return;
	    while ((c = *++argv)==',' || c==' ' || c=='\t' || c=='\n' || c==';')
		continue;
        }
}



	static void
getauthor(argv)
char   *argv;
/*   function:  get the author's name from command line   */
/*              and store in authorlist                   */

{
        register    c;
        struct     authors  * newauthor;

        argv--;
	while ((c = *++argv)==',' || c==' ' || c=='\t' || c=='\n' || c==';')
	    continue;
        if ( c == '\0' ) {
	    authorlist = talloc(struct authors);
	    authorlist->login = getusername(false);
	    authorlist->nextauthor = 0;
            return;
        }

        while( c != '\0' ) {
	    newauthor = talloc(struct authors);
            newauthor->nextauthor = authorlist;
            newauthor->login = argv;
            authorlist = newauthor;
	    while ((c = *++argv) && c!=',' && c!=' ' && c!='\t' && c!='\n' && c!=';')
		continue;
            * argv = '\0';
            if ( c == '\0') return;
	    while ((c = *++argv)==',' || c==' ' || c=='\t' || c=='\n' || c==';')
		continue;
        }
}




	static void
getstate(argv)
char   * argv;
/*   function :  get the states of revisions from command line  */
/*               and store in statelist                         */

{
        register  char  c;
        struct    stateattri    *newstate;

        argv--;
	while ((c = *++argv)==',' || c==' ' || c=='\t' || c=='\n' || c==';')
	    continue;
        if ( c == '\0'){
	    error("missing state attributes after -s options");
            return;
        }

        while( c != '\0' ) {
	    newstate = talloc(struct stateattri);
            newstate->nextstate = statelist;
            newstate->status = argv;
            statelist = newstate;
	    while ((c = *++argv) && c!=',' && c!=' ' && c!='\t' && c!='\n' && c!=';')
		continue;
            *argv = '\0';
            if ( c == '\0' ) return;
	    while ((c = *++argv)==',' || c==' ' || c=='\t' || c=='\n' || c==';')
		continue;
        }
}



	static void
trunclocks()
/*  Function:  Truncate the list of locks to those that are held by the  */
/*             id's on lockerlist. Do not truncate if lockerlist empty.  */

{
	struct rcslockers const *plocker;
	struct rcslock *p, **pp;

	if (!lockerlist) return;

        /* shorten Locks to those contained in lockerlist */
	for (pp = &Locks;  (p = *pp);  )
	    for (plocker = lockerlist;  ;  )
		if (strcmp(plocker->login, p->login) == 0) {
		    pp = &p->nextlock;
		    break;
		} else if (!(plocker = plocker->lockerlink)) {
		    *pp = p->nextlock;
		    break;
		}
}



	static void
recentdate(root, pd)
	struct hshentry const *root;
	struct Datepairs *pd;
/*  function:  Finds the delta that is closest to the cutoff date given by   */
/*             pd among the revisions selected by exttree.                   */
/*             Successively narrows down the interval given by pd,           */
/*             and sets the strtdate of pd to the date of the selected delta */
{
	struct branchhead const *newbranch;

	if (!root) return;
	if (root->selector) {
	     if ( cmpdate(root->date, pd->strtdate) >= 0 &&
		  cmpdate(root->date, pd->enddate) <= 0)
		VOID strcpy(pd->strtdate, root->date);
        }

        recentdate(root->next, pd);
        newbranch = root->branches;
        while( newbranch) {
           recentdate(newbranch->hsh, pd);
           newbranch = newbranch->nextbranch;
	}
}






	static int
extdate(root)
struct  hshentry        * root;
/*  function:  select revisions which are in the date range specified     */
/*             in duelst  and datelist, start at root                     */
/* Yield number of revisions selected, including those already selected.  */
{
	struct branchhead const *newbranch;
	struct Datepairs const *pdate;
	int revno, ne;

	if (!root)
	    return 0;

        if ( datelist || duelst) {
            pdate = datelist;
            while( pdate ) {
		ne = pdate->ne_date;
		if (
			(!pdate->strtdate[0]
			|| ne <= cmpdate(root->date, pdate->strtdate))
		    &&
			(!pdate->enddate[0]
			|| ne <= cmpdate(pdate->enddate, root->date))
		)
                        break;
                pdate = pdate->dnext;
            }
	    if (!pdate) {
                pdate = duelst;
		for (;;) {
		   if (!pdate) {
			root->selector = false;
			break;
		   }
		   if (cmpdate(root->date, pdate->strtdate) == 0)
                      break;
                   pdate = pdate->dnext;
                }
            }
        }
	revno = root->selector + extdate(root->next);

        newbranch = root->branches;
        while( newbranch ) {
	   revno += extdate(newbranch->hsh);
           newbranch = newbranch->nextbranch;
        }
	return revno;
}



	static char
extractdelta(pdelta)
	struct hshentry const *pdelta;
/*  function:  compare information of pdelta to the authorlist, lockerlist,*/
/*             statelist, revlist and yield true if pdelta is selected.    */

{
	struct rcslock const *plock;
	struct stateattri const *pstate;
	struct authors const *pauthor;
	struct Revpairs const *prevision;
	int length;

	if ((pauthor = authorlist)) /* only certain authors wanted */
	    while (strcmp(pauthor->login, pdelta->author) != 0)
		if (!(pauthor = pauthor->nextauthor))
		    return false;
	if ((pstate = statelist)) /* only certain states wanted */
	    while (strcmp(pstate->status, pdelta->state) != 0)
		if (!(pstate = pstate->nextstate))
		    return false;
	if (lockflag) /* only locked revisions wanted */
	    for (plock = Locks;  ;  plock = plock->nextlock)
		if (!plock)
		    return false;
		else if (plock->delta == pdelta)
		    break;
	if ((prevision = Revlst)) /* only certain revs or branches wanted */
	    for (;;) {
                length = prevision->numfld;
		if (
		    countnumflds(pdelta->num) == length+(length&1) &&
		    0 <= compartial(pdelta->num, prevision->strtrev, length) &&
		    0 <= compartial(prevision->endrev, pdelta->num, length)
		)
		     break;
		if (!(prevision = prevision->rnext))
		    return false;
            }
	return true;
}



	static void
getdatepair(argv)
   char   * argv;
/*  function:  get time range from command line and store in datelist if    */
/*             a time range specified or in duelst if a time spot specified */

{
        register   char         c;
        struct     Datepairs    * nextdate;
	char const		* rawdate;
	int                     switchflag;

        argv--;
	while ((c = *++argv)==',' || c==' ' || c=='\t' || c=='\n' || c==';')
	    continue;
        if ( c == '\0' ) {
	    error("missing date/time after -d");
            return;
        }

        while( c != '\0' )  {
	    switchflag = false;
	    nextdate = talloc(struct Datepairs);
            if ( c == '<' ) {   /*   case: -d <date   */
                c = *++argv;
		if (!(nextdate->ne_date = c!='='))
		    c = *++argv;
                (nextdate->strtdate)[0] = '\0';
	    } else if (c == '>') { /* case: -d'>date' */
		c = *++argv;
		if (!(nextdate->ne_date = c!='='))
		    c = *++argv;
		(nextdate->enddate)[0] = '\0';
		switchflag = true;
	    } else {
                rawdate = argv;
		while( c != '<' && c != '>' && c != ';' && c != '\0')
		     c = *++argv;
                *argv = '\0';
		if ( c == '>' ) switchflag=true;
		str2date(rawdate,
			 switchflag ? nextdate->enddate : nextdate->strtdate);
		if ( c == ';' || c == '\0') {  /*  case: -d date  */
		    VOID strcpy(nextdate->enddate,nextdate->strtdate);
                    nextdate->dnext = duelst;
                    duelst = nextdate;
		    goto end;
		} else {
		    /*   case:   -d date<  or -d  date>; see switchflag */
		    int eq = argv[1]=='=';
		    nextdate->ne_date = !eq;
		    argv += eq;
		    while ((c = *++argv) == ' ' || c=='\t' || c=='\n')
			continue;
		    if ( c == ';' || c == '\0') {
			/* second date missing */
			if (switchflag)
			    *nextdate->strtdate= '\0';
			else
			    *nextdate->enddate= '\0';
			nextdate->dnext = datelist;
			datelist = nextdate;
			goto end;
		    }
                }
            }
            rawdate = argv;
	    while( c != '>' && c != '<' && c != ';' && c != '\0')
 		c = *++argv;
            *argv = '\0';
	    str2date(rawdate,
		     switchflag ? nextdate->strtdate : nextdate->enddate);
            nextdate->dnext = datelist;
	    datelist = nextdate;
     end:
	    if (RCSversion < VERSION(5))
		nextdate->ne_date = 0;
	    if ( c == '\0')  return;
	    while ((c = *++argv) == ';' || c == ' ' || c == '\t' || c =='\n')
		continue;
        }
}



	static int
getnumericrev()
/*  function:  get the numeric name of revisions which stored in revlist  */
/*             and then stored the numeric names in Revlst                */
/*             if branchflag, also add default branch                     */

{
        struct  Revpairs        * ptr, *pt;
	int n;
	struct buf s, e;
	char const *lrev;
	struct buf const *rstart, *rend;

	Revlst = 0;
        ptr = revlist;
	bufautobegin(&s);
	bufautobegin(&e);
        while( ptr ) {
	    n = 0;
	    rstart = &s;
	    rend = &e;

	    switch (ptr->numfld) {

	      case 1: /* -rREV */
		if (!expandsym(ptr->strtrev, &s))
		    goto freebufs;
		rend = &s;
		n = countnumflds(s.string);
		if (!n  &&  (lrev = tiprev())) {
		    bufscpy(&s, lrev);
		    n = countnumflds(lrev);
		}
		break;

	      case 2: /* -rREV: */
		if (!expandsym(ptr->strtrev, &s))
		    goto freebufs;
		bufscpy(&e, s.string);
		n = countnumflds(s.string);
		(n<2 ? e.string : strrchr(e.string,'.'))[0]  =  0;
		break;

	      case 3: /* -r:REV */
		if (!expandsym(ptr->endrev, &e))
		    goto freebufs;
		if ((n = countnumflds(e.string)) < 2)
		    bufscpy(&s, ".0");
		else {
		    bufscpy(&s, e.string);
		    VOID strcpy(strrchr(s.string,'.'), ".0");
		}
		break;

	      default: /* -rREV1:REV2 */
		if (!(
			expandsym(ptr->strtrev, &s)
		    &&	expandsym(ptr->endrev, &e)
		    &&	checkrevpair(s.string, e.string)
		))
		    goto freebufs;
		n = countnumflds(s.string);
		/* Swap if out of order.  */
		if (compartial(s.string,e.string,n) > 0) {
		    rstart = &e;
		    rend = &s;
		}
		break;
	    }

	    if (n) {
		pt = ftalloc(struct Revpairs);
		pt->numfld = n;
		pt->strtrev = fstr_save(rstart->string);
		pt->endrev = fstr_save(rend->string);
                pt->rnext = Revlst;
                Revlst = pt;
	    }
	    ptr = ptr->rnext;
        }
        /* Now take care of branchflag */
	if (branchflag && (Dbranch||Head)) {
	    pt = ftalloc(struct Revpairs);
	    pt->strtrev = pt->endrev =
		Dbranch ? Dbranch : fstr_save(partialno(&s,Head->num,1));
	    pt->rnext=Revlst; Revlst=pt;
	    pt->numfld = countnumflds(pt->strtrev);
        }

      freebufs:
	bufautoend(&s);
	bufautoend(&e);
	return !ptr;
}



	static int
checkrevpair(num1,num2)
	char const *num1, *num2;
/*  function:  check whether num1, num2 are legal pair,i.e.
    only the last field are different and have same number of
    fields( if length <= 2, may be different if first field)   */

{
	int length = countnumflds(num1);

	if (
			countnumflds(num2) != length
		||	(2 < length  &&  compartial(num1, num2, length-1) != 0)
	) {
	    rcserror("invalid branch or revision pair %s : %s", num1, num2);
            return false;
        }

        return true;
}



	static void
getrevpairs(argv)
register     char    * argv;
/*  function:  get revision or branch range from command line, and   */
/*             store in revlist                                      */

{
        register    char    c;
        struct      Revpairs  * nextrevpair;
	int separator;

	c = *argv;

	/* Support old ambiguous '-' syntax; this will go away.  */
	if (strchr(argv,':'))
	    separator = ':';
	else {
	    if (strchr(argv,'-')  &&  VERSION(5) <= RCSversion)
		warn("`-' is obsolete in `-r%s'; use `:' instead", argv);
	    separator = '-';
	}

	for (;;) {
	    while (c==' ' || c=='\t' || c=='\n')
		c = *++argv;
	    nextrevpair = talloc(struct Revpairs);
            nextrevpair->rnext = revlist;
            revlist = nextrevpair;
	    nextrevpair->numfld = 1;
	    nextrevpair->strtrev = argv;
	    for (;;  c = *++argv) {
		switch (c) {
		    default:
			continue;
		    case '\0': case ' ': case '\t': case '\n':
		    case ',': case ';':
			break;
		    case ':': case '-':
			if (c == separator)
			    break;
			continue;
		}
		break;
	    }
	    *argv = '\0';
	    while (c==' ' || c=='\t' || c=='\n')
		c = *++argv;
	    if (c == separator) {
		while ((c = *++argv) == ' ' || c == '\t' || c =='\n')
		    continue;
		nextrevpair->endrev = argv;
		for (;;  c = *++argv) {
		    switch (c) {
			default:
			    continue;
			case '\0': case ' ': case '\t': case '\n':
			case ',': case ';':
			    break;
			case ':': case '-':
			    if (c == separator)
				break;
			    continue;
		    }
		    break;
		}
		*argv = '\0';
		while (c==' ' || c=='\t' || c =='\n')
		    c = *++argv;
		nextrevpair->numfld =
		    !nextrevpair->endrev[0] ? 2 /* -rREV: */ :
		    !nextrevpair->strtrev[0] ? 3 /* -r:REV */ :
		    4 /* -rREV1:REV2 */;
            }
	    if (!c)
		break;
	    else if (c==',' || c==';')
		c = *++argv;
	    else
		error("missing `,' near `%c%s'", c, argv+1);
	}
}
