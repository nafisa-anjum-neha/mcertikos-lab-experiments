#include <lib/x86.h>
#include <lib/thread.h>
#include <lib/spinlock.h>
#include <lib/debug.h>
#include <dev/lapic.h>
#include <pcpu/PCPUIntro/export.h>
#include <kern/thread/PTCBIntro/export.h>

#include "import.h"

static spinlock_t sched_lk;

unsigned int sched_ticks[NUM_CPUS];

void thread_init(unsigned int mbi_addr)
{
    unsigned int i;
    for (i = 0; i < NUM_CPUS; i++) {
        sched_ticks[i] = 0;
    }

    spinlock_init(&sched_lk);
    tqueue_init(mbi_addr);
    set_curid(0);
    tcb_set_state(0, TSTATE_RUN);
}

/**
 * Allocates a new child thread context, sets the state of the new child thread
 * to ready, and pushes it to the ready queue.
 * It returns the child thread id.
 *
 * The ready queue of the current CPU is NUM_IDS + get_pcpu_idx() (just
 * NUM_IDS while there is a single CPU). Also record the CPU of the new
 * thread with tcb_set_cpu.
 * The scheduler state is protected by sched_lk (lab 6); keep your code
 * between the acquire and the release.
 */
unsigned int thread_spawn(void *entry, unsigned int id, unsigned int quota)
{
    unsigned int pid = NUM_IDS;
    unsigned int rdq = NUM_IDS + get_pcpu_idx();

    spinlock_acquire(&sched_lk);

    pid = kctx_new(entry, id, quota);
    if (pid != NUM_IDS) {
        // record where it runs, mark it ready, and append it to the
        // ready queue of the current CPU
        tcb_set_cpu(pid, get_pcpu_idx());
        tcb_set_state(pid, TSTATE_READY);
        tqueue_enqueue(rdq, pid);
    }

    spinlock_release(&sched_lk);

    return pid;
}

/**
 * Yield to the next thread in the ready queue.
 * You should set the currently running thread state as ready,
 * and push it back to the ready queue.
 * Then set the state of the popped thread as running, set the
 * current thread id, and switch to the new kernel context.
 * Hint: If you are the only thread that is ready to run,
 * do you need to switch to yourself?
 * Note: the scheduler state is protected by sched_lk (lab 6). Release it
 * with spinlock_release(&sched_lk) before calling kctx_switch, and also on
 * the path where no switch is needed.
 */
void thread_yield(void)
{
    unsigned int oldpid = get_curid();
    unsigned int rdq = NUM_IDS + get_pcpu_idx();
    unsigned int newpid;

    spinlock_acquire(&sched_lk);

    // 1. put the current thread back at the end of the ready queue
    tcb_set_state(oldpid, TSTATE_READY);
    tqueue_enqueue(rdq, oldpid);

    // 2. take the thread at the front of the ready queue and run it
    newpid = tqueue_dequeue(rdq);
    tcb_set_state(newpid, TSTATE_RUN);
    set_curid(newpid);

    // 3. release the lock before switching
    spinlock_release(&sched_lk);

    // 4. switch to the new thread (skipped if it is just us again)
    if (newpid != oldpid)
        kctx_switch(oldpid, newpid);
}

void sched_update(void)
{
    spinlock_acquire(&sched_lk);
    sched_ticks[get_pcpu_idx()] += 1000 / LAPIC_TIMER_INTR_FREQ;
    if (sched_ticks[get_pcpu_idx()] >= SCHED_SLICE) {
        sched_ticks[get_pcpu_idx()] = 0;
        spinlock_release(&sched_lk);
        thread_yield();
    }
    else {
        spinlock_release(&sched_lk);
    }
}

/**
 * Atomically release lock and sleep on chan.
 * Reacquires lock when awakened.
 * Record the channel with tcb_set_chan and set the state to TSTATE_SLEEP,
 * then switch to the next thread in the ready queue of the current CPU
 * (NUM_IDS + get_pcpu_idx()), as in thread_yield.
 */
void thread_sleep(void *chan, spinlock_t *lk)
{
    unsigned int oldpid = get_curid();
    unsigned int rdq = NUM_IDS + get_pcpu_idx();
    unsigned int newpid;

    if (lk == 0)
        KERN_PANIC("sleep without lock");

    // 1. Acquire sched_lk before releasing lk. This makes "release the lock"
    //    and "go to sleep" atomic with respect to thread_wakeup (which also
    //    takes sched_lk), so a wakeup cannot slip into the gap and be lost.
    spinlock_acquire(&sched_lk);
    spinlock_release(lk);

    // 2. Record the channel and mark this thread as sleeping.
    tcb_set_chan(oldpid, chan);
    tcb_set_state(oldpid, TSTATE_SLEEP);

    // 3. Dequeue the next runnable thread of this CPU and make it current.
    //    The sleeping thread is NOT put back into the ready queue.
    newpid = tqueue_dequeue(rdq);
    tcb_set_state(newpid, TSTATE_RUN);
    set_curid(newpid);

    // 4. Release sched_lk and switch to it.
    spinlock_release(&sched_lk);
    kctx_switch(oldpid, newpid);

    // 5. Resumed after a thread_wakeup on our channel: forget the channel
    //    and take the caller's lock again before returning.
    tcb_set_chan(oldpid, (void *) 0);
    spinlock_acquire(lk);
}

/**
 * Wake up all processes sleeping on chan.
 * Each of them becomes ready and goes back to the ready queue of the CPU it
 * ran on (NUM_IDS + tcb_get_cpu(pid)). Hold sched_lk while doing this.
 */
void thread_wakeup(void *chan)
{
    unsigned int pid;

    spinlock_acquire(&sched_lk);

    for (pid = 0; pid < NUM_IDS; pid++) {
        if (tcb_get_state(pid) == TSTATE_SLEEP && tcb_get_chan(pid) == chan) {
            tcb_set_state(pid, TSTATE_READY);
            tqueue_enqueue(NUM_IDS + tcb_get_cpu(pid), pid);
        }
    }

    spinlock_release(&sched_lk);
}
