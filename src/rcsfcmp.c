/* Compare working files, ignoring RCS keyword strings.

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

/*****************************************************************************
 *                       rcsfcmp()
 *                       Testprogram: define FCMPTEST
 *****************************************************************************
 */

/*
#define FCMPTEST
*/
/* Testprogram; prints out whether two files are identical,
 * except for keywords
 */

#include  "rcsbase.h"

libId(fcmpId, "$Id: rcsfcmp.c,v 5.14 1995/06/16 06:19:24 eggert Exp $")

	static int discardkeyval P((int,RILE*));
	static int
discardkeyval(c, f)
	register int c;
	register RILE *f;
{
	for (;;)
		switch (c) {
			case KDELIM:
			case '\n':
				return c;
			default:
				Igeteof_(f, c, return EOF;)
				break;
		}
}

	int
rcsfcmp(xfp, xstatp, uname, delta)
	register RILE *xfp;
	struct stat const *xstatp;
	char const *uname;
	struct hshentry const *delta;
/* Compare the files xfp and uname.  Return zero
 * if xfp has the same contents as uname and neither has keywords,
 * otherwise -1 if they are the same ignoring keyword values,
 * and 1 if they differ even ignoring
 * keyword values. For the LOG-keyword, rcsfcmp skips the log message
 * given by the parameter delta in xfp.  Thus, rcsfcmp returns nonpositive
 * if xfp contains the same as uname, with the keywords expanded.
 * Implementation: character-by-character comparison until $ is found.
 * If a $ is found, read in the marker keywords; if they are real keywords
 * and identical, read in keyword value. If value is terminated properly,
 * disregard it and optionally skip log message; otherwise, compare value.
 */
{
    register int xc, uc;
    char xkeyword[keylength+2];
    int eqkeyvals;
    register RILE *ufp;
    register int xeof, ueof;
    register char * tp;
    register char const *sp;
    register size_t leaderlen;
    int result;
    enum markers match1;
    struct stat ustat;

    if (!(ufp = Iopen(uname, FOPEN_R_WORK, &ustat))) {
       efaterror(uname);
    }
    xeof = ueof = false;
    if (MIN_UNEXPAND <= Expand) {
	if (!(result = xstatp->st_size!=ustat.st_size)) {
#	    if large_memory && maps_memory
		result = !!memcmp(xfp->base,ufp->base,(size_t)xstatp->st_size);
#	    else
		for (;;) {
		    /* get the next characters */
		    Igeteof_(xfp, xc, xeof=true;)
		    Igeteof_(ufp, uc, ueof=true;)
		    if (xeof | ueof)
			goto eof;
		    if (xc != uc)
			goto return1;
		}
#	    endif
	}
    } else {
	xc = 0;
	uc = 0; /* Keep lint happy.  */
	leaderlen = 0;
	result = 0;

	for (;;) {
	  if (xc != KDELIM) {
	    /* get the next characters */
	    Igeteof_(xfp, xc, xeof=true;)
	    Igeteof_(ufp, uc, ueof=true;)
	    if (xeof | ueof)
		goto eof;
	  } else {
	    /* try to get both keywords */
	    tp = xkeyword;
	    for (;;) {
		Igeteof_(xfp, xc, xeof=true;)
		Igeteof_(ufp, uc, ueof=true;)
		if (xeof | ueof)
		    goto eof;
		if (xc != uc)
		    break;
		switch (xc) {
		    default:
			if (xkeyword+keylength <= tp)
			    break;
			*tp++ = xc;
			continue;
		    case '\n': case KDELIM: case VDELIM:
			break;
		}
		break;
	    }
	    if (
		(xc==KDELIM || xc==VDELIM)  &&  (uc==KDELIM || uc==VDELIM)  &&
		(*tp = xc,  (match1 = trymatch(xkeyword)) != Nomatch)
	    ) {
#ifdef FCMPTEST
	      VOID printf("found common keyword %s\n",xkeyword);
#endif
	      result = -1;
	      for (;;) {
		  if (xc != uc) {
		      xc = discardkeyval(xc, xfp);
		      uc = discardkeyval(uc, ufp);
		      if ((xeof = xc==EOF)  |  (ueof = uc==EOF))
			  goto eof;
		      eqkeyvals = false;
		      break;
		  }
		  switch (xc) {
		      default:
			  Igeteof_(xfp, xc, xeof=true;)
			  Igeteof_(ufp, uc, ueof=true;)
			  if (xeof | ueof)
			      goto eof;
			  continue;

		      case '\n': case KDELIM:
			  eqkeyvals = true;
			  break;
		  }
		  break;
	      }
	      if (xc != uc)
		  goto return1;
	      if (xc==KDELIM) {
		  /* Skip closing KDELIM.  */
		  Igeteof_(xfp, xc, xeof=true;)
		  Igeteof_(ufp, uc, ueof=true;)
		  if (xeof | ueof)
		      goto eof;
		  /* if the keyword is LOG, also skip the log message in xfp*/
		  if (match1==Log) {
		      /* first, compute the number of line feeds in log msg */
		      int lncnt;
		      size_t ls, ccnt;
		      sp = delta->log.string;
		      ls = delta->log.size;
		      if (ls<sizeof(ciklog)-1 || memcmp(sp,ciklog,sizeof(ciklog)-1)) {
			/*
			* This log message was inserted.  Skip its header.
			* The number of newlines to skip is
			* 1 + (C+1)*(1+L+1), where C is the number of newlines
			* in the comment leader, and L is the number of
			* newlines in the log string.
			*/
			int c1 = 1;
			for (ccnt=Comment.size; ccnt--; )
			    c1 += Comment.string[ccnt] == '\n';
			lncnt = 2*c1 + 1;
			while (ls--) if (*sp++=='\n') lncnt += c1;
			for (;;) {
			    if (xc=='\n')
				if(--lncnt==0) break;
			    Igeteof_(xfp, xc, goto returnresult;)
			}
			/* skip last comment leader */
			/* Can't just skip another line here, because there may be */
			/* additional characters on the line (after the Log....$)  */
			ccnt = RCSversion<VERSION(5) ? Comment.size : leaderlen;
			do {
			    Igeteof_(xfp, xc, goto returnresult;)
			    /*
			     * Read to the end of the comment leader or '\n',
			     * whatever comes first, because the leader's
			     * trailing white space was probably stripped.
			     */
			} while (ccnt-- && (xc!='\n' || --c1));
		      }
		  }
	      } else {
		  /* both end in the same character, but not a KDELIM */
		  /* must compare string values.*/
#ifdef FCMPTEST
		  VOID printf("non-terminated keywords %s, potentially different values\n",xkeyword);
#endif
		  if (!eqkeyvals)
		      goto return1;
	      }
	    }
	  }
	  if (xc != uc)
	      goto return1;
	  if (xc == '\n')
	      leaderlen = 0;
	  else
	      leaderlen++;
	}
    }

  eof:
    if (xeof==ueof)
	goto returnresult;
  return1:
    result = 1;
  returnresult:
    Ifclose(ufp);
    return result;
}



#ifdef FCMPTEST

char const cmdid[] = "rcsfcmp";

main(argc, argv)
int  argc; char  *argv[];
/* first argument: comment leader; 2nd: log message, 3rd: expanded file,
 * 4th: unexpanded file
 */
{       struct hshentry delta;

	Comment.string = argv[1];
	Comment.size = strlen(argv[1]);
	delta.log.string = argv[2];
	delta.log.size = strlen(argv[2]);
	if (rcsfcmp(Iopen(argv[3], FOPEN_R_WORK, (struct stat*)0), argv[4], &delta))
                VOID printf("files are the same\n");
        else    VOID printf("files are different\n");
}
#endif
