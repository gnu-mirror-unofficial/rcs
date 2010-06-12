/* Convert struct partime into time_t.

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
#include <time.h>
#include "partime.h"
#include "maketime.h"

#define MAKETIMESTUFF(x)  (BE (maketimestuff)-> x)

/* Make no assumptions about the ‘time_t’ epoch or the range of ‘time_t’ values.
   Avoid ‘mktime’ because it's not universal and because there's no easy,
   portable way for ‘mktime’ to return the inverse of ‘gmtime_r’.  */

#define TM_YEAR_ORIGIN 1900

static bool
isleap (int y)
{
  return (y & 3) == 0 && (y % 100 != 0 || y % 400 == 0);
}

static const int const month_yday[] = {
  /* Days in year before start of months 0-12.  */
  0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
};

static int
month_days (struct tm const *tm)
/* Return the number of days in ‘tm’ month.  */
{
  int m = tm->tm_mon;

  return month_yday[m + 1] - month_yday[m]
    + (m == 1 && isleap (tm->tm_year + TM_YEAR_ORIGIN));
}

struct tm *
time2tm (time_t unixtime, bool localzone)
/* Convert ‘unixtime’ to ‘struct tm’ form.
   Use ‘gmtime_r’ if available and if ‘!localzone’, ‘localtime_r’ otherwise.  */
{
  struct tm *tm;

#if TZ_must_be_set
  if (!MAKETIMESTUFF (TZ) && !(MAKETIMESTUFF (TZ) = getenv ("TZ")))
    PFATAL
      ("The TZ environment variable is not set; please set it to your timezone");
#endif
  if (localzone || !(tm = gmtime_r (&unixtime, &MAKETIMESTUFF (time2tm_stash))))
    tm = localtime_r (&unixtime, &MAKETIMESTUFF (time2tm_stash));
  return tm;
}

time_t
difftm (struct tm const *a, struct tm const *b)
/* Return ‘a - b’, measured in seconds.  */
{
  int ay = a->tm_year + (TM_YEAR_ORIGIN - 1);
  int by = b->tm_year + (TM_YEAR_ORIGIN - 1);
  int difference_in_day_of_year = a->tm_yday - b->tm_yday;
  int intervening_leap_days = (((ay >> 2) - (by >> 2))
                               - (ay / 100 - by / 100)
                               + ((ay / 100 >> 2) - (by / 100 >> 2)));
  time_t difference_in_years = ay - by;
  time_t difference_in_days = (difference_in_years * 365
                               + (intervening_leap_days +
                                  difference_in_day_of_year));

  return ((24 * difference_in_days + (a->tm_hour - b->tm_hour)) * 60 +
          (a->tm_min - b->tm_min)) * 60 + (a->tm_sec - b->tm_sec);
}

void
adjzone (register struct tm *t, long seconds)
/* Adjust time ‘t’ by adding ‘seconds’.  ‘seconds’ must be at most 24
   hours' worth.  In ‘t’, adjust only the ‘year’, ‘mon’, ‘mday’, ‘hour’,
   ‘min’ and ‘sec’ members; plus adjust ‘wday’ if it is defined.  */
{
  /* This code can be off by a second if ‘seconds’ is not a multiple of
     60, if ‘t’ is local time, and if a leap second happens during this
     minute.  But this bug has never occurred, and most likely will not
     ever occur.  Liberia, the last country for which ‘seconds’ % 60 was
     nonzero, switched to UTC in May 1972; the first leap second was in
     June 1972.  */
  int leap_second = t->tm_sec == 60;
  long sec = seconds + (t->tm_sec - leap_second);

  if (sec < 0)
    {
      if ((t->tm_min -= (59 - sec) / 60) < 0)
        {
          if ((t->tm_hour -= (59 - t->tm_min) / 60) < 0)
            {
              t->tm_hour += 24;
              if (TM_DEFINED (t->tm_wday) && --t->tm_wday < 0)
                t->tm_wday = 6;
              if (--t->tm_mday <= 0)
                {
                  if (--t->tm_mon < 0)
                    {
                      --t->tm_year;
                      t->tm_mon = 11;
                    }
                  t->tm_mday = month_days (t);
                }
            }
          t->tm_min += 24 * 60;
        }
      sec += 24L * 60 * 60;
    }
  else if (60 <= (t->tm_min += sec / 60))
    if (24 <= (t->tm_hour += t->tm_min / 60))
      {
        t->tm_hour -= 24;
        if (TM_DEFINED (t->tm_wday) && ++t->tm_wday == 7)
          t->tm_wday = 0;
        if (month_days (t) < ++t->tm_mday)
          {
            if (11 < ++t->tm_mon)
              {
                ++t->tm_year;
                t->tm_mon = 0;
              }
            t->tm_mday = 1;
          }
      }
  t->tm_min %= 60;
  t->tm_sec = (int) (sec % 60) + leap_second;
}

time_t
tm2time (struct tm *tm, bool localzone)
/* Convert ‘tm’ to ‘time_t’, using ‘localtime_r’ if ‘localzone’ and ‘gmtime_r’
   otherwise.  From ‘tm’, use only ‘year’, ‘mon’, ‘mday’, ‘hour’, ‘min’,
   and ‘sec’ members.  Ignore old members ‘tm_yday’ and ‘tm_wday’, but
   fill in their correct values.  Return -1 on failure (e.g. a member out
   of range).  POSIX 1003.1-1990 doesn't allow leap seconds, but some
   implementations have them anyway, so allow them if ‘localtime_r’/‘gmtime_r’
   does.  */
{
  time_t d, gt;
  struct tm const *gtm;
  /* The maximum number of iterations should be enough to handle any
     combinations of leap seconds, time zone rule changes, and solar time.
     4 is probably enough; we use a bigger number just to be safe.  */
  int remaining_tries = 8;

  /* Avoid subscript errors.  */
  if (12 <= (unsigned) tm->tm_mon)
    return -1;

  tm->tm_yday = month_yday[tm->tm_mon] + tm->tm_mday
    - (tm->tm_mon < 2 || !isleap (tm->tm_year + TM_YEAR_ORIGIN));

  /* Make a first guess.  */
  gt = MAKETIMESTUFF (t_cache)[localzone];
  gtm = gt ? &MAKETIMESTUFF (tm_cache)[localzone] : time2tm (gt, localzone);

  /* Repeatedly use the error from the guess to improve the guess.  */
  while ((d = difftm (tm, gtm)) != 0)
    {
      if (--remaining_tries == 0)
        return -1;
      gt += d;
      gtm = time2tm (gt, localzone);
    }
  MAKETIMESTUFF (t_cache)[localzone] = gt;
  MAKETIMESTUFF (tm_cache)[localzone] = *gtm;

  /* Check that the guess actually matches; overflow can cause ‘difftm’
     to return 0 even on differing times, or ‘tm’ may have members out of
     range (e.g. bad leap seconds).  */
  if ((tm->tm_year ^ gtm->tm_year)
      | (tm->tm_mon ^ gtm->tm_mon)
      | (tm->tm_mday ^ gtm->tm_mday)
      | (tm->tm_hour ^ gtm->tm_hour)
      | (tm->tm_min ^ gtm->tm_min) | (tm->tm_sec ^ gtm->tm_sec))
    return -1;

  tm->tm_wday = gtm->tm_wday;
  return gt;
}

static time_t
maketime (struct partime const *pt, time_t default_time)
/* Check ‘*pt’ and convert it to ‘time_t’.  If it is incompletely specified,
   use ‘default_time’ to fill it out.  Use ‘localtime_r’ if ‘pt->zone’ is the
   special value ‘TM_LOCAL_ZONE’.  Return -1 on failure.  ISO 8601 day-of-year
   and week numbers are not yet supported.  */
{
  bool localzone = pt->zone == TM_LOCAL_ZONE;
  int wday;
  struct tm tm;
  struct tm *tm0 = NULL;
  time_t r;

  tm0 = NULL;                           /* Keep gcc -Wall happy.  */

  tm = pt->tm;

  if (TM_DEFINED (pt->ymodulus) || !TM_DEFINED (tm.tm_year))
    {
      /* Get tm corresponding to current time.  */
      tm0 = time2tm (default_time, localzone);
      if (!localzone)
        adjzone (tm0, pt->zone);
    }

  if (TM_DEFINED (pt->ymodulus))
    tm.tm_year +=
      (tm0->tm_year + TM_YEAR_ORIGIN) / pt->ymodulus * pt->ymodulus;
  else if (!TM_DEFINED (tm.tm_year))
    {
      /* Set default year, month, day from current time.  */
      tm.tm_year = tm0->tm_year + TM_YEAR_ORIGIN;
      if (!TM_DEFINED (tm.tm_mon))
        {
          tm.tm_mon = tm0->tm_mon;
          if (!TM_DEFINED (tm.tm_mday))
            tm.tm_mday = tm0->tm_mday;
        }
    }

  /* Convert from ‘partime’ year (Gregorian) to POSIX year.  */
  tm.tm_year -= TM_YEAR_ORIGIN;

  /* Set remaining default fields to be their minimum values.  */
  if (!TM_DEFINED (tm.tm_mon))
    tm.tm_mon = 0;
  if (!TM_DEFINED (tm.tm_mday))
    tm.tm_mday = 1;
  if (!TM_DEFINED (tm.tm_hour))
    tm.tm_hour = 0;
  if (!TM_DEFINED (tm.tm_min))
    tm.tm_min = 0;
  if (!TM_DEFINED (tm.tm_sec))
    tm.tm_sec = 0;

  if (!localzone)
    adjzone (&tm, -pt->zone);
  wday = tm.tm_wday;

  /* Convert and fill in the rest of the ‘tm’.  */
  r = tm2time (&tm, localzone);

  /* Check weekday.  */
  if (r != -1 && TM_DEFINED (wday) && wday != tm.tm_wday)
    return -1;

  return r;
}

time_t
str2time (char const *source, time_t default_time, long default_zone)
/* Parse a free-format date in ‘source’, returning a Unix format time.  */
{
  struct partime pt;

  if (*partime (source, &pt))
    return -1;
  if (pt.zone == TM_UNDEFINED_ZONE)
    pt.zone = default_zone;
  return maketime (&pt, default_time);
}

/* maketime.c ends here */
