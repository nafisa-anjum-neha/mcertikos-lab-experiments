#include <lib/gcc.h>
#include <lib/x86.h>
#include <lib/kstack.h>

#include "import.h"

/**
 * Checks whether enough resources are available for allocation and
 * allocates memory for the new child thread, then sets the eip, and esp
 * of the thread states. The eip should be set to [entry], and the
 * esp should be set to the corresponding kernel stack TOP
 * (see proc_kstack in lib/kstack.h).
 * Don't forget the stack is going down from high address to low.
 * We do not care about the rest of states when a new thread starts.
 * The function returns the child thread (process) id.
 * In case of an error, return NUM_IDS.
 */
unsigned int kctx_new(void *entry, unsigned int id, unsigned int quota)
{
    unsigned int chid;

    // Is there enough quota left in process # [id] to give to the child?
    if (container_can_consume(id, quota) == 0)
        return NUM_IDS;

    // Split off the child container; alloc_mem_quota returns NUM_IDS on error.
    chid = alloc_mem_quota(id, quota);
    if (chid == NUM_IDS)
        return NUM_IDS;

    /**
     * The stack grows down (towards lower addresses), so a brand new thread
     * starts at the TOP of its kernel stack, and with its entry function
     * as the return address of the first cswitch.
     */
    kctx_set_esp(chid, (void *) proc_kstack[chid].kstack_hi);
    kctx_set_eip(chid, entry);

    return chid;
}
