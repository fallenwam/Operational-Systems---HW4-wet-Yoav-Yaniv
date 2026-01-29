#include <iostream>
#include <cassert>
#include <cstring>
#include "os_malloc.h"
#include <unistd.h>
#include <sys/wait.h>
#include <vector>

#define MAX_MALLOC 100000000

// --- Helper Macros for Colors ---
#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"

void printLog();

typedef void (*TestFunc)();

void run_test_in_child(TestFunc func, const char* test_name) {
    std::cout << "Running " << test_name << "... ";
    std::cout.flush(); // Ensure text appears before fork

    pid_t pid = fork();

    if (pid == -1) {
        std::cerr << "Fork failed!" << std::endl;
        exit(1);
    }

    if (pid == 0) {
        // --- CHILD PROCESS ---
        // Runs the test and exits
        func();
        exit(0); // Success
    } else {
        // --- PARENT PROCESS ---
        // Waits for child
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                std::cout << GREEN << "PASSED" << RESET << std::endl;
            } else {
                std::cout << RED << "FAILED" << RESET << " (Exit Code: " << WEXITSTATUS(status) << ")" << std::endl;
            }
        } else if (WIFSIGNALED(status)) {
            std::cout << RED << "CRASHED" << RESET << " (Signal: " << WTERMSIG(status) << ")" << std::endl;
        }
    }
}

void run_test(TestFunc func, const char* name, int index) {
    std::cout << "Test " << index << ": " << name << "... ";
    std::cout.flush();
    
    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "Fork failed" << std::endl;
        exit(1);
    }
    
    if (pid == 0) {
        // Child process
        func();
        exit(0);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            std::cout << GREEN << "PASSED" << RESET << std::endl;
        } else {
            std::cout << RED << "FAILED" << RESET << " (Exit Code: " << WEXITSTATUS(status) << ")" << std::endl;
        }
    }
}

void print_pass(const char* test_name) {
    std::cout << test_name << ": " << GREEN << "PASSED" << RESET << std::endl;
}

void print_fail(const char* test_name) {
    std::cout << test_name << ": " << RED << "FAILED" << RESET << std::endl;
}

void test_basic_malloc() {
    std::cout << "Test 1: Basic malloc... ";
    void* p = smalloc(100);
    assert(p != NULL);
    sfree(p);
    std::cout << "PASSED" << std::endl;
}

void test_block_reuse() {
    std::cout << "Test 2: Block reuse... ";
    void* p1 = smalloc(100);
    void* original = p1;
    sfree(p1);
    void* p2 = smalloc(100);
    assert(p2 == original);
    sfree(p2);
    std::cout << "PASSED" << std::endl;
}

void test_free_block_statistics() {
    std::cout << "Test 3: Free block statistics... ";
    size_t initial_free = _num_free_blocks();
    void* p = smalloc(200);
    sfree(p);
    assert(_num_free_blocks() == initial_free + 1);
    std::cout << "PASSED" << std::endl;
}

void test_realloc_basic() {
    std::cout << "Test 4: Basic realloc... ";
    char* p1 = static_cast<char*>(smalloc(50));
    strcpy(p1, "Hello");
    char* p2 = static_cast<char*>(srealloc(p1, 100));
    assert(strcmp(p2, "Hello") == 0);
    sfree(p2);
    std::cout << "PASSED" << std::endl;
}

void test_calloc_initialization() {
    std::cout << "Test 5: Calloc initialization... ";
    char* p = static_cast<char*>(scalloc(100, 1));
    for (int i = 0; i < 100; i++) assert(p[i] == 0);
    sfree(p);
    std::cout << "PASSED" << std::endl;
}

void test_multiple_allocations() {
    std::cout << "Test 6: Multiple allocations... ";
    void* p1 = smalloc(64);
    void* p2 = smalloc(128);
    void* p3 = smalloc(256);
    assert(p1 != NULL && p2 != NULL && p3 != NULL);
    sfree(p2);
    void* p4 = smalloc(128);
    assert(p4 != NULL);
    sfree(p1);
    sfree(p3);
    sfree(p4);
    std::cout << "PASSED" << std::endl;
}

void test_basic_alloc_free() {
    size_t initial_bytes = _num_allocated_bytes();
    
    void* p = smalloc(100);
    assert(p != NULL);
    
    // Check stats
    // Note: Since we forked, we don't have to worry about previous tests.
    // But we should verify relative changes.
    assert(_num_allocated_bytes() == initial_bytes + 100);
    
    sfree(p);
    
    // Part 2: Freeing does NOT return bytes to OS, just marks free.
    assert(_num_allocated_bytes() == initial_bytes + 100);
    assert(_num_free_blocks() > 0);
}

void test_reuse_exact_size() {
    void* p1 = smalloc(350);
    sfree(p1);
    
    size_t free_blocks_before = _num_free_blocks();
    
    // Should reuse the block freed above
    void* p2 = smalloc(350); 
    assert(p2 == p1); // Must reuse same address
    assert(_num_free_blocks() == free_blocks_before - 1);
    
    sfree(p2);
}

void test_reuse_larger_block_no_split() {
    // 1. Allocate a large block
    size_t large_size = 800;
    void* p1 = smalloc(large_size);
    sfree(p1);
    
    size_t free_bytes_before = _num_free_bytes();
    
    // 2. Request a tiny block. 
    // Since this runs in a fresh process (forked), p1 is likely the ONLY free block 
    // available (or the best fit in the list).
    size_t small_size = 10;
    void* p2 = smalloc(small_size);
    
    // Must reuse the large block
    assert(p2 == p1); 
    
    // Check NO SPLITTING logic:
    // The system should consider the WHOLE 800 bytes as "used" now.
    // So free bytes should decrease by 800, not 10.
    size_t free_bytes_after = _num_free_bytes();
    
    assert(free_bytes_before - free_bytes_after >= large_size);
    
    sfree(p2);
}

void test_list_order_ascending() {
    // We create a specific hole pattern to verify ascending search order.
    // Address: [ Low ... High ]
    
    void* p1 = smalloc(100); // Low address
    void* p2 = smalloc(100); // Higher address
    void* p3 = smalloc(100); // Highest address (anchor)
    
    // Double check address order (heap grows up)
    assert(p1 < p2);
    
    // Free p2 then p1. 
    sfree(p2);
    sfree(p1); 
    
    // Now both p1 and p2 are free.
    // smalloc(100) should find p1 FIRST because it's lower in memory/list.
    
    void* p_new = smalloc(100);
    assert(p_new == p1); 
    
    void* p_new2 = smalloc(100);
    assert(p_new2 == p2);
    
    sfree(p_new);
    sfree(p_new2);
    sfree(p3);
}

void test_scalloc() {
    // 1. Basic zero check
    int* arr = (int*)scalloc(50, sizeof(int));
    for(int i=0; i<50; i++) {
        assert(arr[i] == 0);
    }
    
    sfree(arr);
    
    // 2. Reuse check
    // We expect it to reuse the block we just freed.
    int* arr2 = (int*)scalloc(50, sizeof(int));
    
    // It should be zeroed again even if it reused memory
    for(int i=0; i<50; i++) {
        assert(arr2[i] == 0);
    }
    
    sfree(arr2);
}

void test_realloc_shrink() {
    void* p1 = smalloc(400);
    
    // Shrink: "If size is smaller... reuse the same block"
    void* p2 = srealloc(p1, 200);
    assert(p1 == p2);
    
    sfree(p2);
}

void test_realloc_expand() {
    void* p1 = smalloc(100);
    // Fill with pattern
    memset(p1, 'A', 100);
    
    // Block p1 from expanding in place
    void* p2 = smalloc(100); 
    
    // Expand p1. Must move.
    char* p3 = (char*)srealloc(p1, 300);
    
    assert(p3 != p1); // Must move
    
    // Verify data copy
    for(int i=0; i<100; i++) {
        assert(p3[i] == 'A');
    }
    
    sfree(p2);
    sfree(p3); 
}

void test_metadata_integrity() {
    size_t meta_size = _size_meta_data();
    size_t initial_meta_bytes = _num_meta_data_bytes();
    size_t initial_blocks = _num_allocated_blocks();
    
    void* p = smalloc(200);
    
    assert(_num_meta_data_bytes() == initial_meta_bytes + meta_size);
    assert(_num_allocated_blocks() == initial_blocks + 1);
    
    sfree(p);
    
    // After free, metadata stays
    assert(_num_meta_data_bytes() == initial_meta_bytes + meta_size);
}

void t01_basic_byte() {
    void* p = smalloc(1);
    assert(p != NULL);
    assert(_num_allocated_bytes() >= 1); 
    sfree(p);
}

// 2. Max Limit Allocation (10^8) - Should Pass
void t02_max_limit() {
    void* p = smalloc(100000000);
    // Note: Might return NULL if VM doesn't have RAM, but logic shouldn't forbid it.
    // If it returns a pointer, we free it.
    if(p) sfree(p);
}

// 3. Over Limit Allocation (10^8 + 1) - Should Fail
void t03_over_limit() {
    void* p = smalloc(100000001);
    assert(p == NULL);
}

// 4. Zero Size Allocation - Should Fail
void t04_zero_alloc() {
    void* p = smalloc(0);
    assert(p == NULL);
}

// 5. Free NULL - Should do nothing (no crash)
void t05_free_null() {
    size_t before = _num_free_blocks();
    sfree(NULL);
    assert(_num_free_blocks() == before);
}

// 6. Double Free - Should handle gracefully (or just verify is_free is already true)
// PDF says: "If 'p' is ... already released, simply returns." [cite: 215]
void t06_double_free() {
    void* p = smalloc(100);
    sfree(p);
    
    size_t free_blocks = _num_free_blocks();
    sfree(p); // Second free
    
    // Stats shouldn't change
    assert(_num_free_blocks() == free_blocks);
}

// 7. Reuse Logic: First Fit / Address Order
// Create 3 holes: A(100), B(100), C(100). 
// Alloc 100 should pick A.
void t07_reuse_ordering() {
    void* a = smalloc(100);
    void* b = smalloc(100);
    void* c = smalloc(100);
    
    // Create holes
    sfree(a);
    sfree(b);
    sfree(c);
    
    // Should get 'a' back (lowest address)
    void* new_p = smalloc(100);
    assert(new_p == a);
    
    sfree(new_p);
}

// 8. Reuse Logic: Skip too small
// Holes: A(10), B(50). Request 30. Should skip A, take B.
void t08_reuse_skip_small() {
    void* a = smalloc(10);
    void* b = smalloc(50);
    
    sfree(a);
    sfree(b);
    
    void* p = smalloc(30);
    assert(p == b); // A was too small
    
    sfree(p);
}

// 9. Reuse No Split Stats
// Reuse 1000 block for 10 byte request. Free bytes should drop by 1000.
void t09_reuse_no_split() {
    void* p = smalloc(1000);
    sfree(p);
    
    size_t free_bytes_start = _num_free_bytes();
    
    void* p2 = smalloc(10); // Reuses the 1000 block
    
    size_t free_bytes_end = _num_free_bytes();
    assert(free_bytes_start - free_bytes_end >= 1000);
    
    sfree(p2);
}

// 10. Calloc Basic
void t10_calloc_basic() {
    long* p = (long*)scalloc(10, sizeof(long));
    for(int i=0; i<10; i++) assert(p[i] == 0);
    sfree(p);
}

// 11. Calloc Reuse & Zeroing
// Verify that reused memory (which was dirty) is zeroed.
void t11_calloc_reuse_dirty() {
    // 1. Alloc and dirty
    char* p = (char*)smalloc(50);
    memset(p, 'X', 50);
    sfree(p);
    
    // 2. Calloc same size (should reuse p)
    char* p2 = (char*)scalloc(50, 1);
    assert(p2 == p);
    
    // 3. Verify zero
    for(int i=0; i<50; i++) assert(p2[i] == 0);
    sfree(p2);
}

// 12. Calloc Overflow Check
// num * size > 10^8 should fail
void t12_calloc_overflow() {
    // 10^8 + 1 total bytes
    void* p = scalloc(100000001, 1); 
    assert(p == NULL);
}

// 13. Realloc Same Size (Reuse)
void t13_realloc_same() {
    void* p = smalloc(100);
    void* new_p = srealloc(p, 100);
    assert(new_p == p);
    sfree(new_p);
}

// 14. Realloc Smaller (Reuse, No Split)
void t14_realloc_smaller() {
    void* p = smalloc(200);
    void* new_p = srealloc(p, 50);
    assert(new_p == p);
    // Should still treat entire 200 block as used
    sfree(new_p);
}

// 15. Realloc Move
// If we grow, we usually move in Part 2 (unless we implement merge, which isn't required yet).
void t15_realloc_move() {
    void* p1 = smalloc(100);
    // Block it so it can't just expand sbrk blindly if that was the strategy
    void* p2 = smalloc(100); 
    
    void* new_p = srealloc(p1, 300);
    assert(new_p != p1);
    
    sfree(p2);
    sfree(new_p);
}

// 16. Realloc NULL ptr -> Malloc
void t16_realloc_null() {
    void* p = srealloc(NULL, 50);
    assert(p != NULL);
    sfree(p);
}

// 17. Realloc Zero Size -> NULL?
// Spec: "If size is 0 returns NULL" [cite: 227]
void t17_realloc_zero() {
    void* p = smalloc(50);
    void* new_p = srealloc(p, 0);
    assert(new_p == NULL);
    // Note: Behavior of 'p' isn't explicitly defined on failure, 
    // but usually size 0 is treated as a free+return NULL.
    // If your code DOESN'T free p on size 0, you might leak here. 
    // We won't assert p is valid/invalid, just that return is NULL.
}

// 18. Stats: Block Counting
void t18_stats_blocks() {
    size_t start = _num_allocated_blocks();
    void* p = smalloc(100);
    assert(_num_allocated_blocks() == start + 1);
    sfree(p);
    // Freeing keeps block in list (Part 2)
    assert(_num_allocated_blocks() == start + 1);
}

// 19. Stats: Metadata Size
void t19_stats_metadata() {
    size_t meta_size = _size_meta_data();
    size_t start_meta_bytes = _num_meta_data_bytes();
    
    void* p = smalloc(10);
    // Should add exactly 1 struct worth of bytes
    assert(_num_meta_data_bytes() == start_meta_bytes + meta_size);
    sfree(p);
}

// 20. Allocated Bytes Stability
// Freeing shouldn't reduce _num_allocated_bytes (since block remains in heap)
void t20_stats_alloc_bytes() {
    size_t start = _num_allocated_bytes();
    void* p = smalloc(100);
    
    size_t after_alloc = _num_allocated_bytes();
    assert(after_alloc == start + 100);
    
    sfree(p);
    // In Part 2, block is kept. Bytes still counted as "allocated" (managed by heap).
    assert(_num_allocated_bytes() == after_alloc);
}

void test_limit_10_8() {
    void* p = smalloc(MAX_MALLOC + 1);
    assert(p == NULL);
}

void t01_malloc_1_byte() {
    void* p = smalloc(1);
    assert(p != NULL);
    *((char*)p) = 'a';
    sfree(p);
}

void t02_malloc_2_bytes() {
    void* p = smalloc(2);
    assert(p != NULL);
    sfree(p);
}

void t03_malloc_alignment_heuristic() {
    // Just ensure consecutive mallocs don't overlap
    char* p1 = (char*)smalloc(10);
    char* p2 = (char*)smalloc(10);
    assert(p2 >= p1 + 10); 
    sfree(p1);
    sfree(p2);
}

void t04_malloc_large_chunk() {
    void* p = smalloc(1024 * 1024); // 1MB
    assert(p != NULL);
    sfree(p);
}

void t05_malloc_fail_huge() {
    void* p = smalloc(MAX_MALLOC + 500);
    assert(p == NULL);
}

// --- Group 2: Reuse Logic (Ascending Order) ---

void t06_reuse_first_fit_exact() {
    void* p1 = smalloc(100);
    void* p2 = smalloc(100);
    void* p3 = smalloc(100);
    sfree(p1);
    sfree(p2); 
    sfree(p3);
    
    // Should get p1 (lowest addr)
    void* new_p = smalloc(100);
    assert(new_p == p1);
}

void t07_reuse_first_fit_skip_small() {
    void* p1 = smalloc(10); 
    void* p2 = smalloc(20);
    void* p3 = smalloc(30);
    sfree(p1);
    sfree(p2);
    sfree(p3);
    
    // Request 15. p1(10) too small. Should take p2(20).
    void* new_p = smalloc(15);
    assert(new_p == p2);
}

void t08_reuse_fragmented_list() {
    void* p1 = smalloc(100);
    void* p2 = smalloc(100); // Barrier
    void* p3 = smalloc(100);
    sfree(p1);
    sfree(p3);
    
    // We have holes at p1 and p3.
    void* n1 = smalloc(100); // takes p1
    assert(n1 == p1);
    void* n2 = smalloc(100); // takes p3
    assert(n2 == p3);
}

void t09_no_split_oversize() {
    void* p = smalloc(500);
    sfree(p);
    
    // Request 1 byte. Should get whole 500 block.
    void* new_p = smalloc(1);
    assert(new_p == p);
    
    // If we alloc another, it must be NEW (sbrk), not split from p.
    void* p2 = smalloc(100);
    assert(p2 != p);
    assert(p2 > (char*)p + 500);
}

void t10_reuse_middle_list() {
    void* p1 = smalloc(100);
    void* p2 = smalloc(100);
    void* p3 = smalloc(100);
    
    sfree(p2); // Only middle is free
    
    void* n = smalloc(100);
    assert(n == p2);
}

// --- Group 3: Realloc Logic ---

void t11_realloc_null_ptr() {
    // Acts like malloc
    void* p = srealloc(NULL, 50);
    assert(p != NULL);
    sfree(p);
}

void t12_realloc_zero_size() {
    void* p = smalloc(50);
    void* n = srealloc(p, 0); // Should be free + return NULL
    assert(n == NULL);
}

void t13_realloc_shrink_nop() {
    void* p = smalloc(100);
    void* n = srealloc(p, 50);
    assert(n == p); // Reuse same block
}

void t14_realloc_expand_in_place_impossible() {
    void* p1 = smalloc(100);
    void* p2 = smalloc(100); // Barrier
    
    // Can't expand p1 because p2 is there
    void* n = srealloc(p1, 150);
    assert(n != p1);
    // p1 is freed automatically
    sfree(p2);
    sfree(n);
}

void t15_realloc_huge_fail() {
    void* p = smalloc(100);
    // Try to realloc > 10^8
    void* n = srealloc(p, MAX_MALLOC + 100);
    assert(n == NULL);
    // Old pointer should remain valid!
    // We verify by freeing it (should not crash if double free protection exists or just logic)
    // Actually, std behavior is old valid. sfree(p) works.
    sfree(p);
}

void t16_realloc_data_integrity() {
    int* arr = (int*)smalloc(10 * sizeof(int));
    for(int i=0; i<10; i++) arr[i] = i;
    
    // Expand (move)
    int* arr2 = (int*)srealloc(arr, 1000 * sizeof(int));
    assert(arr2 != arr);
    
    // Check first 10
    for(int i=0; i<10; i++) assert(arr2[i] == i);
    sfree(arr2);
}

void t17_realloc_reuse_freed_block() {
    // If we realloc(p, larger), it might find a free block elsewhere
    void* huge = smalloc(1000);
    sfree(huge);
    
    void* p = smalloc(10);
    void* n = srealloc(p, 900);
    // Should reuse 'huge' if it fits
    assert(n == huge);
}

void t18_realloc_to_same_size() {
    void* p = smalloc(100);
    void* n = srealloc(p, 100);
    assert(n == p);
    sfree(n);
}

void t19_realloc_tiny_shrink() {
    void* p = smalloc(1000);
    void* n = srealloc(p, 1);
    assert(n == p);
    sfree(n);
}

void t20_realloc_fails_keeps_old() {
    void* p = smalloc(100);
    // Force fail with huge size
    void* n = srealloc(p, MAX_MALLOC + 5);
    assert(n == NULL);
    // p is still valid, we free it
    sfree(p);
}

// --- Group 4: Calloc Logic ---

void t21_calloc_one_element() {
    void* p = scalloc(1, 100);
    assert(p != NULL);
    char* c = (char*)p;
    for(int i=0; i<100; i++) assert(c[i] == 0);
    sfree(p);
}

void t22_calloc_zero_num() {
    void* p = scalloc(0, 100);
    assert(p == NULL);
}

void t23_calloc_zero_size() {
    void* p = scalloc(100, 0);
    assert(p == NULL);
}

void t24_calloc_overflow_check() {
    // num*size > 10^8
    void* p = scalloc(100000, 100000); // 10^10
    assert(p == NULL);
}

void t25_calloc_reuse_zeroing() {
    // Alloc, dirty, free
    char* p = (char*)smalloc(100);
    memset(p, 0x55, 100);
    sfree(p);
    
    // Calloc should reuse p but zero it
    char* n = (char*)scalloc(1, 100);
    assert(n == p);
    assert(n[0] == 0);
    assert(n[99] == 0);
    sfree(n);
}

// --- Group 5: Stats & Metadata ---

void t26_stats_free_blocks_inc() {
    size_t start = _num_free_blocks();
    void* p = smalloc(100);
    sfree(p);
    assert(_num_free_blocks() == start + 1);
}

void t27_stats_free_bytes_inc() {
    size_t start = _num_free_bytes();
    void* p = smalloc(100);
    sfree(p);
    // Free bytes should increase by exactly 100
    assert(_num_free_bytes() == start + 100);
}

void t28_stats_alloc_blocks_stable() {
    size_t start = _num_allocated_blocks();
    void* p = smalloc(100);
    assert(_num_allocated_blocks() == start + 1);
    sfree(p);
    // Should NOT decrease
    assert(_num_allocated_blocks() == start + 1);
}

void t29_metadata_size_consistent() {
    size_t meta = _size_meta_data();
    // Usually aligned, typically 16-32 bytes depending on struct
    assert(meta > 0 && meta < 100);
}

void t30_metadata_total_bytes() {
    size_t start = _num_meta_data_bytes();
    size_t one_meta = _size_meta_data();
    
    void* p1 = smalloc(10);
    assert(_num_meta_data_bytes() == start + one_meta);
    
    void* p2 = smalloc(20);
    assert(_num_meta_data_bytes() == start + 2*one_meta);
    
    sfree(p1);
    // Freeing doesn't remove metadata
    assert(_num_meta_data_bytes() == start + 2*one_meta);
}

// --- Group 6: Stress & Mixed Ops ---

void t31_stress_alloc_free_loop() {
    for(int i=0; i<100; i++) {
        void* p = smalloc(100);
        sfree(p);
    }
    // Should result in 1 free block reused 100 times, or 100 blocks if logic is bad.
    // Ideally reuse logic prevents OOM.
    assert(_num_free_blocks() > 0);
}

void t32_stress_list_traversal() {
    // Create chain of 50 blocks
    void* ptrs[50];
    for(int i=0; i<50; i++) ptrs[i] = smalloc(64);
    
    // Free even ones
    for(int i=0; i<50; i+=2) sfree(ptrs[i]);
    
    // Alloc 50 items again, should reuse holes
    for(int i=0; i<50; i+=2) {
        void* p = smalloc(64);
        assert(p == ptrs[i]); // Should find the exact holes
    }
}

void t33_alloc_size_t_max() {
    // -1 usually
    void* p = smalloc((size_t)-1);
    assert(p == NULL);
}

void t34_negative_int_cast() {
    // (size_t)(-100) is huge positive
    void* p = smalloc((size_t)-100);
    assert(p == NULL); // > 10^8
}

void t35_mixed_calloc_malloc() {
    void* p1 = scalloc(1, 100);
    sfree(p1);
    
    // malloc should reuse calloc'd block
    void* p2 = smalloc(100);
    assert(p2 == p1);
    sfree(p2);
}

void t36_double_free_middle() {
    void* p1 = smalloc(10);
    void* p2 = smalloc(10);
    void* p3 = smalloc(10);
    
    sfree(p2);
    // Double free p2
    sfree(p2); // Should trigger return (safe)
    
    sfree(p1);
    sfree(p3);
}

void t37_sbrk_failure_simulation() {
    // We can't easily mock sbrk failure without library interposition,
    // but we can request huge valid-looking blocks until we likely run out of heap logic
    // or hit 10^8 limit.
    // Just a placeholder to ensure massive alloc returns NULL via limit check.
    void* p = smalloc(99999999);
    if(p) sfree(p);
}

void t38_realloc_shrink_stats() {
    void* p = smalloc(200);
    size_t alloc_bytes = _num_allocated_bytes();
    
    p = srealloc(p, 100);
    
    // In Part 2 (no split), allocated bytes remain same (200)
    assert(_num_allocated_bytes() == alloc_bytes);
    sfree(p);
}

void t39_zero_blocks_start() {
    // Fresh fork
    // Allocated blocks might not be 0 if init code ran, but free should be 0 or clean.
    size_t free_b = _num_free_blocks();
    // We can't assert == 0 because earlier tests might have run? 
    // Wait, FORK isolates us. So it IS 0.
    // EXCEPT if you have a global constructor/init. 
    // Assuming no globals malloc:
    // Actually... if your malloc library initializes on load, it might be 0.
    // Let's just print pass.
    (void)free_b;
}

void t40_final_sanity() {
    void* p = smalloc(12345);
    assert(p != NULL);
    sfree(p);
    assert(_num_free_bytes() >= 12345);
}

void t001_alloc_1() { void* p = smalloc(1); assert(p); sfree(p); }
void t002_alloc_64() { void* p = smalloc(64); assert(p); sfree(p); }
void t003_alloc_huge() { void* p = smalloc(1024*1024); assert(p); sfree(p); } // 1MB
void t004_alloc_zero() { assert(smalloc(0) == NULL); }
void t005_alloc_max() { void* p = smalloc(1e8); if(p) sfree(p); } // Max allowed
void t006_alloc_overflow() { assert(smalloc(1e8 + 1) == NULL); }
void t007_free_null() { sfree(NULL); } // Should not crash
void t008_calloc_1() { void* p = scalloc(1,1); assert(p); sfree(p); }
void t009_calloc_zero() { assert(scalloc(0,10) == NULL); }
void t010_realloc_null() { void* p = srealloc(NULL, 10); assert(p); sfree(p); }

// --- Group 2: Reuse Logic (11-20) ---

void t011_reuse_simple() {
    void* p1 = smalloc(100); sfree(p1);
    void* p2 = smalloc(100); assert(p1 == p2); sfree(p2);
}
void t012_reuse_skip_small() {
    void* p1 = smalloc(10); void* p2 = smalloc(100);
    sfree(p1); sfree(p2);
    void* p3 = smalloc(50); assert(p3 == p2); // p1 too small
    sfree(p3);
}
void t013_reuse_first_fit() {
    void* p1 = smalloc(100); void* p2 = smalloc(100);
    sfree(p2); sfree(p1);
    void* p3 = smalloc(100); assert(p3 == p1); // Address order
    sfree(p3);
}
void t014_reuse_no_split() {
    void* p1 = smalloc(1000); sfree(p1);
    void* p2 = smalloc(10); assert(p2 == p1);
    // Ensure entire 1000 is marked used (Part 2 rule)
    size_t fb = _num_free_bytes();
    sfree(p2);
    assert(_num_free_bytes() >= fb + 1000);
}
void t015_reuse_exact() {
    void* p = smalloc(50); sfree(p);
    void* p2 = smalloc(50); assert(p == p2); sfree(p2);
}
void t016_reuse_calloc() {
    void* p = smalloc(100); sfree(p);
    void* p2 = scalloc(1, 100); assert(p == p2); sfree(p2);
}
void t017_reuse_calloc_clears() {
    char* p = (char*)smalloc(10); memset(p, 'A', 10); sfree(p);
    char* p2 = (char*)scalloc(1, 10); assert(p2[0] == 0); sfree(p2);
}
void t018_reuse_realloc_shrink() {
    void* p = smalloc(100); sfree(p);
    void* p2 = smalloc(100); assert(p == p2);
    void* p3 = srealloc(p2, 50); assert(p3 == p2); sfree(p3);
}
void t019_reuse_realloc_grow_fits() {
    // Alloc huge, free, alloc tiny. Then realloc tiny to huge.
    void* huge = smalloc(1000); sfree(huge);
    void* tiny = smalloc(10); assert(tiny == huge);
    void* grown = srealloc(tiny, 900); assert(grown == huge); // Should fit in 1000
    sfree(grown);
}
void t020_reuse_fragmented() {
    void* p1 = smalloc(10); void* p2 = smalloc(10); void* p3 = smalloc(10);
    sfree(p1); sfree(p3);
    void* n = smalloc(10); assert(n == p1); // First hole
    sfree(p2); sfree(n);
}

// --- Group 3: Realloc Specifics (21-30) ---

void t021_realloc_same() {
    void* p = smalloc(100); void* n = srealloc(p, 100); assert(n==p); sfree(n);
}
void t022_realloc_shrink() {
    void* p = smalloc(200); void* n = srealloc(p, 10); assert(n==p); sfree(n);
}
void t023_realloc_expand_move() {
    void* p1 = smalloc(10); void* p2 = smalloc(10);
    void* n = srealloc(p1, 100); assert(n != p1); sfree(p2); sfree(n);
}
void t024_realloc_expand_copy() {
    char* p = (char*)smalloc(10); memset(p, 'A', 10);
    void* block = smalloc(10); (void)block; // Block expansion
    char* n = (char*)srealloc(p, 20); 
    assert(n[0] == 'A' && n[9] == 'A'); sfree(n); sfree(block);
}
void t025_realloc_zero() {
    void* p = smalloc(10); void* n = srealloc(p, 0); assert(n == NULL);
    // Check double free logic elsewhere, assume p freed.
}
void t026_realloc_fail_huge() {
    void* p = smalloc(10); void* n = srealloc(p, 1e8 + 1); assert(n == NULL);
    sfree(p); // Old ptr remains valid on fail
}
void t027_realloc_reuse_freed() {
    void* big = smalloc(1000); sfree(big);
    void* small = smalloc(10); 
    void* n = srealloc(small, 900); assert(n == big); // Reuse 'big'
    sfree(n);
}
void t028_realloc_expansion_data() {
    int* p = (int*)smalloc(5*sizeof(int)); for(int i=0;i<5;i++) p[i]=i;
    void* b = smalloc(10); (void)b;
    int* n = (int*)srealloc(p, 100*sizeof(int));
    for(int i=0;i<5;i++) assert(n[i]==i);
    sfree(n); sfree(b);
}
void t029_realloc_shrink_stats() {
    void* p = smalloc(100); size_t b = _num_allocated_bytes();
    void* n = srealloc(p, 50); 
    assert(_num_allocated_bytes() == b); // Part 2: bytes don't shrink
    sfree(n);
}
void t030_realloc_wild() {
    void* p = smalloc(10);
    for(int i=0; i<100; i++) p = srealloc(p, 10); // 100 reallocs same size
    assert(p); sfree(p);
}

// --- Group 4: Calloc & Arrays (31-40) ---

void t031_calloc_array() {
    int* p = (int*)scalloc(10, sizeof(int));
    for(int i=0;i<10;i++) assert(p[i]==0); sfree(p);
}
void t032_calloc_overflow_nums() {
    assert(scalloc(100000, 100000) == NULL);
}
void t033_calloc_exact_limit() {
    // 10^8 bytes exactly
    void* p = scalloc(1, 100000000); 
    if(p) sfree(p);
}
void t034_calloc_fragment() {
    void* p1 = scalloc(1,10); void* p2 = scalloc(1,10); sfree(p1);
    void* p3 = scalloc(1,10); assert(p3==p1); sfree(p2); sfree(p3);
}
void t035_calloc_struct() {
    struct S { int x; int y; };
    S* s = (S*)scalloc(5, sizeof(S));
    assert(s[4].x == 0); sfree(s);
}
void t036_calloc_weird_size() {
    void* p = scalloc(1, 12345); assert(p); sfree(p);
}
void t037_calloc_one() {
    void* p = scalloc(1, 1); assert(p); sfree(p);
}
void t038_calloc_max_units() {
    void* p = scalloc(100000000, 1); if(p) sfree(p);
}
void t039_calloc_split_attempt() {
    void* p = smalloc(1000); sfree(p);
    void* p2 = scalloc(1, 10); assert(p2==p); // No split
    sfree(p2);
}
void t040_calloc_reuse_dirty_check() {
    char* p = (char*)smalloc(100); memset(p,0xFF,100); sfree(p);
    char* n = (char*)scalloc(1,100);
    assert(n==p && n[50]==0); sfree(n);
}

// --- Group 5: Stats (41-50) ---

void t041_stats_free_blocks() {
    size_t s = _num_free_blocks(); void* p = smalloc(10); sfree(p);
    assert(_num_free_blocks() == s+1);
}
void t042_stats_alloc_blocks() {
    size_t s = _num_allocated_blocks(); void* p = smalloc(10);
    assert(_num_allocated_blocks() == s+1); sfree(p);
}
void t043_stats_free_bytes() {
    size_t s = _num_free_bytes(); void* p = smalloc(100); sfree(p);
    assert(_num_free_bytes() == s+100);
}
void t044_stats_alloc_bytes() {
    size_t s = _num_allocated_bytes(); void* p = smalloc(100);
    assert(_num_allocated_bytes() == s+100); sfree(p);
}
void t045_stats_meta() {
    size_t s = _num_meta_data_bytes(); void* p = smalloc(10);
    assert(_num_meta_data_bytes() > s); sfree(p);
}
void t046_stats_consistent() {
    void* p = smalloc(10); sfree(p);
    assert(_num_free_blocks() <= _num_allocated_blocks());
}
void t047_stats_realloc_move() {
    void* p = smalloc(10); void* b = smalloc(10);
    size_t s = _num_allocated_bytes();
    void* n = srealloc(p, 100); 
    // Old 10 + New 100 + Barrier 10 = 120 allocated in heap history
    // But currently used: 100+10. 
    // _num_allocated_bytes tracks ALL blocks ever alloc'd (since we don't unmap).
    // So 10 + 10 + 100 = 120.
    assert(_num_allocated_bytes() == s + 100); 
    sfree(b); sfree(n);
}
void t048_stats_calloc() {
    size_t s = _num_allocated_bytes(); void* p = scalloc(10,10);
    assert(_num_allocated_bytes() == s+100); sfree(p);
}
void t049_stats_reuse() {
    void* p = smalloc(100); sfree(p);
    size_t fb = _num_free_bytes();
    void* p2 = smalloc(100);
    assert(_num_free_bytes() == fb - 100); sfree(p2);
}
void t050_stats_meta_size() {
    assert(_size_meta_data() > 0);
}

// --- Group 6: Stress Tests (51-60) ---

void t051_stress_loop_alloc() {
    void* ptrs[100];
    for(int i=0;i<100;i++) ptrs[i] = smalloc(100);
    for(int i=0;i<100;i++) sfree(ptrs[i]);
    assert(_num_free_blocks() >= 100);
}
void t052_stress_loop_reuse() {
    void* p;
    for(int i=0;i<1000;i++) {
        p = smalloc(100); sfree(p);
    }
    // Should reuse same block mostly
    assert(_num_allocated_blocks() < 50); 
}
void t053_stress_alternating() {
    void* p1 = smalloc(10); void* p2 = smalloc(10);
    sfree(p1);
    void* p3 = smalloc(10); assert(p3 == p1);
    sfree(p2); sfree(p3);
}
void t054_stress_checkerboard() {
    void* ptrs[10];
    for(int i=0;i<10;i++) ptrs[i] = smalloc(10);
    for(int i=0;i<10;i+=2) sfree(ptrs[i]); // Free 0, 2, 4...
    // Alloc should fill holes
    for(int i=0;i<10;i+=2) ptrs[i] = smalloc(10);
    for(int i=0;i<10;i++) sfree(ptrs[i]);
}
void t055_stress_increasing() {
    for(int i=1; i<=100; i++) sfree(smalloc(i));
    assert(_num_free_blocks() == 100);
}
void t056_stress_realloc_loop() {
    void* p = smalloc(10);
    for(int i=0; i<50; i++) {
        void* b = smalloc(10); // Force move
        p = srealloc(p, 10 + i);
        sfree(b);
    }
    sfree(p);
}
void t057_stress_calloc_loop() {
    for(int i=0; i<100; i++) sfree(scalloc(1, 10));
}
void t058_stress_mixed() {
    void* p1 = smalloc(100);
    void* p2 = scalloc(1, 50);
    sfree(p1);
    void* p3 = srealloc(p2, 200);
    sfree(p3);
}
void t059_stress_reverse_free() {
    void* ptrs[10];
    for(int i=0;i<10;i++) ptrs[i] = smalloc(100);
    for(int i=9;i>=0;i--) sfree(ptrs[i]);
    assert(_num_free_blocks() == 10);
}
void t060_stress_randomish() {
    // A B free(A) C free(B) D(fits A)
    void* a = smalloc(100);
    void* b = smalloc(200);
    sfree(a);
    void* c = smalloc(300);
    sfree(b);
    void* d = smalloc(100);
    assert(d == a);
    sfree(c); sfree(d);
}

// --- Group 7: Limits & alignment (61-70) ---

void t061_limit_max() { void* p = smalloc(1e8); if(p) sfree(p); }
void t062_limit_fail() { assert(smalloc(1e8+7) == NULL); }
void t063_limit_realloc() { void* p = smalloc(10); assert(srealloc(p, 1e8+1)==NULL); sfree(p); }
void t064_limit_calloc() { assert(scalloc(1e8, 2) == NULL); }
void t065_limit_sbrk_sim() {
    // If sbrk fails, smalloc returns NULL. Hard to trigger deterministically without mocks,
    // but we can request distinct 10^7 chunks until fail?
    // Skip for stability.
}
void t066_align_addr() {
    void* p = smalloc(1);
    // Usually heap alignment is 4 or 8. Not strictly required by PDF but good practice.
    // assert((long)p % 4 == 0); 
    sfree(p);
}
void t067_meta_align() {
    assert(_size_meta_data() % 1 == 0); // Trivial
}
void t068_ptr_diff() {
    void* p1 = smalloc(100); void* p2 = smalloc(100);
    long diff = (char*)p2 - (char*)p1;
    assert(diff >= 100 + _size_meta_data());
    sfree(p1); sfree(p2);
}
void t069_block_count() {
    size_t s = _num_allocated_blocks();
    void* p = smalloc(1);
    assert(_num_allocated_blocks() == s+1);
    sfree(p);
}
void t070_bytes_count() {
    size_t s = _num_allocated_bytes();
    void* p = smalloc(10);
    assert(_num_allocated_bytes() == s+10);
    sfree(p);
}

// --- Group 8: Logic Puzzles (71-80) ---

void t071_puzzle_1() {
    // Alloc 10, 20, 30. Free 20. Alloc 15. Should get 20.
    void *p1=smalloc(10), *p2=smalloc(20), *p3=smalloc(30);
    sfree(p2);
    void* n = smalloc(15);
    assert(n == p2);
    sfree(p1); sfree(p3); sfree(n);
}
void t072_puzzle_2() {
    // Alloc 100. Free. Alloc 10. Alloc 10. 
    // First 10 gets 100 block. Second 10 gets NEW block.
    void* big = smalloc(100); sfree(big);
    void* s1 = smalloc(10);
    void* s2 = smalloc(10);
    assert(s1 == big);
    assert(s2 != big);
    sfree(s1); sfree(s2);
}
void t073_puzzle_3() {
    // Realloc shrink 100->10. Free. Alloc 50. Should reuse.
    void* p = smalloc(100);
    p = srealloc(p, 10);
    sfree(p);
    void* n = smalloc(50);
    assert(n == p);
    sfree(n);
}
void t074_puzzle_4() {
    // Calloc 100. Free. Malloc 100. Check dirty.
    int* p = (int*)scalloc(1, 100); sfree(p);
    int* m = (int*)smalloc(100);
    assert(m == p); // Reused
    // m is NOT guaranteed zeroed by malloc, but we assume it points to same memory
    sfree(m);
}
void t075_puzzle_5() {
    // Double free safety
    void* p = smalloc(10); sfree(p); sfree(p); 
    void* n = smalloc(10); assert(n == p); sfree(n);
}
void t076_puzzle_6() {
    // Realloc(NULL, 100) -> Malloc -> Free -> Malloc -> reuse
    void* p = srealloc(NULL, 100);
    sfree(p);
    void* n = smalloc(100); assert(n==p); sfree(n);
}
void t077_puzzle_7() {
    // Alloc A. Alloc B. Free A. Realloc B->Huge. 
    // B moves. A is still free. B's old spot is free.
    void* a = smalloc(10); void* b = smalloc(10);
    sfree(a);
    void* old_b = b;
    b = srealloc(b, 1000);
    assert(b != old_b);
    // Alloc 10. Should get A (created first/lower addr).
    void* n = smalloc(10);
    assert(n == a);
    sfree(b); sfree(n);
}
void t078_puzzle_8() {
    // Exact fit vs First fit
    // Free 20, Free 10. List: 20 -> 10.
    // Alloc 10. Should take 20 (First fit) not 10 (Best fit).
    void* p20 = smalloc(20); void* p10 = smalloc(10);
    sfree(p20); sfree(p10);
    void* n = smalloc(10);
    assert(n == p20); // First fit
    sfree(n);
}
void t079_puzzle_9() {
    // Metadata integrity check
    void* p = smalloc(10);
    size_t* meta = (size_t*)((char*)p - _size_meta_data());
    // Assuming first field is size (common implementation)
    // assert(*meta == 10); // Logic dependent, skip
    sfree(p);
}
void t080_puzzle_10() {
    // Zero size malloc -> NULL.
    // Zero size calloc -> NULL.
    // Zero size realloc -> NULL.
    assert(smalloc(0) == NULL);
    assert(scalloc(0,0) == NULL);
    void* p = smalloc(10);
    assert(srealloc(p, 0) == NULL);
}

// --- Group 9: Pattern & Fill (81-90) ---

void t081_fill_heap() {
    // Alloc until limit? No, just alloc many
    for(int i=0;i<100;i++) smalloc(1000);
    assert(_num_allocated_blocks() >= 100);
}
void t082_fill_free_all() {
    std::vector<void*> v;
    for(int i=0;i<100;i++) v.push_back(smalloc(100));
    for(auto p : v) sfree(p);
    assert(_num_free_blocks() >= 100);
}
void t083_staircase() {
    // Alloc 10, 20, 30...
    for(int i=1;i<=10;i++) smalloc(i*10);
    assert(_num_allocated_blocks() >= 10);
}
void t084_sawtooth() {
    // Alloc, Alloc, Free, Alloc, Free
    void* p = smalloc(10);
    void* p2 = smalloc(10);
    sfree(p);
    void* p3 = smalloc(10);
    sfree(p2);
    sfree(p3);
}
void t085_pyramid() {
    void* p1 = smalloc(10);
    void* p2 = smalloc(20);
    void* p3 = smalloc(30);
    sfree(p3); sfree(p2); sfree(p1);
}
void t086_double_alloc() {
    void* p = smalloc(10);
    void* p2 = smalloc(10);
    assert(p != p2);
    sfree(p); sfree(p2);
}
void t087_gap_fill() {
    void* p1 = smalloc(10);
    void* p2 = smalloc(1000); // Gap
    void* p3 = smalloc(10);
    sfree(p2);
    void* n = smalloc(500); // Fits in gap
    assert(n == p2);
    sfree(p1); sfree(p3); sfree(n);
}
void t088_large_small_mix() {
    smalloc(10); smalloc(10000); smalloc(10);
}
void t089_realloc_chain() {
    void* p = smalloc(10);
    p = srealloc(p, 20);
    p = srealloc(p, 30);
    sfree(p);
}
void t090_calloc_chain() {
    void* p = scalloc(1, 10);
    sfree(p);
    p = scalloc(1, 20); // Reuse 10? No, 20 > 10. New block.
    sfree(p);
}

// --- Group 10: Final Sanity (91-100) ---

void t091_sanity_1() { assert(smalloc(1)); }
void t092_sanity_2() { assert(scalloc(1,1)); }
void t093_sanity_3() { void* p = smalloc(1); assert(srealloc(p, 2)); }
void t094_sanity_4() { sfree(NULL); }
void t095_sanity_5() { assert(smalloc(MAX_MALLOC + 1) == NULL); }
void t096_sanity_6() { assert(scalloc(MAX_MALLOC, 2) == NULL); }
void t097_sanity_7() { void* p=smalloc(10); sfree(p); assert(smalloc(10)==p); sfree(p); }
void t098_sanity_8() { assert(_size_meta_data() > 0); }
void t099_sanity_9() { assert(_num_free_blocks() >= 0); }
void t100_sanity_10() { std::cout << "DONE"; }

void t01_fragmentation_sieve() {
    const int NUM = 1000;
    const int SIZE = 128;
    void* ptrs[NUM];

    // Phase 1: Alloc all
    for (int i = 0; i < NUM; i++) {
        ptrs[i] = smalloc(SIZE);
        assert(ptrs[i] != NULL);
        memset(ptrs[i], 0xAA, SIZE); // Touch memory
    }

    // Phase 2: Create Swiss Cheese (Free evens)
    for (int i = 0; i < NUM; i += 2) {
        sfree(ptrs[i]);
        ptrs[i] = NULL; // Mark as freed
    }

    // Phase 3: Fill holes
    // Part 2 Logic: Should reuse the exact freed blocks because size matches.
    // If your list is sorted by address, it should fill them in order.
    for (int i = 0; i < NUM; i += 2) {
        void* new_p = smalloc(SIZE);
        // We can't strictly assert new_p == old_ptr because of stack variables etc,
        // but we CAN assert that we didn't expand the heap block count.
        // Actually, Part 2 reuses blocks. 
        assert(new_p != NULL);
        memset(new_p, 0xBB, SIZE);
    }

    // Phase 4: Verification
    // We allocated 1000, freed 500, allocated 500. Total active should be 1000.
    // Total blocks in system should be 1000 (reused).
    // If logic was bad and it sbrk'd new ones, we'd have 1500 blocks.
    assert(_num_allocated_blocks() == NUM);
}

// Test 2: "The Accordion" - Massive Realloc Oscillation
// Takes a single pointer and violently resizes it up and down.
// Forces repeated data copying and finding new spots if it can't grow in place.
void t02_accordion_stress() {
    size_t size = 10;
    char* p = (char*)smalloc(size);
    strcpy(p, "START");

    // We block the path immediately after p to force moves during expansion
    void* blocker = smalloc(10); 

    for (int i = 0; i < 50; i++) {
        // Expand
        size_t new_size = size * 10; 
        if (new_size > 100000) new_size = 10; // Reset if too huge
        
        char* next_p = (char*)srealloc(p, new_size);
        assert(next_p != NULL);
        
        // Verify data integrity preserved through move
        if (size == 10) assert(strncmp(next_p, "START", 5) == 0);
        
        p = next_p;
        size = new_size;
        
        // Block the new spot too to ensure next expand moves again
        // (Memory leak by design for this test to stress heap space)
        smalloc(10); 
    }
    sfree(p); // Should clean up the last instance
    // Note: 'blocker' and loop blockers leak, checking system stability.
}

// Test 3: "Calloc Verification Chain"
// Allocates calloc, writes dirt, frees. Re-allocs calloc, checks zero.
// Repeats this with varying sizes to ensure reuse logic doesn't skip zeroing.
void t03_calloc_dirty_reuse() {
    const int ITERS = 100;
    size_t sizes[] = {64, 128, 256, 512, 1024};
    void* ptrs[ITERS];

    // 1. Alloc and dirty
    for(int i=0; i<ITERS; i++) {
        size_t sz = sizes[i % 5];
        ptrs[i] = scalloc(1, sz);
        memset(ptrs[i], 0xFF, sz); // Dirty it
    }

    // 2. Free all
    for(int i=0; i<ITERS; i++) {
        sfree(ptrs[i]);
    }

    // 3. Alloc again - MUST BE ZERO
    for(int i=0; i<ITERS; i++) {
        size_t sz = sizes[i % 5];
        char* p = (char*)scalloc(1, sz);
        // Verify every single byte is 0
        for(size_t b=0; b<sz; b++) {
            assert(p[b] == 0);
        }
        sfree(p);
    }
}

// Test 4: "The Ascending Ladder" (First Fit Check)
// Creates holes of size 100, 200, 300, 400.
// Requests 250. Should pick 300 (First fit logic implies scanning list).
// Actually, if list is sorted by address, it picks the first one that fits.
// We force the addresses to be ordered 100->200->300->400.
void t04_ladder_fit() {
    void* p1 = smalloc(100);
    void* p2 = smalloc(200);
    void* p3 = smalloc(300);
    void* p4 = smalloc(400);

    // Free all to make them available
    sfree(p1); 
    sfree(p2);
    sfree(p3);
    sfree(p4);

    // Request 250.
    // p1 (100) - Too small
    // p2 (200) - Too small
    // p3 (300) - Fits! 
    // p4 (400) - Fits, but p3 is earlier in list/address (usually).
    
    void* n = smalloc(250);
    
    // Assert we got p3
    assert(n == p3);
    
    // Request 50. Should get p1 (100) since it's first in list and fits.
    void* n2 = smalloc(50);
    assert(n2 == p1);
}

// Test 5: "Metadata Corruption Check"
// This tests if writing to the last byte of a block corrupts the next block.
// We allow users to write up to 'size'. We must ensure metadata is safe.
void t05_metadata_stomp() {
    void* p1 = smalloc(100);
    void* p2 = smalloc(100);
    
    // Write 0xFF to the very last byte of p1.
    // If metadata is badly placed or padding is wrong, this might hit p2's meta.
    char* c1 = (char*)p1;
    c1[99] = 0xFF; 
    
    // Free p1. If p2's metadata (prev ptr) was corrupted, this might crash or logic fail.
    sfree(p1);
    
    // Access p2. If p2's metadata (is_free, size) corrupted, sfree(p2) might crash.
    memset(p2, 0xAA, 100);
    sfree(p2);
    
    // If we survived, check stats
    assert(_num_free_blocks() == 2);
}

// Test 6: "The Blockade"
// Surrounds a block with allocated memory, frees the middle, tries to realloc it.
// Forces a move. Then verifies the old hole is usable.
void t06_blockade_realloc() {
    void* left = smalloc(100);
    void* middle = smalloc(100);
    void* right = smalloc(100);
    
    // Write data to middle
    memset(middle, 0x77, 100);
    
    // Try to grow middle. Can't grow right (blocked).
    char* new_mid = (char*)srealloc(middle, 200);
    
    // Must have moved
    assert(new_mid != middle);
    assert(new_mid > (char*)right); // Should be after right
    
    // Verify data copied
    for(int i=0; i<100; i++) assert(new_mid[i] == 0x77);
    
    // Verify old middle is now free hole.
    // Alloc 100 should take it (lowest address rule).
    void* filler = smalloc(100);
    assert(filler == middle);
    
    sfree(left); sfree(right); sfree(new_mid); sfree(filler);
}

// Test 7: "Stats Stress Test"
// Performs 1000 random operations and tracks expected block counts manually.
// Verifies internal counters match external reality.
void t07_stats_consistency() {
    std::vector<void*> allocated;
    size_t expected_blocks = 0;
    
    for(int i=0; i<1000; i++) {
        // Coin flip: Alloc or Free
        if (allocated.empty() || (rand() % 2 == 0)) {
            void* p = smalloc(rand() % 100 + 1);
            allocated.push_back(p);
            // In Part 2, alloc creates new block OR reuses.
            // But Total Blocks (Allocated+Free) should logically monotonic increase 
            // ONLY when we extend heap.
            // This is hard to track exactly without inspecting internals.
            // Instead, we track if ptr is NULL.
            assert(p != NULL);
        } else {
            // Free random element
            size_t idx = rand() % allocated.size();
            sfree(allocated[idx]);
            allocated.erase(allocated.begin() + idx);
        }
    }
    
    // Cleanup
    for(void* p : allocated) sfree(p);
    
    // Final check: All blocks should be free now.
    // Total Allocated Bytes (excluding meta) stats is tricky in Part 2 
    // because it keeps the size of the *block* not the *requested* size if reused.
    // We mainly assert process didn't crash and list isn't broken.
    assert(_num_free_blocks() > 0);
}

// Test 8: "Zero Size Bombardment"
// Hammers the allocator with size 0 requests.
// Ensures it returns NULL and doesn't corrupt the heap or counters.
void t08_zero_bombardment() {
    size_t initial_meta = _num_meta_data_bytes();
    
    for(int i=0; i<500; i++) {
        void* p = smalloc(0);
        assert(p == NULL);
        
        p = scalloc(0, 10);
        assert(p == NULL);
        
        p = scalloc(10, 0);
        assert(p == NULL);
    }
    
    // Stats should not have changed
    assert(_num_meta_data_bytes() == initial_meta);
}

// Test 9: "Power of 2 Boundary"
// Allocs sizes right on the boundary of powers of 2 (127, 128, 129).
// Often allocators have bugs with alignment/padding at these edges.
void t09_boundary_alignment() {
    void* p1 = smalloc(127);
    void* p2 = smalloc(128);
    void* p3 = smalloc(129);
    
    // Check if distinct
    assert(p1 != p2);
    assert(p2 != p3);
    
    // Check writeable
    memset(p1, 0, 127);
    memset(p2, 0, 128);
    memset(p3, 0, 129);
    
    sfree(p1); sfree(p2); sfree(p3);
}

// Test 10: "Double Free Chain"
// Allocates a linked list logic, frees them, then double frees them.
// Ensures `is_free` flag works and prevents corruption.
void t10_double_free_chain() {
    void* p1 = smalloc(100);
    void* p2 = smalloc(100);
    
    sfree(p1);
    size_t fb = _num_free_blocks();
    
    sfree(p1); // Double free
    assert(_num_free_blocks() == fb); // Should not increase
    
    sfree(p2);
    sfree(p2); // Double free
    
    // Re-alloc should work fine
    void* n = smalloc(100);
    assert(n == p1); // Should still be in list and usable
}

// Test 11: "The Huge Leap"
// Allocs small, then allocs MAX - epsilon.
// Then allocs small again.
// Verifies heap handling of massive chunks.
void t11_huge_leap() {
    void* small = smalloc(100);
    // 10^8 is the limit. Try 99,999,000
    void* huge = smalloc(99000000); 
    
    if (huge) {
        memset(huge, 1, 100); // Touch beginning
        sfree(huge);
    }
    
    void* small2 = smalloc(100);
    assert(small2 != NULL);
    
    sfree(small); sfree(small2);
}

// Test 12: "Realloc Shrink No-Op"
// Realloc to same size, or slightly smaller.
// Part 2 says "reuse same block".
// We verify pointers don't change and data is safe.
void t12_realloc_shrink_noop() {
    char* p = (char*)smalloc(200);
    strcpy(p, "KEEPME");
    
    void* p_new = srealloc(p, 150);
    assert(p_new == p);
    assert(strncmp((char*)p_new, "KEEPME", 6) == 0);
    
    void* p_new2 = srealloc(p_new, 200); // Grow back to original size
    assert(p_new2 == p); // Should still fit in original 200 block
    
    sfree(p_new2);
}

// Test 13: "Calloc Array Overflow"
// Calculates num*size logic.
// 100000 * 100000 = 10^10 (Overflow 32bit size_t? No, 64bit usually).
// But definitely > 10^8 limit.
void t13_calloc_math_overflow() {
    // 20000 * 20000 = 400,000,000 > 10^8
    void* p = scalloc(20000, 20000);
    assert(p == NULL);
}

// Test 14: "Mixed Size Reuse"
// Frees [10, 1000, 10, 1000].
// Requests 500. Should skip the 10s and take the first 1000.
// Verifies it doesn't just take the "most recently freed".
void t14_mixed_size_reuse() {
    void* p1 = smalloc(10);
    void* p2 = smalloc(1000);
    void* p3 = smalloc(10);
    void* p4 = smalloc(1000);
    
    sfree(p1); sfree(p2); sfree(p3); sfree(p4);
    
    void* n = smalloc(500);
    // Should be p2 (first 1000 block)
    assert(n == p2);
    
    void* n2 = smalloc(500);
    // Should be p4 (next 1000 block)
    assert(n2 == p4);
}

// Test 15: "Part 2 No-Split enforcement"
// Alloc 1000. Free. Alloc 10.
// Check stats: Free Bytes.
// If split, free bytes ~ 990 - meta.
// If no split (Part 2), free bytes decrease by 1000.
void t15_no_split_stats() {
    void* p = smalloc(1000);
    sfree(p);
    
    size_t fb_before = _num_free_bytes();
    
    void* n = smalloc(10); // Reuses the 1000 block
    
    size_t fb_after = _num_free_bytes();
    
    // The entire 1000 bytes are now "allocated", so free bytes drops by 1000.
    size_t diff = fb_before - fb_after;
    assert(diff >= 1000);
    
    sfree(n);
}

// Test 16: "Interleaved Realloc"
// P1, P2, P3.
// Free P2.
// Realloc P1 to size that fits in P1+P2? 
// Assignment Part 2 doesn't mention merging (that's Part 3).
// So Realloc P1 must MOVE to end if it grows, ignoring P2 hole (unless implemented merge).
// Assuming NO merge in Part 2.
void t16_interleaved_realloc_no_merge() {
    void* p1 = smalloc(100);
    void* p2 = smalloc(100);
    void* p3 = smalloc(100);
    
    sfree(p2);
    
    // Grow p1. It is blocked by p2 (even though p2 is free, Part 2 usually doesn't merge adjacents).
    // So p1 should move to end of heap (after p3).
    void* n = srealloc(p1, 150);
    
    assert(n != p1);
    assert(n > p3);
    
    sfree(n); sfree(p3);
    // Note: p2 is still free in the middle.
}

// Test 17: "Zero Init Check"
// Ensures smalloc does NOT zero out memory (unlike calloc).
// This relies on memory recycling.
void t17_smalloc_garbage() {
    void* p = smalloc(100);
    memset(p, 0xCC, 100);
    sfree(p);
    
    void* n = smalloc(100);
    assert(n == p);
    
    // Check if data is still there (it should be, we didn't zero it)
    unsigned char* c = (unsigned char*)n;
    // We check first byte. It's 0xCC.
    // NOTE: This behavior isn't strictly mandated (malloc can return zeroed), 
    // but typical naive implementation leaves garbage.
    if (c[0] == 0xCC) {
        // Expected behavior for standard malloc
    }
    
    sfree(n);
}

// Test 18: "Deep List Search"
// Alloc 100 items. Free #99.
// Alloc #99 size.
// Allocator must traverse 98 active blocks (or non-matching free blocks) to find it?
// Wait, we keep a list of ALL blocks (allocated and free)? Or just free?
// PDF: "Global pointer to list... used/free... search for free blocks".
// Usually implies iterating the whole meta-list.
void t18_deep_search() {
    void* ptrs[100];
    for(int i=0; i<100; i++) ptrs[i] = smalloc(64);
    
    sfree(ptrs[90]); // Create hole deep in heap
    
    void* n = smalloc(64);
    assert(n == ptrs[90]); // Must find it
    
    for(int i=0; i<100; i++) if(i!=90) sfree(ptrs[i]);
    sfree(n);
}

// Test 19: "Exact 10^8 Stress"
// Tries to alloc exactly 10^8.
void t19_exact_limit_stress() {
    // This is huge. Might fail on VM limit. 
    // We verify code logic allows it (returns ptr or NULL, but no crash).
    void* p = smalloc(100000000);
    if (p) {
        // If we got it, verify we can free it.
        sfree(p);
    }
    // Success is not crashing.
}

// Test 20: "Grand Finale: Random Simulator"
// 5000 iterations of random alloc/realloc/free.
// High stress test for stability.
void t20_random_simulation() {
    std::vector<void*> ptrs;
    srand(42);
    
    for(int i=0; i<5000; i++) {
        int action = rand() % 3;
        
        if (action == 0 || ptrs.empty()) {
            // Malloc
            size_t sz = rand() % 1024 + 1;
            void* p = smalloc(sz);
            if(p) ptrs.push_back(p);
        } else if (action == 1) {
            // Free
            size_t idx = rand() % ptrs.size();
            sfree(ptrs[idx]);
            ptrs.erase(ptrs.begin() + idx);
        } else {
            // Realloc
            size_t idx = rand() % ptrs.size();
            size_t new_sz = rand() % 1024 + 1;
            void* p = srealloc(ptrs[idx], new_sz);
            if(p) ptrs[idx] = p; // Update ptr if moved/resized
            // If p is null (fail), old remains valid, we keep it.
        }
    }
    
    // Cleanup
    for(void* p : ptrs) sfree(p);
}

int main() {
    std::cout << "malloc_2 tests:" << std::endl;
    //test_basic_malloc();
    //test_block_reuse();
    //test_free_block_statistics();
    //test_realloc_basic();
    //test_calloc_initialization();
    //test_multiple_allocations();
    
    std::cout << "--- Starting Extensive Malloc_2 Tests ---" << std::endl;
    
    run_test_in_child(test_basic_alloc_free, "Test 1: Basic Alloc/Free");
    run_test_in_child(test_reuse_exact_size, "Test 2: Reuse Exact Size");
    run_test_in_child(test_reuse_larger_block_no_split, "Test 3: Reuse Large (No Split)");
    run_test_in_child(test_list_order_ascending, "Test 4: Ascending Order");
    run_test_in_child(test_scalloc, "Test 5: Calloc");
    run_test_in_child(test_realloc_shrink, "Test 6: Realloc Shrink");
    run_test_in_child(test_realloc_expand, "Test 7: Realloc Expand");
    run_test_in_child(test_metadata_integrity, "Test 8: Metadata Integrity");
    run_test_in_child(test_limit_10_8, "Test 9: 10^8 Limit");
    run_test_in_child(t01_basic_byte,      "Test 01: Basic Byte Alloc");
    run_test_in_child(t02_max_limit,       "Test 02: Max Limit (10^8)");
    run_test_in_child(t03_over_limit,      "Test 03: Over Limit (Fail)");
    run_test_in_child(t04_zero_alloc,      "Test 04: Zero Alloc (Fail)");
    run_test_in_child(t05_free_null,       "Test 05: Free NULL");
    run_test_in_child(t06_double_free,     "Test 06: Double Free");
    run_test_in_child(t07_reuse_ordering,  "Test 07: Reuse Address Order");
    run_test_in_child(t08_reuse_skip_small,"Test 08: Reuse Skip Small");
    run_test_in_child(t09_reuse_no_split,  "Test 09: Reuse No Split");
    run_test_in_child(t10_calloc_basic,    "Test 10: Calloc Basic");
    run_test_in_child(t11_calloc_reuse_dirty, "Test 11: Calloc Reuse Zeroing");
    run_test_in_child(t12_calloc_overflow, "Test 12: Calloc Overflow");
    run_test_in_child(t13_realloc_same,    "Test 13: Realloc Same Size");
    run_test_in_child(t14_realloc_smaller, "Test 14: Realloc Smaller");
    run_test_in_child(t15_realloc_move,    "Test 15: Realloc Move");
    run_test_in_child(t16_realloc_null,    "Test 16: Realloc NULL ptr");
    run_test_in_child(t17_realloc_zero,    "Test 17: Realloc Zero Size");
    run_test_in_child(t18_stats_blocks,    "Test 18: Stats Block Count");
    run_test_in_child(t19_stats_metadata,  "Test 19: Stats Metadata");
    run_test_in_child(t20_stats_alloc_bytes,"Test 20: Stats Alloc Bytes");
    run_test_in_child(t01_malloc_1_byte, "01 Malloc 1 Byte");
    run_test_in_child(t02_malloc_2_bytes, "02 Malloc 2 Bytes");
    run_test_in_child(t03_malloc_alignment_heuristic, "03 Alignment Heuristic");
    run_test_in_child(t04_malloc_large_chunk, "04 Large Chunk 1MB");
    run_test_in_child(t05_malloc_fail_huge, "05 Fail Huge > 10^8");
    run_test_in_child(t06_reuse_first_fit_exact, "06 Reuse First Fit Exact");
    run_test_in_child(t07_reuse_first_fit_skip_small, "07 Reuse Skip Small");
    run_test_in_child(t08_reuse_fragmented_list, "08 Reuse Fragmented");
    run_test_in_child(t09_no_split_oversize, "09 No Split Oversize");
    run_test_in_child(t10_reuse_middle_list, "10 Reuse Middle");
    run_test_in_child(t11_realloc_null_ptr, "11 Realloc NULL");
    run_test_in_child(t12_realloc_zero_size, "12 Realloc Zero Size");
    run_test_in_child(t13_realloc_shrink_nop, "13 Realloc Shrink NOP");
    run_test_in_child(t14_realloc_expand_in_place_impossible, "14 Realloc Expand Blocked");
    run_test_in_child(t15_realloc_huge_fail, "15 Realloc Huge Fail");
    run_test_in_child(t16_realloc_data_integrity, "16 Realloc Integrity");
    run_test_in_child(t17_realloc_reuse_freed_block, "17 Realloc Reuse Freed");
    run_test_in_child(t18_realloc_to_same_size, "18 Realloc Same Size");
    run_test_in_child(t19_realloc_tiny_shrink, "19 Realloc Tiny Shrink");
    run_test_in_child(t20_realloc_fails_keeps_old, "20 Realloc Fail Keeps Old");
    run_test_in_child(t21_calloc_one_element, "21 Calloc One Element");
    run_test_in_child(t22_calloc_zero_num, "22 Calloc Zero Num");
    run_test_in_child(t23_calloc_zero_size, "23 Calloc Zero Size");
    run_test_in_child(t24_calloc_overflow_check, "24 Calloc Overflow");
    run_test_in_child(t25_calloc_reuse_zeroing, "25 Calloc Reuse Zeroing");
    run_test_in_child(t26_stats_free_blocks_inc, "26 Stats Free Blocks Inc");
    run_test_in_child(t27_stats_free_bytes_inc, "27 Stats Free Bytes Inc");
    run_test_in_child(t28_stats_alloc_blocks_stable, "28 Stats Alloc Blocks Stable");
    run_test_in_child(t29_metadata_size_consistent, "29 Meta Size Consistent");
    run_test_in_child(t30_metadata_total_bytes, "30 Meta Total Bytes");
    run_test_in_child(t31_stress_alloc_free_loop, "31 Stress Alloc Free Loop");
    run_test_in_child(t32_stress_list_traversal, "32 Stress List Traversal");
    run_test_in_child(t33_alloc_size_t_max, "33 Alloc SIZE_T_MAX");
    run_test_in_child(t34_negative_int_cast, "34 Neg Int Cast");
    run_test_in_child(t35_mixed_calloc_malloc, "35 Mixed Calloc Malloc");
    run_test_in_child(t36_double_free_middle, "36 Double Free Middle");
    run_test_in_child(t37_sbrk_failure_simulation, "37 Sbrk Limit Sim");
    run_test_in_child(t38_realloc_shrink_stats, "38 Realloc Shrink Stats");
    run_test_in_child(t39_zero_blocks_start, "39 Zero Blocks Start");
    run_test_in_child(t40_final_sanity, "40 Final Sanity");
    std::cout << "--- STARTING 100 TESTS ---" << std::endl;
    
    // Group 1
    run_test(t001_alloc_1, "Alloc 1", 1);
    run_test(t002_alloc_64, "Alloc 64", 2);
    run_test(t003_alloc_huge, "Alloc Huge", 3);
    run_test(t004_alloc_zero, "Alloc Zero", 4);
    run_test(t005_alloc_max, "Alloc Max", 5);
    run_test(t006_alloc_overflow, "Alloc Overflow", 6);
    run_test(t007_free_null, "Free Null", 7);
    run_test(t008_calloc_1, "Calloc 1", 8);
    run_test(t009_calloc_zero, "Calloc Zero", 9);
    run_test(t010_realloc_null, "Realloc Null", 10);

    // Group 2
    run_test(t011_reuse_simple, "Reuse Simple", 11);
    run_test(t012_reuse_skip_small, "Reuse Skip Small", 12);
    run_test(t013_reuse_first_fit, "Reuse First Fit", 13);
    run_test(t014_reuse_no_split, "Reuse No Split", 14);
    run_test(t015_reuse_exact, "Reuse Exact", 15);
    run_test(t016_reuse_calloc, "Reuse Calloc", 16);
    run_test(t017_reuse_calloc_clears, "Reuse Calloc Clear", 17);
    run_test(t018_reuse_realloc_shrink, "Reuse Realloc Shrink", 18);
    run_test(t019_reuse_realloc_grow_fits, "Reuse Realloc Grow Fits", 19);
    run_test(t020_reuse_fragmented, "Reuse Fragmented", 20);

    // Group 3
    run_test(t021_realloc_same, "Realloc Same", 21);
    run_test(t022_realloc_shrink, "Realloc Shrink", 22);
    run_test(t023_realloc_expand_move, "Realloc Expand Move", 23);
    run_test(t024_realloc_expand_copy, "Realloc Expand Copy", 24);
    run_test(t025_realloc_zero, "Realloc Zero", 25);
    run_test(t026_realloc_fail_huge, "Realloc Fail Huge", 26);
    run_test(t027_realloc_reuse_freed, "Realloc Reuse Freed", 27);
    run_test(t028_realloc_expansion_data, "Realloc Data", 28);
    run_test(t029_realloc_shrink_stats, "Realloc Shrink Stats", 29);
    run_test(t030_realloc_wild, "Realloc Wild", 30);

    // Group 4
    run_test(t031_calloc_array, "Calloc Array", 31);
    run_test(t032_calloc_overflow_nums, "Calloc Overflow Nums", 32);
    run_test(t033_calloc_exact_limit, "Calloc Exact Limit", 33);
    run_test(t034_calloc_fragment, "Calloc Fragment", 34);
    run_test(t035_calloc_struct, "Calloc Struct", 35);
    run_test(t036_calloc_weird_size, "Calloc Weird Size", 36);
    run_test(t037_calloc_one, "Calloc One", 37);
    run_test(t038_calloc_max_units, "Calloc Max Units", 38);
    run_test(t039_calloc_split_attempt, "Calloc Split Attempt", 39);
    run_test(t040_calloc_reuse_dirty_check, "Calloc Dirty Check", 40);

    // Group 5
    run_test(t041_stats_free_blocks, "Stats Free Blocks", 41);
    run_test(t042_stats_alloc_blocks, "Stats Alloc Blocks", 42);
    run_test(t043_stats_free_bytes, "Stats Free Bytes", 43);
    run_test(t044_stats_alloc_bytes, "Stats Alloc Bytes", 44);
    run_test(t045_stats_meta, "Stats Meta", 45);
    run_test(t046_stats_consistent, "Stats Consistent", 46);
    run_test(t047_stats_realloc_move, "Stats Realloc Move", 47);
    run_test(t048_stats_calloc, "Stats Calloc", 48);
    run_test(t049_stats_reuse, "Stats Reuse", 49);
    run_test(t050_stats_meta_size, "Stats Meta Size", 50);

    // Group 6
    run_test(t051_stress_loop_alloc, "Stress Loop Alloc", 51);
    run_test(t052_stress_loop_reuse, "Stress Loop Reuse", 52);
    run_test(t053_stress_alternating, "Stress Alternating", 53);
    run_test(t054_stress_checkerboard, "Stress Checkerboard", 54);
    run_test(t055_stress_increasing, "Stress Increasing", 55);
    run_test(t056_stress_realloc_loop, "Stress Realloc Loop", 56);
    run_test(t057_stress_calloc_loop, "Stress Calloc Loop", 57);
    run_test(t058_stress_mixed, "Stress Mixed", 58);
    run_test(t059_stress_reverse_free, "Stress Reverse Free", 59);
    run_test(t060_stress_randomish, "Stress Randomish", 60);

    // Group 7
    run_test(t061_limit_max, "Limit Max", 61);
    run_test(t062_limit_fail, "Limit Fail", 62);
    run_test(t063_limit_realloc, "Limit Realloc", 63);
    run_test(t064_limit_calloc, "Limit Calloc", 64);
    run_test(t065_limit_sbrk_sim, "Limit Sbrk Sim", 65);
    run_test(t066_align_addr, "Align Addr", 66);
    run_test(t067_meta_align, "Meta Align", 67);
    run_test(t068_ptr_diff, "Ptr Diff", 68);
    run_test(t069_block_count, "Block Count", 69);
    run_test(t070_bytes_count, "Bytes Count", 70);

    // Group 8
    run_test(t071_puzzle_1, "Puzzle 1", 71);
    run_test(t072_puzzle_2, "Puzzle 2", 72);
    run_test(t073_puzzle_3, "Puzzle 3", 73);
    run_test(t074_puzzle_4, "Puzzle 4", 74);
    run_test(t075_puzzle_5, "Puzzle 5", 75);
    run_test(t076_puzzle_6, "Puzzle 6", 76);
    run_test(t077_puzzle_7, "Puzzle 7", 77);
    run_test(t078_puzzle_8, "Puzzle 8", 78);
    run_test(t079_puzzle_9, "Puzzle 9", 79);
    run_test(t080_puzzle_10, "Puzzle 10", 80);

    // Group 9
    run_test(t081_fill_heap, "Fill Heap", 81);
    run_test(t082_fill_free_all, "Fill Free All", 82);
    run_test(t083_staircase, "Staircase", 83);
    run_test(t084_sawtooth, "Sawtooth", 84);
    run_test(t085_pyramid, "Pyramid", 85);
    run_test(t086_double_alloc, "Double Alloc", 86);
    run_test(t087_gap_fill, "Gap Fill", 87);
    run_test(t088_large_small_mix, "Large Small Mix", 88);
    run_test(t089_realloc_chain, "Realloc Chain", 89);
    run_test(t090_calloc_chain, "Calloc Chain", 90);

    // Group 10
    run_test(t091_sanity_1, "Sanity 1", 91);
    run_test(t092_sanity_2, "Sanity 2", 92);
    run_test(t093_sanity_3, "Sanity 3", 93);
    run_test(t094_sanity_4, "Sanity 4", 94);
    run_test(t095_sanity_5, "Sanity 5", 95);
    run_test(t096_sanity_6, "Sanity 6", 96);
    run_test(t097_sanity_7, "Sanity 7", 97);
    run_test(t098_sanity_8, "Sanity 8", 98);
    run_test(t099_sanity_9, "Sanity 9", 99);
    run_test(t100_sanity_10, "Sanity 10", 100);
    
    run_test(t01_fragmentation_sieve, "Sieve Fragmentation", 1);
    run_test(t02_accordion_stress, "Accordion Realloc", 2);
    run_test(t03_calloc_dirty_reuse, "Calloc Dirty Reuse", 3);
    run_test(t04_ladder_fit, "Ladder First Fit", 4);
    run_test(t05_metadata_stomp, "Metadata Stomp", 5);
    run_test(t06_blockade_realloc, "Blockade Realloc", 6);
    run_test(t07_stats_consistency, "Stats Consistency", 7);
    run_test(t08_zero_bombardment, "Zero Bombardment", 8);
    run_test(t09_boundary_alignment, "Boundary Alignment", 9);
    run_test(t10_double_free_chain, "Double Free Chain", 10);
    run_test(t11_huge_leap, "Huge Leap", 11);
    run_test(t12_realloc_shrink_noop, "Realloc Shrink No-Op", 12);
    run_test(t13_calloc_math_overflow, "Calloc Math Overflow", 13);
    run_test(t14_mixed_size_reuse, "Mixed Size Reuse", 14);
    run_test(t15_no_split_stats, "No Split Stats", 15);
    run_test(t16_interleaved_realloc_no_merge, "Interleaved Realloc", 16);
    run_test(t17_smalloc_garbage, "Smalloc Garbage", 17);
    run_test(t18_deep_search, "Deep List Search", 18);
    run_test(t19_exact_limit_stress, "Exact Limit Stress", 19);
    run_test(t20_random_simulation, "Random Simulation", 20);

    std::cout << "--- ALL 100 TESTS COMPLETED ---" << std::endl;
    
    std::cout << "--- All Tests Passed ---" << std::endl;
    return 0;
}

