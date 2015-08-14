/* Source: http://git.postgresql.org/gitweb/?p=postgresql.git;a=blob;f=src/port/gettimeofday.c;h=75a91993b74414c0a1c13a2a09ce739cb8aa8a08;hb=HEAD
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

#include "windows_helper.h"

#ifdef _MSC_VER

#include <stdlib.h>
#include <stdio.h>

/* FILETIME of Jan 1 1970 00:00:00. */
static const unsigned __int64 epoch = ((unsigned __int64)116444736000000000ULL);

/*
* timezone information is stored outside the kernel so tzp isn't used anymore.
*
* Note: this function is not for Win32 high precision timing purpose. See
* elapsed_time().
*/
int gettimeofday(struct timeval * tp, struct timezone * tzp) {
	FILETIME    file_time;
	SYSTEMTIME  system_time;
	ULARGE_INTEGER ularge;

	GetSystemTime(&system_time);
	SystemTimeToFileTime(&system_time, &file_time);
	ularge.LowPart = file_time.dwLowDateTime;
	ularge.HighPart = file_time.dwHighDateTime;

	tp->tv_sec = (long)((ularge.QuadPart - epoch) / 10000000L);
	tp->tv_usec = (long)(system_time.wMilliseconds * 1000);

	return 0;
}

int rand_r(unsigned int *seedp) {
	unsigned int randVal = 0;
	errno_t result = rand_s(&randVal);
	if (result != 0) {
		fprintf(stderr, "Error: rand_r failed to generate a random number!\n");
		return rand();
	}

	return (int)(randVal % RAND_MAX);
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
	DWORD time;
	time = (DWORD)((req->tv_sec * 1000) + (req->tv_nsec / 1000000));
	Sleep(time);

	if (rem != NULL) {
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}

	return 0;
}

#endif