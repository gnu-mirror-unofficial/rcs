#$Header: /usr/src/local/bin/rcs/src/RCS/Makefile,v 1.20 90/07/09 10:39:16 hammer Exp $
# Copyright (C) 1982, 1988, 1989 Walter Tichy
#   Distributed under license by the Free Software Foundation, Inc.
#
# This file is part of RCS.
#
# RCS is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 1, or (at your option)
# any later version.
#
# RCS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with RCS; see the file COPYING.  If not, write to
# the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
#
# Report problems and direct all questions to:
#
#    rcs-bugs@cs.purdue.edu
#
#               INSTRUCTIONS
#               ============
# 0.  If you are porting to different hardware,
#     check the instructions in rcsbase.h
#
# 1. Early version of diff did not support options needed by RCS.
#    Newer versions of diff support them, and diff is no longer distributed with
#    RCS. The following definitions apply only if you have both old versions
#    and new versions of diff, and you want RCS to use the new versions.
#    The default should be :
#

DIFF      = /bin/diff
DIFF3     = /usr/lib/diff3

# 1a. If you are using GNU diff and diff3, you must use the alternate form
#     of the merge shell script called gnu-merge.sh.  In order to do that,
#     set MERGE accordingly

#MERGE = gnu-merge.sh
MERGE = merge.sh


# 2.  Figure out where to put the RCS commands; define RCSDIR accordingly.

RCSDIR  = /usr/local/bin


# 3.  Figure out where to put your old RCS commands; define OLDDIR
#     accordingly. This is not needed if you don't have an old release of RCS.
OLDDIR    = /usr/old/bin

# 4.  Set the operating system variable OS. If you run on 4.1 BSD, do not
#     define OS:
#     OS  =
#     If you run on USG Unix or UN*X, use:
#     OS  = -DUSG
#     If you run on Version 6 Unix, use:
#     OS  = -DV6
#     If you run on SunOS, Ultrix, 4.2 or 4.3 BSD, use:
#     OS = -DV4_2BSD
OS        = -DV4_2BSD

#     Also, set SIGNAL_TYPE to be what your signal() handlers yield.
#     Older signal handlers yield 'int'; some newer ones 'void'.
#     For SunOS 4.0 the correct type is 'void'; using 'int' will
#     cause compile time errors.  Check the signal(3) man page to be sure.

SIGNAL_TYPE = int

# 5.  Decide whether you want strict locking as the default or not.
#     If you want strict locking, use:
#     LOCKING = 1
#     otherwise:
#     LOCKING = 0
LOCKING   = 1

# 6.  Decide what loader flags you need, and define LDFLAGS accordingly.
#     On the Vax you need none, on PDP-11s with split i/d you need -i.
#LDFLAGS  = -i
LDFLAGS   = -s

# 7.  RCS can maintain a log of the use of the co and ci commands. If you
#     want the logging, you must
#     a) define SNOOP (that's where the process that writes the log goes),
#     b) define SNOOPFILE (that's the file where SNOOP writes the log;
#        it must be owned by the same user that owns SNOOP),
#     c) include SNOOP and SNOOPFILE in the CFLAGS macro.
#     If you don't want the logging of command usage, omit the SNOOP and
#     SNOOPFILE definitions from CFLAGS.
#     The two variants of the CFLAG macros are given below.
SNOOPFILE = /usr/log/rcslog/log
SNOOP	= /usr/local/bin/snoop

DEFINES= -DCOMPAT2 $(OS) -DSIGNAL_TYPE=$(SIGNAL_TYPE) -DSTRICT_LOCKING=$(LOCKING) -DDIFF='"$(DIFF)"' -DCO='"${RCSDIR}/co"' -DRCS_CMD='"${RCSDIR}/rcs"' -DMERGE='"${RCSDIR}/merge"'

# for command logging use
#CFLAGS	= -O $(DEFINES) -DSNOOP='"${SNOOP}"' -DSNOOPFILE='"${SNOOPFILE}"'
CFLAGS	= -O $(DEFINES)

LINTFLAGS = -c -u
LINT = lint $(LINTFLAGS) -Dlint $(DEFINES)

# 8.  If you don't have release 2 RCS files around (e.g., if you don't
#     have release 2 of RCS), remove the word -DCOMPAT2 from whatever
#     variant of CFLAGS you selected. -DCOMPAT2 assures that release 2
#     RCS files can still be read. (After all release 2 RCS files have
#     been updated with release 3 or 4 ci or rcs, you can remake RCS without
#     -DCOMPAT2.) Also, make sure that the co commands at the end of this
#     file are commented out; otherwise, Make will complain.
#
# 9.  Now you are ready. Try to make 'depend' and 'all'. If all went well, make
#     'install' if you want no logging of command usage, 'installog' otherwise.
#
# 10. IMPORTANT! When you have RCS installed, rename old
#     release 2 RCS files as follows (if you have any):
#     If the working file was "f.c" and the RCS file "f.c.v", rename the
#     RCS file to "f.c,v". If the working file was "f.c" and the RCS file
#     "f.v", rename the RCS file "f.c,v". Thus, suffixes are no longer
#     dropped and RCS files end in ",v" rather than ".v".
#
# 11. If you want to maintain RCS with itself, be sure you preserve the
#     original revision numbers, dates, etc. This is done by checking the
#     files in with the -k option.
#
BCOMMANDS   =   ci ident rcs rcsclean rcsdiff rcsmerge rlog co
# binary comands

RCSCOMMANDS = merge $(BCOMMANDS)
# all commands

BOBJECTS = ci.o co.o ident.o rcs.o rcsdiff.o rcsedit.o\
	rcsfcmp.o rcsfnms.o rcsgen.o rcskeep.o rcskeys.o rcslex.o rcsmerge.o\
	rcsrev.o rcssyn.o rcsutil.o rlog.o snoop.o

all:	$(RCSCOMMANDS)

INSTALL = install -c -m 775 -o root -g staff

$(BOBJECTS):	rcsbase.h

partime.o maketime.o co.o ci.o rcs.o rlog.o rcsutil.o:        time.h

install:	all 
#	Save the old commands if you have any.
#	-cd ${RCSDIR}; mv $(RCSCOMMANDS) ${RCSDIR}$(OLDDIR)
	${INSTALL} ci ${RCSDIR}
	${INSTALL} ident ${RCSDIR}
	${INSTALL} rcs ${RCSDIR}
	${INSTALL} rcsdiff ${RCSDIR}
	${INSTALL} rcsmerge ${RCSDIR}
	${INSTALL} rlog ${RCSDIR}
	${INSTALL} co ${RCSDIR}
	${INSTALL} merge ${RCSDIR}
	${INSTALL} rcsclean ${RCSDIR}
#	${INSTALL} rcsfreeze.sh ${RCSDIR}/rcsfreeze

installog:	installsnoop install

installsnoop:	snoop
	${INSTALL} snoop $(SNOOP)
	chmod u+s $(SNOOP)
	touch $(SNOOPFILE)
	chmod 644 $(SNOOPFILE)
# CAUTION: You may have to do a chown of SNOOP and SNOOPFILE (if not owned by root).
lint: ci.lint co.lint ident.lint rlog.lint rcs.lint rcsdiff.lint rcsmerge.lint snoop.lint

clean:
	-rm -f *.o; rm -f $(RCSCOMMANDS) snoop

CIFILES = ci.o rcslex.o rcssyn.o rcsgen.o rcsedit.o rcskeys.o rcsrev.o rcsutil.o rcsfnms.o partime.o maketime.o rcskeep.o rcsfcmp.o

CISRC = ci.c rcslex.c rcssyn.c rcsgen.c rcsedit.c rcskeys.c rcsrev.c rcsutil.c rcsfnms.c partime.c maketime.c rcskeep.c rcsfcmp.c

ci.lint:
	$(LINT) $(CISRC)
	
ci:	$(CIFILES)
	${CC} -o ci $(CIFILES) $(LDFLAGS)

COFILES = co.o rcslex.o rcssyn.o rcsgen.o rcsedit.o rcskeys.o rcsrev.o rcsutil.o rcsfnms.o partime.o maketime.o

COSRC = co.c rcslex.c rcssyn.c rcsgen.c rcsedit.c rcskeys.c rcsrev.c rcsutil.c rcsfnms.c partime.c maketime.c
co.lint:
	$(LINT) $(COSRC)
co:	$(COFILES)
	${CC} -o co $(COFILES) $(LDFLAGS)

ident:	ident.o rcskeys.o
	${CC} -o ident ident.o rcskeys.o $(LDFLAGS)

ident.lint:
	$(LINT) ident.c rcskeys.c

merge:	${MERGE}
	sed -e '/^#/d' \
		-e 's:DIFF=.*$$:DIFF=$(DIFF):' \
		-e 's:DIFF3=.*$$:DIFF3=$(DIFF3):' \
		${MERGE} > merge
#	This takes out the comment lines and substitutes in DIFF and DIFF3.
#	(Comments are not permitted in some older shells.)
	chmod 755 merge

RLOG = rlog.o rcslex.o rcssyn.o rcsrev.o rcsutil.o partime.o maketime.o rcsfnms.o
RLOGSRC = rlog.c rcslex.c rcssyn.c rcsrev.c rcsutil.c partime.c maketime.c rcsfnms.c
rlog:	$(RLOG)
	${CC} -o rlog $(RLOG) $(LDFLAGS)

rlog.lint:
	$(LINT) $(RLOGSRC)

RCS = rcs.o rcslex.o rcssyn.o rcsrev.o rcsutil.o rcsgen.o rcsedit.o rcskeys.o rcsfnms.o
RCSSRC = rcs.c rcslex.c rcssyn.c rcsrev.c rcsutil.c rcsgen.c rcsedit.c rcskeys.c rcsfnms.c
rcs:	$(RCS)
	${CC} -o rcs $(RCS) $(LDFLAGS)

rcs.lint:
	$(LINT) $(RCSSRC)

RCSCLEAN = rcsclean.o rcslex.o rcsfnms.o rcsrev.o rcssyn.o rcsutil.o
RCSCLEANSRC = rcsclean.c rcslex.c rcsfnms.c rcsrev.c rcssyn.c rcsutil.c
rcsclean:	$(RCSCLEAN)
	${CC} -o rcsclean $(RCSCLEAN) $(LDFLAGS)

rcsclean.lint:
	$(LINT) $(RCSCLEANSRC)

RCSDIFF = rcsdiff.o rcsutil.o rcsfnms.o rcsrev.o rcssyn.o rcslex.o

RCSDIFFSRC = rcsdiff.c rcsutil.c rcsfnms.c rcsrev.c rcssyn.c rcslex.c
rcsdiff:	$(RCSDIFF)
	${CC} -o rcsdiff $(RCSDIFF) ${LDFLAGS}

rcsdiff.lint:
	$(LINT) $(RCSDIFFSRC)
RCSMERGE = rcsmerge.o rcsutil.o rcsfnms.o rcsrev.o rcssyn.o rcslex.o
RCSMERGESRC = rcsmerge.c rcsutil.c rcsfnms.c rcsrev.c rcssyn.c rcslex.c
rcsmerge:	$(RCSMERGE)
	${CC} -o rcsmerge $(RCSMERGE) ${LDFLAGS}
rcsmerge.lint:
	$(LINT) $(RCSMERGESRC)
	
snoop:	snoop.o
	${CC} -o snoop snoop.o ${LDFLAGS}
snoop.lint:
	$(LINT) snoop.c

SOURCE=	ci.c co.c curdir.c ident.c maketime.c partime.c rcs.c \
	rcsclean.c rcsdiff.c rcsedit.c rcsfcmp.c rcsfnms.c rcsgen.c \
	rcskeep.c rcskeys.c rcslex.c rcsmerge.c rcsrev.c rcssyn.c rcsutil.c \
	rlog.c snoop.c

HFILES=	rcsbase.h time.h

depend:	${SOURCE} ${HFILES}
	(sed '/^# DO NOT DELETE THIS LINE/q' Makefile && \
	 cc -M ${CFLAGS} ${SOURCE} | sed 's/\.\///; /\//d' \
	) >Makefile.new
	cp Makefile Makefile.bak
	cp Makefile.new Makefile
	rm -f Makefile.new


# DO NOT DELETE THIS LINE - 
ci.o: ci.c
ci.o: rcsbase.h
ci.o: time.h
co.o: co.c
co.o: rcsbase.h
co.o: time.h
curdir.o: curdir.c
curdir.o: rcsbase.h
ident.o: ident.c
ident.o: rcsbase.h
maketime.o: maketime.c
maketime.o: rcsbase.h
maketime.o: time.h
partime.o: partime.c
partime.o: time.h
rcs.o: rcs.c
rcs.o: rcsbase.h
rcsclean.o: rcsclean.c
rcsclean.o: rcsbase.h
rcsdiff.o: rcsdiff.c
rcsdiff.o: rcsbase.h
rcsedit.o: rcsedit.c
rcsedit.o: rcsbase.h
rcsfcmp.o: rcsfcmp.c
rcsfcmp.o: rcsbase.h
rcsfnms.o: rcsfnms.c
rcsfnms.o: rcsbase.h
rcsgen.o: rcsgen.c
rcsgen.o: rcsbase.h
rcskeep.o: rcskeep.c
rcskeep.o: rcsbase.h
rcskeys.o: rcskeys.c
rcskeys.o: rcsbase.h
rcslex.o: rcslex.c
rcslex.o: rcsbase.h
rcsmerge.o: rcsmerge.c
rcsmerge.o: rcsbase.h
rcsrev.o: rcsrev.c
rcsrev.o: rcsbase.h
rcssyn.o: rcssyn.c
rcssyn.o: rcsbase.h
rcsutil.o: rcsutil.c
rcsutil.o: rcsbase.h
rlog.o: rlog.c
rlog.o: time.h
rlog.o: rcsbase.h
snoop.o: snoop.c
snoop.o: rcsbase.h
