/* Convert between RCS time format and POSIX and/or C formats.

   Copyright (C) 2010 Thien-Thi Nguyen
   Copyright (C) 1992, 1993, 1994, 1995 Paul Eggert

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

#include "base.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "b-complain.h"
#include "partime.h"
#include "maketime.h"

#define proper_dot_2(a,b)  (PRINTF_DOT2_OK ? (a) : (b))

void
time2date (time_t unixtime, char date[datesize])
/* Convert Unix time to RCS format.  For compatibility with older versions of
   RCS, dates from 1900 through 1999 are stored without the leading "19".  */
{
  register struct tm const *tm = time2tm (unixtime, BE (version) < VERSION (5));
  sprintf (date, proper_dot_2 ("%.2d.%.2d.%.2d.%.2d.%.2d.%.2d",
                               "%02d.%02d.%02d.%02d.%02d.%02d"),
           tm->tm_year + ((unsigned) tm->tm_year < 100 ? 0 : 1900),
           tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static time_t
str2time_checked (char const *source, time_t default_time, long default_zone)
/* Like ‘str2time’, except die if an error was found.  */
{
  time_t t = str2time (source, default_time, default_zone);

  if (t == -1)
    PFATAL ("unknown date/time: %s", source);
  return t;
}

void
str2date (char const *source, char target[datesize])
/* Parse a free-format date in ‘source’, convert it into
   RCS internal format, and store the result into ‘target’.  */
{
  time2date (str2time_checked (source, BE (now),
                               BE (zone_offset.valid)
                               ? BE (zone_offset.seconds)
                               : (BE (version) < VERSION (5)
                                  ? TM_LOCAL_ZONE
                                  : 0)),
             target);
}

time_t
date2time (char const source[datesize])
/* Convert an RCS internal format date to ‘time_t’.  */
{
  char s[datesize + zonelenmax];

  return str2time_checked (date2str (source, s), (time_t) 0, 0);
}

void
zone_set (char const *s)
/* Set the time zone for ‘date2str’ output.  */
{
  if ((BE (zone_offset.valid) = !!(*s)))
    {
      long zone;
      char const *zonetail = parzone (s, &zone);

      if (!zonetail || *zonetail)
        PERR ("%s: not a known time zone", s);
      else
        BE (zone_offset.seconds) = zone;
    }
}

char const *
date2str (char const date[datesize], char datebuf[datesize + zonelenmax])
/* Format a user-readable form of the RCS format ‘date’
   into the buffer ‘datebuf’.  Return ‘datebuf’.  */
{
  register char const *p = date;

  while (*p++ != '.')
    continue;
  if (!BE (zone_offset.valid))
    sprintf (datebuf,
             ("19%.*s/%.2s/%.2s %.2s:%.2s:%s"
              + (date[2] == '.' && VERSION (5) <= BE (version) ? 0 : 2)),
             (int) (p - date - 1), date, p, p + 3, p + 6, p + 9, p + 12);
  else
    {
      struct tm t;
      struct tm const *z;
      int non_hour, w;
      long zone;
      char c;

      t.tm_year = atoi (date) - (date[2] == '.' ? 0 : 1900);
      t.tm_mon = atoi (p) - 1;
      t.tm_mday = atoi (p + 3);
      t.tm_hour = atoi (p + 6);
      t.tm_min = atoi (p + 9);
      t.tm_sec = atoi (p + 12);
      t.tm_wday = -1;
      zone = BE (zone_offset.seconds);
      if (zone == TM_LOCAL_ZONE)
        {
          time_t u = tm2time (&t, false), d;

          z = localtime (&u);
          d = difftm (z, &t);
          zone = (time_t) - 1 < 0 || d < -d ? d : -(long) -d;
        }
      else
        {
          adjzone (&t, zone);
          z = &t;
        }
      c = '+';
      if (zone < 0)
        {
          zone = -zone;
          c = '-';
        }
      w = sprintf (datebuf, proper_dot_2 ("%.2d-%.2d-%.2d %.2d:%.2d:%.2d%c%.2d",
                                          "%02d-%02d-%02d %02d:%02d:%02d%c%02d"),
                   z->tm_year + 1900,
                   z->tm_mon + 1, z->tm_mday, z->tm_hour, z->tm_min, z->tm_sec,
                   c, (int) (zone / (60 * 60)));
      if ((non_hour = zone % (60 * 60)))
        {
          const char *fmt = proper_dot_2 (":%.2d",
                                          ":%02d");

          w += sprintf (datebuf + w, fmt, non_hour / 60);
          if ((non_hour %= 60))
            w += sprintf (datebuf + w, fmt, non_hour);
        }
    }
  return datebuf;
}

/* rcstime.c ends here */
