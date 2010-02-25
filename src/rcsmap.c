/* RCS map of character types

   Copyright (C) 2010 Thien-Thi Nguyen
   Copyright (C) 1990, 1991, 1995 Paul Eggert
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

#include "rcsbase.h"

/* map of character types */
/* ISO 8859/1 (Latin-1) */
enum tokens const ctab[] = {
	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,
	SPACE,	SPACE,	NEWLN,	SPACE,	SPACE,	SPACE,	UNKN,	UNKN,
	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,
	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,
	SPACE,	IDCHAR,	IDCHAR,	IDCHAR,	DELIM,	IDCHAR,	IDCHAR,	IDCHAR,
	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	DELIM,	IDCHAR,	PERIOD,	IDCHAR,
	DIGIT,	DIGIT,	DIGIT,	DIGIT,	DIGIT,	DIGIT,	DIGIT,	DIGIT,
	DIGIT,	DIGIT,	COLON,	SEMI,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,
	SBEGIN,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,
	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,
	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,
	LETTER,	LETTER,	LETTER,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,
	IDCHAR,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,
	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,
	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,
	Letter,	Letter,	Letter,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	UNKN,
	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,
	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,
	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,
	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,	UNKN,
	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,
	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,
	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,
	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,	IDCHAR,
	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,
	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,
	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	IDCHAR,
	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	LETTER,	Letter,
	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,
	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,
	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	IDCHAR,
	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter,	Letter
};
