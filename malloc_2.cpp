// Basic Malloc + free
#include <iostream>
#include <unistd.h>

const int MAX_SIZE = 100000000;
MallocMetadata* firstMeta = nullptr;

size_t free_blocks = 0;
size_t free_bytes = 0;
size_t allocated_blocks = 0;
size_t allocated_bytes = 0;


struct MallocMetadata {
    size_t size;
    bool is_free = false;
    MallocMetadata* next = nullptr;
    MallocMetadata* prev = nullptr;
};

void* smalloc(size_t size){
    if (size <= 0) return NULL;
    if (size > MAX_SIZE) return NULL;
    // sbrk failed - return NULL
    void * ptr  = firstMeta;

    do  {
        if ( ptr !=nullptr &&
            ptr->is_free == true &&
            ptr->size >= size) {
                ptr->is_free = false;
                free_blocks--;
                free_bytes -= size;
                return ptr + sizeof(MallocMetadata);
        }
        else {
            ptr = ptr->next;
        }
    } while (ptr != firstMeta)


    MallocMetadata* meta = (MallocMetadata*) sbrk( sizeof(MallocMetadata) + size ) ;
    ptr = meta + sizeof(MallocMetadata); //pointer to the first allocated byte within the allocated block.
    // void* bp = sbrk( sizeof(size_t) );
    meta.size = size;
    allocated_blocks++;
    allocated_bytes += meta.size;
    if (firstMeta == nullptr) {
        firstMeta = meta;
        meta.next = meta+sizeof(MAllocMetadata);
        meta.prev = meta+sizeof(MAllocMetadata);
    }
    else {

        MallocMetadata* previousLast = firstMeta->prev;
        //firstMeta = meta;
        meta->next = firstMeta;
        meta->prev = previousLast;
        previousLast->next = meta;
        firstMeta->prev = meta;
    }
        //cout  << "sbrk(0) returned" << sbrk(0) << endl;
    if (ptr == (void*)-1) {
        return NULL;
    }
    return ptr;
}

void sfree(void* p) {

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
    return sizeof (MallocMetadata) * allocated_blocks;

}
size_t _size_meta_data() {
    return sizeof (MallocMetadata);
}