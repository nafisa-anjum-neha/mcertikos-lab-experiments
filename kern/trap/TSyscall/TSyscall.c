#include <lib/debug.h>
#include <lib/pmap.h>
#include <lib/types.h>
#include <lib/x86.h>
#include <lib/trap.h>
#include <lib/syscall.h>
#include <dev/intr.h>
#include <pcpu/PCPUIntro/export.h>
#include <lib/seg.h>
#include <vmm/MPTOp/export.h>
#include <vmm/MPTComm/export.h>
#include <pmm/MContainer/export.h>
#include <thread/PThread/export.h>
#include <proc/PProc/export.h>

#include "import.h"
#include "bbuf.h"

extern tf_t uctx_pool[NUM_IDS];

#define PDIRSIZE       (PAGESIZE * 1024)
#define VM_USERLO_PDE  (VM_USERLO / PDIRSIZE)
#define VM_USERHI_PDE  (VM_USERHI / PDIRSIZE)

static char sys_buf[NUM_IDS][PAGESIZE];

/**
 * Copies a string from user into buffer and prints it to the screen.
 * This is called by the user level "printf" library as a system call.
 */
void sys_puts(tf_t *tf)
{
    unsigned int cur_pid;
    unsigned int str_uva, str_len;
    unsigned int remain, cur_pos, nbytes;

    cur_pid = get_curid();
    str_uva = syscall_get_arg2(tf);
    str_len = syscall_get_arg3(tf);

    if (!(VM_USERLO <= str_uva && str_uva + str_len <= VM_USERHI)) {
        syscall_set_errno(tf, E_INVAL_ADDR);
        return;
    }

    remain = str_len;
    cur_pos = str_uva;

    while (remain) {
        if (remain < PAGESIZE - 1)
            nbytes = remain;
        else
            nbytes = PAGESIZE - 1;

        if (pt_copyin(cur_pid, cur_pos, sys_buf[cur_pid], nbytes) != nbytes) {
            syscall_set_errno(tf, E_MEM);
            return;
        }

        sys_buf[cur_pid][nbytes] = '\0';
#if LAB == 6
        KERN_INFO("From cpu %d: %s", get_pcpu_idx(), sys_buf[cur_pid]);
#else
        KERN_INFO("%s", sys_buf[cur_pid]);
#endif

        remain -= nbytes;
        cur_pos += nbytes;
    }

    syscall_set_errno(tf, E_SUCC);
}

extern uint8_t _binary___obj_user_pingpong_ping_start[];
extern uint8_t _binary___obj_user_pingpong_pong_start[];
extern uint8_t _binary___obj_user_pingpong_ding_start[];
extern uint8_t _binary___obj_user_fstest_fstest_start[];
extern uint8_t _binary___obj_user_fork_test_fork_test_start[];

/**
 * Spawns a new child process.
 * The user level library function sys_spawn (defined in user/include/syscall.h)
 * takes two arguments [elf_id] and [quota], and returns the new child process id
 * or NUM_IDS (as failure), with appropriate error number.
 * Currently, we have three user processes defined in user/pingpong/ directory,
 * ping, pong, and ding, plus user/fstest (lab 7) and user/fork_test.
 * The linker ELF addresses for those compiled binaries are defined above.
 * Since we do not yet have a file system implemented in mCertiKOS,
 * we statically load the ELF binaries into the memory based on the
 * first parameter [elf_id].
 * For example, ping, pong, ding, fstest and fork_test correspond to the
 * elf_ids 1, 2, 3, 4 and 5, respectively.
 * If the parameter [elf_id] is none of these, then it should return
 * NUM_IDS with the error number E_INVAL_PID. The same error case apply
 * when the proc_create fails.
 * Otherwise, you should mark it as successful, and return the new child process id.
 * Before creating the process, also check that
 *   - the current process can consume [quota] pages (else E_EXCEEDS_QUOTA),
 *   - the child id would not exceed NUM_IDS, i.e.
 *     curid * MAX_CHILDREN + 1 + MAX_CHILDREN <= NUM_IDS
 *     (else E_MAX_NUM_CHILDEN_REACHED),
 *   - the current process has fewer than MAX_CHILDREN children
 *     (else E_INVAL_CHILD_ID),
 * returning NUM_IDS in each of these error cases.
 */
void sys_spawn(tf_t *tf)
{
    unsigned int elf_id = syscall_get_arg2(tf);
    unsigned int quota = syscall_get_arg3(tf);
    unsigned int curid = get_curid();
    void *elf_addr;
    unsigned int pid;

    // 1. can the current process afford to give away quota pages?
    if (container_can_consume(curid, quota) == 0) {
        syscall_set_errno(tf, E_EXCEEDS_QUOTA);
        syscall_set_retval1(tf, NUM_IDS);
        return;
    }

    // 2. would the child id exceed the range of thread ids?
    if (curid * MAX_CHILDREN + 1 + MAX_CHILDREN > NUM_IDS) {
        syscall_set_errno(tf, E_MAX_NUM_CHILDEN_REACHED);
        syscall_set_retval1(tf, NUM_IDS);
        return;
    }

    // 3. does the current process still have room for a child?
    if (container_get_nchildren(curid) >= MAX_CHILDREN) {
        syscall_set_errno(tf, E_INVAL_CHILD_ID);
        syscall_set_retval1(tf, NUM_IDS);
        return;
    }

    // 4. pick the ELF image corresponding to [elf_id]
    switch (elf_id) {
    case 1:
        elf_addr = _binary___obj_user_pingpong_ping_start;
        break;
    case 2:
        elf_addr = _binary___obj_user_pingpong_pong_start;
        break;
    case 3:
        elf_addr = _binary___obj_user_pingpong_ding_start;
        break;
    case 4:
        elf_addr = _binary___obj_user_fstest_fstest_start;
        break;
    case 5:
        elf_addr = _binary___obj_user_fork_test_fork_test_start;
        break;
    default:
        syscall_set_errno(tf, E_INVAL_PID);
        syscall_set_retval1(tf, NUM_IDS);
        return;
    }

    // 5. load the program and create the process
    pid = proc_create(elf_addr, quota);
    if (pid == NUM_IDS) {
        syscall_set_errno(tf, E_INVAL_PID);
        syscall_set_retval1(tf, NUM_IDS);
        return;
    }

    syscall_set_errno(tf, E_SUCC);
    syscall_set_retval1(tf, pid);
}

/**
 * Yields to another thread/process.
 * The user level library function sys_yield (defined in user/include/syscall.h)
 * does not take any argument and does not have any return values.
 * Do not forget to set the error number as E_SUCC.
 */
void sys_yield(tf_t *tf)
{
    thread_yield();
    syscall_set_errno(tf, E_SUCC);
}

/**
 * Forks the current process (lab 5, optional).
 * The child gets a copy of the parent's address space. Instead of copying
 * every page, share the pages read-only and mark them with PTE_COW, then make
 * the private copy in the page fault handler (pgflt_handler in
 * kern/trap/TTrapHandler/TTrapHandler.c) when either process writes to it.
 * Returns the child's process id to the parent and 0 to the child.
 * The test program is user/fork_test (elf_id 5).
 */
void sys_fork(tf_t *tf)
{
    unsigned int curid = get_curid();
    unsigned int quota = container_get_quota(curid);
    unsigned int usage = container_get_usage(curid);
    unsigned int child_quota, child;
    unsigned int pde_index, pte_index, va, pte, perm, nptbl = 0;

    // 1. would the child id exceed the range of thread ids?
    if (curid * MAX_CHILDREN + 1 + MAX_CHILDREN > NUM_IDS) {
        syscall_set_errno(tf, E_MAX_NUM_CHILDEN_REACHED);
        syscall_set_retval1(tf, NUM_IDS);
        return;
    }

    // 2. does the current process still have room for a child?
    if (container_get_nchildren(curid) >= MAX_CHILDREN) {
        syscall_set_errno(tf, E_INVAL_CHILD_ID);
        syscall_set_retval1(tf, NUM_IDS);
        return;
    }

    // count the page tables the child will have to duplicate
    for (pde_index = VM_USERLO_PDE; pde_index < VM_USERHI_PDE; pde_index++)
        if (get_pdir_entry_by_va(curid, pde_index << 22) != 0)
            nptbl++;

    // 3. the child gets half of the pages the parent still has, so that
    //    both of them keep room for page tables and copy-on-write copies
    child_quota = (quota - usage) / 2;
    if (child_quota < nptbl + 8) {
        syscall_set_errno(tf, E_EXCEEDS_QUOTA);
        syscall_set_retval1(tf, NUM_IDS);
        return;
    }

    // 4. create the child process; it starts with an empty user address space
    child = thread_spawn((void *) proc_start_user, curid, child_quota);
    if (child == NUM_IDS) {
        syscall_set_errno(tf, E_INVAL_PID);
        syscall_set_retval1(tf, NUM_IDS);
        return;
    }

    // 5. duplicate the user page tables. The physical pages are shared:
    //    make every mapping read-only in both address spaces and mark it
    //    with PTE_COW, so that a private copy is made on the next write
    //    (see pgflt_handler in kern/trap/TTrapHandler/TTrapHandler.c).
    for (pde_index = VM_USERLO_PDE; pde_index < VM_USERHI_PDE; pde_index++) {
        va = pde_index << 22;
        if (get_pdir_entry_by_va(curid, va) == 0)
            continue;
        if (alloc_ptbl(child, va) == 0)
            KERN_PANIC("sys_fork: no quota for the child's page table\n");
        for (pte_index = 0; pte_index < 1024; pte_index++) {
            pte = get_ptbl_entry_by_va(curid, va | (pte_index << 12));
            if ((pte & PTE_P) == 0)
                continue;
            perm = (pte & 0xFFF & ~PTE_W) | PTE_COW;
            set_ptbl_entry_by_va(curid, va | (pte_index << 12), pte >> 12,
                                 perm);
            set_ptbl_entry_by_va(child, va | (pte_index << 12), pte >> 12,
                                 perm);
        }
    }

    // 6. the child resumes right where the parent entered the kernel, with
    //    the fork return value 0; the parent gets the child's id
    uctx_pool[child] = *tf;
    uctx_pool[child].regs.eax = E_SUCC;
    uctx_pool[child].regs.ebx = 0;
    seg_init_proc(get_pcpu_idx(), child);

    syscall_set_errno(tf, E_SUCC);
    syscall_set_retval1(tf, child);
}

/**
 * Lab 6: producer / consumer.
 * The user programs ping (producer) and pong (consumer) call these system
 * calls (see user/pingpong/). On a machine with 3 or more CPUs, ping runs
 * on CPU 1 and pong on CPU 2.
 *
 * sys_produce puts the next item of the current process into the bounded
 * buffer (bbuf.c); the n-th item (n = 0, 1, ...) of process # pid is
 * pid * 1000 + n. sys_consume takes one item out. Both print what they did:
 *     CPU <cpu>: Process <pid>: Produced <item>
 *     CPU <cpu>: Process <pid>: Consumed <item>
 * and set the error number to E_SUCC.
 */
static unsigned int nproduced[NUM_IDS];

void sys_produce(tf_t *tf)
{
    unsigned int pid = get_curid();
    unsigned int item = pid * 1000 + nproduced[pid];

    nproduced[pid]++;
    bbuf_put(item);

    KERN_INFO("CPU %d: Process %d: Produced %d\n", get_pcpu_idx(), pid, item);
    syscall_set_errno(tf, E_SUCC);
}

void sys_consume(tf_t *tf)
{
    unsigned int pid = get_curid();
    unsigned int item = bbuf_get();

    KERN_INFO("CPU %d: Process %d: Consumed %d\n", get_pcpu_idx(), pid, item);
    syscall_set_errno(tf, E_SUCC);
}
