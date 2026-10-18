#ifndef __SIMPLE_SEMAPHORE_H__
#define __SIMPLE_SEMAPHORE_H__

#include "simple_pthread_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

int sem_destroy(sem_t *sem);
int sem_getvalue(sem_t *sem,
                 int *sval);
int sem_init(sem_t *sem,
             int pshared,
             unsigned value);
int sem_post(sem_t *sem);
int sem_timedwait(sem_t *sem,
                  const struct timespec *abstime);
int sem_trywait(sem_t *sem);
int sem_wait(sem_t *sem);

#ifdef __cplusplus
}
#endif

#endif // __SIMPLE_SEMAPHORE_H__
