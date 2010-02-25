/* Check in revisions of RCS files from working files.

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

struct Symrev {
       char const *ssymbol;
       int override;
       struct Symrev * nextsym;
};

static char const *getcurdate P((void));
static int addbranch P((struct hshentry*,struct buf*,int));
static int addelta P((void));
static int addsyms P((char const*));
static int fixwork P((mode_t,time_t));
static int removelock P((struct hshentry*));
static int xpandfile P((RILE*,struct hshentry const*,char const**,int));
static struct cbuf getlogmsg P((void));
static void cleanup P((void));
static void incnum P((char const*,struct buf*));
static void addassoclst P((int,char const*));

static FILE *exfile;
static RILE *workptr;			/* working file pointer		*/
static struct buf newdelnum;		/* new revision number		*/
static struct cbuf msg;
static int exitstatus;
static int forceciflag;			/* forces check in		*/
static int keepflag, keepworkingfile, rcsinitflag;
static struct hshentries *gendeltas;	/* deltas to be generated	*/
static struct hshentry *targetdelta;	/* old delta to be generated	*/
static struct hshentry newdelta;	/* new delta to be inserted	*/
static struct stat workstat;
static struct Symrev *assoclst, **nextassoc;

mainProg(ciId, "ci")
{
	static char const cmdusage[] =
		"\nci usage: ci -{fIklMqru}[rev] -d[date] -mmsg -{nN}name -sstate -ttext -T -Vn -wwho -xsuff -zzone file ...";
	static char const default_state[] = DEFAULTSTATE;

	char altdate[datesize];
	char olddate[datesize];
	char newdatebuf[datesize + zonelenmax];
	char targetdatebuf[datesize + zonelenmax];
	char *a, **newargv, *textfile;
	char const *author, *krev, *rev, *state;
	char const *diffname, *expname;
	char const *newworkname;
	int initflag, mustread;
	int lockflag, lockthis, mtimeflag, removedlock, Ttimeflag;
	int r;
	int changedRCS, changework, dolog, newhead;
	int usestatdate; /* Use mod time of file for -d.  */
	mode_t newworkmode; /* mode for working file */
	time_t mtime, wtime;
	struct hshentry *workdelta;

	setrid();

	author = rev = state = textfile = 0;
	initflag = lockflag = mustread = false;
	mtimeflag = false;
	Ttimeflag = false;
	altdate[0]= '\0'; /* empty alternate date for -d */
	usestatdate=false;
	suffixes = X_DEFAULT;
	nextassoc = &assoclst;

	argc = getRCSINIT(argc, argv, &newargv);
	argv = newargv;
	while (a = *++argv,  0<--argc && *a++=='-') {
		switch (*a++) {

                case 'r':
			if (*a)
				goto revno;
			keepworkingfile = lockflag = false;
			break;

		case 'l':
			keepworkingfile = lockflag = true;
		revno:
			if (*a) {
				if (rev) warn("redefinition of revision number");
				rev = a;
                        }
                        break;

                case 'u':
                        keepworkingfile=true; lockflag=false;
                        goto revno;

		case 'i':
			initflag = true;
			goto revno;

		case 'j':
			mustread = true;
			goto revno;

		case 'I':
			interactiveflag = true;
			goto revno;

                case 'q':
                        quietflag=true;
                        goto revno;

                case 'f':
                        forceciflag=true;
                        goto revno;

                case 'k':
                        keepflag=true;
                        goto revno;

                case 'm':
			if (msg.size) redefined('m');
			msg = cleanlogmsg(a, strlen(a));
			if (!msg.size)
				error("missing message for -m option");
                        break;

                case 'n':
			if (!*a) {
                                error("missing symbolic name after -n");
				break;
            		}
			checkssym(a);
			addassoclst(false, a);
		        break;

		case 'N':
			if (!*a) {
                                error("missing symbolic name after -N");
				break;
            		}
			checkssym(a);
			addassoclst(true, a);
		        break;

                case 's':
			if (*a) {
				if (state) redefined('s');
				checksid(a);
				state = a;
			} else
				error("missing state for -s option");
                        break;

                case 't':
			if (*a) {
				if (textfile) redefined('t');
				textfile = a;
                        }
                        break;

		case 'd':
			if (altdate[0] || usestatdate)
				redefined('d');
			altdate[0] = '\0';
			if (!(usestatdate = !*a))
				str2date(a, altdate);
                        break;

		case 'M':
			mtimeflag = true;
			goto revno;

		case 'w':
			if (*a) {
				if (author) redefined('w');
				checksid(a);
				author = a;
			} else
				error("missing author for -w option");
                        break;

		case 'x':
			suffixes = a;
			break;

		case 'V':
			setRCSversion(*argv);
			break;

		case 'z':
			zone_set(a);
			break;

		case 'T':
			if (!*a) {
				Ttimeflag = true;
				break;
			}
			/* fall into */
                default:
			error("unknown option: %s%s", *argv, cmdusage);
                };
        }  /* end processing of options */

	/* Handle all pathnames.  */
	if (nerror) cleanup();
	else if (argc < 1) faterror("no input file%s", cmdusage);
	else for (;  0 < argc;  cleanup(), ++argv, --argc) {
	targetdelta = 0;
	ffree();

	switch (pairnames(argc, argv, rcswriteopen, mustread, false)) {

        case -1:                /* New RCS file */
#		if has_setuid && has_getuid
		    if (euid() != ruid()) {
			workerror("setuid initial checkin prohibited; use `rcs -i -a' first");
			continue;
		    }
#		endif
		rcsinitflag = true;
                break;

        case 0:                 /* Error */
                continue;

        case 1:                 /* Normal checkin with prev . RCS file */
		if (initflag) {
			rcserror("already exists");
			continue;
		}
		rcsinitflag = !Head;
        }

	/*
	 * RCSname contains the name of the RCS file, and
	 * workname contains the name of the working file.
	 * If the RCS file exists, finptr contains the file descriptor for the
	 * RCS file, and RCSstat is set. The admin node is initialized.
         */

	diagnose("%s  <--  %s\n", RCSname, workname);

	if (!(workptr = Iopen(workname, FOPEN_R_WORK, &workstat))) {
		eerror(workname);
		continue;
	}

	if (finptr) {
		if (same_file(RCSstat, workstat, 0)) {
			rcserror("RCS file is the same as working file %s.",
				workname
			);
			continue;
		}
		if (!checkaccesslist())
			continue;
	}

	krev = rev;
        if (keepflag) {
                /* get keyword values from working file */
		if (!getoldkeys(workptr)) continue;
		if (!rev  &&  !*(krev = prevrev.string)) {
			workerror("can't find a revision number");
                        continue;
                }
		if (!*prevdate.string && *altdate=='\0' && usestatdate==false)
			workwarn("can't find a date");
		if (!*prevauthor.string && !author)
			workwarn("can't find an author");
		if (!*prevstate.string && !state)
			workwarn("can't find a state");
        } /* end processing keepflag */

	/* Read the delta tree.  */
	if (finptr)
	    gettree();

        /* expand symbolic revision number */
	if (!fexpandsym(krev, &newdelnum, workptr))
	    continue;

        /* splice new delta into tree */
	if ((removedlock = addelta()) < 0)
	    continue;

	newdelta.num = newdelnum.string;
	newdelta.branches = 0;
	newdelta.lockedby = 0; /* This might be changed by addlock().  */
	newdelta.selector = true;
	newdelta.name = 0;
	clear_buf(&newdelta.ig);
	clear_buf(&newdelta.igtext);
	/* set author */
	if (author)
		newdelta.author=author;     /* set author given by -w         */
	else if (keepflag && *prevauthor.string)
		newdelta.author=prevauthor.string; /* preserve old author if possible*/
	else    newdelta.author=getcaller();/* otherwise use caller's id      */
	newdelta.state = default_state;
	if (state)
		newdelta.state=state;       /* set state given by -s          */
	else if (keepflag && *prevstate.string)
		newdelta.state=prevstate.string;   /* preserve old state if possible */
	if (usestatdate) {
	    time2date(workstat.st_mtime, altdate);
	}
	if (*altdate!='\0')
		newdelta.date=altdate;      /* set date given by -d           */
	else if (keepflag && *prevdate.string) {
		/* Preserve old date if possible.  */
		str2date(prevdate.string, olddate);
		newdelta.date = olddate;
	} else
		newdelta.date = getcurdate();  /* use current date */
	/* now check validity of date -- needed because of -d and -k          */
	if (targetdelta &&
	    cmpdate(newdelta.date,targetdelta->date) < 0) {
		rcserror("Date %s precedes %s in revision %s.",
			date2str(newdelta.date, newdatebuf),
			date2str(targetdelta->date, targetdatebuf),
			targetdelta->num
		);
		continue;
	}


	if (lockflag  &&  addlock(&newdelta, true) < 0) continue;

	if (keepflag && *prevname.string)
	    if (addsymbol(newdelta.num, prevname.string, false)  <  0)
		continue;
	if (!addsyms(newdelta.num))
	    continue;


	putadmin();
        puttree(Head,frewrite);
	putdesc(false,textfile);

	changework = Expand < MIN_UNCHANGED_EXPAND;
	dolog = true;
	lockthis = lockflag;
	workdelta = &newdelta;

        /* build rest of file */
	if (rcsinitflag) {
		diagnose("initial revision: %s\n", newdelta.num);
                /* get logmessage */
                newdelta.log=getlogmsg();
		putdftext(&newdelta, workptr, frewrite, false);
		RCSstat.st_mode = workstat.st_mode;
		RCSstat.st_nlink = 0;
		changedRCS = true;
        } else {
		diffname = maketemp(0);
		newhead  =  Head == &newdelta;
		if (!newhead)
			foutptr = frewrite;
		expname = buildrevision(
			gendeltas, targetdelta, (FILE*)0, false
		);
		if (
		    !forceciflag  &&
		    strcmp(newdelta.state, targetdelta->state) == 0  &&
		    (changework = rcsfcmp(
			workptr, &workstat, expname, targetdelta
		    )) <= 0
		) {
		    diagnose("file is unchanged; reverting to previous revision %s\n",
			targetdelta->num
		    );
		    if (removedlock < lockflag) {
			diagnose("previous revision was not locked; ignoring -l option\n");
			lockthis = 0;
		    }
		    dolog = false;
		    if (! (changedRCS = lockflag<removedlock || assoclst))
			workdelta = targetdelta;
		    else {
			/*
			 * We have started to build the wrong new RCS file.
			 * Start over from the beginning.
			 */
			long hwm = ftell(frewrite);
			int bad_truncate;
			Orewind(frewrite);

			/*
			* Work around a common ftruncate() bug:
			* NFS won't let you truncate a file that you
			* currently lack permissions for, even if you
			* had permissions when you opened it.
			* Also, Posix 1003.1b-1993 sec 5.6.7.2 p 128 l 1022
			* says ftruncate might fail because it's not supported.
			*/
#			if !has_ftruncate
#			    undef ftruncate
#			    define ftruncate(fd,length) (-1)
#			endif
			bad_truncate = ftruncate(fileno(frewrite), (off_t)0);

			Irewind(finptr);
			Lexinit();
			getadmin();
			gettree();
			if (!(workdelta = genrevs(
			    targetdelta->num, (char*)0, (char*)0, (char*)0,
			    &gendeltas
			)))
			    continue;
			workdelta->log = targetdelta->log;
			if (newdelta.state != default_state)
			    workdelta->state = newdelta.state;
			if (lockthis<removedlock && removelock(workdelta)<0)
			    continue;
			if (!addsyms(workdelta->num))
			    continue;
			if (dorewrite(true, true) != 0)
			    continue;
			fastcopy(finptr, frewrite);
			if (bad_truncate)
			    while (ftell(frewrite) < hwm)
				/* White out any earlier mistake with '\n's.  */
				/* This is unlikely.  */
				afputc('\n', frewrite);
		    }
		} else {
		    int wfd = Ifileno(workptr);
		    struct stat checkworkstat;
		    char const *diffv[6 + !!OPEN_O_BINARY], **diffp;
#		    if large_memory && !maps_memory
			FILE *wfile = workptr->stream;
			long wfile_off;
#		    endif
#		    if !has_fflush_input && !(large_memory && maps_memory)
		        off_t wfd_off;
#		    endif

		    diagnose("new revision: %s; previous revision: %s\n",
			newdelta.num, targetdelta->num
		    );
		    newdelta.log = getlogmsg();
#		    if !large_memory
			Irewind(workptr);
#			if has_fflush_input
			    if (fflush(workptr) != 0)
				Ierror();
#			endif
#		    else
#			if !maps_memory
			    if (
			    	(wfile_off = ftell(wfile)) == -1
			     ||	fseek(wfile, 0L, SEEK_SET) != 0
#			     if has_fflush_input
			     ||	fflush(wfile) != 0
#			     endif
			    )
				Ierror();
#			endif
#		    endif
#		    if !has_fflush_input && !(large_memory && maps_memory)
			wfd_off = lseek(wfd, (off_t)0, SEEK_CUR);
			if (wfd_off == -1
			    || (wfd_off != 0
				&& lseek(wfd, (off_t)0, SEEK_SET) != 0))
			    Ierror();
#		    endif
		    diffp = diffv;
		    *++diffp = DIFF;
		    *++diffp = DIFFFLAGS;
#		    if OPEN_O_BINARY
			if (Expand == BINARY_EXPAND)
			    *++diffp = "--binary";
#		    endif
		    *++diffp = newhead ? "-" : expname;
		    *++diffp = newhead ? expname : "-";
		    *++diffp = 0;
		    switch (runv(wfd, diffname, diffv)) {
			case DIFF_FAILURE: case DIFF_SUCCESS: break;
			default: rcsfaterror("diff failed");
		    }
#		    if !has_fflush_input && !(large_memory && maps_memory)
			if (lseek(wfd, wfd_off, SEEK_CUR) == -1)
			    Ierror();
#		    endif
#		    if large_memory && !maps_memory
			if (fseek(wfile, wfile_off, SEEK_SET) != 0)
			    Ierror();
#		    endif
		    if (newhead) {
			Irewind(workptr);
			putdftext(&newdelta, workptr, frewrite, false);
			if (!putdtext(targetdelta,diffname,frewrite,true)) continue;
		    } else
			if (!putdtext(&newdelta,diffname,frewrite,true)) continue;

		    /*
		    * Check whether the working file changed during checkin,
		    * to avoid producing an inconsistent RCS file.
		    */
		    if (
			fstat(wfd, &checkworkstat) != 0
		     ||	workstat.st_mtime != checkworkstat.st_mtime
		     ||	workstat.st_size != checkworkstat.st_size
		    ) {
			workerror("file changed during checkin");
			continue;
		    }

		    changedRCS = true;
                }
        }

	/* Deduce time_t of new revision if it is needed later.  */
	wtime = (time_t)-1;
	if (mtimeflag | Ttimeflag)
		wtime = date2time(workdelta->date);

	if (donerewrite(changedRCS,
		!Ttimeflag ? (time_t)-1
		: finptr && wtime < RCSstat.st_mtime ? RCSstat.st_mtime
		: wtime
	) != 0)
		continue;

        if (!keepworkingfile) {
		Izclose(&workptr);
		r = un_link(workname); /* Get rid of old file */
        } else {
		newworkmode = WORKMODE(RCSstat.st_mode,
			!   (Expand==VAL_EXPAND  ||  lockthis < StrictLocks)
		);
		mtime = mtimeflag ? wtime : (time_t)-1;

		/* Expand if it might change or if we can't fix mode, time.  */
		if (changework  ||  (r=fixwork(newworkmode,mtime)) != 0) {
		    Irewind(workptr);
		    /* Expand keywords in file.  */
		    locker_expansion = lockthis;
		    workdelta->name =
			namedrev(
				assoclst ? assoclst->ssymbol
				: keepflag && *prevname.string ? prevname.string
				: rev,
				workdelta
			);
		    switch (xpandfile(
			workptr, workdelta, &newworkname, dolog
		    )) {
			default:
			    continue;

			case 0:
			    /*
			     * No expansion occurred; try to reuse working file
			     * unless we already tried and failed.
			     */
			    if (changework)
				if ((r=fixwork(newworkmode,mtime)) == 0)
				    break;
			    /* fall into */
			case 1:
			    Izclose(&workptr);
			    aflush(exfile);
			    ignoreints();
			    r = chnamemod(&exfile, newworkname,
				    workname, 1, newworkmode, mtime
			    );
			    keepdirtemp(newworkname);
			    restoreints();
		    }
		}
        }
	if (r != 0) {
	    eerror(workname);
	    continue;
	}
	diagnose("done\n");

	}

	tempunlink();
	exitmain(exitstatus);
}       /* end of main (ci) */

	static void
cleanup()
{
	if (nerror) exitstatus = EXIT_FAILURE;
	Izclose(&finptr);
	Izclose(&workptr);
	Ozclose(&exfile);
	Ozclose(&fcopy);
	ORCSclose();
	dirtempunlink();
}

	void
exiterr()
{
	ORCSerror();
	dirtempunlink();
	tempunlink();
	_exit(EXIT_FAILURE);
}

/*****************************************************************/
/* the rest are auxiliary routines                               */


	static int
addelta()
/* Function: Appends a delta to the delta tree, whose number is
 * given by newdelnum.  Updates Head, newdelnum, newdelnumlength,
 * and the links in newdelta.
 * Return -1 on error, 1 if a lock is removed, 0 otherwise.
 */
{
	register char *tp;
	register int i;
	int removedlock;
	int newdnumlength;  /* actual length of new rev. num. */

	newdnumlength = countnumflds(newdelnum.string);

	if (rcsinitflag) {
                /* this covers non-existing RCS file and a file initialized with rcs -i */
		if (newdnumlength==0 && Dbranch) {
			bufscpy(&newdelnum, Dbranch);
			newdnumlength = countnumflds(Dbranch);
		}
		if (newdnumlength==0) bufscpy(&newdelnum, "1.1");
		else if (newdnumlength==1) bufscat(&newdelnum, ".1");
		else if (newdnumlength>2) {
		    rcserror("Branch point doesn't exist for revision %s.",
			newdelnum.string
		    );
		    return -1;
                } /* newdnumlength == 2 is OK;  */
                Head = &newdelta;
		newdelta.next = 0;
		return 0;
        }
        if (newdnumlength==0) {
                /* derive new revision number from locks */
		switch (findlock(true, &targetdelta)) {

		  default:
		    /* found two or more old locks */
		    return -1;

		  case 1:
                    /* found an old lock */
                    /* check whether locked revision exists */
		    if (!genrevs(targetdelta->num,(char*)0,(char*)0,(char*)0,&gendeltas))
			return -1;
                    if (targetdelta==Head) {
                        /* make new head */
                        newdelta.next=Head;
                        Head= &newdelta;
		    } else if (!targetdelta->next && countnumflds(targetdelta->num)>2) {
                        /* new tip revision on side branch */
                        targetdelta->next= &newdelta;
			newdelta.next = 0;
                    } else {
                        /* middle revision; start a new branch */
			bufscpy(&newdelnum, "");
			return addbranch(targetdelta, &newdelnum, 1);
                    }
		    incnum(targetdelta->num, &newdelnum);
		    return 1; /* successful use of existing lock */

		  case 0:
                    /* no existing lock; try Dbranch */
                    /* update newdelnum */
		    if (StrictLocks || !myself(RCSstat.st_uid)) {
			rcserror("no lock set by %s", getcaller());
			return -1;
                    }
                    if (Dbranch) {
			bufscpy(&newdelnum, Dbranch);
                    } else {
			incnum(Head->num, &newdelnum);
                    }
		    newdnumlength = countnumflds(newdelnum.string);
                    /* now fall into next statement */
                }
        }
        if (newdnumlength<=2) {
                /* add new head per given number */
                if(newdnumlength==1) {
                    /* make a two-field number out of it*/
		    if (cmpnumfld(newdelnum.string,Head->num,1)==0)
			incnum(Head->num, &newdelnum);
		    else
			bufscat(&newdelnum, ".1");
                }
		if (cmpnum(newdelnum.string,Head->num) <= 0) {
		    rcserror("revision %s too low; must be higher than %s",
			  newdelnum.string, Head->num
		    );
		    return -1;
                }
		targetdelta = Head;
		if (0 <= (removedlock = removelock(Head))) {
		    if (!genrevs(Head->num,(char*)0,(char*)0,(char*)0,&gendeltas))
			return -1;
		    newdelta.next = Head;
		    Head = &newdelta;
		}
		return removedlock;
        } else {
                /* put new revision on side branch */
                /*first, get branch point */
		tp = newdelnum.string;
		for (i = newdnumlength - ((newdnumlength&1) ^ 1);  --i;  )
			while (*tp++ != '.')
				continue;
		*--tp = 0; /* Kill final dot to get old delta temporarily. */
		if (!(targetdelta=genrevs(newdelnum.string,(char*)0,(char*)0,(char*)0,&gendeltas)))
		    return -1;
		if (cmpnum(targetdelta->num, newdelnum.string) != 0) {
		    rcserror("can't find branch point %s", newdelnum.string);
		    return -1;
                }
		*tp = '.'; /* Restore final dot. */
		return addbranch(targetdelta, &newdelnum, 0);
        }
}



	static int
addbranch(branchpoint, num, removedlock)
	struct hshentry *branchpoint;
	struct buf *num;
	int removedlock;
/* adds a new branch and branch delta at branchpoint.
 * If num is the null string, appends the new branch, incrementing
 * the highest branch number (initially 1), and setting the level number to 1.
 * the new delta and branchhead are in globals newdelta and newbranch, resp.
 * the new number is placed into num.
 * Return -1 on error, 1 if a lock is removed, 0 otherwise.
 * If REMOVEDLOCK is 1, a lock was already removed.
 */
{
	struct branchhead *bhead, **btrail;
	struct buf branchnum;
	int result;
	int field, numlength;
	static struct branchhead newbranch;  /* new branch to be inserted */

	numlength = countnumflds(num->string);

	if (!branchpoint->branches) {
                /* start first branch */
                branchpoint->branches = &newbranch;
                if (numlength==0) {
			bufscpy(num, branchpoint->num);
			bufscat(num, ".1.1");
		} else if (numlength&1)
			bufscat(num, ".1");
		newbranch.nextbranch = 0;

	} else if (numlength==0) {
                /* append new branch to the end */
                bhead=branchpoint->branches;
                while (bhead->nextbranch) bhead=bhead->nextbranch;
                bhead->nextbranch = &newbranch;
		bufautobegin(&branchnum);
		getbranchno(bhead->hsh->num, &branchnum);
		incnum(branchnum.string, num);
		bufautoend(&branchnum);
		bufscat(num, ".1");
		newbranch.nextbranch = 0;
        } else {
                /* place the branch properly */
		field = numlength - ((numlength&1) ^ 1);
                /* field of branch number */
		btrail = &branchpoint->branches;
		while (0 < (result=cmpnumfld(num->string,(*btrail)->hsh->num,field))) {
			btrail = &(*btrail)->nextbranch;
			if (!*btrail) {
				result = -1;
				break;
			}
                }
		if (result < 0) {
                        /* insert/append new branchhead */
			newbranch.nextbranch = *btrail;
			*btrail = &newbranch;
			if (numlength&1) bufscat(num, ".1");
                } else {
                        /* branch exists; append to end */
			bufautobegin(&branchnum);
			getbranchno(num->string, &branchnum);
			targetdelta = genrevs(
				branchnum.string, (char*)0, (char*)0, (char*)0,
				&gendeltas
			);
			bufautoend(&branchnum);
			if (!targetdelta)
			    return -1;
			if (cmpnum(num->string,targetdelta->num) <= 0) {
				rcserror("revision %s too low; must be higher than %s",
				      num->string, targetdelta->num
				);
				return -1;
                        }
			if (!removedlock
			    && 0 <= (removedlock = removelock(targetdelta))
			) {
			    if (numlength&1)
				incnum(targetdelta->num,num);
			    targetdelta->next = &newdelta;
			    newdelta.next = 0;
			}
			return removedlock;
			/* Don't do anything to newbranch.  */
                }
        }
        newbranch.hsh = &newdelta;
	newdelta.next = 0;
	if (branchpoint->lockedby)
	    if (strcmp(branchpoint->lockedby, getcaller()) == 0)
		return removelock(branchpoint); /* This returns 1.  */
	return removedlock;
}

	static int
addsyms(num)
	char const *num;
{
	register struct Symrev *p;

	for (p = assoclst;  p;  p = p->nextsym)
		if (addsymbol(num, p->ssymbol, p->override)  <  0)
			return false;
	return true;
}


	static void
incnum(onum,nnum)
	char const *onum;
	struct buf *nnum;
/* Increment the last field of revision number onum by one and
 * place the result into nnum.
 */
{
	register char *tp, *np;
	register size_t l;

	l = strlen(onum);
	bufalloc(nnum, l+2);
	np = tp = nnum->string;
	strcpy(np, onum);
	for (tp = np + l;  np != tp;  )
		if (isdigit(*--tp)) {
			if (*tp != '9') {
				++*tp;
				return;
			}
			*tp = '0';
		} else {
			tp++;
			break;
		}
	/* We changed 999 to 000; now change it to 1000.  */
	*tp = '1';
	tp = np + l;
	*tp++ = '0';
	*tp = 0;
}



	static int
removelock(delta)
struct hshentry * delta;
/* function: Finds the lock held by caller on delta,
 * removes it, and returns nonzero if successful.
 * Print an error message and return -1 if there is no such lock.
 * An exception is if !StrictLocks, and caller is the owner of
 * the RCS file. If caller does not have a lock in this case,
 * return 0; return 1 if a lock is actually removed.
 */
{
	register struct rcslock *next, **trail;
	char const *num;

        num=delta->num;
	for (trail = &Locks;  (next = *trail);  trail = &next->nextlock)
	    if (next->delta == delta)
		if (strcmp(getcaller(), next->login) == 0) {
		    /* We found a lock on delta by caller; delete it.  */
		    *trail = next->nextlock;
		    delta->lockedby = 0;
		    return 1;
		} else {
		    rcserror("revision %s locked by %s", num, next->login);
		    return -1;
                }
	if (!StrictLocks && myself(RCSstat.st_uid))
	    return 0;
	rcserror("no lock set by %s for revision %s", getcaller(), num);
	return -1;
}



	static char const *
getcurdate()
/* Return a pointer to the current date.  */
{
	static char buffer[datesize]; /* date buffer */

	if (!buffer[0])
		time2date(now(), buffer);
        return buffer;
}

	static int
#if has_prototypes
fixwork(mode_t newworkmode, time_t mtime)
  /* The `#if has_prototypes' is needed because mode_t might promote to int.  */
#else
  fixwork(newworkmode, mtime)
	mode_t newworkmode;
	time_t mtime;
#endif
{
	return
			1 < workstat.st_nlink
		    ||	(newworkmode&S_IWUSR && !myself(workstat.st_uid))
		    ||	setmtime(workname, mtime) != 0
		?   -1
	    :	workstat.st_mode == newworkmode  ?  0
#if has_fchmod
	    :	fchmod(Ifileno(workptr), newworkmode) == 0  ?  0
#endif
#if bad_chmod_close
	    :	-1
#else
	    :	chmod(workname, newworkmode)
#endif
	;
}

	static int
xpandfile(unexfile, delta, exname, dolog)
	RILE *unexfile;
	struct hshentry const *delta;
	char const **exname;
	int dolog;
/*
 * Read unexfile and copy it to a
 * file, performing keyword substitution with data from delta.
 * Return -1 if unsuccessful, 1 if expansion occurred, 0 otherwise.
 * If successful, stores the stream descriptor into *EXFILEP
 * and its name into *EXNAME.
 */
{
	char const *targetname;
	int e, r;

	targetname = makedirtemp(1);
	if (!(exfile = fopenSafer(targetname, FOPEN_W_WORK))) {
		eerror(targetname);
		workerror("can't build working file");
		return -1;
        }
	r = 0;
	if (MIN_UNEXPAND <= Expand)
		fastcopy(unexfile,exfile);
	else {
		for (;;) {
			e = expandline(
				unexfile, exfile, delta, false, (FILE*)0, dolog
			);
			if (e < 0)
				break;
			r |= e;
			if (e <= 1)
				break;
		}
	}
	*exname = targetname;
	return r & 1;
}




/* --------------------- G E T L O G M S G --------------------------------*/


	static struct cbuf
getlogmsg()
/* Obtain and yield a log message.
 * If a log message is given with -m, yield that message.
 * If this is the initial revision, yield a standard log message.
 * Otherwise, reads a character string from the terminal.
 * Stops after reading EOF or a single '.' on a
 * line. getlogmsg prompts the first time it is called for the
 * log message; during all later calls it asks whether the previous
 * log message can be reused.
 */
{
	static char const
		emptych[] = EMPTYLOG,
		initialch[] = "Initial revision";
	static struct cbuf const
		emptylog = { emptych, sizeof(emptych)-sizeof(char) },
		initiallog = { initialch, sizeof(initialch)-sizeof(char) };
	static struct buf logbuf;
	static struct cbuf logmsg;

	register char *tp;
	register size_t i;
	char const *caller;

	if (msg.size) return msg;

	if (keepflag) {
		/* generate std. log message */
		caller = getcaller();
		i = sizeof(ciklog)+strlen(caller)+3;
		bufalloc(&logbuf, i + datesize + zonelenmax);
		tp = logbuf.string;
		sprintf(tp, "%s%s at ", ciklog, caller);
		date2str(getcurdate(), tp+i);
		logmsg.string = tp;
		logmsg.size = strlen(tp);
		return logmsg;
	}

	if (!targetdelta && (
		cmpnum(newdelnum.string,"1.1")==0 ||
		cmpnum(newdelnum.string,"1.0")==0
	))
		return initiallog;

	if (logmsg.size) {
                /*previous log available*/
	    if (yesorno(true, "reuse log message of previous file? [yn](y): "))
		return logmsg;
        }

        /* now read string from stdin */
	logmsg = getsstdin("m", "log message", "", &logbuf);

        /* now check whether the log message is not empty */
	if (logmsg.size)
		return logmsg;
	return emptylog;
}

/*  Make a linked list of Symbolic names  */

        static void
addassoclst(flag, sp)
	int flag;
	char const *sp;
{
        struct Symrev *pt;

	pt = talloc(struct Symrev);
	pt->ssymbol = sp;
	pt->override = flag;
	pt->nextsym = 0;
	*nextassoc = pt;
	nextassoc = &pt->nextsym;
}
