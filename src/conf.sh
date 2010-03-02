#! /bin/sh
# Output RCS compile-time configuration.

# Copyright (C) 2010 Thien-Thi Nguyen
# Copyright (C) 1990, 1991, 1992, 1993, 1994, 1995 Paul Eggert
#
# This file is part of GNU RCS.
#
# GNU RCS is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# GNU RCS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


# Standard output should already be directed to "a.h";
# later parts of this procedure need it.
# Standard error can be ignored if a.h is OK,
# and can be inspected for clues otherwise.

# The Makefile overrides the following defaults.
: ${RCSPREFIX=/usr/local/bin/}
: ${ALL_CFLAGS=-Dhas_conf_h}
: ${CC=cc}
: ${COMPAT2=0}
: ${DIFF=${RCSPREFIX}diff}
: ${DIFF3=${DIFF}3}
: ${DIFF3_BIN=1}
: ${DIFFFLAGS=-an}
: ${DIFF_L=1}
: ${DIFF_SUCCESS=0} ${DIFF_FAILURE=1} ${DIFF_TROUBLE=2}
: ${ED=/bin/ed}
: ${SENDMAIL='"/usr/lib/sendmail"'}
# : ${LDFLAGS=} ${LIBS=} tickles old shell bug

C="$CC $ALL_CFLAGS"
CL="$CC $ALL_CFLAGS $LDFLAGS -o a.out"
L=$LIBS

cat <<EOF
/* RCS compile-time configuration */

/*
 * This file is generated automatically.
 * If you edit it by hand your changes may be lost.
 * Instead, please try to fix conf.sh,
 * and send your fixes to rcs-bugs@cs.purdue.edu.
 */

EOF

n='
'
case `echo -n` in
-n)
	ech='echo' dots='... \c';;
*)
	ech='echo -n' dots='... '
esac

$ech >&3 "$0: testing permissions $dots"
rm -f a.d &&
date >a.d &&
chmod 0 a.d &&
{ test -w a.d || cp /dev/null a.d 2>/dev/null; } && {
	echo >&3 "$n$0: This command should not be run with superuser permissions."
	exit 1
}
echo >&3 OK
rm -f a.d || exit

$ech >&3 "$0: testing compiler for plausibility $dots"
echo 'main() { return 0; }' >a.c
rm -f a.exe a.out || exit
$CL a.c $L >&2 || {
	echo >&3 "$n$0: The command '$CL a.c $L' failed on a trivial program."
	exit 1
}
echo 'this is not a C source file' >a.c
rm -f a.exe a.out || exit
$CL a.c $L >&2 && {
	echo >&3 "$n$0: The command '$CL a.c $L' succeeds when it should fail."
	exit 1
}
echo >&3 OK

$ech >&3 "$0: determining default compiler output file name $dots"
cat >a.c <<EOF
#include "a.h"
int main(argc,argv) int argc; char **argv; { return argc-1; }
EOF
rm -f a.exe a.out || exit
if $CL a.c $L >&2
then A_H=a.h
else
	echo >&3 failed
	$ech >&3 "$0: attempting to work around Domain/OS brain damage $dots"
	cat >a.c <<EOF
#include "a.hap"
int main(argc,argv) int argc; char **argv; { return argc-1; }
EOF
	cat <a.h >a.hap &&
	$CL a.c $L >&2 || exit 1
	# The Domain/OS C compiler refuses to read a.h because the file
	# is currently open for writing.  Work around this brain damage by
	# copying it to a.hap before each compilation; include a.hap instead.
	A_H=a.hap
fi
if test -f a.out
then aout=./a.out
elif test -f a.exe
then aout=./a.exe
else
	echo >&3 "$n$0: C compiler creates neither a.out nor a.exe."
	exit 1
fi
echo >&3 $aout

: PREPARE_CC
case $A_H in
a.h)
	PREPARE_CC="rm -f $aout";;
*)
	echo "rm -f $aout \$1 && cat <a.h >$A_H" >a.pre
	PREPARE_CC="sh a.pre"
esac

for id in _POSIX_C_SOURCE _POSIX_SOURCE
do
	$ech >&3 "$0: configuring $id $dots"
	cat >a.c <<EOF
#include "$A_H"
#include <stdio.h>
int
main() {
#	ifdef fileno
#		define f(x) fileno(x)
#	else
		/* Force a compile-time error if fileno isn't declared.  */
		int (*p)() = fileno;
#		define f(x) (*p)(x)
#	endif
	/* Some buggy linkers seem to need the getchar.  */
	return (getchar() != '#' || fileno(stdout) != 1);
}
#if syntax_error
syntax error
#endif
EOF
	a='/* ' z='*/ '
	case $id in
	_POSIX_SOURCE)
		version=1003.1-1990
		value=;;
	_POSIX_C_SOURCE)
		version='1003.1b-1993 or later'
		value='2147483647L ';;
	esac
	$PREPARE_CC || exit
	if ($CL a.c $L && $aout <a.c) >&2
	then :
	elif $PREPARE_CC || exit; ($CL -D$id=$value a.c $L && $aout <a.c) >&2
	then a= z=
	fi
	case $a in
	?*) echo >&3 OK;;
	'') echo >&3 "must define it, unfortunately"
	esac
	echo "$a#define $id $value$z/* if strict C + Posix $version */"
done

cat <<'EOF'

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* Comment out #include lines below that do not work.  */
EOF

$ech >&3 "$0: configuring how to check for syntax errors $dots"
# Run `$CS a.c $LS' instead of `$CL a.c $L' for compile-time checking only.
# This speeds up the configuration process.
if
	rm -f a.s && $C -S a.c >&2 && test -s a.s && rm -f a.s &&
	if $C -S -Dsyntax_error=1 a.c >&2 && test -s a.s
	then false
	else :
	fi
then
	# Generate assembly language output.
	CS="$C -S" LS= o=a.s PREPARE_CC="$PREPARE_CC $o"
elif
	rm -f a.o a.obj && $C -c a.c >&2 &&
	if test -s a.o
	then o=a.o
	elif test -s a.obj
	then o=a.obj
	else false
	fi &&
	if $C -c -Dsyntax_error=1 a.c >&2 && test -s $o
	then false
	else :
	fi
then
	# Generate object code.
	CS="$C -c" LS= PREPARE_CC="$PREPARE_CC $o"
else
	# Generate an executable.
	CS=$CL LS=$L o=$aout
fi
CS_OK="test -s $o"
echo >&3 $CS

# standard include files
# This list must be synced with ../configure.in, q.v.
# (That is, the one in ../configure.in should be a superset of this one.)
# (The unique exception is vfork, which is checked as part of AC_FUNC_FORK.)
for h in \
	fcntl limits mach/mach net/errno \
	pwd siginfo signal \
	sys/mman sys/wait ucontext unistd utime vfork
do
	i="#include <$h.h>"
	$ech >&3 "$0: configuring $i $dots"
	ok=OK
	guard=$(echo HAVE_${h}_H | sed y,/abcdefghijklmnopqrstuvwxyz,_ABCDEFGHIJKLMNOPQRSTUVWXYZ,)
	grep "#define $guard 1" auto-sussed.h >/dev/null || {
		i="/* $i */"
		ok="commenting it out"
	}
	echo >&3 "$ok"
	echo "$i"
done

# *_t
all_types='mode_t off_t pid_t sig_atomic_t size_t ssize_t time_t uid_t'
$ech >&3 "$0: checking types: $all_types $dots"
for t in $all_types
do
        grep "#define $t " auto-sussed.h
done
echo >&3 OK

cat - <<EOF

#if O_BINARY
	/* Text and binary i/o behave differently.  */
	/* This is incompatible with Posix and Unix.  */
#	define FOPEN_RB "rb"
#	define FOPEN_R_WORK (Expand==BINARY_EXPAND ? "r" : "rb")
#	define FOPEN_WB "wb"
#	define FOPEN_W_WORK (Expand==BINARY_EXPAND ? "w" : "wb")
#	define FOPEN_WPLUS_WORK (Expand==BINARY_EXPAND ? "w+" : "w+b")
#	define OPEN_O_BINARY O_BINARY
#else
	/*
	* Text and binary i/o behave the same.
	* Omit "b", since some nonstandard hosts reject it.
	*/
#	define FOPEN_RB "r"
#	define FOPEN_R_WORK "r"
#	define FOPEN_WB "w"
#	define FOPEN_W_WORK "w"
#	define FOPEN_WPLUS_WORK "w+"
#	define OPEN_O_BINARY 0
#endif

/* This may need changing on non-Unix systems (notably DOS).  */
#define OPEN_CREAT_READONLY (S_IRUSR|S_IRGRP|S_IROTH) /* lock file mode */
#define OPEN_O_LOCK 0 /* extra open flags for creating lock file */
#define OPEN_O_WRONLY O_WRONLY /* main open flag for creating a lock file */

/* Define or comment out the following symbols as needed.  */
EOF

echo "#define getlogin_is_secure 0 /* Is getlogin() secure?  Usually it's not.  */"

grep '#define GCC_HAS_ATTRIBUTE_NORETURN 1' auto-sussed.h
cat <<EOF
#ifdef GCC_HAS_ATTRIBUTE_NORETURN
#	define exiting __attribute__((noreturn))
#else
#	define exiting
#endif
EOF
rm -f a.c || exit

echo "#define bad_NFS_rename 0 /* Can rename(A,B) falsely report success?  */"

echo "#define has_setreuid 0 /* Does setreuid() work?  See ../INSTALL.RCS.  */"

echo "#define needs_getabsname 0 /* Must we define getabsname?  */"

: configuring has_NFS
echo "#define has_NFS 1 /* Might NFS be used?  */"

grep '#define HAVE_WORKING_V*FORK 1' auto-sussed.h

$ech >&3 "$0: configuring bad_wait_if_SIGCHLD_ignored $dots"
cat >a.c <<EOF
#include "$A_H"
#ifndef SIGCHLD
#define SIGCHLD SIGCLD
#endif
int main() {
	signal(SIGCHLD, SIG_IGN);
	{
#	if defined HAVE_WORKING_FORK
		int status;
		pid_t p = fork();
		if (p < 0) {
			perror("fork");
			return (2);
		}
		if (p == 0)
			_exit(0);
		while (wait(&status) != p) {
			if (errno == ECHILD)
				return (1);
			if (errno != EINTR) {
				perror("wait");
				return (2);
			}
		}
#	else
#		if has_system
			if (system("true") != 0)
				return (1);
#		endif
#	endif
	}
	return (0);
}
EOF
$PREPARE_CC || exit
b=0 ok=OK
if $CL a.c $L >&2
then
	$aout >&2
	case $? in
	0) ;;
	1) b=1 ok='will work around bug';;
	*) exit
	esac
fi
rm -f a.c || exit
echo >&3 $ok
echo "#define bad_wait_if_SIGCHLD_ignored $b /* Does ignoring SIGCHLD break wait()?  */"


echo '#define RCS_SHELL "/bin/sh" /* shell to run RCS subprograms */'

grep '#define GCC_HAS_ATTRIBUTE_FORMAT 1' auto-sussed.h
cat <<EOF
#ifdef GCC_HAS_ATTRIBUTE_FORMAT
#	define printf_string(m, n) __attribute__((format(printf, m, n)))
#else
#	define printf_string(m, n)
#endif
#if defined GCC_HAS_ATTRIBUTE_FORMAT && defined GCC_HAS_ATTRIBUTE_NORETURN
	/* Work around a bug in GCC 2.5.x.  */
#	define printf_string_exiting(m, n) __attribute__((format(printf, m, n), noreturn))
#else
#	define printf_string_exiting(m, n) printf_string(m, n) exiting
#endif
EOF

: configuring large_memory
if grep '#define MMAP_SIGNAL 0' auto-sussed.h >/dev/null
then l=0
else l=1
fi
echo "#define large_memory $l /* Can main memory hold entire RCS files?  */"

: configuring same_file
echo "/* Do struct stat s and t describe the same file?  Answer d if unknown.  */"
echo "#define same_file(s,t,d) ((s).st_ino==(t).st_ino && (s).st_dev==(t).st_dev)"

: configuring CO
echo "#define CO \"${RCSPREFIX}co\" /* name of 'co' program */"

: configuring COMPAT2
echo "#define COMPAT2 $COMPAT2 /* Are version 2 files supported?  */"

: configuring DIFF
echo "#define DIFF \"${DIFF}\" /* name of 'diff' program */"

: configuring DIFF3
echo "#define DIFF3 \"${DIFF3}\" /* name of 'diff3' program */"

: configuring DIFF3_BIN
echo "#define DIFF3_BIN $DIFF3_BIN /* Is diff3 user-visible (not the /usr/lib auxiliary)?  */"

: configuring DIFFFLAGS
echo "#define DIFFFLAGS \"$DIFFFLAGS\" /* Make diff output suitable for RCS.  */"

: configuring DIFF_L
echo "#define DIFF_L $DIFF_L /* Does diff -L work?  */"

: configuring DIFF_SUCCESS, DIFF_FAILURE, DIFF_TROUBLE
cat <<EOF
#define DIFF_SUCCESS $DIFF_SUCCESS /* DIFF status if no differences are found */
#define DIFF_FAILURE $DIFF_FAILURE /* DIFF status if differences are found */
#define DIFF_TROUBLE $DIFF_TROUBLE /* DIFF status if trouble */
EOF

: configuring ED
echo "#define ED \"${ED}\" /* name of 'ed' program (used only if !DIFF3_BIN) */"

: configuring MERGE
echo "#define MERGE \"${RCSPREFIX}merge\" /* name of 'merge' program */"

: configuring '*SLASH*', ROOTPATH, TMPDIR, X_DEFAULT
case ${PWD-`pwd`} in
/*) # Posix
	SLASH=/
	qSLASH="'/'"
	SLASHes=$qSLASH
	isSLASH='#define isSLASH(c) ((c) == SLASH)'
	ROOTPATH='isSLASH((p)[0])'
	X_DEFAULT=",v$SLASH";;
?:[/\\\\]*) # MS-DOS # \\\\ instead of \\ doesn't hurt, and avoids common bugs
	SLASH='\'
	qSLASH="'\\\\'"
	SLASHes="$qSLASH: case '/': case ':'"
	isSLASH='int isSLASH (int);'
	ROOTPATH="(isSLASH((p)[0]) || (p)[0] && (p)[1]==':')"
	X_DEFAULT="$SLASH,v";;
*)
	echo >&3 $0: cannot deduce SLASH
	exit 1
esac
cat <<EOF
#define TMPDIR "${SLASH}tmp" /* default directory for temporary files */
#define SLASH $qSLASH /* principal filename separator */
#define SLASHes $SLASHes /* \`case SLASHes:' labels all filename separators */
$isSLASH /* Is arg a filename separator?  */
#define ROOTPATH(p) $ROOTPATH /* Is p an absolute pathname?  */
#define X_DEFAULT "$X_DEFAULT" /* default value for -x option */
EOF

$ech >&3 "$0: configuring SLASHSLASH_is_SLASH $dots"
cat >a.c <<EOF
#include "$A_H"
static struct stat s, ss;
static char f[3];
int
main() {
	f[0] = SLASH; if (stat(f, &s ) != 0) return (1);
	f[1] = SLASH; if (stat(f, &ss) != 0) return (1);
	return (!same_file(s, ss, 0));
}
EOF
$PREPARE_CC || exit
if ($CL a.c $L && $aout) >&2
then eq=1 ok=OK
else eq=0 ok=no
fi
echo >&3 $ok
echo "#define SLASHSLASH_is_SLASH $eq /* Are // and / the same directory?  */"

$ech >&3 "$0: configuring ALL_ABSOLUTE, DIFF_ABSOLUTE $dots"
cat >a.c <<EOF
#include "$A_H"
#ifndef isSLASH
static int
isSLASH(c) int c; {
	switch (c) { case SLASHes: return 1; } return 0;
}
#endif
int
main(argc, argv) int argc; char **argv; {
	return (1<argc && !ROOTPATH(argv[1]));
}
EOF
$PREPARE_CC && ($CL a.c $L && $aout) >&2 || exit
a=1
for i in "$DIFF" "$DIFF3" "$ED" "$RCSPREFIX" "$SENDMAIL"
do
	case $i in
	\"*\") i=`expr "$i" : '"\(.*\)"'`
	esac
	case $i in
	?*) $aout "$i" || { a=0; break; }
	esac
done
echo "#define ALL_ABSOLUTE $a /* Do all subprograms satisfy ROOTPATH?  */"
if $aout "$DIFF"
then a=1
else a=0
fi
echo "#define DIFF_ABSOLUTE $a /* Is ROOTPATH(DIFF) true?  */"
echo >&3 OK

: configuring SENDMAIL
case $SENDMAIL in
'') a='/* ' z='*/ ';;
*) a= z=
esac
echo "$a#define SENDMAIL $SENDMAIL $z/* how to send mail */"

: configuring TZ_must_be_set
echo "#define TZ_must_be_set 0 /* Must TZ be set for gmtime() to work?  */"
