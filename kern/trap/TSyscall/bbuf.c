#include <lib/spinlock.h>
#include <thread/PThread/export.h>

#include "bbuf.h"

static struct {
    spinlock_t lock;
    unsigned int items[BBUF_SIZE];
    unsigned int head;   // index of the oldest item
    unsigned int count;  // number of items in the buffer
} bbuf;

void bbuf_init(void)
{
    spinlock_init(&bbuf.lock);
    bbuf.head = 0;
    bbuf.count = 0;
}

/*
 * The buffer is protected by bbuf.lock. While it is full (bbuf_put) or
 * empty (bbuf_get), the caller sleeps on a channel; after changing the
 * buffer, the threads waiting for that change are woken up. The address of
 * a field of bbuf is used as a channel: &bbuf.head for "not full" and
 * &bbuf.count for "not empty" (the two are always different addresses).
 */
void bbuf_put(unsigned int item)
{
    spinlock_acquire(&bbuf.lock);

    while (bbuf.count == BBUF_SIZE)
        thread_sleep(&bbuf.head, &bbuf.lock);

    bbuf.items[(bbuf.head + bbuf.count) % BBUF_SIZE] = item;
    bbuf.count++;

    thread_wakeup(&bbuf.count);   // the buffer is not empty any more
    spinlock_release(&bbuf.lock);
}

unsigned int bbuf_get(void)
{
    unsigned int item = 0;

    spinlock_acquire(&bbuf.lock);

    while (bbuf.count == 0)
        thread_sleep(&bbuf.count, &bbuf.lock);

    item = bbuf.items[bbuf.head];
    bbuf.head = (bbuf.head + 1) % BBUF_SIZE;
    bbuf.count--;

    thread_wakeup(&bbuf.head);    // the buffer is not full any more
    spinlock_release(&bbuf.lock);

    return item;
}

unsigned int bbuf_count(void)
{
    return bbuf.count;
}
