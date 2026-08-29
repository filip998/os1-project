#ifndef OS_PROJECT_HANDLERS_H
#define OS_PROJECT_HANDLERS_H

#include "../lib/hw.h"
#include "../lib/console.h"

#include "../h/MemoryAllocator.h"
#include "../h/tcb.h"
#include "../h/sem.h"


class Handlers {
    public:
        static void handle_timer_interrupt();
        static void handle_console_interrupt();
        static void handle_exception();
        static void handle_sys_call();

    private:
        //clear sip bit
        static void mc_sip(uint64 mask);

        //read and write sepc reg
        static uint64 r_sepc();
        static void w_sepc(uint64 sepc);

        //read and write sstatus reg
        static uint64 r_sstatus();
        static void w_sstatus(uint64 sstatus);

        //sys call handlers
        static void handle_mem_alloc();
        static void handle_mem_free();

        static void handle_thread_create();
        static void handle_thread_exit();
        static void handle_thread_dispatch();
        static void handle_time_sleep();

        static void handle_sem_open();
        static void handle_sem_close();
        static void handle_sem_wait();
        static void handle_sem_signal();
        static void handle_sem_wait_n();
        static void handle_sem_signal_n();

        static void handle_getc();
        static void handle_putc();
};

inline void Handlers::mc_sip(uint64 mask) {
    __asm__ volatile("csrc sip, %[mask]" : : [mask] "r"(mask));
}

inline uint64 Handlers::r_sepc() {
    uint64 sepc;
    __asm__ volatile("csrr %[sepc], sepc" : [sepc] "=r"(sepc));
    return sepc;
}

inline void Handlers::w_sepc(uint64 sepc) {
    __asm__ volatile("csrw sepc, %[sepc]" : : [sepc] "r"(sepc));
}

inline uint64 Handlers::r_sstatus() {
    uint64 volatile sstatus;
    __asm__ volatile("csrr %[sstatus], sstatus" : [sstatus] "=r"(sstatus));
    return sstatus;
}

inline void Handlers::w_sstatus(uint64 sstatus) {
    __asm__ volatile("csrw sstatus, %[sstatus]" : : [sstatus] "r"(sstatus));
}


#endif //OS_PROJECT_HANDLERS_H
