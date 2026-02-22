#if defined(_MSC_VER) && !defined(__clang__)

#include <string.h>

char* optarg = 0;
int optind = 1;

int getopt(int argc, char* const argv[], const char* optstring)
{
    static int pos = 1;
    char* arg;

    if (optind >= argc) return -1;

    arg = argv[optind];
    if (arg[0] != '-' || arg[1] == '\0') return -1;
    if (arg[1] == '-' && arg[2] == '\0') { optind++; return -1; } /* "--" */

    char c = arg[pos];

    const char* p = optstring;
    while (*p && *p != c) p++;
    if (!*p) {
        pos = 1;
        optind++;
        return '?';
    }

    if (p[1] == ':') {
        /* option expects an argument */
        if (arg[pos + 1] != '\0') {
            optarg = &arg[pos + 1];
            pos = 1;
            optind++;
        }
        else if (optind + 1 < argc) {
            optarg = argv[optind + 1];
            pos = 1;
            optind += 2;
        }
        else {
            pos = 1;
            optind++;
            return '?';
        }
    }
    else {
        /* no argument */
        optarg = 0;
        if (arg[++pos] == '\0') { pos = 1; optind++; }
    }

    return (int)c;
}

#endif