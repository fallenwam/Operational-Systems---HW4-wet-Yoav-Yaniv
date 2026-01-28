// Basic Malloc + free
#include <iostream>
#include <unistd.h>
#include <cstring>

const int MAX_SIZE = 100000000;

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

MallocMetadata* firstMeta = nullptr;

void* smalloc(size_t size){
    if (size <= 0) return NULL;
    if (size > MAX_SIZE) return NULL;
    MallocMetadata * ptr  = firstMeta;

//	Searches for a free block with at least
// ‘size’ bytes or
    do  {
        if ( ptr !=nullptr &&
            ptr->is_free == true &&
            ptr->size >= size) {
                ptr->is_free = false;
                free_blocks--;
                free_bytes -= ptr->size;
                return ptr + sizeof(MallocMetadata);
        }
        else {
            ptr = ptr->next;
        }
    } while (ptr != firstMeta);

//  allocates (sbrk()) -- none are found.
    MallocMetadata* meta = (MallocMetadata*) sbrk( sizeof(MallocMetadata) + size ) ;
	if(meta == (void*)-1 ) return NULL;

    ptr = meta + sizeof(MallocMetadata); // pointer to the first allocated byte within the allocated block.

    meta.size = size;
    allocated_blocks++;
    allocated_bytes += meta.size;
    if (firstMeta == nullptr) {
        firstMeta = meta;
        meta.next = meta+sizeof(MallocMetadata);
        meta.prev = meta+sizeof(MallocMetadata);
    }
    else {

        MallocMetadata* previousLast = firstMeta->prev;
        //firstMeta = meta;
        meta->next = firstMeta;
        meta->prev = previousLast;
        previousLast->next = meta;
        firstMeta->prev = meta;
    }
    return ptr;
}
void* scalloc(size_t num, size_t size){
if(num<= 0 || size <=0 ||  size >= MAX_SIZE ||
 num*size>=MAX_SIZE ) {return NULL; }

    void* ptr = smalloc(num*size);
	if (ptr == NULL) {return NULL; }
    //set to 0'z
    memset(ptr, 0, num*size);

    return ptr;
}


void sfree(void* p) {
    if (p==nullptr || p<= (void*) sizeof(MallocMetadata) ) return NULL;
    MallocMetadata* meta_ptr = p - sizeof(MallocMetadata);
    size_t total_size = meta_ptr->size + sizeof(MallocMetadata);
    meta_ptr->is_free = true;

    free_blocks++;
    free_bytes += meta_ptr->size;
}

void* srealloc(void* oldp, size_t size) {

    if (size <= 0 ||size >= MAX_SIZE ) return NULL;
    if (oldp==nullptr) {
        return smalloc(size);
    }


    MallocMetadata* old_meta_ptr = oldp - sizeof(MallocMetadata);

    if (size <= old_meta_ptr->size) return oldp;
    void* new_ptr = smalloc(size);
    if (new_ptr == NULL) {return NULL;}


    memmove (new_ptr,oldp,old_meta_ptr->size);

    old_meta_ptr->is_free = true ;

    free_blocks++;
    free_bytes += old_meta_ptr->size;

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
    return sizeof (MallocMetadata) * allocated_blocks;

}
size_t _size_meta_data() {
    return sizeof (MallocMetadata);
}