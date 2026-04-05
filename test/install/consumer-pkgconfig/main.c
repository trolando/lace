#include <lace.h>
#include <stdio.h>

int main(void) {
    lace_start(1, 0);
    printf("Lace %d.%d.%d OK\n", LACE_VERSION_MAJOR, LACE_VERSION_MINOR, LACE_VERSION_PATCH);
    lace_stop();
    return 0;
}