/* GPLv2 (c) Airbus */
#include <debug.h>
#include <types.h>
#include <pagemem.h>
#include <segmem.h>
#include <cr.h>
#include <pic.h>
#include <intr.h>
#include <asm.h>

/* ==========================================================================
 * 1. WRAPPERS ASSEMBLEUR
 * ========================================================================== */

extern uint32_t do_timer_logic(uint32_t old_esp);
extern void do_syscall_logic(uint32_t esp);

__asm__(
    ".text \n"
    ".global timer_wrapper \n"
    "timer_wrapper: \n"
    "    pusha \n"
    "    push %ds; push %es; push %fs; push %gs \n"
    "    mov $0x10, %ax; mov %ax, %ds; mov %ax, %es \n"
    "    push %esp \n"
    "    call do_timer_logic \n"
    "    mov %eax, %esp \n"
    "    pop %gs; pop %fs; pop %es; pop %ds \n"
    "    popa \n"
    "    iret \n"
    
    ".global syscall_wrapper \n"
    "syscall_wrapper: \n"
    "    pusha \n"
    "    push %ds; push %es; push %fs; push %gs \n"
    "    mov $0x10, %ax; mov %ax, %ds; mov %ax, %es \n"
    "    push %esp \n"
    "    call do_syscall_logic \n"
    "    add $4, %esp \n"
    "    pop %gs; pop %fs; pop %es; pop %ds \n"
    "    popa \n"
    "    iret \n"
);

void timer_wrapper(void);
void syscall_wrapper(void);

/* ==========================================================================
 * 2. CONFIGURATION
 * ========================================================================== */

#define P_USER_CODE_BASE 0x00400000 
#define P_SHARED_PAGE    0x00A00000
#define V_SHARED_T1      0x10000000 
#define V_SHARED_T2      0x20000000 
#define V_STACK_USER_TOP 0x40001000 
#define MY_SEG_UCODE     0x2B 
#define MY_SEG_UDATA     0x33 
#define MY_SEG_TSS       0x38 
#define PDE_PRESENT 0x01
#define PDE_RW      0x02
#define PDE_USER    0x04 
#define PDE_PS      0x80 
#define SYSCALL_INT 0x80

struct tss_entry_t { 
    uint32_t link, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi, es, cs, ss, ds, fs, gs, ldt; 
    uint16_t trap, iomap_base; 
} __attribute__((packed));

typedef struct { uint32_t kstack_top; uint32_t esp; uint32_t cr3; } task_t;

static task_t tasks[2];
static int current_task_id = 0;
static struct tss_entry_t my_tss;
uint64_t my_new_gdt[16]; 

static uint8_t kstack_t1[4096] __attribute__((aligned(4096)));
static uint8_t kstack_t2[4096] __attribute__((aligned(4096)));
static uint8_t ustack_t1[4096] __attribute__((aligned(4096)));
static uint8_t ustack_t2[4096] __attribute__((aligned(4096)));
static uint32_t pgd_t1[1024] __attribute__((aligned(4096))), pgd_t2[1024] __attribute__((aligned(4096)));
static uint32_t pt_stack_t1[1024] __attribute__((aligned(4096))), pt_stack_t2[1024] __attribute__((aligned(4096)));
static uint32_t pt_shared_t1[1024] __attribute__((aligned(4096))), pt_shared_t2[1024] __attribute__((aligned(4096)));

/* ==========================================================================
 * 3. SETUP HARDWARE
 * ========================================================================== */

void setup_safe_gdt_and_tss(void) {
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) dtr;
    __asm__ volatile("sgdt %0" : "=m"(dtr));
    uint8_t *old_gdt = (uint8_t *)dtr.base;
    uint8_t *new_gdt_ptr = (uint8_t *)my_new_gdt;
    for (int i = 0; i <= dtr.limit; i++) new_gdt_ptr[i] = old_gdt[i];

    uint8_t *gdt = (uint8_t *)my_new_gdt;
    int i = 5 * 8; 
    gdt[i+0]=0xFF; gdt[i+1]=0xFF; gdt[i+2]=0x00; gdt[i+3]=0x00; gdt[i+4]=0x00; gdt[i+5]=0xFA; gdt[i+6]=0xCF; gdt[i+7]=0x00;
    i = 6 * 8; 
    gdt[i+0]=0xFF; gdt[i+1]=0xFF; gdt[i+2]=0x00; gdt[i+3]=0x00; gdt[i+4]=0x00; gdt[i+5]=0xF2; gdt[i+6]=0xCF; gdt[i+7]=0x00;
    
    uint32_t base = (uint32_t)&my_tss; uint32_t limit = sizeof(my_tss) - 1;
    i = 7 * 8; 
    gdt[i+0]=limit&0xFF; gdt[i+1]=(limit>>8)&0xFF; gdt[i+2]=base&0xFF; gdt[i+3]=(base>>8)&0xFF; gdt[i+4]=(base>>16)&0xFF; gdt[i+5]=0x89; gdt[i+6]=0x00; gdt[i+7]=(base>>24)&0xFF;

    dtr.base = (uint32_t)my_new_gdt; dtr.limit = (8 * 8) - 1;
    __asm__ volatile("lgdt %0" : : "m"(dtr));
    __asm__ volatile("ltr %%ax" :: "a"(MY_SEG_TSS));
}

uint32_t do_timer_logic(uint32_t old_esp) {
    outb(0x20, 0x20);
    tasks[current_task_id].esp = old_esp;
    current_task_id = (current_task_id + 1) % 2;
    my_tss.esp0 = tasks[current_task_id].kstack_top;
    __asm__ volatile("mov %0, %%cr3" :: "r"(tasks[current_task_id].cr3));
    return tasks[current_task_id].esp;
}

void do_syscall_logic(uint32_t esp) {
    uint32_t *stack = (uint32_t*)esp;
    uint32_t ebx = stack[8]; 
    if (ebx == 0) return;
    printf("\n[Syscall] T2 Compteur: %d", *(uint32_t*)ebx);
}

static inline void sys_counter(uint32_t *ptr) {
    __asm__ volatile("int $0x80" : : "b"(ptr));
}

/* ==========================================================================
 * 4. TÂCHES
 * ========================================================================== */

void __attribute__((section(".user"))) user1(void) {
    volatile uint32_t *counter = (volatile uint32_t *)V_SHARED_T1;
    *counter = 100; 
    for (;;) {
        (*counter)++;
        for (volatile int i = 0; i < 50000; i++); 
    }
}

void __attribute__((section(".user"))) user2(void) {
    for (;;) {
        sys_counter((uint32_t *)V_SHARED_T2);
        for (volatile int i = 0; i < 100000; i++);
    }
}

/* ==========================================================================
 * 5. INIT
 * ========================================================================== */

void setup_task_paging(int task_id) {
    uint32_t *pgd = (task_id == 0) ? pgd_t1 : pgd_t2;
    uint32_t *pt_stack = (task_id == 0) ? pt_stack_t1 : pt_stack_t2;
    uint32_t *pt_shared = (task_id == 0) ? pt_shared_t1 : pt_shared_t2;
    uint32_t v_shared = (task_id == 0) ? V_SHARED_T1 : V_SHARED_T2;

    for(int i=0; i<1024; i++) pgd[i] = 0;

    /* === LE CORRECTIF EST ICI === */
    /* On ajoute PDE_USER pour autoriser user1 à s'exécuter à 0x304267 */
    pgd[0] = 0x0 | PDE_PRESENT | PDE_RW | PDE_PS | PDE_USER; 
    
    pgd[1] = P_USER_CODE_BASE | PDE_PRESENT | PDE_RW | PDE_USER | PDE_PS;
    pgd[256] = (uint32_t)pt_stack | PDE_PRESENT | PDE_RW | PDE_USER;
    pt_stack[0] = (uint32_t)((task_id == 0) ? ustack_t1 : ustack_t2) | PDE_PRESENT | PDE_RW | PDE_USER;
    pgd[v_shared >> 22] = (uint32_t)pt_shared | PDE_PRESENT | PDE_RW | PDE_USER;
    pt_shared[0] = P_SHARED_PAGE | PDE_PRESENT | PDE_RW | PDE_USER;

    tasks[task_id].cr3 = (uint32_t)pgd;
}

void forge_context(int task_id) {
    uint32_t *k = (uint32_t*) tasks[task_id].kstack_top;
    *(--k) = MY_SEG_UDATA; *(--k) = V_STACK_USER_TOP; *(--k) = 0x202; *(--k) = MY_SEG_UCODE;
    *(--k) = (task_id == 0) ? (uint32_t)user1 : (uint32_t)user2;
    for(int i=0; i<8; i++) *(--k) = 0;
    for(int i=0; i<4; i++) *(--k) = MY_SEG_UDATA; 
    tasks[task_id].esp = (uint32_t)k;
}

void tp(void) {
    tasks[0].kstack_top = (uint32_t)kstack_t1 + 4096;
    tasks[1].kstack_top = (uint32_t)kstack_t2 + 4096;
    
    setup_safe_gdt_and_tss();
    my_tss.ss0 = 0x10; my_tss.esp0 = tasks[0].kstack_top;
    
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) idtr;
    __asm__ volatile("sidt %0" : "=m"(idtr));
    struct gate { uint16_t l, s; uint8_t r, f; uint16_t h; } __attribute__((packed)) *idt = (void*)idtr.base;
    
    uint32_t b = (uint32_t)timer_wrapper;
    idt[32].l = b & 0xFFFF; idt[32].h = b >> 16; idt[32].s = 0x08; idt[32].f = 0x8E;
    b = (uint32_t)syscall_wrapper;
    idt[0x80].l = b & 0xFFFF; idt[0x80].h = b >> 16; idt[0x80].s = 0x08; idt[0x80].f = 0xEE;

    setup_task_paging(0); setup_task_paging(1);
    forge_context(0); forge_context(1);

    outb(0x43, 0x36); outb(0x40, 11931 & 0xFF); outb(0x40, 11931 >> 8);
    outb(0x21, inb(0x21) & ~0x01);

    debug("[INIT] Launching Task 1...\n");
    
    __asm__ volatile (
        "mov %0, %%cr3 \n\t"
        "mov %%cr4, %%eax \n\t"
        "or $0x00000010, %%eax \n\t" /* PSE ON */
        "mov %%eax, %%cr4 \n\t"
        "mov %%cr0, %%eax \n\t"
        "or $0x80000000, %%eax \n\t" /* PG ON */
        "mov %%eax, %%cr0 \n\t"
        
        "mov %1, %%esp \n\t"
        "pop %%gs; pop %%fs; pop %%es; pop %%ds \n\t"
        "popa \n\t"
        "iret" 
        : : "r" (tasks[0].cr3), "r" (tasks[0].esp) : "eax", "memory"
    );
    while(1);
}
