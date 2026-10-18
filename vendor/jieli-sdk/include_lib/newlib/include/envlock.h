/* envlock.h -- header file for env routines.  */

#ifndef _INCLUDE_ENVLOCK_H_
#define _INCLUDE_ENVLOCK_H_

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"

#include <_ansi.h>
#include <sys/reent.h>

#define ENV_LOCK __env_lock(reent_ptr)
#define ENV_UNLOCK __env_unlock(reent_ptr)

void _EXFUN(__env_lock, (struct _reent *reent));
void _EXFUN(__env_unlock, (struct _reent *reent));

#pragma GCC diagnostic pop

#endif /* _INCLUDE_ENVLOCK_H_ */
