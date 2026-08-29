#include "../h/buffer.hpp"
#include "../h/sem.h"

Buffer::Buffer(int _cap) : cap(_cap + 1), head(0), tail(0) {
    buffer = (int *)mem_alloc(sizeof(int) * cap);
    semaphore::sem_open(&itemAvailable, 0);
    semaphore::sem_open(&spaceAvailable, _cap);
    semaphore::sem_open(&mutexHead, 1);
    semaphore::sem_open(&mutexTail, 1);
}

Buffer::~Buffer() {
    putc('\n');
    printString("Buffer deleted!\n");
    while (getCnt() > 0) {
        char ch = buffer[head];
        putc(ch);
        head = (head + 1) % cap;
    }
    putc('!');
    putc('\n');

    mem_free(buffer);
    semaphore::sem_close(itemAvailable);
    semaphore::sem_close(spaceAvailable);
    semaphore::sem_close(mutexTail);
    semaphore::sem_close(mutexHead);
}

void Buffer::put(int val) {
    semaphore::sem_wait(spaceAvailable);

    semaphore::sem_wait(mutexTail);
    buffer[tail] = val;
    tail = (tail + 1) % cap;
    semaphore::sem_signal(mutexTail);

    semaphore::sem_signal(itemAvailable);

}

int Buffer::get() {
    semaphore::sem_wait(itemAvailable);

    semaphore::sem_wait(mutexHead);

    int ret = buffer[head];
    head = (head + 1) % cap;
    semaphore::sem_signal(mutexHead);

    semaphore::sem_signal(spaceAvailable);

    return ret;
}

int Buffer::getCnt() {
    int ret;

    semaphore::sem_wait(mutexHead);
    semaphore::sem_wait(mutexTail);

    if (tail >= head) {
        ret = tail - head;
    } else {
        ret = cap - head + tail;
    }

    semaphore::sem_signal(mutexTail);
    semaphore::sem_signal(mutexHead);

    return ret;
}
