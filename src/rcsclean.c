/*
 *                     RCS rcsclean operation
 */

#ifndef lint
static char rcsid[]=
"$Header: /usr/src/local/bin/rcs/src/RCS/rcsclean.c,v 4.5 89/10/30 12:29:00 trinkle Exp $ Purdue CS";
#endif
/*****************************************************************************
 *                       remove unneded working files
 *****************************************************************************
 */

/* Copyright (C) 1982, 1988, 1989 Walter Tichy
   Distributed under license by the Free Software Foundation, Inc.

This file is part of RCS.

RCS is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 1, or (at your option)
any later version.

RCS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RCS; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

Report problems and direct all questions to:

    rcs-bugs@cs.purdue.edu

*/




/* $Log:	rcsclean.c,v $
 * Revision 4.5  89/10/30  12:29:00  trinkle
 * Added -q option to agree with man page, added code to actually unlock
 * the RCS file if there were no differences, picked a bit of lint.
 * 
 * Revision 4.4  89/05/01  15:12:21  narten
 * changed copyright header to reflect current distribution rules
 * 
 * Revision 4.3  88/11/08  15:59:54  narten
 * removed reference to TARGETDIR
 * 
 * Revision 4.2  87/10/18  10:30:43  narten
 * Updating version numbers. Changes relative to 1.1 are actually
 * relative to 4.1
 * 
 * Revision 1.2  87/09/24  13:59:13  narten
 * Sources now pass through lint (if you ignore printf/sprintf/fprintf 
 * warnings)
 * 
 * Revision 1.1  84/01/23  14:50:16  kcs
 * Initial revision
 * 
 * Revision 4.1  83/12/15  12:26:18  wft
 * Initial revision.
 * 
 */
#include "rcsbase.h"
#define ERRCODE 2                   /*error code for exit status            */
#ifndef lint
static char rcsbaseid[] = RCSBASE;
#endif

extern int    cleanup();            /* cleanup after signals                */
extern char * getcaller();          /* get login of caller                  */
extern struct hshentry   * findlock(); /* find and remove lock              */
extern char * mktempfile();         /*temporary file name generator         */
extern int    fterror();            /*forward for special fatal error func. */
extern struct hshentry * genrevs(); /*generate delta numbers                */
extern int    nerror;               /*counter for errors                    */
extern int    quietflag;            /*suppresses diagnostics                */
extern FILE * finptr;               /* RCS input file                       */
extern FILE * fopen();

char *RCSfilename;
char *workfilename;
char * tempfile;
char * caller;                      /* caller's login;                      */
FILE * file1, * file2;              /*file descriptors for comparison       */

main (argc, argv)
int argc; char **argv;
{
        char * cmdusage;
        char command[NCPPN+revlength+40];
	char * rev;                   /* revision number from command line  */
        char numericrev[revlength];   /* holds expanded revision number     */
	int  revnums;                 /* number of -r options               */
        struct hshentry * gendeltas[hshsize];/*stores deltas to be generated*/
        struct hshentry * target;
	register int c1;              /* reading input                      */
	int  result;                  /* result of comparison               */
	int pairresult;               /* reulst of pairfilenames            */

        catchints();
	cmdid = "rcsclean";
	cmdusage = "command format:\n    rcsclean [-rrev] [-qrev] file";
	revnums=0;
	quietflag=false;
	caller=getcaller();
        while (--argc,++argv, argc>=1 && ((*argv)[0] == '-')) {
                switch ((*argv)[1]) {
                case 'r':
		revno:	if ((*argv)[2]!='\0') {
                            if (revnums==0) {
				    rev= *argv+2; revnums=1;
                            } else {
				    fterror("too many revision numbers");
                            }
                        } /* do nothing for empty -r */
                        break;
		case 'q':
			quietflag=true;
			goto revno;

                default:
			fterror("unknown option: %s\n%s", *argv, cmdusage);
                };
        } /* end of option processing */

	if (argc<1) fterror("No input file\n%s", cmdusage);

        /* now handle all filenames */
        do {
                finptr=NULL;
		pairresult=pairfilenames(argc,argv,false,false);

		if (pairresult==0) continue; /* error */
		if (!(access(workfilename,4)==0)) {
			diagnose("Can't open %s",workfilename);
                        continue;
		} elsif (pairresult == -1) {
			warn("Can't find RCS file for %s",workfilename);
			continue;
		}
                diagnose("RCS file: %s",RCSfilename);
                if (!trysema(RCSfilename,false)) continue; /* give up */


                gettree(); /* reads in the delta tree */

                if (Head==nil) {
                        error("no revisions present");
                        continue;
                }
                if (revnums==0)
			rev=(Dbranch!=nil?Dbranch->num:Head->num); /* default rev1 */

		if (!expandsym(rev,numericrev)) continue;
                if (!(target=genrevs(numericrev,(char *)nil,(char *)nil,(char *)nil,gendeltas))) continue;

		tempfile=mktempfile("/tmp/",TMPFILE1);
		diagnose("retrieving revision %s",target->num);
                VOID sprintf(command,"%s -q -p%s %s > %s\n",
			CO ,target->num,RCSfilename,tempfile);
                if (system(command)){
                        error("co failed");
                        continue;
                }
		/* now do comparison */
		if ((file1=fopen(tempfile,"r"))==NULL) {
			error("Can't open checked out file %s",tempfile);
			continue;
		}
		if ((file2=fopen(workfilename,"r"))==NULL) {
			error("Can't open %s",workfilename);
			continue;
		}
		result=1;
		while ((c1=getc(file1))==getc(file2)) {
			if (c1==EOF) {
				/* identical files; can remove working file */
				result=0;
				diagnose("files identical; %s removed",workfilename);
				if (unlink(workfilename)!=0) {
					error("Can't unlink %s",workfilename);
				}
				if (findlock(caller, false)) {
				    VOID sprintf(command,"%s -q -u%s %s\n",
						 RCS_CMD, target->num, RCSfilename);
				    if (system(command)) {
					error("unlock failed");
					continue;
				    }
				}
				    
				break;
			}
		}
		VOID fclose(file1); VOID fclose(file2);

		if (result==1) diagnose ("files different");


        } while (cleanup(),
                 ++argv, --argc >=1);


	if (nerror>0) {
		exit(ERRCODE);
	} else {
		exit(result);
	}

}


/*VARARGS1*/
fterror(e, e1, e2)
char * e, * e1, * e2;
/* prints error message and terminates program with ERRCODE */
{       nerror++;
        VOID fprintf(stderr,"%s error: ",cmdid);
	VOID fprintf(stderr,e, e1, e2);
        VOID fprintf(stderr,"\n%s aborted\n",cmdid);
        VOID cleanup();
	exit(ERRCODE);
}

