/* Extract RCS keyword string values from working files.

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

#include  "rcsbase.h"

static int badly_terminated P((void));
static int checknum P((char const*));
static int get0val P((int,RILE*,struct buf*,int));
static int getval P((RILE*,struct buf*,int));
static int keepdate P((RILE*));
static int keepid P((int,RILE*,struct buf*));
static int keeprev P((RILE*));

int prevkeys;
struct buf prevauthor, prevdate, prevname, prevrev, prevstate;

	int
getoldkeys(fp)
	register RILE *fp;
/* Function: Tries to read keyword values for author, date,
 * revision number, and state out of the file fp.
 * If fp is null, workname is opened and closed instead of using fp.
 * The results are placed into
 * prevauthor, prevdate, prevname, prevrev, prevstate.
 * Aborts immediately if it finds an error and returns false.
 * If it returns true, it doesn't mean that any of the
 * values were found; instead, check to see whether the corresponding arrays
 * contain the empty string.
 */
{
    register int c;
    char keyword[keylength+1];
    register char * tp;
    int needs_closing;
    int prevname_found;

    if (prevkeys)
	return true;

    needs_closing = false;
    if (!fp) {
	if (!(fp = Iopen(workname, FOPEN_R_WORK, (struct stat*)0))) {
	    eerror(workname);
	    return false;
	}
	needs_closing = true;
    }

    /* initialize to empty */
    bufscpy(&prevauthor, "");
    bufscpy(&prevdate, "");
    bufscpy(&prevname, "");  prevname_found = 0;
    bufscpy(&prevrev, "");
    bufscpy(&prevstate, "");

    c = '\0'; /* anything but KDELIM */
    for (;;) {
        if ( c==KDELIM) {
	    do {
		/* try to get keyword */
		tp = keyword;
		for (;;) {
		    Igeteof_(fp, c, goto ok;)
		    switch (c) {
			default:
			    if (keyword+keylength <= tp)
				break;
			    *tp++ = c;
			    continue;

			case '\n': case KDELIM: case VDELIM:
			    break;
		    }
		    break;
		}
	    } while (c==KDELIM);
            if (c!=VDELIM) continue;
	    *tp = c;
	    Igeteof_(fp, c, break;)
	    switch (c) {
		case ' ': case '\t': break;
		default: continue;
	    }

	    switch (trymatch(keyword)) {
            case Author:
		if (!keepid(0, fp, &prevauthor))
		    return false;
		c = 0;
                break;
            case Date:
		if (!(c = keepdate(fp)))
		    return false;
                break;
            case Header:
            case Id:
		if (!(
		      getval(fp, (struct buf*)0, false) &&
		      keeprev(fp) &&
		      (c = keepdate(fp)) &&
		      keepid(c, fp, &prevauthor) &&
		      keepid(0, fp, &prevstate)
		))
		    return false;
		/* Skip either ``who'' (new form) or ``Locker: who'' (old).  */
		if (getval(fp, (struct buf*)0, true) &&
		    getval(fp, (struct buf*)0, true))
			c = 0;
		else if (nerror)
			return false;
		else
			c = KDELIM;
		break;
            case Locker:
		(void) getval(fp, (struct buf*)0, false);
		c = 0;
		break;
            case Log:
            case RCSfile:
            case Source:
		if (!getval(fp, (struct buf*)0, false))
		    return false;
		c = 0;
                break;
	    case Name:
		if (getval(fp, &prevname, false)) {
		    if (*prevname.string)
			checkssym(prevname.string);
		    prevname_found = 1;
		}
		c = 0;
		break;
            case Revision:
		if (!keeprev(fp))
		    return false;
		c = 0;
                break;
            case State:
		if (!keepid(0, fp, &prevstate))
		    return false;
		c = 0;
                break;
            default:
               continue;
            }
	    if (!c)
		Igeteof_(fp, c, c=0;)
	    if (c != KDELIM) {
		workerror("closing %c missing on keyword", KDELIM);
		return false;
	    }
	    if (prevname_found &&
		*prevauthor.string && *prevdate.string &&
		*prevrev.string && *prevstate.string
	    )
                break;
        }
	Igeteof_(fp, c, break;)
    }

 ok:
    if (needs_closing)
	Ifclose(fp);
    else
	Irewind(fp);
    prevkeys = true;
    return true;
}

	static int
badly_terminated()
{
	workerror("badly terminated keyword value");
	return false;
}

	static int
getval(fp, target, optional)
	register RILE *fp;
	struct buf *target;
	int optional;
/* Reads a keyword value from FP into TARGET.
 * Returns true if one is found, false otherwise.
 * Does not modify target if it is 0.
 * Do not report an error if OPTIONAL is set and KDELIM is found instead.
 */
{
	int c;
	Igeteof_(fp, c, return badly_terminated();)
	return get0val(c, fp, target, optional);
}

	static int
get0val(c, fp, target, optional)
	register int c;
	register RILE *fp;
	struct buf *target;
	int optional;
/* Reads a keyword value from C+FP into TARGET, perhaps OPTIONALly.
 * Same as getval, except C is the lookahead character.
 */
{   register char * tp;
    char const *tlim;
    register int got1;

    if (target) {
	bufalloc(target, 1);
	tp = target->string;
	tlim = tp + target->size;
    } else
	tlim = tp = 0;
    got1 = false;
    for (;;) {
	switch (c) {
	    default:
		got1 = true;
		if (tp) {
		    *tp++ = c;
		    if (tlim <= tp)
			tp = bufenlarge(target, &tlim);
		}
		break;

	    case ' ':
	    case '\t':
		if (tp) {
		    *tp = 0;
#		    ifdef KEEPTEST
			printf("getval: %s\n", target);
#		    endif
		}
		return got1;

	    case KDELIM:
		if (!got1 && optional)
		    return false;
		/* fall into */
	    case '\n':
	    case 0:
		return badly_terminated();
	}
	Igeteof_(fp, c, return badly_terminated();)
    }
}


	static int
keepdate(fp)
	RILE *fp;
/* Function: reads a date prevdate; checks format
 * Return 0 on error, lookahead character otherwise.
 */
{
    struct buf prevday, prevtime;
    register int c;

    c = 0;
    bufautobegin(&prevday);
    if (getval(fp,&prevday,false)) {
	bufautobegin(&prevtime);
	if (getval(fp,&prevtime,false)) {
	    Igeteof_(fp, c, c=0;)
	    if (c) {
		register char const *d = prevday.string, *t = prevtime.string;
		bufalloc(&prevdate, strlen(d) + strlen(t) + 9);
		sprintf(prevdate.string, "%s%s %s%s",
		    /* Parse dates put out by old versions of RCS.  */
		      isdigit(d[0]) && isdigit(d[1]) && !isdigit(d[2])
		    ? "19" : "",
		    d, t,
		    strchr(t,'-') || strchr(t,'+')  ?  ""  :  "+0000"
		);
	    }
	}
	bufautoend(&prevtime);
    }
    bufautoend(&prevday);
    return c;
}

	static int
keepid(c, fp, b)
	int c;
	RILE *fp;
	struct buf *b;
/* Get previous identifier from C+FP into B.  */
{
	if (!c)
	    Igeteof_(fp, c, return false;)
	if (!get0val(c, fp, b, false))
	    return false;
	checksid(b->string);
	return !nerror;
}

	static int
keeprev(fp)
	RILE *fp;
/* Get previous revision from FP into prevrev.  */
{
	return getval(fp,&prevrev,false) && checknum(prevrev.string);
}


	static int
checknum(s)
	char const *s;
{
    register char const *sp;
    register int dotcount = 0;
    for (sp=s; ; sp++) {
	switch (*sp) {
	    case 0:
		if (dotcount & 1)
		    return true;
		else
		    break;

	    case '.':
		dotcount++;
		continue;

	    default:
		if (isdigit(*sp))
		    continue;
		break;
	}
	break;
    }
    workerror("%s is not a revision number", s);
    return false;
}



#ifdef KEEPTEST

/* Print the keyword values found.  */

char const cmdid[] ="keeptest";

	int
main(argc, argv)
int  argc; char  *argv[];
{
        while (*(++argv)) {
		workname = *argv;
		getoldkeys((RILE*)0);
                printf("%s:  revision: %s, date: %s, author: %s, name: %s, state: %s\n",
			    *argv, prevrev.string, prevdate.string, prevauthor.string, prevname.string, prevstate.string);
	}
	exitmain(EXIT_SUCCESS);
}
#endif
