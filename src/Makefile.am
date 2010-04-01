# Make RCS.

# Copyright (C) 2010 Thien-Thi Nguyen
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

AM_CPPFLAGS = -I$(top_builddir)/lib

noinst_LIBRARIES = libparts.a
libparts_a_SOURCES = \
  b-complain.h b-kwxout.h base.h gnu-h-v.h maketime.h partime.h \
  b-anchor.c b-complain.c b-kwxout.c \
  bother.c gnu-h-v.c \
  maketime.c merger.c partime.c rcsedit.c rcsfcmp.c rcsfnms.c \
  rcsgen.c rcskeep.c rcslex.c rcsmap.c rcsrev.c rcssyn.c \
  rcstime.c rcsutil.c

bin_PROGRAMS = ci co ident merge rcs rcsclean rcsdiff rcsmerge rlog

BUILT_SOURCES = bother.c $(bin_PROGRAMS:%=%.help)

LDADD = libparts.a

CLEANFILES = $(BUILT_SOURCES)

# This is for the benefit of "make installcheck".
# Note that it should end in ":".
PATHPREFIX =

TESTS_ENVIRONMENT = PATH="$(PATHPREFIX)$$(pwd)/0pre/bin:$$PATH" sh
TESTS = alive.test

EXTRA_DIST = TAGS rcsfreeze.sh $(TESTS)

dist-hook:
	rm -f $(distdir)/bother.c

.c.help:
	$(top_srcdir)/build-aux/extract-help -o $@ $<

check-TESTS: test-prep
	@overall=true ;				\
	ls='$(TESTS)' ; if [ "$$ls" ] ; then	\
	  if [ '.' = '$(srcdir)' ] ; then d= ;	\
	    else d='$(srcdir)/' ; fi ;		\
	  for t in "$$ls" ; do			\
	    if $(TESTS_ENVIRONMENT) "$$d$$t" ;	\
	    then echo "PASS: $$t" ;		\
	    else echo "FAIL: $$t" ;		\
	         overall=false ;		\
	    fi ;				\
	  done ;				\
	fi ; $$overall

# Install RCS and (if applicable) GNU diff before running these tests.
installcheck: test-prep
	$(MAKE) check PATHPREFIX='$(DESTDIR)$(bindir):'

clean-local:
	rm -rf 0pre RCS
	rm -f a.* xorlf*

bother.c: FORCE
	@( echo '/* brooked other (shared mother) */' ; \
	   echo 'const char const prog_co[] = "$(bindir)/co";' ; \
	   echo 'const char const prog_merge[] = "$(bindir)/merge";' ; \
	   ) > $@T
	@if [ -f $@ ] && cmp -s $@T $@ ; \
	  then rm -f $@T ; \
	  else mv $@T $@ ; echo Created $@ ; \
	fi

test-prep:
	@if [ -d 0pre ] &&						\
	    [ 0pre = $$(ls -1td 0pre $(bin_PROGRAMS) | sed 1q) ] ;	\
        then : ; else							\
	  rm -rf 0pre ; mkdir 0pre 0pre/bin 0pre/stash ;		\
	  cp -p $(bin_PROGRAMS) *.o *.a bother.c 0pre/stash ;		\
	  rm -f $(bin_PROGRAMS) ;					\
	  $(MAKE) all bindir=$$(pwd)/0pre/bin >/dev/null ;		\
	  $(MAKE) xorlf >/dev/null ;					\
	  mv -v $(bin_PROGRAMS) xorlf 0pre/bin ;			\
	  cp -pf 0pre/stash/* . ;					\
	fi

xorlf:
	@{ echo '#include <stdio.h>'               ;	\
	   echo 'int main ()'                      ;	\
	   echo '{'                                ;	\
	   echo '  int c;'                         ;	\
	   echo '  while (EOF != (c = getchar()))' ;	\
	   echo '    putchar (c ^ 012);'           ;	\
	   echo '  return 0;'                      ;	\
	   echo '}'                                ;	\
	} > $@.c
	$(CC) -o $@ $@.c
	rm -f $@.c

FORCE:;

# Makefile.am ends here
