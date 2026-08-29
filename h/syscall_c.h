#ifndef OS_PROJECT_SYSCALL_C_H
#define OS_PROJECT_SYSCALL_C_H

#include "../lib/hw.h"

size_t calculate_blocks(size_t size);

//if we want to use struct in cpp language
#ifdef __cplusplus
extern "C"
{
#endif

typedef unsigned long time_t;
typedef struct TCB* thread_t;
typedef struct semaphore* sem_t;

typedef struct elem {
    size_t blocks;
    struct elem* next;
} Elem;

// memory allocation
void* mem_alloc(size_t size);
int mem_free(void *addr);

//threads
int thread_create(thread_t* handle, void(*start_routine)(void*), void* arg);
int thread_exit();
void thread_dispatch();

//semaphores
int sem_open(sem_t* handle, unsigned init);
int sem_close(sem_t handle);
int sem_wait(sem_t id);
int sem_signal(sem_t id);
int sem_wait_n(sem_t id, unsigned n);
int sem_signal_n(sem_t id, unsigned n);

int time_sleep(time_t time);

//console
void putc(char chr);
char getc();

#ifdef __cplusplus
}
#endif

#endif // OS_PROJECT_SYSCALL_C_H
