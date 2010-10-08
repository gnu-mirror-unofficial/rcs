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

check_PROGRAMS = xorlf

# This helps certain RCS programs find their peers.
# Note that it should NOT end in $(PATH_SEPARATOR).
PATHPREFIX = $(abs_top_builddir)/src

TESTS_ENVIRONMENT = PATH="$(PATHPREFIX)$(PATH_SEPARATOR)$$PATH" sh
TESTS = \
 t150 \
 t300 \
 t310 \
 t311 \
 t420 \
 t500 \
 t501 \
 t502 \
 t700 \
 t999

EXTRA_DIST = $(TESTS) common fake

installcheck-local:
	$(MAKE) check PATHPREFIX='$(DESTDIR)$(bindir)'

clean-local:
	rm -rf *.d

describe:
	@for f in $(TESTS) ; do			\
	  sed 's/^...//;s/ ---/:/;q' $$f ;	\
	  if [ x"$(VERBOSE)" = x1 ] ; then	\
	    echo ;				\
	    sed -e '/^##$$/,/^##$$/!d'		\
	        -e 's/^.//'			\
	        -e 's/^#$$//'			\
	        -e '/./,/^$$/!d'		\
	        -e 's/^./    &/'		\
	        $$f ;				\
	  fi ;					\
	done

# Makefile.am ends here
