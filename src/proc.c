#include "proc.h"
#include "elf.h"
#include "fat.h"
#include "gdt.h"
#include "idt.h"
#include "mem.h"
#include "print.h"
#include "x86.h"

struct proc proc_table[MAX_PROCS];
struct proc *current_proc;
static struct context *cpu_context;  // scheduler's saved context
static u32 next_pid = 1;

static struct proc *proc_alloc(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            struct proc *p = &proc_table[i];
            p->pid = next_pid++;
            p->kstack = kalloc(KSTACK_SIZE / PAGE_SIZE);
            if (!p->kstack) return 0;
            memset(p->kstack, 0, KSTACK_SIZE);
            return p;
        }
    }
    return 0;
}

struct proc *proc_create(const char *path) {
    // Read ELF from FAT
    struct fat_dir_entry file;
    if (fat_open(path, &file) != 0) {
        printf("proc: cannot open %s\n", path);
        return 0;
    }

    u32 file_pages = (file.file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    u8 *elf_buf = kalloc(file_pages);
    if (!elf_buf) {
        printf("proc: out of memory for ELF\n");
        return 0;
    }
    fat_read(&file, 0, file.file_size, elf_buf);

    // Validate ELF header
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_buf;
    if (ehdr->e_magic != ELF_MAGIC || ehdr->e_class != ELFCLASS64 ||
        ehdr->e_machine != EM_X86_64) {
        printf("proc: invalid ELF\n");
        kfree(elf_buf, file_pages);
        return 0;
    }

    // Allocate process
    struct proc *p = proc_alloc();
    if (!p) {
        kfree(elf_buf, file_pages);
        return 0;
    }

    // Create user page table
    p->pml4 = create_user_pml4();
    if (!p->pml4) {
        kfree(elf_buf, file_pages);
        return 0;
    }

    // Load PT_LOAD segments
    Elf64_Phdr *phdr = (Elf64_Phdr *)(elf_buf + ehdr->e_phoff);
    for (u16 i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        u64 va_start = phdr[i].p_vaddr & ~(PAGE_SIZE - 1);
        u64 va_end = (phdr[i].p_vaddr + phdr[i].p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        // Build page table flags
        u64 flags = PTE_USER;
        if (phdr[i].p_flags & PF_W) flags |= PTE_WRITE;

        // Allocate and map pages
        for (u64 va = va_start; va < va_end; va += PAGE_SIZE) {
            void *page = kalloc(1);
            if (!page) {
                printf("proc: out of memory mapping segment\n");
                kfree(elf_buf, file_pages);
                return 0;
            }
            memset(page, 0, PAGE_SIZE);

            // Copy file data into this page if applicable
            u64 seg_start = phdr[i].p_vaddr;
            u64 seg_file_end = seg_start + phdr[i].p_filesz;
            if (va + PAGE_SIZE > seg_start && va < seg_file_end) {
                u64 copy_start = (va > seg_start) ? va : seg_start;
                u64 copy_end = (va + PAGE_SIZE < seg_file_end) ? va + PAGE_SIZE : seg_file_end;
                u64 src_off = phdr[i].p_offset + (copy_start - seg_start);
                u64 dst_off = copy_start - va;
                memcpy((u8 *)page + dst_off, elf_buf + src_off, copy_end - copy_start);
            }

            map_page_pml4(p->pml4, va, VIRT_TO_PHYS((u64)page), flags);
        }
    }

    // Map user stack (one page)
    void *stack_page = kalloc(1);
    if (!stack_page) {
        kfree(elf_buf, file_pages);
        return 0;
    }
    memset(stack_page, 0, PAGE_SIZE);
    map_page_pml4(p->pml4, USER_STACK_BASE,
                  VIRT_TO_PHYS((u64)stack_page),
                  PTE_USER | PTE_WRITE);

    // Set up kernel stack: trap frame at top, context below
    u8 *sp = p->kstack + KSTACK_SIZE;

    sp -= sizeof(struct trap_frame);
    p->tf = (struct trap_frame *)sp;
    memset(p->tf, 0, sizeof(struct trap_frame));
    p->tf->cs = USER_CS;
    p->tf->ss = USER_DS;
    p->tf->rip = ehdr->e_entry;
    p->tf->rsp = USER_STACK_TOP;
    p->tf->rflags = 0x202;  // IF=1

    sp -= sizeof(struct context);
    p->context = (struct context *)sp;
    memset(p->context, 0, sizeof(struct context));

    extern void trapret(void);
    p->context->rip = (u64)trapret;

    // Copy name from path
    const char *name = path;
    for (const char *s = path; *s; s++) {
        if (*s == '/') name = s + 1;
    }
    int j = 0;
    while (name[j] && j < 15) { p->name[j] = name[j]; j++; }
    p->name[j] = 0;

    p->state = PROC_RUNNABLE;

    printf("proc: created pid %u '%s' entry=%p\n", p->pid, p->name, ehdr->e_entry);

    kfree(elf_buf, file_pages);
    return p;
}

void yield(void) {
    struct proc *p = current_proc;
    if (!p) return;
    p->state = PROC_RUNNABLE;
    swtch(&p->context, cpu_context);
}

void scheduler(void) {
    for (;;) {
        sti();
        for (int i = 0; i < MAX_PROCS; i++) {
            struct proc *p = &proc_table[i];
            if (p->state != PROC_RUNNABLE) continue;

            p->state = PROC_RUNNING;
            current_proc = p;

            // Switch to process address space
            lcr3(VIRT_TO_PHYS((u64)p->pml4));
            tss_set_rsp0((u64)p->kstack + KSTACK_SIZE);

            swtch(&cpu_context, p->context);

            // Returned from process
            current_proc = 0;
        }
    }
}
