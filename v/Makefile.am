# Validate RCS behavior.

# Copyright (C) 2010 Thien-Thi Nguyen
#
# This file is part of GNU RCS.
#
# GNU RCS is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# GNU RCS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# This is for the benefit of "make installcheck".
# Note that it should end in ":".
PATHPREFIX =

TESTS_ENVIRONMENT = PATH="$(PATHPREFIX)$$(pwd)/../src/0pre/bin:$$PATH" sh
TESTS = alive.test \
  t000.test \
  t001.test

EXTRA_DIST = $(TESTS)

test-prep:
	@cd ../src ; $(MAKE) test-prep

check-TESTS: test-prep
	@overall=true ;							\
	ls='$(TESTS)' ; if [ "$$ls" ] ; then				\
	  d='$(srcdir)/' ; test './' = $$d && d= ;			\
          zonk='rm -rf' ; test "$(KEEPD)" && zonk=':' ;			\
	  for t in $$ls ; do						\
            test -r "$$d$$t" || { echo "DNE: $$t" ; continue ; } ;	\
	    b=$$(basename $$t .test) ; rm -rf $$b.d ; mkdir $$b.d ;	\
	    $(TESTS_ENVIRONMENT) "$$d$$t" >$$b.d/out 2>$$b.d/err ;	\
	    res=$$? ;							\
	    if [ 0 = $$res ] ; then					\
	      echo "PASS: $$t" ; $$zonk $$b.d ;				\
	    elif [ 77 = $$res ] ; then					\
	      echo "SKIP: $$t" ; $$zonk $$b.d ;				\
	    else echo "FAIL: $$t" ; overall=false ;			\
	    fi ;							\
	  done ;							\
	fi ; $$overall

installcheck: test-prep
	$(MAKE) check PATHPREFIX='$(DESTDIR)$(bindir):'

clean-local:
	rm -rf *.d

# Makefile.am ends here
