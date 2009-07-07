/*
 * time.c:  time/date utilities
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#include <string.h>
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_time.h>
#include <apr_strings.h>
#include "svn_io.h"
#include "svn_time.h"
#include "svn_utf.h"
#include "svn_error.h"
#include "svn_private_config.h"



/*** Code. ***/

/* Our timestamp strings look like this:
 *
 *    "2002-05-07Thh:mm:ss.uuuuuuZ"
 *
 * The format is conformant with ISO-8601 and the date format required
 * by RFC2518 for creationdate. It is a direct conversion between
 * apr_time_t and a string, so converting to string and back retains
 * the exact value.
 */
#define TIMESTAMP_FORMAT "%04d-%02d-%02dT%02d:%02d:%02d.%06dZ"

/* Our old timestamp strings looked like this:
 *
 *    "Tue 3 Oct 2000 HH:MM:SS.UUU (day 277, dst 1, gmt_off -18000)"
 *
 * The idea is that they are conventionally human-readable for the
 * first part, and then in parentheses comes everything else required
 * to completely fill in an apr_time_exp_t: tm_yday, tm_isdst,
 * and tm_gmtoff.
 *
 * This format is still recognized on input, for backward
 * compatibility, but no longer generated.
 */
#define OLD_TIMESTAMP_FORMAT \
        "%3s %d %3s %d %02d:%02d:%02d.%06d (day %03d, dst %d, gmt_off %06d)"

/* Our human representation of dates looks like this:
 *
 *    "2002-06-23 11:13:02 +0300 (Sun, 23 Jun 2002)"
 *
 * This format is used whenever time is shown to the user. It consists
 * of a machine parseable, almost ISO-8601, part in the beginning -
 * and a human explanatory part at the end. The machine parseable part
 * is generated strictly by APR and our code, with a apr_snprintf. The
 * human explanatory part is generated by apr_strftime, which means
 * that its generation can be affected by locale, it can fail and it
 * doesn't need to be constant in size. In other words, perfect to be
 * converted to a configuration option later on.
 */
/* Maximum length for the date string. */
#define SVN_TIME__MAX_LENGTH 80
/* Machine parseable part, generated by apr_snprintf. */
#define HUMAN_TIMESTAMP_FORMAT "%.4d-%.2d-%.2d %.2d:%.2d:%.2d %+.2d%.2d"
/* Human explanatory part, generated by apr_strftime as "Sat, 01 Jan 2000" */
#define human_timestamp_format_suffix _(" (%a, %d %b %Y)")

const char *
svn_time_to_cstring(apr_time_t when, apr_pool_t *pool)
{
  apr_time_exp_t exploded_time;

  /* We toss apr_status_t return value here -- for one thing, caller
     should pass in good information.  But also, where APR's own code
     calls these functions it tosses the return values, and
     furthermore their current implementations can only return success
     anyway. */

  /* We get the date in GMT now -- and expect the tm_gmtoff and
     tm_isdst to be not set. We also ignore the weekday and yearday,
     since those are not needed. */

  apr_time_exp_gmt(&exploded_time, when);

  /* It would be nice to use apr_strftime(), but APR doesn't give a
     way to convert back, so we wouldn't be able to share the format
     string between the writer and reader. */
  return apr_psprintf(pool,
                      TIMESTAMP_FORMAT,
                      exploded_time.tm_year + 1900,
                      exploded_time.tm_mon + 1,
                      exploded_time.tm_mday,
                      exploded_time.tm_hour,
                      exploded_time.tm_min,
                      exploded_time.tm_sec,
                      exploded_time.tm_usec);
}


static int
find_matching_string(char *str, apr_size_t size, const char strings[][4])
{
  apr_size_t i;

  for (i = 0; i < size; i++)
    if (strings[i] && (strcmp(str, strings[i]) == 0))
      return i;

  return -1;
}


svn_error_t *
svn_time_from_cstring(apr_time_t *when, const char *data, apr_pool_t *pool)
{
  apr_time_exp_t exploded_time;
  apr_status_t apr_err;
  char wday[4], month[4];
  char *c;

  /* Open-code parsing of the new timestamp format, as this
     is a hot path for reading the entries file.  This format looks
     like:  "2001-08-31T04:24:14.966996Z"  */
  exploded_time.tm_year = strtol(data, &c, 10);
  if (*c++ != '-') goto fail;
  exploded_time.tm_mon = strtol(c, &c, 10);
  if (*c++ != '-') goto fail;
  exploded_time.tm_mday = strtol(c, &c, 10);
  if (*c++ != 'T') goto fail;
  exploded_time.tm_hour = strtol(c, &c, 10);
  if (*c++ != ':') goto fail;
  exploded_time.tm_min = strtol(c, &c, 10);
  if (*c++ != ':') goto fail;
  exploded_time.tm_sec = strtol(c, &c, 10);
  if (*c++ != '.') goto fail;
  exploded_time.tm_usec = strtol(c, &c, 10);
  if (*c++ != 'Z') goto fail;

  exploded_time.tm_year  -= 1900;
  exploded_time.tm_mon   -= 1;
  exploded_time.tm_wday   = 0;
  exploded_time.tm_yday   = 0;
  exploded_time.tm_isdst  = 0;
  exploded_time.tm_gmtoff = 0;

  apr_err = apr_time_exp_gmt_get(when, &exploded_time);
  if (apr_err == APR_SUCCESS)
    return SVN_NO_ERROR;

  return svn_error_create(SVN_ERR_BAD_DATE, NULL, NULL);

 fail:
  /* Try the compatibility option.  This does not need to be fast,
     as this format is no longer generated and the client will convert
     an old-format entries file the first time it reads it.  */
  if (sscanf(data,
             OLD_TIMESTAMP_FORMAT,
             wday,
             &exploded_time.tm_mday,
             month,
             &exploded_time.tm_year,
             &exploded_time.tm_hour,
             &exploded_time.tm_min,
             &exploded_time.tm_sec,
             &exploded_time.tm_usec,
             &exploded_time.tm_yday,
             &exploded_time.tm_isdst,
             &exploded_time.tm_gmtoff) == 11)
    {
      exploded_time.tm_year -= 1900;
      exploded_time.tm_yday -= 1;
      /* Using hard coded limits for the arrays - they are going away
         soon in any case. */
      exploded_time.tm_wday = find_matching_string(wday, 7, apr_day_snames);
      exploded_time.tm_mon = find_matching_string(month, 12, apr_month_snames);

      apr_err = apr_time_exp_gmt_get(when, &exploded_time);
      if (apr_err != APR_SUCCESS)
        return svn_error_create(SVN_ERR_BAD_DATE, NULL, NULL);

      return SVN_NO_ERROR;
    }
  /* Timestamp is something we do not recognize. */
  else
    return svn_error_create(SVN_ERR_BAD_DATE, NULL, NULL);
}


const char *
svn_time_to_human_cstring(apr_time_t when, apr_pool_t *pool)
{
  apr_time_exp_t exploded_time;
  apr_size_t len, retlen;
  apr_status_t ret;
  char *datestr, *curptr, human_datestr[SVN_TIME__MAX_LENGTH];

  /* Get the time into parts */
  ret = apr_time_exp_lt(&exploded_time, when);
  if (ret)
    return NULL;

  /* Make room for datestring */
  datestr = apr_palloc(pool, SVN_TIME__MAX_LENGTH);

  /* Put in machine parseable part */
  len = apr_snprintf(datestr,
                     SVN_TIME__MAX_LENGTH,
                     HUMAN_TIMESTAMP_FORMAT,
                     exploded_time.tm_year + 1900,
                     exploded_time.tm_mon + 1,
                     exploded_time.tm_mday,
                     exploded_time.tm_hour,
                     exploded_time.tm_min,
                     exploded_time.tm_sec,
                     exploded_time.tm_gmtoff / (60 * 60),
                     (abs(exploded_time.tm_gmtoff) / 60) % 60);

  /* If we overfilled the buffer, just return what we got. */
  if (len >= SVN_TIME__MAX_LENGTH)
    return datestr;

  /* Calculate offset to the end of the machine parseable part. */
  curptr = datestr + len;

  /* Put in human explanatory part */
  ret = apr_strftime(human_datestr,
                     &retlen,
                     SVN_TIME__MAX_LENGTH - len,
                     human_timestamp_format_suffix,
                     &exploded_time);

  /* If there was an error, ensure that the string is zero-terminated. */
  if (ret || retlen == 0)
    *curptr = '\0';
  else
    {
      const char *utf8_string;
      svn_error_t *err;

      err = svn_utf_cstring_to_utf8(&utf8_string, human_datestr, pool);
      if (err)
        {
          *curptr = '\0';
          svn_error_clear(err);
        }
      else
        apr_cpystrn(curptr, utf8_string, SVN_TIME__MAX_LENGTH - len);
    }

  return datestr;
}


void
svn_sleep_for_timestamps(void)
{
  svn_io_sleep_for_timestamps(NULL, NULL);
}
