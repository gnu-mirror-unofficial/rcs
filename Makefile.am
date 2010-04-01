# Master makefile for RCS

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

SUBDIRS = lib doc man src v

EXTRA_DIST = build-aux/extract-help m4/gnulib-cache.m4

dist-hook:
	case $(PACKAGE_VERSION) in (*.*.*) \
	  cp -p $(srcdir)/HACKING $(distdir) ;; esac || true

ACLOCAL_AMFLAGS = -I m4

# Makefile.am ends here
