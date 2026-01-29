
// Basic Malloc + free
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <math.h>

const int MAX_SIZE = 100000000;
const int MAX_ORDER = 10;
const size_t BLOCK_SIZE = 128 * pow(2,MAX_ORDER);

size_t free_blocks = 0;
size_t free_bytes = 0;
size_t allocated_blocks = 0;
size_t allocated_bytes = 0;


struct MallocMetadata {
    size_t size = 0;
    bool is_free = false;
    bool is_left = false;
    MallocMetadata* next = nullptr;
    MallocMetadata* prev = nullptr;
};

MallocMetadata* free_lists[MAX_ORDER + 1] = {nullptr};
MallocMetadata* mmap_list = nullptr;

bool is_initialized = false;



void insert(int index, MallocMetadata* p){
    MallocMetadata* current = free_lists[index];
    if(current == nullptr){
        free_lists[index] = p;
        p->next = nullptr;
        p->prev = nullptr;
    }
    else{
        while(current->next != nullptr && current->next < p){
            current = current->next;
        }
        // Insert p after current
        MallocMetadata* next_elem = current->next;

        if(next_elem != nullptr){
            next_elem->prev = p;
        }
        p->next = next_elem;
        p->prev = current;
        current->next = p;
    }
}

void init(){
    size_t total_size = 32 * BLOCK_SIZE;
    intptr_t current_brk = (intptr_t)sbrk(0);
    size_t padding = 0;

    if (current_brk % total_size != 0) {
        padding = total_size - (current_brk % total_size);
    }
    void* ptr = sbrk(padding + total_size);
    if(ptr == (void*)-1) return;
    auto* first_block = (MallocMetadata*)((char*)ptr + padding);

    for (int i = 0; i < 32; ++i) {
        auto* block = (MallocMetadata*)((char*)first_block + i * BLOCK_SIZE);
        block->size = BLOCK_SIZE;
        block->is_free = true;
        block->is_left = true;
        free_blocks++;
        free_bytes += (block->size - sizeof(MallocMetadata));
        insert(MAX_ORDER, block);
    }
    is_initialized = true;
}

//TODO: remove is_left

int find_order(size_t size){
    int order = 0;
    size_t current_size = 128;
    while (current_size < size && order < MAX_ORDER) {
        current_size *= 2;
        order++;
    }
    return order;
}

void remove(MallocMetadata* ptr){
    MallocMetadata* prev_elem = ptr->prev;
    MallocMetadata* next_elem = ptr->next;

    int order = find_order(ptr->size);

    if (ptr->prev == nullptr) {
        // ptr is the head, update the array
        free_lists[order] = ptr->next;
    } else {
        ptr->prev->next = ptr->next;
    }

    if(ptr->next != nullptr){
        ptr->next->prev = ptr->prev;
    }

    ptr->next = nullptr;
    ptr->prev = nullptr;
}


void* smalloc(size_t size){
    if (size <= 0 || size > MAX_SIZE) return nullptr;
    if(!is_initialized){
        init();
    }
    size_t required_size = size + sizeof(MallocMetadata);
    //if required > max block size, mmap (challenge 3)


    int power = find_order(required_size);

    // large block
    if (required_size >= BLOCK_SIZE ) {
        void* out = nullptr;

        out = (void*) mmap(NULL, pow(2,power), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS,
            -1, 0);
        if (out == (void*) -1) {
            return nullptr;
        }
        auto* meta = (MallocMetadata*) out;
        meta->size= size;
        meta->is_free= false;
        meta->size = required_size;

        // add to list of allocated
        meta->prev= nullptr;
        if (mmap_list !=nullptr) {
            meta->next= mmap_list;
            mmap_list->prev= meta;
        }
        mmap_list = meta;
        return meta;
    }

    //small block
    int current_power = power;
    while (current_power <= MAX_ORDER && free_lists[current_power] == nullptr) {
        current_power++;
    }

    if (current_power > MAX_ORDER) return nullptr;



    //the block we want to use
    MallocMetadata* output = free_lists[current_power];
    remove(output);
    output->is_free = false; //TODO: remove this field entirely
    free_blocks--;
    free_bytes -= (output->size - sizeof(MallocMetadata));


    while(current_power > power){
        current_power--;
        size_t new_size = output->size / 2;

        auto* buddy = (MallocMetadata*)((char*)output + new_size);
        buddy->size = new_size;
        buddy->is_free = true;
        buddy->is_left = false;
        insert(current_power, buddy);

        free_blocks++;
        free_bytes += (buddy->size - sizeof(MallocMetadata));

        output->size = new_size;
        allocated_blocks++;
    }
    return output + 1;
}
void* scalloc(size_t num, size_t size){
    if(num<= 0 || size <=0 ||  size >= MAX_SIZE ||
       num*size>=MAX_SIZE ) {return nullptr; }

    void* ptr = smalloc(num*size);
    if (ptr == nullptr) {return nullptr; }
    //set to 0'z
    memset(ptr, 0, num*size);

    return ptr;
}

void sfree(void* p) {
    if (p==nullptr || p<= (void*) sizeof(MallocMetadata) ) return;
    MallocMetadata* meta = (MallocMetadata*)p - 1;

    //TODO: munmap
    if (meta->size >= BLOCK_SIZE) {
        MallocMetadata* prev_elem = meta->prev;
        MallocMetadata* next_elem = meta->next;

        if (meta->prev == nullptr) {
            // meta is the head, update the array
            mmap_list = meta->next;
        } else {
            meta->prev->next = meta->next;
        }

        if(meta->next != nullptr){
            meta->next->prev = meta->prev;
        }

        meta->next = nullptr;
        meta->prev = nullptr;
    }

    if (meta->is_free) return; // Double free protection

    int order = find_order(meta->size);
    meta->is_free = true;
    allocated_blocks--;
    allocated_bytes -= (meta->size - sizeof(MallocMetadata));

    // Add to stats before merging (will be adjusted in merge)
    // Actually, logic is easier if we just try to merge immediately

    // Challenge 2: Iterative Merge
    while (order < MAX_ORDER) {
        // XOR Trick to find buddy address
        intptr_t block_addr = (intptr_t)meta;
        intptr_t buddy_addr = block_addr ^ meta->size;
        MallocMetadata* buddy = (MallocMetadata*)buddy_addr;

        // Check if buddy is free AND correct size
        // (Buddy might be split, so size check is crucial)
        if (!buddy->is_free || buddy->size != meta->size) {
            break;
        }

        // Buddy is free! Merge.
        remove(buddy); // Remove buddy from free list

        // Stats update: We lose one free block (the buddy)
        free_blocks--;
        free_bytes -= (buddy->size - sizeof(MallocMetadata));

        // Combine: The one with lower address becomes the start
        if (buddy < meta) {
            meta = buddy;
        }

        meta->size *= 2;
        order++;
    }

    // Insert the final merged block
    insert(order, meta);
    free_blocks++;
    free_bytes += (meta->size - sizeof(MallocMetadata));
}

void* srealloc(void* oldp, size_t size) {

    if (size <= 0 ||size >= MAX_SIZE ) return nullptr;
    if (oldp==nullptr) {
        return smalloc(size);
    }

    MallocMetadata* old_meta_ptr = (MallocMetadata*) oldp - 1;

    if (size <= old_meta_ptr->size - sizeof(MallocMetadata)) return oldp;
    void* new_ptr = smalloc(size);
    if (new_ptr == nullptr) {return nullptr;}

    memmove (new_ptr,oldp,old_meta_ptr->size - sizeof(MallocMetadata));
    sfree(oldp);
    return new_ptr;
}

size_t _num_free_blocks() {
    return free_blocks;

}
size_t _num_free_bytes() {
    return free_bytes;

}
size_t _num_allocated_blocks() {
    return allocated_blocks;

}
size_t _num_allocated_bytes() {
    return allocated_bytes;

}
size_t _num_meta_data_bytes() {
    return (sizeof (MallocMetadata) * allocated_blocks);

}
size_t _size_meta_data() {
    return sizeof (MallocMetadata);
}
