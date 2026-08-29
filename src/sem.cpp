#include "../h/sem.h"

extern "C" void context_switch(TCB::Context* old_context, TCB::Context* new_context);

int semaphore::sem_open(sem_t *handle, unsigned init) {
    if (!handle) return -1; //bad parametar
    auto sem = (semaphore*)Mem::mem_alloc(sizeof(semaphore));

    if (!sem) return -2; //failed to create semaphore

    sem->handle = *handle;
    sem->init = (int)init;

    sem->head = sem->tail = nullptr;
    *handle = sem;

    return 0;
}

int semaphore::sem_close(sem_t handle) {
    if (!handle) return -1; //bad parameter

    Blocked* temp;
    while (handle->head) {
        temp = handle->head;
        temp->thread->valid = -1;
        Scheduler::put(temp->thread);
        handle->head = handle->head->next;
        Mem::mem_free(temp);
    }

    handle -> tail = nullptr;
    Mem::mem_free(handle);
    return 0;
}

void semaphore::block(unsigned n) {
    TCB::running->on_semaphore = 1;

    auto new_blocked = (Blocked*)Mem::mem_alloc(sizeof(Blocked));
    if (!new_blocked) return;

    new_blocked->thread = TCB::running;
    new_blocked -> n = n;
    new_blocked->next = nullptr;

    if (!this->head) {
        this->head = new_blocked;
    }else {
        this->tail->next = new_blocked;
    }
    this->tail = new_blocked;

    TCB* old = TCB::running;
    TCB::running = Scheduler::get();

    context_switch(&old->context, &TCB::running->context);
}

void semaphore::unblock() {
    if (!head) return; //no threads on semaphore

    if (init < (int)head->n) return; //not enough resources

    auto free = head;
    init -= free->n;
    head = head->next;

    if (!head) tail = nullptr;

    Scheduler::put(free->thread);
    Mem::mem_free(free);
}

int semaphore::sem_wait(sem_t id) {
    if (!id) return -1; //bad parametar

    if (id->init > 0) {
        id-> init -= 1;
        return 0;
    }else {
        id->block(1);
    }

    if (TCB::running->on_semaphore == 0) {
        return 0; //success
    }

    int valid = TCB::running->valid;

    TCB::running->on_semaphore = 0;
    TCB::running->valid = 0;

    if (valid == -1) {
        return -2; //semaphore was closed, thread is not regular unblocked from semaphore
    }
    return 0;
}

int semaphore::sem_signal(sem_t id) {
    if (!id) return -1; //bad parametar
    id->init += 1;

    if (id->head && id->init >= (int)id->head->n) {
        id->unblock();
    }

    return 0;
}

int semaphore::sem_wait_n(sem_t id, unsigned n) {
    if (!id) return -1; //bad parametar
    if (n == 0) return 0;

    if (id->init >= (int)n) {
        id->init -=n; //success
        return 0;
    }

    id->block(n);
    int valid = TCB::running->valid;

    TCB::running->on_semaphore = 0;
    TCB::running->valid = 0;
    if (valid == -1) {
        return -2;
    }

    return 0;
}

int semaphore::sem_signal_n(sem_t id, unsigned n) {
    if (!id) return -1; // bad parametar

    id->init += n;

    while (id->head && id->init >= (int)id->head->n) {
        id -> unblock();
    }

    return 0;
}