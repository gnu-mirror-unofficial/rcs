/* b-kwxout.h --- keyword expansion on output

   Copyright (C) 2010 Thien-Thi Nguyen

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

/* The expansion context (parameter block for expansion funcs, basically).  */
struct expctx
{
  FILE *to, *rewr;
  struct fro *from;
  struct delta const *delta;
  const bool delimstuffed, dolog;

  /* Some space to (temporarily) hold key/value/line fragments
     (for kwxout-internal use; not set by callers).  */
  struct divvy *lparts;
};

/* Idioms.  Note that .delta is hardcoded ‘delta’.  */

#define EXPCTX(TO,REWR,FROM,DELIMSTUFFED,DOLOG)         \
    {                                                   \
      .to = TO,                                         \
      .rewr = REWR,                                     \
      .from = FROM,                                     \
      .delta = delta,                                   \
      .delimstuffed = DELIMSTUFFED,                     \
      .dolog = DOLOG                                    \
    }

#define EXPCTX_1OUT(TO,FROM,DELIMSTUFFED,DOLOG) \
  EXPCTX (TO, NULL, FROM, DELIMSTUFFED, DOLOG)

extern int expandline (struct expctx *ctx);

/* b-kwxout.h ends here */
