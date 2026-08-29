#ifndef OS_PROJECT_SEM_H
#define OS_PROJECT_SEM_H

#include "../lib/hw.h"
#include "tcb.h"
#include "MemoryAllocator.h"

class semaphore;
typedef semaphore* sem_t;

class semaphore {
    public:
        static int sem_open(sem_t* handle, unsigned init);
        static int sem_close(sem_t handle);

        static int sem_wait(sem_t id);
        static int sem_signal(sem_t id);
        static int sem_signal_n(sem_t id, unsigned n);
        static int sem_wait_n(sem_t id, unsigned n);

    protected:
        void block(unsigned n);
        void unblock();

    private:
        //struct for blocked threads queue
        typedef struct blocked {
            TCB* thread;
            unsigned n;
            struct blocked* next;
        }Blocked;

        Blocked* head, *tail;
        int init;
        sem_t handle;

};

#endif
