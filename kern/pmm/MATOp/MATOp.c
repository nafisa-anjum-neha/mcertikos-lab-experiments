#include <lib/debug.h>
#include <lib/types.h>
#include "import.h"

#define PAGESIZE     4096
#define VM_USERLO    0x40000000
#define VM_USERHI    0xF0000000
#define VM_USERLO_PI (VM_USERLO / PAGESIZE)
#define VM_USERHI_PI (VM_USERHI / PAGESIZE)

/**
 * Allocate a physical page.
 *
 * 1. First, implement a naive page allocator that scans the allocation table (AT)
 *    using the functions defined in import.h to find the first unallocated page
 *    with normal permissions.
 *    (Q: Do you have to scan the allocation table from index 0? Recall how you have
 *    initialized the table in pmem_init.)
 *    Then mark the page as allocated in the allocation table and return the page
 *    index of the page found. In the case when there is no available page found,
 *    return 0.
 * 2. Optimize the code using memoization so that you do not have to
 *    scan the allocation table from scratch every time.
 *
 * Note: palloc may run on several CPUs at the same time (lab 6), so the
 * allocation table is protected by mem_lock() / mem_unlock(). Keep your code
 * between the two calls and do not return in between.
 */
unsigned int palloc()
{
    unsigned int palloc_free_index = 0;

    mem_lock();

    static unsigned int next_page_index = VM_USERLO_PI;
    unsigned int start = next_page_index;
    unsigned int i = start;

    do {
        if (at_is_norm(i) && !at_is_allocated(i)) {
            at_set_allocated(i, 1);
            palloc_free_index = i;
            next_page_index = i + 1;
            if (next_page_index >= VM_USERHI_PI) {
                next_page_index = VM_USERLO_PI;
            }
            break;
        }
        i++;
        if (i >= VM_USERHI_PI) {
            i = VM_USERLO_PI;
        }
    } while (i != start);

    mem_unlock();

    return palloc_free_index;
}

/**
 * Free a physical page.
 *
 * This function marks the page with given index as unallocated
 * in the allocation table.
 *
 * Hint: Simple.
 */
void pfree(unsigned int pfree_index)
{
    mem_lock();
    at_set_allocated(pfree_index, 0);
    mem_unlock();
}
