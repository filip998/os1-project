#include "../h/riscv.h"
#include "../h/syscall_c.h"
#include "../h/tcb.h"
#include "../h/printing.hpp"

extern void userMain();
extern "C" void consoleinit();
extern "C" void plicinit();
extern "C" void plicinithart();

void wrapperUserMain(void*) {
    userMain();
}

int main() {
    Riscv::w_stvec((uint64)&Riscv::supervisorTrap);
    consoleinit();
    plicinit();
    plicinithart();
    Riscv::ms_sie(Riscv::SIE_SEIE);
    Riscv::ms_sstatus(Riscv::SSTATUS_SIE);

    thread_t m;
    thread_create(&m, nullptr, nullptr);

    TCB::running = m;
    //m->set_finished(true);

    thread_t t;
    thread_create(&t, wrapperUserMain, nullptr);

    while (true) {
        thread_dispatch();
    }

    return 0;
}
