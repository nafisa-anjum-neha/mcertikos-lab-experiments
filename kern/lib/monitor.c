// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <lib/debug.h>
#include <lib/elf.h>
#include <lib/types.h>
#include <lib/gcc.h>
#include <lib/string.h>
#include <lib/x86.h>
#include <lib/thread.h>
#include <lib/monitor.h>
#include <dev/console.h>
#include <vmm/MPTIntro/export.h>
#include <vmm/MPTNew/export.h>

#define CMDBUF_SIZE 80  // enough for one VGA text line

struct Command {
    const char *name;
    const char *desc;
    // return -1 to force monitor to exit
    int (*func)(int argc, char **argv, struct Trapframe *tf);
};

static struct Command commands[] = {
    {"help", "Display this list of commands", mon_help},
    {"kerninfo", "Display information about the kernel", mon_kerninfo},
    {"backtrace", "Display a backtrace of the kernel stack", mon_backtrace},
    {"startuser", "Start the user idle process (lab 5)", mon_start_user},
};

#define NCOMMANDS (sizeof(commands) / sizeof(commands[0]))

extern uint8_t _binary___obj_user_idle_idle_start[];
extern uint8_t _binary___obj_user_fork_test_fork_test_start[];
extern unsigned int proc_create(void *elf_addr, unsigned int quota);
extern void tqueue_remove(unsigned int chid, unsigned int pid);
extern void tcb_set_state(unsigned int pid, unsigned int state);
extern void set_curid(unsigned int curid);
extern void kctx_switch(unsigned int from_pid, unsigned int to_pid);

/***** Implementations of basic kernel monitor commands *****/
int mon_help(int argc, char **argv, struct Trapframe *tf)
{
    int i;

    for (i = 0; i < NCOMMANDS; i++)
        dprintf("%s - %s\n", commands[i].name, commands[i].desc);
    return 0;
}

int mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
    extern uint8_t start[], etext[], edata[], end[];

    dprintf("Special kernel symbols:\n");
    dprintf("  start  %08x\n", start);
    dprintf("  etext  %08x\n", etext);
    dprintf("  edata  %08x\n", edata);
    dprintf("  end    %08x\n", end);
    dprintf("Kernel executable memory footprint: %dKB\n",
            ROUNDUP(end - start, 1024) / 1024);
    return 0;
}

int mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
    unsigned int *ebp = (unsigned int *) read_ebp();

    while (ebp != 0) {
        dprintf("ebp %08x eip %08x\n", (unsigned int) ebp, ebp[1]);
        ebp = (unsigned int *) ebp[0];
    }
    return 0;
}

int mon_start_user(int argc, char **argv, struct Trapframe *tf)
{
    unsigned int idle_pid;
#if LAB == 5
    unsigned int fork_pid;
#endif

    idle_pid = proc_create(_binary___obj_user_idle_idle_start, 10000);
    KERN_DEBUG("process idle %d is created.\n", idle_pid);

#if LAB == 5
    /*
     * Lab 5 (optional bonus): run user/fork_test as process 2. The lab
     * manual says to launch it with spawn(5, 1000) from idle, but that
     * cannot work: idle already has MAX_CHILDREN = 3 children, so
     * sys_spawn rejects a fourth one with E_INVAL_CHILD_ID (verified),
     * and a child of idle would get id 7, whose own child (22) and
     * grandchild (67) do not fit into NUM_IDS = 64 either. Created from
     * process 0 here, fork_test is process 2, its child is 7 and its
     * grandchild 22, so both forks fit into NUM_IDS.
     */
    fork_pid = proc_create(_binary___obj_user_fork_test_fork_test_start, 1000);
    KERN_DEBUG("process fork_test %d is created.\n", fork_pid);
#endif

    KERN_INFO("Start user-space ... \n");

    tqueue_remove(NUM_IDS, idle_pid);
    tcb_set_state(idle_pid, TSTATE_RUN);
    set_curid(idle_pid);
    kctx_switch(0, idle_pid);

    KERN_PANIC("mon_start_user() should never reach here.\n");
    return 0;
}

/***** Kernel monitor command interpreter *****/
#define WHITESPACE "\t\r\n "
#define MAXARGS    16

static int runcmd(char *buf, struct Trapframe *tf)
{
    int argc;
    char *argv[MAXARGS];
    int i;

    // Parse the command buffer into whitespace-separated arguments
    argc = 0;
    argv[argc] = 0;
    while (1) {
        // gobble whitespace
        while (*buf && strchr(WHITESPACE, *buf))
            *buf++ = 0;
        if (*buf == 0)
            break;

        // save and scan past next arg
        if (argc == MAXARGS - 1) {
            dprintf("Too many arguments (max %d)\n", MAXARGS);
            return 0;
        }
        argv[argc++] = buf;
        while (*buf && !strchr(WHITESPACE, *buf))
            buf++;
    }
    argv[argc] = 0;

    // Lookup and invoke the command
    if (argc == 0)
        return 0;
    for (i = 0; i < NCOMMANDS; i++) {
        if (strcmp(argv[0], commands[i].name) == 0)
            return commands[i].func(argc, argv, tf);
    }
    dprintf("Unknown command '%s'\n", argv[0]);
    return 0;
}

void monitor(struct Trapframe *tf)
{
    char *buf;

    dprintf("\n****************************************\n\n");
    dprintf("Welcome to the mCertiKOS kernel monitor!\n");
    dprintf("\n****************************************\n\n");
    dprintf("Type 'help' for a list of commands.\n");

    while (1) {
        buf = (char *) readline("$> ");
        if (buf != NULL)
            if (runcmd(buf, tf) < 0)
                break;
    }
}
