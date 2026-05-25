/*
 * mm.c
 *
 * Name: [Hao Wang]
 This memory allocator uses an implicit free list with strict 16-byte alignment.
 It implements a first fit search for allocation and performs immediate merge whenever memory is free.

 During until_improvement, I rewrite the code to added a segregated free list, which is a collection of 
 explicit free lists, each responsible for a specific size class of free blocks. This allows for faster 
 allocation and deallocation by reducing the search space for free blocks.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

/*
 * If you want to enable your debugging output and heap checker code,
 * uncomment the following line. Be sure not to have debugging enabled
 * in your final submission.
 */
// #define DEBUG

#ifdef DEBUG
/* When debugging is enabled, the underlying functions get called */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#else
/* When debugging is disabled, no code gets generated */
#define dbg_printf(...)
#define dbg_assert(...)
#endif /* DEBUG */

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* DRIVER */

/* What is the correct alignment? */
#define ALIGNMENT 16

/* rounds up to the nearest multiple of ALIGNMENT */
static size_t align(size_t x)
{
    return ALIGNMENT * ((x+ALIGNMENT-1)/ALIGNMENT);
}

/*
 * constant
 */
static const size_t word_size = 8; /* Each square is a word 8 bytes for 64 x86 */
static const size_t double_word_size = 16; // The required alignment size
static const size_t init_block = (1<<12);  /* Virtual memory is procured from the OS at a relatively coarse granularity (page) */

static const int seg_list_count = 12; // The number of free lists
static char **seg_lists; // Array of pointers to the heads of the segregated free lists

static char *heap_start = 0; /* Pointer to the first byte of the heap */
static void insert_node(void *block_ptr);
static void remove_node(void *block_ptr);

/*
 * helper functions
 */
static inline size_t read_word(void *p) { /* Read a word at address p. */
    return (*(size_t *)(p)); 
}
static inline void write_word(void *p, size_t val) { /* Write a word at address p. */
    (*(size_t *)(p) = (val)); 
}
static inline size_t pack(size_t size, size_t prev_alloc, size_t alloc) { /* Pack size and allocated bit into a word. */
    return size + (prev_alloc << 1) + alloc;
}
static inline size_t get_size(void *p) {
    return read_word(p) & ~0xf; // Get the size of the header/footer by cutting off the last 4 bits
}
static inline size_t get_alloc(void *p) {
    return read_word(p) & 0x1; // Get the allocated bit from a header/footer word.
}
static inline size_t max(size_t x, size_t y) {
    if (x > y) {
        return x;
    } else {
        return y;
    }
}
static inline int get_seg_index(size_t size) {
    if (size <= 32) return 0;      
    if (size <= 64) return 1;
    if (size <= 128) return 2;
    if (size <= 256) return 3;
    if (size <= 512) return 4;
    if (size <= 1024) return 5;
    if (size <= 2048) return 6;
    if (size <= 4096) return 7;
    if (size <= 8192) return 8;
    if (size <= 16384) return 9;
    if (size <= 32768) return 10;
    return 11;                     
}
static inline size_t get_prev_alloc(void *p) {
    size_t word = read_word(p);
    return (word & 0x2) >> 1; // extract the 0x2 bit and shift it back to the least significant bit to get 0 or 1
}

// When the left neighbor is free, clear the bit in the current block's header that indicates the previous block is allocated. 
static inline void clear_prev_alloc(void *p) {
    size_t word = read_word(p);
    write_word(p, word & ~0x2); // Turn the first bit into 0, which indicates the left neighbor is free
}

// When the left neighbor is allocated, set the bit in the current block's header that indicates the previous block is allocated.
static inline void set_prev_alloc(void *p) {
    size_t word = read_word(p);
    write_word(p, word | 0x2); // turn the first bit into 1, which indicates the left neighbor is allocated
}

// Address calculations
static inline void *header_of(void *block_ptr) { 
    return (char *)block_ptr - word_size; 
}

static inline void *footer_of(void *block_ptr) { 
    size_t size = get_size(header_of(block_ptr));
    return (char *)block_ptr + size - double_word_size; 
}

static inline void *next_block(void *block_ptr) {  
    size_t size = get_size(header_of(block_ptr));
    return (char *)block_ptr + size; 
}
static inline void *prev_block(void *block_ptr) { 
    // if previous block is free, we can get its size from the footer and calculate its starting address
    void *prev_footer = (char *)block_ptr - double_word_size;
    size_t prev_size = get_size(prev_footer);
    return (char *)block_ptr - prev_size; 
}

static inline void *get_next_free(void *block_ptr) {
    return (void *)read_word(block_ptr);
}

static inline void *get_prev_free(void *block_ptr) {
    void *prev_ptr_address = (char *)block_ptr + word_size;
    return (void *)read_word(prev_ptr_address);
}

static inline void set_next_free(void *block_ptr, void *target_ptr) {
    write_word(block_ptr, (size_t)target_ptr);
}

static inline void set_prev_free(void *block_ptr, void *target_ptr) {
    void *prev_ptr_address = (char *)block_ptr + word_size;
    write_word(prev_ptr_address, (size_t)target_ptr);
}

/*
 * helper fuuntions 2
 */

// Updated insert_node and remove_node to work with the segregated free list. 
// When inserting, we find the appropriate free list based on the block size and add the block to the front of that list. 
// When removing, we update the next and previous pointers of the neighboring free blocks and update the head of the free list if necessary.
static void insert_node(void *block_ptr) {
    if (block_ptr == NULL)
        return;
    
    size_t size = get_size(header_of(block_ptr));
    int index = get_seg_index(size);
    
    void *current_head = seg_lists[index];

    set_next_free(block_ptr, current_head);
    set_prev_free(block_ptr, NULL);

    if (current_head != NULL) {
        set_prev_free(current_head, block_ptr);
    }
    seg_lists[index] = block_ptr;
}

static void remove_node(void *block_ptr) {
    if (block_ptr == NULL)
        return;
    
    size_t size = get_size(header_of(block_ptr));
    int index = get_seg_index(size);
    
    void *prev = get_prev_free(block_ptr);
    void *next = get_next_free(block_ptr);

    if (prev != NULL) {
        set_next_free(prev, next);
    } else {
        seg_lists[index] = next;
    }

    if (next != NULL) {
        set_prev_free(next, prev);
    }
}
static void *merge_blocks(void *block_ptr)
{
    size_t prev_alloc = get_prev_alloc(header_of(block_ptr)); // read current block's header to get the prev_alloc bit
    size_t next_alloc = get_alloc(header_of(next_block(block_ptr)));        // read next block's header to get the alloc bit
    size_t size = get_size(header_of(block_ptr));

    /* Both neighbors are allocated */
    if (prev_alloc && next_alloc) {
        clear_prev_alloc(header_of(next_block(block_ptr))); // Clear the prev_alloc bit in the next block's header to indicate the current block is free
        // Mark the current block as free
        write_word(header_of(block_ptr), pack(size, prev_alloc, 0));
        write_word(footer_of(block_ptr), pack(size, prev_alloc, 0));
    }

    /* Next block is free */
    else if (prev_alloc && !next_alloc) {

        remove_node(next_block(block_ptr)); // Remove the next block from the free list before merging

        size += get_size(header_of(next_block(block_ptr)));
        write_word(header_of(block_ptr), pack(size, prev_alloc, 0));
        write_word(footer_of(block_ptr), pack(size, prev_alloc, 0));
    }

    /* Previous block is free */
    else if (!prev_alloc && next_alloc) {

        remove_node(prev_block(block_ptr)); // Remove the previous block from the free list before merging
        clear_prev_alloc(header_of(next_block(block_ptr))); // Clear the prev_alloc bit in the next block's header to indicate the merged block is free

        size += get_size(header_of(prev_block(block_ptr)));
        size_t prev_alloc = get_prev_alloc(header_of(prev_block(block_ptr))); // Get the prev_alloc bit from the previous block's header

        write_word(header_of(prev_block(block_ptr)), pack(size, prev_alloc, 0));
        write_word(footer_of(block_ptr), pack(size, prev_alloc, 0));

        block_ptr = prev_block(block_ptr);
    }

    /* Both neighbors are free */
    else {

        remove_node(prev_block(block_ptr));
        remove_node(next_block(block_ptr));

        size += get_size(header_of(prev_block(block_ptr))) + get_size(footer_of(next_block(block_ptr)));
        size_t actual_prev_alloc = get_prev_alloc(header_of(prev_block(block_ptr))); // Get the prev_alloc bit from the previous block's header before merging
        write_word(header_of(prev_block(block_ptr)), pack(size, actual_prev_alloc, 0));
        write_word(footer_of(next_block(block_ptr)), pack(size, actual_prev_alloc, 0));

        block_ptr = prev_block(block_ptr);
    }
    insert_node(block_ptr); // Insert the merged block into the free list
    return block_ptr;
}
static void *extend_heap(size_t words)
{
    char *block_ptr;
    size_t size;

    if (words % 2 != 0) {
        size = (words + 1) * word_size;
    } else {
        size = words * word_size;
    }

    block_ptr = mem_sbrk(size);
    if (block_ptr == (void *)-1) {
        return NULL;
    }

    /* Initialize the new free block */
    size_t prev_alloc = get_prev_alloc(header_of(block_ptr));
    write_word(header_of(block_ptr), pack(size, prev_alloc, 0));
    write_word(footer_of(block_ptr), pack(size, prev_alloc, 0));
    write_word(header_of(next_block(block_ptr)), pack(0, 0, 1)); // Update the epilogue header of the new block to have the correct prev_alloc bit

    /* Merge if the previous block was free */
    return merge_blocks(block_ptr);
}
/*
Updated find_fit to search through the appropriate segregated free list based on the requested size.
Search starts from the index corresponding to the requested size and continues through larger size classes until a fit is found or all lists are exhausted.
To prevent excessive searching, we limit the number of blocks we check in each list.
*/
static void *find_fit(size_t asize) {
    int init_slot = get_seg_index(asize);
    void *best_block = NULL;
    size_t smallest_gap = SIZE_MAX; 
    
    int search_count = 0;
    int max_searches = 20; // If we have checked 20 blocks without finding a perfect fit, we can stop searching and return the best fit we have found so far.

    // Start from the smallest appropriate slot and search for a fit
    int i = init_slot;
    while (i < seg_list_count) {
        void *block_ptr = seg_lists[i];
        while (block_ptr != NULL) {
            size_t current_size = get_size(header_of(block_ptr));
            // Check if the current block can accommodate the requested size
            if (current_size >= asize) {
                size_t gap = current_size - asize;
                if (gap == 0) {
                    return block_ptr;
                }

                // Update the best fit if this block is closer to the requested size than the previous best
                if (gap < smallest_gap) {
                    smallest_gap = gap;
                    best_block = block_ptr;
                }
            }
            // If we have checked enough blocks in this list and found a potential fit, we can stop searching further in this list to save time.
            search_count += 1;
            if (search_count >= max_searches && best_block != NULL) {
                return best_block;
            }
            // Move to the next block in the free list
            block_ptr = get_next_free(block_ptr);
        }
        if (best_block != NULL) {
            return best_block;
        }
        // If we exhaust the current list without finding a fit, move to the next larger size class
        i += 1;
    }
    // If we exhaust all lists without finding a fit, return the best fit we found or Null if no fit was found
    return best_block;
}

static void place(void *block_ptr, size_t asize)
{
    size_t current_size = get_size(header_of(block_ptr));
    size_t remaining_size = current_size - asize;
    size_t prev_alloc_status = get_prev_alloc(header_of(block_ptr));

    size_t min_block_size = 2 * double_word_size; // Minimum block size to fit the header, footer, and free list pointers

    remove_node(block_ptr); // Remove the block from the free list before placing the allocated block

    if (remaining_size < min_block_size) {
        // Allocate the entire block if the remaining size is too small to be a free block
        write_word(header_of(block_ptr), pack(current_size, prev_alloc_status, 1));
        
        // If current block is allocated, set the prev_alloc bit in the next block's header to indicate the current block is allocated
        void *right_neighbor = next_block(block_ptr);
        set_prev_alloc(header_of(right_neighbor));
        
        return;
    }
    write_word(header_of(block_ptr), pack(asize, prev_alloc_status, 1));
    void *new_free_block = next_block(block_ptr); // Calculate the starting address of the new free block after splitting
    // Set the header and footer of the new free block with the correct size and prev_alloc bit
    write_word(header_of(new_free_block), pack(remaining_size, 1, 0));
    write_word(footer_of(new_free_block), pack(remaining_size, 1, 0));
    
    insert_node(new_free_block);
}

/*
 * Initialize: returns false on error, true on success.
 */
bool mm_init(void)
{
    /* IMPLEMENT THIS */
    seg_lists = (char **)mem_sbrk(seg_list_count * sizeof(char *));
    if (seg_lists == (void *)-1) {
        return false;
    }
    
    for (int i = 0; i < seg_list_count; i++) {
        seg_lists[i] = NULL;
    }

    heap_start = mem_sbrk(4 * word_size);
    if (heap_start == (void*)-1) {
        return false; }
    
    write_word(heap_start, 0);                                                 /* Alignment padding */
    write_word(heap_start + (1 * word_size), pack(double_word_size, 1, 1));       /* Prologue header */
    write_word(heap_start + (2 * word_size), pack(double_word_size, 1, 1));       /* Prologue footer */
    write_word(heap_start + (3 * word_size), pack(0,1, 1));                      /* Epilogue header */
    
    heap_start += (2 * word_size); /* The first block starts at the prologue footer, which is 16 bytes from the start of the heap */
    
    if (extend_heap(init_block / word_size) == NULL)
        return false;
    
    return true;
}

/*
 * malloc
 */
void* malloc(size_t size)
{
    /* IMPLEMENT THIS */
    size_t adjusted_size; /* Adjusted block size */
    size_t extend_size; /* Amount to extend heap if no fit */
    char *block_ptr; /* Block pointer */

    /* Ignore invalid requests */
    if (size == 0)
        return NULL;

    // Get the adjusted size of the new block
    if (size <= double_word_size)
        adjusted_size = 2 * double_word_size;
    else
        adjusted_size = align(size + word_size);

    // first fit search in the segregated free lists
    block_ptr = find_fit(adjusted_size);
    if (block_ptr != NULL) {
        place(block_ptr, adjusted_size);
        return block_ptr;
    }

    // If no fit found, extend heap
    extend_size = max(adjusted_size, init_block); 
    block_ptr = extend_heap(extend_size / word_size);

    // If heap extension fails, return NULL
    if (block_ptr == NULL) {
        return NULL;
    }

    // Place the new block and return its pointer
    place(block_ptr, adjusted_size);
    return block_ptr;
}

/*
 * free
 */
void free(void* ptr)
{
    /* IMPLEMENT THIS */
    if (ptr == NULL) 
        return;
    // Update: Moved the function to merge blocks
    merge_blocks(ptr); // merge with adjacent free blocks if possible
    return;
}

/*
 * realloc
 */
void* realloc(void* oldptr, size_t size)
{
    /* IMPLEMENT THIS */
    size_t adjusted_size;
    size_t old_size;
    size_t prev_alloc;
    void *next_block_ptr;
    size_t next_alloc;
    size_t next_size;
    size_t combined_size;
    void *newptr;
    size_t payload_size;
    size_t copy_size;

    // If oldptr is NULL, this is just malloc
    if (oldptr == NULL)
        return malloc(size);

    // If size is 0, this is just free
    if (size == 0) {
        free(oldptr);
        return NULL;
    }
    // get the size of the old block
    if (size <= 24)
        adjusted_size = 2 * double_word_size;
    else
        adjusted_size = align(size + word_size);

    old_size = get_size(header_of(oldptr));
    prev_alloc = get_prev_alloc(header_of(oldptr));

    // If the new size is smaller than or equal to the old size, we can just return the old pointer
    if (adjusted_size <= old_size) {
        return oldptr; 
    }

    // If next block is free and the combined size is enough, we can merge with the next block
    next_block_ptr = next_block(oldptr);
    next_alloc = get_alloc(header_of(next_block_ptr));
    next_size = get_size(header_of(next_block_ptr));
    combined_size = old_size + next_size;

    if (!next_alloc && next_size > 0 && combined_size >= adjusted_size) {
        // Remove the next block from the free list before merging
        remove_node(next_block_ptr);
        
        size_t remaining_size = combined_size - adjusted_size;

        // Check if we can split the merged block
        if (remaining_size >= 32) {
            // Allocate the requested part (No footer for allocated blocks)
            write_word(header_of(oldptr), pack(adjusted_size, prev_alloc, 1));
            
            // Create a new free block from the remainder (Must have footer)
            void *new_free = next_block(oldptr);
            write_word(header_of(new_free), pack(remaining_size, 1, 0));
            write_word(footer_of(new_free), pack(remaining_size, 1, 0));
            
            // Insert the new free block into the free list
            insert_node(new_free);
        } else {
            // Not enough space to split, allocate the whole combined block (No footer)
            write_word(header_of(oldptr), pack(combined_size, prev_alloc, 1));
            
            // Notify the next block that its previous block is now allocated
            set_prev_alloc(header_of(next_block(oldptr)));
        }
        
        return oldptr;
    }

    // Otherwise, we need to allocate a new block and copy the data
    newptr = malloc(size);
    if (newptr == NULL)
        return NULL;
    payload_size = old_size - word_size;
    
    if (size < payload_size) {
        copy_size = size;
    } else {
        copy_size = payload_size;
    }
    
    memcpy(newptr, oldptr, copy_size);
    free(oldptr);
    
    return newptr;
}
/*
 * calloc
 * This function is not tested by mdriver, and has been implemented for you.
 */
void* calloc(size_t nmemb, size_t size)
{
    /*
    void* ptr;
    size *= nmemb;
    ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
    */
    return NULL;
}

/*
 * Returns whether the pointer is in the heap.
 * May be useful for debugging.
 */
static bool in_heap(const void* p)
{
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Returns whether the pointer is aligned.
 * May be useful for debugging.
 */
static bool aligned(const void* p)
{
    size_t ip = (size_t) p;
    return align(ip) == ip;
}

/*
 * mm_checkheap
 */
bool mm_checkheap(int lineno)
{
#ifdef DEBUG
    // Check the prologue block
    void *prologue_header = header_of(heap_start);
    size_t prologue_size = get_size(prologue_header);
    bool prologue_is_allocated = get_alloc(prologue_header);
    
    if (prologue_size != double_word_size || !prologue_is_allocated) {
        printf("[line %d] Error: Prologue is wrong \n", lineno);
        return false;
    }

    // Check the heap blocks
    void *current_block = heap_start;
    bool actual_previous_was_allocated = true; // If the previous block was allocated, set true else false

    while (true) {
        void *header = header_of(current_block);
        size_t size = get_size(header);
        bool is_allocated = get_alloc(header);
        bool prev_alloc_bit = get_prev_alloc(header); // Get the prev alloc bit from the current block's header

        // If size is 0, we have reached the epilogue block, break out of the loop to check the epilogue
        if (size == 0) {
            break;
        }

        // Check if the current block is within the heap boundaries
        if ((size_t)current_block % ALIGNMENT != 0) {
            printf("[line %d] Error: Bad alignment found at %p \n", lineno, current_block);
            return false;
        }

        // CHeck if the previous block is allocated
        if (prev_alloc_bit != actual_previous_was_allocated) {
            printf("[line %d] Error: prev_alloc bit mismatch at %p \n", lineno, current_block);
            return false;
        }
        
        // If the current block is free, check if the header and footer match and if there are two adjacent free blocks that should have been merged
        if (!is_allocated) {
            void *footer = footer_of(current_block);
            if (read_word(header) != read_word(footer)) {
                printf("[line %d] Error: Header/Footer mismatch in FREE block at %p \n", lineno, current_block);
                return false;
            }

            // Check for adjacent free blocks that should have been merged
            if (!actual_previous_was_allocated) {
                printf("[line %d] Error: Found two free blocks in a row at %p \n", lineno, current_block);
                return false;
            }
        }

        // Update the actual_previous_was_allocated variable for the next iteration
        actual_previous_was_allocated = is_allocated;
        current_block = next_block(current_block);
    }

    // Check the epilogue block
    void *epilogue_header = header_of(current_block);
    size_t epilogue_size = get_size(epilogue_header);
    bool epilogue_is_allocated = get_alloc(epilogue_header);

    if (epilogue_size != 0 || !epilogue_is_allocated) {
        printf("[line %d] Error: Epilogue is wrong GuGu GaGa\n", lineno);
        return false;
    }

#endif /* DEBUG */
    return true;
}


/*
 * mm_init()
 * malloc(size_t size)
 * free(void* ptr)
 * realloc(void* oldptr, size_t size)
 * mm_checkheap
 */