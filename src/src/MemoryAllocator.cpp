
#include "../h/MemoryAllocator.h"

Mem::Elem* Mem::head = nullptr;

void* Mem::add_fragment(size_t blocks) {
    if (head == nullptr) {
        head = (Elem*)HEAP_START_ADDR;
        head->blocks = calculate_blocks(
            (size_t)HEAP_END_ADDR - (size_t)HEAP_START_ADDR
        );
        head->next = nullptr;
    }

    Elem* temp = head;
    Elem* prev = nullptr;

    while (temp && temp->blocks < blocks) {
        prev = temp;
        temp = temp->next;
    }

    if (!temp)
        return nullptr;

    if (temp->blocks == blocks) {
        if (prev)
            prev->next = temp->next;
        else
            head = temp->next;

        return (void*)((char*)temp + sizeof(Elem));
    }

    size_t rest_blocks = temp->blocks - blocks;
    temp->blocks = blocks;

    Elem* rest_fragment =
        (Elem*)((char*)temp + blocks * MEM_BLOCK_SIZE);

    rest_fragment->blocks = rest_blocks;
    rest_fragment->next = temp->next;

    if (prev)
        prev->next = rest_fragment;
    else
        head = rest_fragment;

    return (void*)((char*)temp + sizeof(Elem));
}

void* Mem::mem_alloc(size_t blocks) {
    return add_fragment(blocks);
}

int Mem::delete_fragment(void* p) {
    if (!p)
        return -1;

    if (p < (Elem*)HEAP_START_ADDR ||
        p >= (Elem*)HEAP_END_ADDR)
        return -2;

    Elem* new_elem =
        (Elem*)((char*)p - sizeof(Elem));

    Elem* temp = head;
    Elem* prev = nullptr;

    while (temp && temp < new_elem) {
        prev = temp;
        temp = temp->next;
    }

    if (prev &&
        (char*)prev + prev->blocks * MEM_BLOCK_SIZE ==
        (char*)new_elem) {

        prev->blocks += new_elem->blocks;
        new_elem = prev;

    } else {
        new_elem->next = temp;

        if (prev)
            prev->next = new_elem;
        else
            head = new_elem;
    }

    if (temp &&
        (char*)new_elem + new_elem->blocks * MEM_BLOCK_SIZE ==
        (char*)temp) {

        new_elem->blocks += temp->blocks;
        new_elem->next = temp->next;
    }

    return 0;
}

int Mem::mem_free(void* p) noexcept {
    return delete_fragment(p);
}

size_t Mem::calculate_blocks(size_t size) {
    size_t blocks = (size + sizeof(Elem)) / MEM_BLOCK_SIZE;

    if (blocks * MEM_BLOCK_SIZE != size + sizeof(Elem))
        blocks++;

    return blocks;
}

