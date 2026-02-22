#pragma once
#if defined(_MSC_VER) && !defined(__clang__)

#ifdef __cplusplus
extern "C" {
#endif

extern char* optarg;
extern int optind;

/* Minimal getopt: supports short options and options with required args (':' in optstring). */
int getopt(int argc, char* const argv[], const char* optstring);

#ifdef __cplusplus
}
#endif

#endif