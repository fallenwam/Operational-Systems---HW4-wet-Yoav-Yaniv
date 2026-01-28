
// Basic Malloc + free
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <math.h>

const int MAX_SIZE = 100000000;
const int MAX_ORDER = 10;

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

struct FreeBlocksArray{
    MallocMetadata* metadata_list[MAX_ORDER + 1]{};

public:
    void init(){
        for (int i = 0; i < 10; ++i) {
            metadata_list[i] = nullptr;
        }
        metadata_list[10] = sbrk(128 * pow(2,MAX_ORDER));
        metadata_list[10]->size = 128 * pow(2,MAX_ORDER);
        metadata_list[10]->is_free = true;
        metadata_list[10]->is_left = true;
        metadata_list[10]->next = nullptr;
        metadata_list[10]->prev = nullptr;

        MallocMetadata* ptr = metadata_list[10];

        for (int i = 0; i < 31; ++i) {
            MallocMetadata* new_metadata = sbrk(128 * pow(2,MAX_ORDER));
            new_metadata->size = 128 * pow(2,MAX_ORDER);
            new_metadata->is_free = true;
            new_metadata->is_left = true;
            new_metadata->next = nullptr;
            ptr->next = new_metadata;
            new_metadata->prev = ptr;
            ptr = ptr->next;
        }
//        ptr->next = metadata_list[10];
//        metadata_list[10]->prev = ptr;

    }

    void insert(int index, MallocMetadata* p){
        MallocMetadata* current = this->metadata_list[index];
        if(current == nullptr){
            this->metadata_list[index] = p;
        }
        else{
            while(current->next <= p){
                current = current->next;
            }

            MallocMetadata* next_elem = current->next;

            if(next_elem != nullptr){
                next_elem->prev = p;
            }
            p->next = next_elem;
            p->prev = current;
            current->next = p;
        }
    }

    void remove(MallocMetadata* ptr){
        MallocMetadata* prev_elem = ptr->prev;
        MallocMetadata* next_elem = ptr->next;

        if(next_elem != nullptr){
            next_elem->prev = prev_elem;
        }
        if(prev_elem != nullptr){
            prev_elem->next = next_elem;
        }
    }

    void merge(int index, MallocMetadata* p){
        if(index == 10){
            return;
        }

        if(p->is_left){
            MallocMetadata* next_in_list = p->next;
            MallocMetadata* next_in_memory = p + p->size;

            if(next_in_list == next_in_memory){
                remove(next_in_memory);
                remove(p);
                p->size *= 2;
                insert(index + 1, p);
                merge(index+1, p);
            }
        } else{
            MallocMetadata* prev_in_list = p->prev;
            MallocMetadata* prev_in_memory = p - p->size;

            if(prev_in_list == prev_in_memory){
                remove(prev_in_memory);
                remove(p);
                p->size *= 2;
                insert(index + 1, p);
                merge(index+1, p);
            }
        }

    }
};

FreeBlocksArray* freeBlocksArray = nullptr;

int find_order(size_t size){
    int i = 0;
    while(size > 128){
        size /= 2;
        i++;
    }
    return i;
}

void* smalloc(size_t size){
    if(freeBlocksArray == nullptr){
        freeBlocksArray->init();
    }
    if (size <= 0) return nullptr;
    if (size > MAX_SIZE) return nullptr;
    int power = find_order(size + sizeof(MallocMetadata));

    for (int i = power; i <= MAX_ORDER; ++i) {
        if(freeBlocksArray->metadata_list[i] == nullptr){
            continue;
        } else{
            MallocMetadata* ptr = freeBlocksArray->metadata_list[i];
            while(ptr != nullptr){
                if(ptr->is_free){
                    ptr->is_free = false;

                    MallocMetadata* prev_elem = ptr->prev;
                    MallocMetadata* next_elem = ptr->next;

                    if(next_elem != nullptr){
                        next_elem->prev = prev_elem;
                    }
                    if(prev_elem != nullptr){
                        prev_elem->next = next_elem;
                    }
                    size_t block_size = ptr->size;
                    for (int j = i; j > power; --j) {
                        block_size /= 2;
                        MallocMetadata* right = ptr + block_size / sizeof (MallocMetadata);
                        right->size = block_size;
                        right->is_free = true;
                        right->is_left = false;
                        freeBlocksArray->insert(j - 1, (MallocMetadata*)right);
                    }
                    return ptr + 1;
                }
                ptr = ptr->next;
            }
        }
    }
    return nullptr;
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
    int order = find_order(meta->size);
    meta->is_free = true;
    freeBlocksArray->insert(order,meta);
    freeBlocksArray->merge(order, meta);

    free_blocks++;
    free_bytes += meta->size;
}

void* srealloc(void* oldp, size_t size) {

    if (size <= 0 ||size >= MAX_SIZE ) return nullptr;
    if (oldp==nullptr) {
        return smalloc(size);
    }

    MallocMetadata* old_meta_ptr = (MallocMetadata*) oldp - 1;

    if (size <= old_meta_ptr->size) return oldp;
    void* new_ptr = smalloc(size);
    if (new_ptr == nullptr) {return nullptr;}


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
    return (sizeof (MallocMetadata) * allocated_blocks);

}
size_t _size_meta_data() {
    return sizeof (MallocMetadata);
}
