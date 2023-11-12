#include "common.h"
#include "kernel.h"

extern char __kernel_base[];
extern char __bss[], __bss_end[], __stack_top[];
extern char __free_ram[], __free_ram_end[];
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];

struct process procs[PROCS_MAX];
struct process *current_proc;
struct process *idle_proc;

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long fid, long eid) {
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;

    __asm__ volatile(
        "ecall"
        : "=r"(a0), "=r"(a1)
        : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
        : "memory"
    );
    return (struct sbiret){.error = a0, .value = a1};
}

void putchar(char ch) {
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1);
}

paddr_t alloc_pages(uint32_t n_pages) {
    static paddr_t next_paddr = (paddr_t)__free_ram;
    paddr_t paddr = next_paddr;
    next_paddr += n_pages * PAGE_SIZE;

    if (next_paddr > (paddr_t)__free_ram_end) {
        PANIC("out of memory");
    }

    memset((void *)paddr, 0, n_pages * PAGE_SIZE);
    return paddr;
}

__attribute__((naked))
void switch_context(uint32_t *prev_sp, uint32_t *next_sp) {
    __asm__ volatile(
        "addi sp, sp, -13 * 4\n"
        "sw ra, 4 * 0(sp)\n"
        "sw s0, 4 * 1(sp)\n"
        "sw s1, 4 * 2(sp)\n"
        "sw s2, 4 * 3(sp)\n"
        "sw s2, 3 * 4(sp)\n"
        "sw s3, 4 * 4(sp)\n"
        "sw s4, 5 * 4(sp)\n"
        "sw s5, 6 * 4(sp)\n"
        "sw s6, 7 * 4(sp)\n"
        "sw s7, 8 * 4(sp)\n"
        "sw s8, 9 * 4(sp)\n"
        "sw s9, 10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"
        "sw sp, (a0)\n"
        "lw sp, (a1)\n"
        "lw ra, 4 * 0(sp)\n"
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n"
        "ret\n"
    );
}

void map_page(uint32_t *table1, vaddr_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE)) {
        PANIC("unalilgned vaddr %x", vaddr);
    }

    if (!is_aligned(paddr, PAGE_SIZE)) {
        PANIC("unalilgned paddr %x", paddr);
    }

    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    if ((table1[vpn1] & PAGE_V) == 0) {
        uint32_t pt_paddr = alloc_pages(1);
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
    }

    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    uint32_t *table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = (paddr / PAGE_SIZE) << 10 | flags | PAGE_V;
}

__attribute__((naked))
void user_entry(void) {
    __asm__ volatile(
        "csrw sepc, %[sepc]\n"
        "csrw sstatus, %[sstatus]\n"
        "sret\n"
        :
        : [sepc] "r" (USER_BASE),
        [sstatus] "r" (SSTATUS_SPIE)
    );
}

struct process *create_process(const void *image, size_t image_size) {
    struct process *proc = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            proc = &procs[i];
            break;
        }
    }

    if (proc == NULL) {
        PANIC("no free process slots");
    }

    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    *(--sp) = 0; // s11
    *(--sp) = 0; // s10
    *(--sp) = 0; // s9
    *(--sp) = 0; // s8
    *(--sp) = 0; // s7
    *(--sp) = 0; // s6
    *(--sp) = 0; // s5
    *(--sp) = 0; // s4
    *(--sp) = 0; // s3
    *(--sp) = 0; // s2
    *(--sp) = 0; // s1
    *(--sp) = 0; // s0
    *(--sp) = (uint32_t)user_entry; // ra

    // Map kernel page
    uint32_t *page_table = (uint32_t *)alloc_pages(1);
    for (paddr_t paddr = (paddr_t)__kernel_base; paddr < (paddr_t)__free_ram_end; paddr += PAGE_SIZE) {
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);
    }

    // Map user page
    for (uint32_t offset = 0; offset < image_size; offset += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);
        memcpy((void *)page, image + offset, PAGE_SIZE);
        map_page(page_table, USER_BASE + offset, page, PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }

    proc->pid = i;
    proc->state = PROC_RUNNABLE;
    proc->sp = (vaddr_t)sp;
    proc->page_table = page_table;
    return proc;
}

void yield(void) {
    struct process *next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i + 1) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc-> pid > 0) {
            next = proc;
            break;
        }
    }

    if (next == current_proc) {
        return;
    }

    __asm__ volatile(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        "csrw sscratch, %[sscratch]\n"
        :
        : [satp] "r" (SATP_SV32 | ((uint32_t)next->page_table / PAGE_SIZE)),
        [sscratch] "r" ((uint32_t)&next->stack[sizeof(next->stack)])
    );

    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}

void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    PANIC("trap: scause = %x, stval = %x, user_pc = %x\n", scause, stval, user_pc);
}

__attribute__((naked))
__attribute__((aligned(4)))
void kernel_entry(void) {
    __asm__ volatile(
        // Preserve current sp
        // tmp = sp; sp = sscratch; sscratch = tmp;
        "csrrw sp, sscratch, sp\n"
        "addi sp, sp, -4 * 31\n"
        "sw ra, 4 * 0(sp)\n"
        "sw gp, 4 * 1(sp)\n"
        "sw tp, 4 * 2(sp)\n"
        "sw t0, 4 * 3(sp)\n"
        "sw t1, 4 * 4(sp)\n"
        "sw t2, 4 * 5(sp)\n"
        "sw t3, 4 * 6(sp)\n"
        "sw t4, 4 * 7(sp)\n"
        "sw t5, 4 * 8(sp)\n"
        "sw t6, 4 * 9(sp)\n"
        "sw a0, 4 * 10(sp)\n"
        "sw a1, 4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"

        // Store preserved sp to stack
        "csrr a0, sscratch\n"
        "sw a0, 4 * 30(sp)\n"

        // Reconstruct kernel stack for the process
        "addi a0, sp, 4 * 31\n"
        "csrw sscratch, a0\n"

        // Restore sp
        "mv a0, sp\n"
        "call handle_trap\n"

        "lw ra,  4 * 0(sp)\n"
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"
        "sret\n"
    );
}

struct process *proc_a;
struct process *proc_b;

void proc_a_entry(void) {
    printf("proc_a_entry\n");
    for (;;) {
        putchar('a');
        yield();
        for (int i = 0; i < 3000000000; i++) {
            __asm__ volatile("nop");
        }
    }
}

void proc_b_entry(void) {
    printf("proc_b_entry\n");
    for (;;) {
        putchar('b');
        yield();
        for (int i = 0; i < 3000000000; i++) {
            __asm__ volatile("nop");
        }
    }
}

void kernel_main(void) {
    memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);

    WRITE_CSR(stvec, (uint32_t)kernel_entry);

    idle_proc = create_process(NULL, 0);
    idle_proc->pid = -1;
    current_proc = idle_proc;

    create_process(_binary_shell_bin_start, (size_t)_binary_shell_bin_size);

    printf("\n\nHello World!\n");
    printf("1 + 2 = %d, %x\n", 1 + 2, 0xdeadbeef);

    yield();
    PANIC("switched to idle process\n");
}

__attribute((section(".text.boot")))
__attribute((naked))
void boot(void) {
    __asm__ volatile(
        "mv sp, %[stack_top]\n"
        "j kernel_main\n"
        :
        : [stack_top] "r" (__stack_top)
    );
}
