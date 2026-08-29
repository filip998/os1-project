#include "../h/MemoryAllocator.h"
#include "../h/scheduler.h"

Scheduler Scheduler::instance ;
int Scheduler::created = 0;
int Scheduler::count = 0;

T* Scheduler::head = nullptr;
T* Scheduler::tail = nullptr;

Scheduler::SleepNode* Scheduler::sleep_head = nullptr;
Scheduler::SleepNode* Scheduler::sleep_tail = nullptr;

Scheduler& Scheduler::get_instance() {
    if (created == 0) {
        instance = Scheduler();
        created = 1;
    }

    return instance;
}

TCB* Scheduler::get() {
    if (Scheduler::count == 0) return nullptr; //there is no threads in scheduler
    T* temp = head;
    head = head->next;

    TCB* ret = temp->thread;

    Mem::mem_free(temp);
    count--;

    return ret;
}

void Scheduler::put(TCB* thread) {
    auto new_thread = (T*)Mem::mem_alloc(sizeof(T));

    new_thread -> next = nullptr;
    new_thread -> thread = thread;

    if (!head) {
        head = new_thread;
    }else {
        tail -> next = new_thread;
    }

    tail = new_thread;

    count++;
}

void Scheduler::remove(TCB* thread) {
    if (!thread) return;

    T* temp = head, *prev = nullptr;
    while (temp) {
        if (temp ->thread == thread) {
            break;
        }
        prev = temp;
        temp = temp->next;
    }

    if (!temp) return;

    if (prev) {
        prev->next = temp->next;
    }else {
        head = head->next;
    }

    Mem::mem_free(temp);

    count--;
}

void Scheduler::put_sleep(TCB* thread, uint64 time) {
    if (!thread || !time) return;
    SleepNode* newNode = (SleepNode*)Mem::mem_alloc(sizeof(SleepNode));
    if (!newNode) return;

    newNode -> next = nullptr;
    newNode-> thread = thread;
    newNode-> time = time;

    if (!sleep_head) {
        sleep_head = newNode;
        sleep_tail = newNode;
    }else {
        sleep_tail -> next = newNode;
    }
    sleep_tail = newNode;
}

void Scheduler::update_sleeping() {
    SleepNode* curr = sleep_head;
    SleepNode* prev = nullptr;

    while (curr) {
        if (curr->time > 0) {
            curr->time--;
        }

        if (curr->time == 0) {
            TCB* thread = curr->thread;

            //we need to remove node from list
            if (prev) {
                prev->next = curr->next;
            } else {
                sleep_head = curr->next;
            }

            if (curr == sleep_tail) {
                sleep_tail = prev;
            }

            SleepNode* temp = curr;
            curr = curr->next;

            Mem::mem_free(temp);

            //thread is awake
            Scheduler::put(thread);
        }else {
            prev = curr;
            curr =  curr->next;
        }
    }
}
