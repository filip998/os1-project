#ifndef OS_PROJECT_TCB_H
#define OS_PROJECT_TCB_H

#include "../lib/hw.h"

#include "scheduler.h"
#include "syscall_c.h"
#include "riscv.h"

#include "../h/riscv.h"

class TCB;

typedef TCB* thread_t;

class TCB {
    public:
        //struct for saving register of cpu
        struct Context {
            uint64 ra; //return address
            uint64 sp; //stack pointer
        };

        static TCB* running; //current executing thread

        //non static methods
        inline void set_finished(bool f) { this->finished = f; }
        inline bool get_finished() const {return this->finished; }
        inline thread_t* get_handle() const {return this->handle; }

        //static methods
        static int thread_exit();
        static void thread_dispatch();
        static int create_thread(thread_t* handle, void(*start_routine)(void*), void* arg, uint64* stack);
        static int time_sleep(time_t time);

        inline static int get_created() { return Scheduler::get_created(); }

        static void reset_time_slice();
        static void decrease_time_slice();
        static bool time_slice_expired();


    private:
        friend class semaphore;
        friend class Thread;

        //the body of the thread is pointer at function
        using Body = void(*)(void*);

        //class fields
        Body body; //funtion
        thread_t* handle; //thread id
        void* arg; //function argument

        Context context;
        uint64* stack;

        bool finished;

        static uint64 time_slice;
        uint64 sleep_time;


        int on_semaphore; //is thread blocked on semaphore
        int valid; //is thread unblocked from semaphore or semaphore was closed

        static void thread_wrapper();

        static void create_thread_stack(TCB* thread, thread_t* handle, void(*start_routine)(void*), void* arg, uint64* stack);
        static void create_thread_no_stack(TCB* thread, thread_t* handle);
};

#endif //OS_PROJECT_TCB_H
