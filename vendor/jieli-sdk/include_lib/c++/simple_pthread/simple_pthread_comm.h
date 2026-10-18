#ifndef __SIMPLE_PTHREAD_COMM_H__
#define __SIMPLE_PTHREAD_COMM_H__

#include <stdint.h>
#include <time.h>
#include "FreeRTOS/FreeRTOS.h"

typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;

typedef StaticQueue_t StaticSemaphore_t;

typedef struct {
    StaticSemaphore_t xSemaphore;
    int value;
} sem_internal_t;

typedef sem_internal_t PosixSemType_t;

typedef PosixSemType_t sem_t;

typedef void *TaskHandle_t;

typedef struct xSTATIC_LIST_ITEM StaticListItem_t;

typedef struct pthread_mutexattr_internal {
    int iType;
} pthread_mutexattr_internal_t;

typedef StaticQueue_t StaticSemaphore_t;

typedef struct pthread_mutex_internal {
    BaseType_t xIsInitialized;
    StaticSemaphore_t xMutex;
    TaskHandle_t xTaskOwner;
    pthread_mutexattr_internal_t xAttr;
} pthread_mutex_internal_t;

typedef struct pthread_cond_internal {
    BaseType_t xIsInitialized;
    StaticSemaphore_t xCondWaitSemaphore;
    unsigned iWaitingThreads;
} pthread_cond_internal_t;

typedef struct pthread_barrier_internal {
    unsigned uThreadCount;
    unsigned uThreshold;
    StaticSemaphore_t xThreadCountSemaphore;
    StaticEventGroup_t xBarrierEventGroup;
} pthread_barrier_internal_t;

typedef pthread_cond_internal_t PthreadCondType_t;
typedef pthread_mutex_internal_t PthreadMutexType_t;
typedef struct pthread_mutexattr {
    uint32_t ulpthreadMutexAttrStorage;
} PthreadMutexAttrType_t;

typedef PthreadCondType_t pthread_cond_t;
typedef void *pthread_condattr_t;
typedef PthreadMutexType_t pthread_mutex_t;
typedef PthreadMutexAttrType_t pthread_mutexattr_t;

typedef struct pthread_attr {
    uint32_t ulpthreadAttrStorage;
} PthreadAttrType_t;

typedef PthreadAttrType_t pthread_attr_t;

typedef pthread_barrier_internal_t PthreadBarrierType_t;
typedef PthreadBarrierType_t pthread_barrier_t;

typedef void *pthread_barrierattr_t;
typedef void *pthread_t;

#endif // __SIMPLE_PTHREAD_COMM_H__
