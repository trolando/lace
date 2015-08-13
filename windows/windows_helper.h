/* License for int gettimeofday(struct timeval * tp, struct timezone * tzp):
 * Source: http://git.postgresql.org/gitweb/?p=postgresql.git;a=blob;f=src/port/gettimeofday.c;h=75a91993b74414c0a1c13a2a09ce739cb8aa8a08;hb=HEAD
 * gettimeofday.c
 *    Win32 gettimeofday() replacement
 *
 * src/port/gettimeofday.c
 *
 * Copyright (c) 2003 SRA, Inc.
 * Copyright (c) 2003 SKC, Inc.
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose, without fee, and without a
 * written agreement is hereby granted, provided that the above
 * copyright notice and this paragraph and the following two
 * paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE TO ANY PARTY FOR DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE AUTHOR SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS
 * IS" BASIS, AND THE AUTHOR HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE,
 * SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#if defined(_MSC_VER) && !defined(LACE_WINDOWS_HELPER_H_)
#define LACE_WINDOWS_HELPER_H_

#include <time.h>
#include <Winsock2.h> // For timeval

#include "lace_config.h"
#ifndef LACE_CONFIG_HAVE_TIMESPEC_STRUCT
struct timespec {
	long long tv_sec;  // Seconds - >= 0
	long long tv_nsec; // Nanoseconds - [0, 999999999]
};
#endif

/*
 * timezone information is stored outside the kernel so tzp isn't used anymore.
 *
 * Note: this function is not for Win32 high precision timing purpose. See
 * elapsed_time().
 */
int gettimeofday(struct timeval * tp, struct timezone * tzp);

/*
 * Emulates rand_r behaviour
 * Ignores the seed parameter, uses the operating system to generate cryptographically secure random numbers.
 */
int rand_r(unsigned int *seedp);

/*
 * Emulates nanosleep behaviour
 * Uses the sleep function, will not give nanosecond resolution (but instead milliseconds).
 * Warning: This function does _not_ cope with being interrupted. It will return all-zero
 * in the *rem struct, if rem != NULL, always, every time, even if no sleeping was performed. 
 */
int nanosleep(const struct timespec *req, struct timespec *rem);

#endif