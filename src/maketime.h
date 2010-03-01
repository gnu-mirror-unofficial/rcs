/* Yield time_t from struct partime yielded by partime.

   Copyright (C) 2010 Thien-Thi Nguyen
   Copyright (C) 1993, 1994, 1995 Paul Eggert

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

#if defined(__STDC__) || has_prototypes
#	define __MAKETIME_P(x) x
#else
#	define __MAKETIME_P(x) ()
#endif

struct tm *time2tm __MAKETIME_P((time_t,int));
time_t difftm __MAKETIME_P((struct tm const *, struct tm const *));
time_t str2time __MAKETIME_P((char const *, time_t, long));
time_t tm2time __MAKETIME_P((struct tm *, int));
void adjzone __MAKETIME_P((struct tm *, long));
