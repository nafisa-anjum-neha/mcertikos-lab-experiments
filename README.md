# mCertiKOS — Lab Experiments 1–7

Complete solutions for all seven lab experiments of **CSE 4502 – Operating
Systems**, developed on the mCertiKOS teaching kernel — a small, layered x86
kernel in the style of xv6 / CertiKOS.

Every lab builds on the code written in the previous labs, so the whole course
lives in one tree: `make LAB=<n>` selects how far the kernel boots and which
tests run.

## Status

| Lab | Topic | Test command | Result |
|-----|-------|--------------|--------|
| 1 | Physical memory allocator | `make LAB=1 TEST=1` | **3 / 3** test groups |
| 2 | Memory containers & page tables | `make LAB=2 TEST=1` | **2 / 2** test groups |
| 3 | Virtual memory management | `make LAB=3 TEST=1` | **4 / 4** test groups + demonstration |
| 4 | Kernel threads & scheduling | `make LAB=4 TEST=1` | **4 / 4** test groups |
| 5 | Processes, traps & system calls | `make LAB=5 TEST=1` | **2 / 2** test groups + demonstration + bonus fork/COW |
| 6 | Multiprocessor & synchronization | `make LAB=6 TEST=1` | **3 / 3** test groups + demonstration |
| 7 | Disk & file system | `make LAB=7 TEST=1` | **full `fstest` suite passes** (13/13 checks) |

All required tasks are implemented — the only `TODO [Lab n]` markers left in
the tree are the optional *"write your own test"* prompts inside `test.c`
files, which were not taken.

---

## The experiments

### Lab 1 — Physical memory allocator (`kern/pmm/MAT*`)

Builds the physical page allocation table (AT) from the BIOS memory map and
the page allocator on top of it.

- **MATIntro** — accessors of the AT: `at_is_norm`, `at_set_perm`,
  `at_is_allocated`, `at_set_allocated`.
- **MATInit** — `pmem_init` computes the number of physical pages `nps` from
  the memory map and assigns every page its permission (BIOS-reserved → 0,
  kernel-only below `VM_USERLO` / at or above `VM_USERHI` → 1, else normal).
- **MATOp** — `palloc` (linear scan improved with memoization — a cursor that
  remembers where the last search ended) and `pfree`, both inside
  `mem_lock()` / `mem_unlock()`.

**Result:** `LAB 1 RESULT: 3 / 3 test groups passed.`

### Lab 2 — Memory containers and page tables (`kern/pmm/MContainer`, `kern/vmm/MPTIntro`)

- **MContainer** — `container_init` computes process 0's quota (all free
  normal pages); getters, `container_can_consume`, `container_split`,
  `container_alloc` and `container_free` enforce per-process memory quotas
  under the container spinlock.
- **MPTIntro** — `set_pdir_base` (loads CR3), `get/set/rmv_pdir_entry`,
  `get/set/rmv_ptbl_entry` and their `*_identity` variants: the two-level x86
  page-directory / page-table primitives.

**Result:** `LAB 2 RESULT: 2 / 2 test groups passed.`

### Lab 3 — Virtual memory management (`kern/vmm/MPTOp`, `MPTComm`, `MPTKern`, `MPTNew`)

- **MPTOp** — the six `*_by_va` translate-and-map helpers, plus `idptbl_init`
  (identity page tables: kernel pages get `PTE_P | PTE_W | PTE_G`, the rest
  `PTE_P | PTE_W`).
- **MPTComm** — `pdir_init` (initial page directory of a process),
  `alloc_ptbl`, `free_ptbl`.
- **MPTKern** — `pdir_init_kern` (identity map of the kernel for every
  process), `map_page`, `unmap_page`.
- **MPTNew** — `alloc_page`: allocate and map a page on demand, used by the
  page-fault handler.

**Result:** `LAB 3 RESULT: 4 / 4 test groups passed.` and the kernel reaches
the monitor with paging enabled.

### Lab 4 — Kernel threads and scheduling (`kern/thread/*`)

- **PKCtxNew** — `kctx_new`: quota check, `container_split` for the child,
  child kernel context with `eip` = entry point and `esp` = top of its kernel
  stack.
- **PTCBInit** — `tcb_init`: thread control block initialization.
- **PTQueueInit** — `tqueue_init`, `tqueue_enqueue`, `tqueue_dequeue`,
  `tqueue_remove` for the index-based doubly linked queues (empty / head /
  tail / middle cases; `NUM_IDS` means "none").
- **PThread** — `thread_spawn` and `thread_yield` with the `sched_lk`
  discipline (always released before `kctx_switch`, including the no-switch
  path).

**Result:** `LAB 4 RESULT: 4 / 4 test groups passed.`

### Lab 5 — Processes, traps and system calls (`kern/trap/*`)

- **TSyscallArg** — argument getters/setters that read/write the trap frame
  registers (`eax`=errno/arg1, `ebx`=retval1/arg2, `ecx`=arg3, `edx`=arg4 …).
- **TSyscall** — `sys_spawn`, `sys_puts`, `sys_yield` and the dispatch
  wiring; user processes get their own address spaces.
- **TTrapHandler** — `trap()` entry/return, `exception_handler`, and
  `pgflt_handler` with demand paging.
- **monitor** — `startuser` creates `idle` and the ping/pong/ding processes.
- **Bonus (Task 5.4): fork with copy-on-write** — `sys_fork` checks
  id-range/children/quota (splitting half of the remaining quota, pre-checked
  to leave at least `nptbl + 8` pages), spawns the child with
  `thread_spawn(proc_start_user, …)`, duplicates the user PDEs 256–959 with
  `(flags & ~PTE_W) | PTE_COW` in **both** address spaces, and copies the
  trap frame (child returns `eax = 0`). `pgflt_handler` gained a COW branch:
  on a write fault to a `PTE_COW` page it allocates a private copy, `memcpy`s
  the old page and maps it writable.

**Result:** `LAB 5 RESULT: 2 / 2 test groups passed.`; demonstration shows
ping/pong/ding each in their own address space at `0xe0000000`, and the fork
test proves COW: parent writes `0x5678` while child and grandchild still read
the original `0x12345678`.

### Lab 6 — Multiprocessor and synchronization

- **TTrapInit** — `trap_init(cpu_idx)` fills each CPU's own
  `TRAP_HANDLER[cpu][trapno]` table: exceptions 0–19 + `T_SECEV` →
  `exception_handler`, the ISA/APIC interrupt ranges (+ `IRQ_ERROR`,
  `IRQ_EHCI_2`, local timer, perf counter) → `interrupt_handler`,
  `T_SYSCALL` → `syscall_dispatch`; everything else stays unregistered.
- **PThread** — `thread_sleep(chan, lk)`: acquire `sched_lk` *before*
  releasing `lk` (so a wakeup cannot slip into the gap), record the channel
  and `TSTATE_SLEEP`, dequeue the next thread of this CPU (the sleeper is
  *not* re-queued), release `sched_lk`, `kctx_switch`, then on wake clear the
  channel and re-acquire `lk`. `thread_wakeup(chan)`: under `sched_lk`, every
  `TSTATE_SLEEP` thread on that channel becomes `TSTATE_READY` and is
  appended to the ready queue of the CPU it ran on.
- **bbuf.c** — blocking bounded buffer (capacity 10): `bbuf_put` sleeps while
  full on `&bbuf.head`, `bbuf_get` sleeps while empty on `&bbuf.count`; each
  side wakes the other after changing the buffer.
- **TSyscall** — `sys_produce` / `sys_consume`: item = `pid * 1000 +
  nproduced[pid]`, printed as `CPU <c>: Process <pid>: Produced/Consumed
  <item>`.

**Result:** `LAB 6 RESULT: 3 / 3 test groups passed.`; the demonstration boots
4 CPUs, runs two pings on CPU 1 and two pongs on CPU 2, and every consumed
item equals the first *N* produced items **in FIFO order, each exactly once**
(machine-checked).

### Lab 7 — Disk and file system

- **Disk interrupt** — `interrupt_handler` handles `T_IRQ0 + IRQ_IDE1` with
  `ide_intr()` (completes the request, `thread_wakeup(b)`, starts the next
  queued one) followed by `intr_eoi()`.
- **Directories** (`kern/fs/dir.c`) — `dir_lookup` reads whole `dirent`s
  (panicking on a short read), skips free entries, compares with
  `dir_namecmp` and returns `inode_get(dp->dev, de.inum)` (+ `*poff`);
  `dir_link` rejects duplicates (−1) and writes the new entry at the first
  free slot or appends at the end of the directory.
- **Path names** (`kern/fs/path.c`) — `skipelem` skips leading/trailing
  slashes, truncates elements of `DIRSIZ` or more characters to
  `DIRSIZ - 1` + `'\0'` while still skipping them entirely, and returns `0`
  only for `""` / `"////"`; `namex` locks each inode, rejects non-directories,
  stops one element early for `nameiparent`, and walks down with `dir_lookup`
  — keeping exactly one reference per iteration.
- **File system calls** (`kern/fs/sysfile.c`) — `fdalloc` finds the first free
  descriptor in the process's open-file table; `sys_read`/`sys_write` validate
  the fd and the user buffer range (`[VM_USERLO, VM_USERHI)`), transfer at
  most one page per step through a `static char kbuf[NUM_IDS][PAGESIZE]`
  staging buffer with `pt_copyout`/`pt_copyin`, and stop on a short transfer
  (end of file); `sys_close` clears the table entry before `file_close`;
  `sys_fstat` fills a kernel `file_stat` and copies it out. Every error
  returns `−1` with `E_BADF`, success sets `E_SUCC`.

**Result:** the complete `fstest` suite passes —
`small file`, `big files`, `many creates/unlink`, `rmdot`, `fourteen`,
`bigfile`, `subdir`, `linktest`, `unlinkread`, `dir vs file`,
`empty file name`, `bigdir` — ending with `*******end of tests*******`.

---

## Building and running

**Requirements** — Ubuntu/Debian, Fedora, macOS or Windows (WSL 2) with the
course toolchain (run `bash ../setup.sh`; see the lab handout, Chapter 2).

```sh
make LAB=<n> TEST=1        # build + boot QEMU + run the lab's tests
make LAB=<n>               # build only
make LAB=<n> qemu-nox      # boot the kernel interactively (Ctrl-a x quits)
make qemu-gdb              # debug with GDB in another terminal
```

Notes:

- The tree is rebuilt from scratch whenever `LAB` or `TEST` changes.
- QEMU uses KVM automatically when `/dev/kvm` is available. Without KVM
  (e.g. WSL 2 without device permission) force the TCG interpreter:
  `make LAB=<n> TEST=1 "QEMUOPTS_TCG=-icount shift=auto"`
- `TEST=1` / `qemu*` copy a fresh file-system image from `newfs/` every run —
  required for Lab 7, where `fstest` refuses to run twice on the same disk.
- Lab 6's demonstration (`make LAB=6 && make qemu-nox`) starts ping/pong by
  itself; the producers sleep forever once the buffer is full — this is
  expected, quit with `Ctrl-a x`.

## Repository layout

```
boot/       boot loader (multiboot entry)
kern/       kernel sources
  pmm/        physical memory: MAT* (labs 1–2)
  vmm/        virtual memory: MPT* (labs 2–3)
  thread/     threads, TCBs, ready queues (labs 4, 6)
  trap/       traps, syscalls, dispatch (labs 5–7)
  fs/         file system: inodes, directories, paths, syscalls (lab 7)
  dev/        devices: PIC/APIC, timer, IDE disk, ...
  init/       boot flow, per-LAB configuration
  lib/        printf, strings, spinlocks, monitor, ...
user/       user programs: pingpong, idle, fstest, fork_test, ...
labs/       lab sheets LAB1.md … LAB7.md (objectives, tasks, evaluation)
newfs/      template file-system image (copied fresh on each run)
scripts/    helper scripts
Makefile    build system (LAB / TEST variables)
```

Tasks are marked in the code with `TODO [Lab <n>]`; to list them:

```sh
grep -rn "TODO \[Lab 1\]" kern user
```

## Notes

1. **Collaboration** — the experiments were implemented and tested
   individually by the repository owner; no code was written together with
   or shared from other students.
2. **Scope** — all *required* tasks of Labs 1–7 plus the optional Lab 5
   bonus (fork with copy-on-write). The optional "write your own test"
   prompts in the `test.c` files were not taken.
3. **Handout contradictions found along the way**
   - *Lab 5*: the sheet suggests launching `fork_test` with `spawn(5, 1000)`
     from `idle`, which is impossible — idle already has the maximum of 3
     children (`E_INVAL_CHILD_ID`), and a child of idle would produce a
     grandchild id beyond `NUM_IDS - 1` (both verified empirically). The
     kernel therefore creates `fork_test` as process 2 inside
     `mon_start_user`, giving ids 2 → 7 → 22 (see comments in
     `user/idle/idle.c` and `kern/lib/monitor.c`).
   - *Lab 6*: the handout shows `ping1 = 1, ping2 = 2, pong1 = 4, pong2 = 5`,
     but the two application CPUs race to allocate ids at boot, so the exact
     numbers vary run to run (behaviour and FIFO correctness are unaffected).
4. **Testing environment** — all tests and demonstrations were run in QEMU
   under WSL 2 using TCG (`-icount shift=auto`), because `/dev/kvm` was not
   accessible.
