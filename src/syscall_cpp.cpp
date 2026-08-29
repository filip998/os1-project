#include "../h/syscall_cpp.hpp"
#include "../h/tcb.h"

#include "../h/printing.hpp"

void* operator new(size_t size){
    return mem_alloc(size);
}

void operator delete(void *pVoid) {
    mem_free(pVoid);
}

Thread::Thread(void (*body)(void *), void *arg) : body(body), arg(arg) {}

Thread::Thread()
    : myHandle(nullptr), body(nullptr), arg(nullptr) {}

int Thread::start() {
    return thread_create(&myHandle, threadWrapper, this);
}

void Thread::dispatch() {
    thread_dispatch();
}

int Thread::sleep(time_t time) {
    //return time_sleep(time);
    return 0;
}

void Thread::threadWrapper(void *arg)  {
    auto thread = static_cast<Thread*>(arg);
    thread->run();
}

Thread::~Thread() {
    if (myHandle) {
        Scheduler::remove(myHandle);
        delete myHandle->stack;
        delete myHandle;
    }
}

Semaphore::Semaphore(unsigned int init) {
    sem_open(&myHandle, init);
}

Semaphore::~Semaphore() {
    sem_close(myHandle);
}

int Semaphore::wait() {
    return sem_wait(myHandle);
}

int Semaphore::signal() {
    return sem_signal(myHandle);
}

char Console::getc() {
    return ::getc();
}

void Console::putc(char c) {
    ::putc(c);
}

