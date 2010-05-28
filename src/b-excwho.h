/* b-excwho.c --- exclusivity / identity

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

extern bool currently_setuid_p (void);
extern char const *getusername (bool suspicious);
extern char const *getcaller (void);
extern bool caller_login_p (char const *login);
extern struct link *lock_memq (struct link *ls, bool loginp, void const *x);
extern void lock_drop (struct link *box, struct link *tp);

/* Idioms.  */

#define lock_login_memq(ls,login)  lock_memq (ls,  true, login)
#define lock_delta_memq(ls,delta)  lock_memq (ls, false, delta)

/* b-excwho.c ends here */
