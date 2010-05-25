# Make RCS man pages.

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

dist_man_MANS = ci.1 co.1 ident.1 merge.1 rcs.1 rcsclean.1 \
  rcsdiff.1 rcsfile.5 rcsintro.1 rcsmerge.1 rlog.1

BUILT_SOURCES = REL

bits = b-environment \
  b-identification \
  b-tichy-cite

EXTRA_DIST = $(dist_man_MANS:%=%in) $(BUILT_SOURCES) $(bits)

soelim = soelim -I -I'$(srcdir)'
nolf = sed '/^\.lf /d'

rcsfile.5: rcsfile.5in REL $(bits)
	$(soelim) $< | $(PIC) $(PICFLAGS) | $(nolf) > $@

.1in.1: REL $(bits)
	$(soelim) $< | $(nolf) > $@

REL: $(top_srcdir)/configure
	@{ echo '.ds Rv $(PACKAGE_VERSION)' ;		\
	   date -u +'.ds Dt %Y-%m-%d' -r $< ; } > $@t ;	\
	if [ -f $@ ] && cmp -s $@t $@ ;			\
	  then rm -f $@t ;				\
	  else mv $@t $@ ; echo Created $@ ;		\
	fi

MAINTAINERCLEANFILES = $(dist_man_MANS) REL

# Makefile.am ends here
