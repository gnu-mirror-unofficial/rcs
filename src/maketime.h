/* Return `time_t' from `struct partime' returned by partime.

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

struct tm *time2tm (time_t, int);
time_t difftm (struct tm const *, struct tm const *);
time_t str2time (char const *, time_t, long);
time_t tm2time (struct tm *, int);
void adjzone (struct tm *, long);

/* maketime.h ends here */
