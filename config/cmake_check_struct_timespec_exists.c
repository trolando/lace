#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Test whether struct timespec is available.
 * Windows/MSVC 2015 knows about timespec, older versions do not.
 */

#ifdef _MSC_VER
#include <windows.h>
#endif

#include <time.h>
 
int main(int argc, char* argv[]) {
	struct timespec ts;
	ts.tv_sec = 12345;
	ts.tv_nsec = 67890;
	
	printf("Size of timespec is %u and the second value is %i.\n", (unsigned int)sizeof(ts), (int)ts.tv_sec);
	
	return 0;
}