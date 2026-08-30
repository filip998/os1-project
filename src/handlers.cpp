#include "../h/handlers.h"

enum sys_call {
    MEM_ALLOC = 0x01,
    MEM_FREE = 0x02,

    THREAD_CREATE = 0x11,
    THREAD_EXIT = 0x12,
    THREAD_DISPATCH = 0x13,

    SEM_OPEN = 0x21,
    SEM_CLOSE = 0x22,
    SEM_WAIT = 0x23,
    SEM_SIGNAL = 0x24,
    SEM_WAIT_N = 0x25,
    SEM_SIGNAL_N = 0x26,

    TIME_SLEEP = 0x31,

    GETC = 0x41,
    PUTC = 0x42
};

enum register_slot {
    REG_A0 = 10,
    REG_A1 = 11,
    REG_A2 = 12,
    REG_A6 = 16,
    REG_A7 = 17
};

void Handlers::handle_timer_interrupt() {
    uint64 volatile sepc = r_sepc();
    uint64 volatile sstatus = r_sstatus();

    mc_sip(1<<1);
    TCB::decrease_time_slice();
    //Scheduler::update_sleeping();

    if (TCB::time_slice_expired()) {
        TCB::thread_dispatch();
    }

    w_sstatus(sstatus);
    w_sepc(sepc);
}

void Handlers::handle_exception() {

}

void Handlers::handle_console_interrupt() {
    console_handler();
}

void Handlers::handle_mem_alloc(uint64* frame) {
    size_t blocks = frame[REG_A1];
    void* result = Mem::mem_alloc(blocks);
    frame[REG_A0] = reinterpret_cast<uint64>(result);
}

void Handlers::handle_mem_free(uint64* frame) {
    void* p = reinterpret_cast<void*>(frame[REG_A1]);
    int result = Mem::mem_free(p);
    frame[REG_A0] = static_cast<uint64>(result);
}


void Handlers::handle_thread_create(uint64* frame) {
    thread_t* handle = reinterpret_cast<thread_t*>(frame[REG_A1]);
    void(*start_routine)(void*) = reinterpret_cast<void(*)(void*)>(frame[REG_A2]);
    void* arg = reinterpret_cast<void*>(frame[REG_A6]);
    uint64* stack = reinterpret_cast<uint64*>(frame[REG_A7]);

    int result = TCB::create_thread(handle, start_routine, arg, stack);
    frame[REG_A0] = static_cast<uint64>(result);
}

void Handlers::handle_thread_exit(uint64* frame) {
    int result = TCB::thread_exit();
    frame[REG_A0] = static_cast<uint64>(result);
}

void Handlers::handle_thread_dispatch() {
    TCB::thread_dispatch();
}

void Handlers::handle_time_sleep(uint64* frame) {
    time_t time = frame[REG_A1];
    int result = TCB::time_sleep(time);
    frame[REG_A0] = static_cast<uint64>(result);
}

void Handlers::handle_sem_open(uint64* frame) {
    sem_t* sem = reinterpret_cast<sem_t*>(frame[REG_A1]);
    unsigned init = static_cast<unsigned>(frame[REG_A2]);
    int result = semaphore::sem_open(sem, init);
    frame[REG_A0] = static_cast<uint64>(result);
}

void Handlers::handle_sem_close(uint64* frame) {
    sem_t sem = reinterpret_cast<sem_t>(frame[REG_A1]);
    int result = semaphore::sem_close(sem);
    frame[REG_A0] = static_cast<uint64>(result);
}

void Handlers::handle_sem_wait(uint64* frame) {
    sem_t sem = reinterpret_cast<sem_t>(frame[REG_A1]);
    int result = semaphore::sem_wait(sem);
    frame[REG_A0] = static_cast<uint64>(result);
}

void Handlers::handle_sem_signal(uint64* frame) {
    sem_t sem = reinterpret_cast<sem_t>(frame[REG_A1]);
    int result = semaphore::sem_signal(sem);
    frame[REG_A0] = static_cast<uint64>(result);
}

void Handlers::handle_sem_wait_n(uint64* frame) {
    sem_t sem = reinterpret_cast<sem_t>(frame[REG_A1]);
    unsigned n = static_cast<unsigned>(frame[REG_A2]);
    int result = semaphore::sem_wait_n(sem, n);
    frame[REG_A0] = static_cast<uint64>(result);
}

void Handlers::handle_sem_signal_n(uint64* frame) {
    sem_t sem = reinterpret_cast<sem_t>(frame[REG_A1]);
    unsigned n = static_cast<unsigned>(frame[REG_A2]);
    int result = semaphore::sem_signal_n(sem, n);
    frame[REG_A0] = static_cast<uint64>(result);
}

void Handlers::handle_getc(uint64* frame) {
    char result = __getc();
    frame[REG_A0] = static_cast<uint64>(result);
}

void Handlers::handle_putc(uint64* frame) {
    char chr = static_cast<char>(frame[REG_A1]);
    __putc(chr);
}

void Handlers::handle_sys_call(uint64* frame) {
    uint64 SYS_CALL = frame[REG_A0];

    uint64 volatile sepc = r_sepc() + 4;
    uint64 volatile sstatus = r_sstatus();

    switch (SYS_CALL) {
        case MEM_ALLOC:
            handle_mem_alloc(frame);
            break;
        case MEM_FREE:
            handle_mem_free(frame);
            break;
        case THREAD_CREATE:
            handle_thread_create(frame);
            break;
        case THREAD_EXIT:
            handle_thread_exit(frame);
            break;
        case THREAD_DISPATCH:
            handle_thread_dispatch();
            break;
        case SEM_OPEN:
            handle_sem_open(frame);
            break;
        case SEM_CLOSE:
            handle_sem_close(frame);
            break;
        case SEM_WAIT:
            handle_sem_wait(frame);
            break;
        case SEM_SIGNAL:
            handle_sem_signal(frame);
            break;
        case TIME_SLEEP:
            //handle_time_sleep();
            break;
        case SEM_WAIT_N:
            handle_sem_wait_n(frame);
            break;
        case SEM_SIGNAL_N:
            handle_sem_signal_n(frame);
            break;
        case PUTC:
            handle_putc(frame);
            break;
        case GETC:
            handle_getc(frame);
            break;
        default:
            break;

    }

    w_sstatus(sstatus);
    w_sepc(sepc);
}
