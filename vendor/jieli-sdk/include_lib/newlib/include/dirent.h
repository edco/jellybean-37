#ifndef _DIRENT_H_
#define _DIRENT_H_

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"

#ifdef __cplusplus
extern "C" {
#endif
#include <sys/dirent.h>

#if !defined(MAXNAMLEN) && !defined(_POSIX_SOURCE)
#define MAXNAMLEN 1024
#endif

#ifdef __cplusplus
}
#endif

#pragma GCC diagnostic pop

#endif /*_DIRENT_H_*/
