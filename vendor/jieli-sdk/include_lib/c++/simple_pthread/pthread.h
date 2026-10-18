#ifndef __SIMPLE_PTHREAD_H__
#define __SIMPLE_PTHREAD_H__

#include "simple_pthread_comm.h"

/**
 * @name Mutex types.
 */
/**@{ */
#ifndef PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_NORMAL        0                        /**< Non-robust, deadlock on relock, does not remember owner. */
#endif
#ifndef PTHREAD_MUTEX_ERRORCHECK
#define PTHREAD_MUTEX_ERRORCHECK    1                        /**< Non-robust, error on relock,  remembers owner. */
#endif
#ifndef PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_RECURSIVE     2                        /**< Non-robust, recursive relock, remembers owner. */
#endif
#ifndef PTHREAD_MUTEX_DEFAULT
#define PTHREAD_MUTEX_DEFAULT       PTHREAD_MUTEX_NORMAL     /**< PTHREAD_MUTEX_NORMAL (default). */
#endif
/**@} */

#define PTHREAD_COND_INITIALIZER    FREERTOS_POSIX_COND_INITIALIZER       /**< pthread_cond_t. */

#define PTHREAD_MUTEX_INITIALIZER    FREERTOS_POSIX_MUTEX_INITIALIZER /**< pthread_mutex_t. */

#define pdFALSE			( ( BaseType_t ) 0 )

#define FREERTOS_POSIX_COND_INITIALIZER \
    ( ( ( pthread_cond_internal_t )         \
    {                                       \
        .xIsInitialized = pdFALSE,          \
        .xCondWaitSemaphore = { { 0 } },    \
        .iWaitingThreads = 0                \
    }                                       \
        )                                   \
    )

#define FREERTOS_POSIX_MUTEX_INITIALIZER \
    ( ( ( pthread_mutex_internal_t )         \
    {                                        \
        .xIsInitialized = pdFALSE,           \
        .xMutex = { { 0 } },                 \
        .xTaskOwner = NULL,                  \
        .xAttr = { .iType = 0 }              \
    }                                        \
        )                                    \
    )

#ifdef __cplusplus
extern "C" {
#endif

int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_getdetachstate(const pthread_attr_t *attr,
                                int *detachstate);
int pthread_attr_getschedparam(const pthread_attr_t *attr,
                               struct sched_param *param);
int pthread_attr_getstacksize(const pthread_attr_t *attr,
                              size_t *stacksize);
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_setdetachstate(pthread_attr_t *attr,
                                int detachstate);
int pthread_attr_setschedparam(pthread_attr_t *attr,
                               const struct sched_param *param);
int pthread_attr_setschedpolicy(pthread_attr_t *attr,
                                int policy);
int pthread_attr_setstacksize(pthread_attr_t *attr,
                              size_t stacksize);

int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr,
                         unsigned count);
int pthread_barrier_wait(pthread_barrier_t *barrier);

int pthread_create(pthread_t *thread,
                   const pthread_attr_t *attr,
                   void *(*startroutine)(void *),
                   void *arg);


int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_init(pthread_cond_t *cond,
                      const pthread_condattr_t *attr);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_timedwait(pthread_cond_t *cond,
                           pthread_mutex_t *mutex,
                           const struct timespec *abstime);
int pthread_cond_wait(pthread_cond_t *cond,
                      pthread_mutex_t *mutex);
int pthread_equal(pthread_t t1,
                  pthread_t t2);
void pthread_exit(void *value_ptr);
int pthread_getschedparam(pthread_t thread,
                          int *policy,
                          struct sched_param *param);
int pthread_join(pthread_t thread,
                 void **retval);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_init(pthread_mutex_t *mutex,
                       const pthread_mutexattr_t *attr);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_timedlock(pthread_mutex_t *mutex,
                            const struct timespec *abstime);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr,
                              int *type);
int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr,
                              int type);
pthread_t pthread_self(void);
int pthread_setschedparam(pthread_t thread,
                          int policy,
                          const struct sched_param *param);

// no implementations

// see: https://man7.org/linux/man-pages/man2/nanosleep.2.html
/*
       nanosleep() suspends the execution of the calling thread until
       either at least the time specified in *req has elapsed, or the
       delivery of a signal that triggers the invocation of a handler in
       the calling thread or that terminates the process.

       If the call is interrupted by a signal handler, nanosleep()
       returns -1, sets errno to EINTR, and writes the remaining time
       into the structure pointed to by rem unless rem is NULL.  The
       value of *rem can then be used to call nanosleep() again and
       complete the specified pause (but see NOTES).

       The structure timespec is used to specify intervals of time with
       nanosecond precision.  It is defined as follows:

           struct timespec {
               time_t tv_sec;        // seconds
               long   tv_nsec;       // nanoseconds
           };

       The value of the nanoseconds field must be in the range 0 to
       999999999.

       Compared to sleep(3) and usleep(3), nanosleep() has the following
       advantages: it provides a higher resolution for specifying the
       sleep interval; POSIX.1 explicitly specifies that it does not
       interact with signals; and it makes the task of resuming a sleep
       that has been interrupted by a signal handler easier.
*/
int nanosleep(const struct timespec *req, struct timespec *rem);

// see: https://man7.org/linux/man-pages/man2/sched_yield.2.html
/*
       sched_yield() causes the calling thread to relinquish the CPU.
       The thread is moved to the end of the queue for its static
       priority and a new thread gets to run.
*/
int sched_yield(void);


/* Keys for thread-specific data */
typedef unsigned int pthread_key_t;

/* Once-only execution */
typedef int pthread_once_t;

#define PTHREAD_ONCE_INIT 0



// see: https://pubs.opengroup.org/onlinepubs/009696799/functions/pthread_key_create.html
/*


    The pthread_key_create() function shall create a thread-specific data key visible to all threads in the process. Key values provided by pthread_key_create() are opaque objects used to locate thread-specific data. Although the same key value may be used by different threads, the values bound to the key by pthread_setspecific() are maintained on a per-thread basis and persist for the life of the calling thread.

    Upon key creation, the value NULL shall be associated with the new key in all active threads. Upon thread creation, the value NULL shall be associated with all defined keys in the new thread.

    An optional destructor function may be associated with each key value. At thread exit, if a key value has a non-NULL destructor pointer, and the thread has a non-NULL value associated with that key, the value of the key is set to NULL, and then the function pointed to is called with the previously associated value as its sole argument. The order of destructor calls is unspecified if more than one destructor exists for a thread when it exits.

    If, after all the destructors have been called for all non-NULL values with associated destructors, there are still some non-NULL values with associated destructors, then the process is repeated. If, after at least {PTHREAD_DESTRUCTOR_ITERATIONS} iterations of destructor calls for outstanding non-NULL values, there are still some non-NULL values with associated destructors, implementations may stop calling destructors, or they may continue calling destructors until no non-NULL values with associated destructors exist, even though this might result in an infinite loop.

*/
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));

// see: https://pubs.opengroup.org/onlinepubs/009696799/functions/pthread_getspecific.html
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);

// see: https://pubs.opengroup.org/onlinepubs/7908799/xsh/pthread_once.html
/*
The first call to pthread_once() by any thread in a process, with a given once_control, will call the init_routine() with no arguments. Subsequent calls of pthread_once() with the same once_control will not call the init_routine(). On return from pthread_once(), it is guaranteed that init_routine() has completed. The once_control parameter is used to determine whether the associated initialisation routine has been called.

The function pthread_once() is not a cancellation point. However, if init_routine() is a cancellation point and is canceled, the effect on once_control is as if pthread_once() was never called.

The constant PTHREAD_ONCE_INIT is defined by the header <pthread.h>.

The behaviour of pthread_once() is undefined if once_control has automatic storage duration or is not initialised by PTHREAD_ONCE_INIT.
*/
int pthread_once(pthread_once_t *once_control,
                 void (*init_routine)(void));

// see: https://pubs.opengroup.org/onlinepubs/009696899/functions/pthread_detach.html
/*
    The pthread_detach() function shall indicate to the implementation that storage for the thread thread can be reclaimed when that thread terminates. If thread has not terminated, pthread_detach() shall not cause it to terminate. The effect of multiple pthread_detach() calls on the same target thread is unspecified.
*/
int pthread_detach(pthread_t thread);

#ifdef __cplusplus
}
#endif

#endif // __SIMPLE_PTHREAD_H__

