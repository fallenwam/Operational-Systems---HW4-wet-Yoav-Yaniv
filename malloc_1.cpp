// Naïve Malloc
#include <iostream>
#include <unistd.h>

const int MAX_SIZE = 100000000;
using namespace std;

void* smalloc(size_t size) {
    if (size <= 0) return NULL;
    if (size > MAX_SIZE) return NULL;
    // sbrk failed - return NULL
    void * ptr  = nullptr;
    try {
        ptr = sbrk(size); //pointer to the first allocated byte within the allocated block.
        //cout  << "sbrk(0) returned" << sbrk(0) << endl;
    }
    catch (...) {
        return NULL;
    }
    if (ptr == (void*)-1) {
        return NULL;
    }
    return ptr;
}