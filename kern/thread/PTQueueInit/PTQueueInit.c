#include "lib/x86.h"

#include "import.h"

/**
 * Initializes all the thread queues with tqueue_init_at_id.
 * There are NUM_IDS + NUM_CPUS queues: queue # NUM_IDS + i is the ready
 * queue of CPU # i (with a single CPU, this is just queue # NUM_IDS).
 */
void tqueue_init(unsigned int mbi_addr)
{
    unsigned int chid;

    tcb_init(mbi_addr);

    // NUM_IDS sleep queues + NUM_CPUS ready queues, all empty
    for (chid = 0; chid < NUM_IDS + NUM_CPUS; chid++)
        tqueue_init_at_id(chid);
}

/**
 * Insert the TCB #pid into the tail of the thread queue #chid.
 * Recall that the doubly linked list is index based.
 * So you only need to insert the index.
 * Hint: there are multiple cases in this function.
 */
void tqueue_enqueue(unsigned int chid, unsigned int pid)
{
    unsigned int tail = tqueue_get_tail(chid);

    if (tail == NUM_IDS) {
        // the queue is empty: pid is both the head and the tail
        tqueue_set_head(chid, pid);
        tqueue_set_tail(chid, pid);
        tcb_set_prev(pid, NUM_IDS);
        tcb_set_next(pid, NUM_IDS);
    }
    else {
        // link pid after the current tail
        tcb_set_next(tail, pid);
        tcb_set_prev(pid, tail);
        tcb_set_next(pid, NUM_IDS);
        tqueue_set_tail(chid, pid);
    }
}

/**
 * Reverse action of tqueue_enqueue, i.e. pops a TCB from the head of the specified queue.
 * It returns the popped thread's id, or NUM_IDS if the queue is empty.
 * Hint: there are multiple cases in this function.
 */
unsigned int tqueue_dequeue(unsigned int chid)
{
    unsigned int pid = tqueue_get_head(chid);
    unsigned int next;

    // the queue is empty
    if (pid == NUM_IDS)
        return NUM_IDS;

    next = tcb_get_next(pid);

    if (next == NUM_IDS) {
        // pid was the only element: the queue becomes empty
        tqueue_set_tail(chid, NUM_IDS);
    }
    else {
        // the next thread becomes the head, with no predecessor
        tcb_set_prev(next, NUM_IDS);
    }
    tqueue_set_head(chid, next);

    // the removed thread is unlinked from the queue
    tcb_set_prev(pid, NUM_IDS);
    tcb_set_next(pid, NUM_IDS);

    return pid;
}

/**
 * Removes the TCB #pid from the queue #chid.
 * Hint: there are many cases in this function.
 */
void tqueue_remove(unsigned int chid, unsigned int pid)
{
    unsigned int prev = tcb_get_prev(pid);
    unsigned int next = tcb_get_next(pid);

    if (prev == NUM_IDS)
        // pid is the head of the queue (possibly the only element)
        tqueue_set_head(chid, next);
    else
        tcb_set_next(prev, next);

    if (next == NUM_IDS)
        // pid is the tail of the queue (possibly the only element)
        tqueue_set_tail(chid, prev);
    else
        tcb_set_prev(next, prev);

    // the removed thread is unlinked from the queue
    tcb_set_prev(pid, NUM_IDS);
    tcb_set_next(pid, NUM_IDS);
}
