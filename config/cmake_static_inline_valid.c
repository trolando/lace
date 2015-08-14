#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Test whether a function declaration with "static inline" modifier is allowed.
 * Windows/MSVC 2015 allows it, older versions do not.
 */

static inline void test_function(int a) {
	printf("int a = %i\n", a);
}
 
int main(int argc, char* argv[]) {
	test_function(0x42);
	
	return 0;
}