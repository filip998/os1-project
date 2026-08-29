#ifndef OS_PROJECT_SCHEDULER_H
#define OS_PROJECT_SCHEDULER_H

#include "MemoryAllocator.h"

class TCB;

typedef struct thread {
    TCB* thread;
    struct thread* next;
}T;

class Scheduler {
    public:
        //singleton DP
        static TCB* get();
        static void put(TCB* thread);
        static void remove(TCB* thread);

        inline static int get_created() { return count; }

        Scheduler(const Scheduler&) = default;
        Scheduler& operator=(const Scheduler&) = default;

        static Scheduler& get_instance();

        static void put_sleep(TCB* thread, uint64 time);
        static void update_sleeping();

    private:
        Scheduler() = default;

        static int count;
        static int created;
        static Scheduler instance;

        static T* head, *tail;

        struct SleepNode {
            TCB* thread;
            uint64 time;
            SleepNode* next;
        };

        static SleepNode* sleep_head, *sleep_tail;

};

#endif //OS_PROJECT_SCHEDULER_H
