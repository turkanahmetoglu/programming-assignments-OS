#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "vm_dbg.h"

#define NOPS (16)

#define OPC(i) ((i)>>12)
#define DR(i) (((i)>>9)&0x7)
#define SR1(i) (((i)>>6)&0x7)
#define SR2(i) ((i)&0x7)
#define FIMM(i) ((i>>5)&01)
#define IMM(i) ((i)&0x1F)
#define SEXTIMM(i) sext(IMM(i),5)
#define FCND(i) (((i)>>9)&0x7)
#define POFF(i) sext((i)&0x3F, 6)
#define POFF9(i) sext((i)&0x1FF, 9)
#define POFF11(i) sext((i)&0x7FF, 11)
#define FL(i) (((i)>>11)&1)
#define BR(i) (((i)>>6)&0x7)
#define TRP(i) ((i)&0xFF)   

//New OS declarations
//  OS Bookkeeping constants 
#define OS_MEM_SIZE 4096    // OS Region size. At the same time, constant free-list starting header 

#define Cur_Proc_ID 0       // id of the current process
#define Proc_Count 1        // total number of processes, including ones that finished executing.
#define OS_STATUS 2         // Bit 0 shows whether the PCB list is full or not

//  Process list and PCB related constants
#define PCB_SIZE 6  // Number of fields in a PCB

#define PID_PCB 0   // holds the pid for a process
#define PC_PCB 1    // value of the program counter for the process
#define BSC_PCB 2   // base value of code section for the process
#define BDC_PCB 3   // bound value of code section for the process
#define BSH_PCB 4   // value of heap section for the process
#define BDH_PCB 5   // holds the bound value of heap section for the process

#define CODE_SIZE 4096
#define HEAP_INIT_SIZE 4096
//New OS declarations


bool running = true;

typedef void (*op_ex_f)(uint16_t i);
typedef void (*trp_ex_f)();

enum { trp_offset = 0x20 };
enum regist { R0 = 0, R1, R2, R3, R4, R5, R6, R7, RPC, RCND, RBSC, RBDC, RBSH, RBDH, RCNT };
enum flags { FP = 1 << 0, FZ = 1 << 1, FN = 1 << 2 };

uint16_t mem[UINT16_MAX] = {0};
uint16_t reg[RCNT] = {0};
uint16_t PC_START = 0x3000;

void initOS();
int createProc(char *fname, char* hname);
void loadProc(uint16_t pid);
static inline void tyld();
uint16_t allocMem(uint16_t ptbr, uint16_t vpn, uint16_t read, uint16_t write);
int freeMem(uint16_t vpn, uint16_t ptbr);
static inline uint16_t mr(uint16_t address);
static inline void mw(uint16_t address, uint16_t val);
static inline void tbrk();
static inline void thalt();
static inline void trap(uint16_t i);

static inline uint16_t sext(uint16_t n, int b) { return ((n>>(b-1))&1) ? (n|(0xFFFF << b)) : n; }
static inline void uf(enum regist r) {
    if (reg[r]==0) reg[RCND] = FZ;
    else if (reg[r]>>15) reg[RCND] = FN;
    else reg[RCND] = FP;
}
static inline void add(uint16_t i)  { reg[DR(i)] = reg[SR1(i)] + (FIMM(i) ? SEXTIMM(i) : reg[SR2(i)]); uf(DR(i)); }
static inline void and(uint16_t i)  { reg[DR(i)] = reg[SR1(i)] & (FIMM(i) ? SEXTIMM(i) : reg[SR2(i)]); uf(DR(i)); }
static inline void ldi(uint16_t i)  { reg[DR(i)] = mr(mr(reg[RPC]+POFF9(i))); uf(DR(i)); }
static inline void not(uint16_t i)  { reg[DR(i)]=~reg[SR1(i)]; uf(DR(i)); }
static inline void br(uint16_t i)   { if (reg[RCND] & FCND(i)) { reg[RPC] += POFF9(i); } }
static inline void jsr(uint16_t i)  { reg[R7] = reg[RPC]; reg[RPC] = (FL(i)) ? reg[RPC] + POFF11(i) : reg[BR(i)]; }
static inline void jmp(uint16_t i)  { reg[RPC] = reg[BR(i)]; }
static inline void ld(uint16_t i)   { reg[DR(i)] = mr(reg[RPC] + POFF9(i)); uf(DR(i)); }
static inline void ldr(uint16_t i)  { reg[DR(i)] = mr(reg[SR1(i)] + POFF(i)); uf(DR(i)); }
static inline void lea(uint16_t i)  { reg[DR(i)] =reg[RPC] + POFF9(i); uf(DR(i)); }
static inline void st(uint16_t i)   { mw(reg[RPC] + POFF9(i), reg[DR(i)]); }
static inline void sti(uint16_t i)  { mw(mr(reg[RPC] + POFF9(i)), reg[DR(i)]); }
static inline void str(uint16_t i)  { mw(reg[SR1(i)] + POFF(i), reg[DR(i)]); }
static inline void rti(uint16_t i) {} // unused
static inline void res(uint16_t i) {} // unused
static inline void tgetc() { reg[R0] = getchar(); }
static inline void tout() { fprintf(stdout, "%c", (char)reg[R0]); }
static inline void tputs() {
    uint16_t *p = mem + reg[R0];
    while(*p) {
        fprintf(stdout, "%c", (char)*p);
        p++;
    }
}
static inline void tin() { reg[R0] = getchar(); fprintf(stdout, "%c", reg[R0]); }
static inline void tputsp() { /* Not Implemented */ }

static inline void tinu16() { fscanf(stdin, "%hu", &reg[R0]); }
static inline void toutu16() { fprintf(stdout, "%hu\n", reg[R0]); }


trp_ex_f trp_ex[10] = { tgetc, tout, tputs, tin, tputsp, thalt, tinu16, toutu16, tyld, tbrk };
static inline void trap(uint16_t i) { trp_ex[TRP(i)-trp_offset](); }
op_ex_f op_ex[NOPS] = { /*0*/ br, add, ld, st, jsr, and, ldr, str, rti, not, ldi, sti, jmp, res, lea, trap };

void ld_img(char *fname, uint16_t offset, uint16_t size) {
    FILE *in = fopen(fname, "rb");
    if (NULL==in) {
        fprintf(stderr, "Cannot open file %s.\n", fname);
        exit(1);    
    }
    uint16_t *p = mem + offset;
    fread(p, sizeof(uint16_t), (size), in);
    fclose(in);
}

void run(char* code, char* heap) {

    while(running) {
        uint16_t i = mr(reg[RPC]++);
        op_ex[OPC(i)](i);
    }
}


// YOUR CODE STARTS HERE

// Constants

#undef PCB_SIZE
#define PCB_SIZE 3
#define PAGE_SIZE 2048    // words per page frame
#define PAGE_TABLE_BASE 4096    // frame 2 starts at word 4096 (0x1000) = OS_MEM_SIZE
#define TOTAL_FRAMES 32      // 128KB / 4KB = 32 page frames
#define PTE_PER_PROC 32      // one PTE per VPN (5-bit VPN → 32 entries)
#define PCB_BASE 12      // first PCB starts at mem[12]

#define RPTBR    RBSC           // page-table base register reuses RBSC slot
#define PTBR     RPTBR          // alias used by test files
#define PTBR_PCB BSC_PCB        // PCB offset 2 stores the PTBR

#define VA_VPN(va)    ((va) >> 11)
#define VA_OFFSET(va) ((va) & 0x07FF)

// VPN layout in VAS
#define RESERVED_VPN   6   // VPNs 0-5 are reserved (0x0000-0x2FFF)
#define CODE_VPN_START 6   // VPN 6 = 0x3000, VPN 7 = 0x3800
#define HEAP_VPN_START 8   // VPN 8 = 0x4000, VPN 9 = 0x4800

#define PTE_VALID_BIT  0        // bit 0: valid
#define PTE_READ_BIT   1        // bit 1: read permission
#define PTE_WRITE_BIT  2        // bit 2: write permission
#define PTE_PFN_SHIFT  11       // PFN occupies bits [15:11]

// Bitmap helpers

static int bitmap_get(int frame) {
    if(frame < 16) return (mem[3] >> (15 - frame)) & 1;
    else           return (mem[4] >> (31 - frame)) & 1;
}

static void bitmap_set(int frame, int val) {
    if(frame < 16){
        if(val) mem[3] |=  (uint16_t)(1u << (15 - frame));
        else    mem[3] &= ~(uint16_t)(1u << (15 - frame));
    } else {
        if(val) mem[4] |=  (uint16_t)(1u << (31 - frame));
        else    mem[4] &= ~(uint16_t)(1u << (31 - frame));
    }
}

// PCB address helper
static uint16_t pcb_addr(uint16_t pid) {
    return (uint16_t)(PCB_BASE + pid * PCB_SIZE);
}

// Round-robin search for next runnable process starting after start_pid.
// Returns the next runnable PID, or -1 if none found.
static int find_next_proc(uint16_t start_pid) {
    uint16_t total = mem[Proc_Count];
    if(total == 0) return -1;
    for(uint16_t i = 1; i <= total; i++){
        uint16_t try_pid = (uint16_t)((start_pid + i) % total);
        uint16_t addr    = pcb_addr(try_pid);
        if(mem[addr + PID_PCB] != 0xffff) return (int)try_pid;
    }
    return -1;
}

void initOS() {
    mem[Cur_Proc_ID] = 0xffff;
    mem[Proc_Count]  = 0;
    mem[OS_STATUS]   = 0x0000;
    mem[3] = 0xffff;    // all 32 frames free (1 = free)
    mem[4] = 0xffff;
    bitmap_set(0, 0);   // frame 0: OS data
    bitmap_set(1, 0);   // frame 1: OS data
    bitmap_set(2, 0);   // frame 2: page tables
}

// process functions to implement
int createProc(char *fname, char* hname) {
    if(mem[OS_STATUS] & 1){
        printf("The OS memory region is full. Cannot create a new PCB.\n");
        return 0;
    }

    uint16_t pid = mem[Proc_Count];
    uint16_t pcb = pcb_addr(pid);

    if(pcb + PCB_SIZE > PAGE_TABLE_BASE){
        mem[OS_STATUS] |= 1;
        printf("The OS memory region is full. Cannot create a new PCB.\n");
        return 0;
    }

    uint16_t ptbr = (uint16_t)(PAGE_TABLE_BASE + pid * PTE_PER_PROC);

    if(ptbr + PTE_PER_PROC > PAGE_TABLE_BASE + PAGE_SIZE){
        mem[OS_STATUS] |= 1;
        printf("The OS memory region is full. Cannot create a new PCB.\n");
        return 0;
    }

    // allocate 2 code pages (VPN 6 & 7, read-only)
    uint16_t c0 = allocMem(ptbr, CODE_VPN_START,   UINT16_MAX, 0);
    if(!c0){
        printf("Cannot create code segment.\n");
        return 0;
    }
    uint16_t c1 = allocMem(ptbr, CODE_VPN_START+1, UINT16_MAX, 0);
    if(!c1){
        freeMem(CODE_VPN_START, ptbr);
        printf("Cannot create code segment.\n");
        return 0;
    }

    // allocate 2 heap pages (VPN 8 & 9, read+write)
    uint16_t h0 = allocMem(ptbr, HEAP_VPN_START,   UINT16_MAX, UINT16_MAX);
    if(!h0){
        freeMem(CODE_VPN_START, ptbr);
        freeMem(CODE_VPN_START+1, ptbr);
        printf("Cannot create heap segment.\n");
        return 0;
    }
    uint16_t h1 = allocMem(ptbr, HEAP_VPN_START+1, UINT16_MAX, UINT16_MAX);
    if(!h1){
        freeMem(CODE_VPN_START, ptbr);
        freeMem(CODE_VPN_START+1, ptbr);
        freeMem(HEAP_VPN_START, ptbr);
        printf("Cannot create heap segment.\n");
        return 0;
    }

    // load binary images into physical frames
    ld_img(fname, c0 * PAGE_SIZE, PAGE_SIZE);
    ld_img(hname, h0 * PAGE_SIZE, PAGE_SIZE);

    mem[pcb + PID_PCB]  = pid;
    mem[pcb + PC_PCB]   = PC_START;  // 0x3000
    mem[pcb + PTBR_PCB] = ptbr;

    mem[Proc_Count]++;
    return 1;
}

void loadProc(uint16_t pid) {
    uint16_t pcb     = pcb_addr(pid);
    reg[RPC]         = mem[pcb + PC_PCB];
    reg[RPTBR]       = mem[pcb + PTBR_PCB];
    mem[Cur_Proc_ID] = pid;
}

int freeMem(uint16_t vpn, uint16_t ptbr) {
    uint16_t pte_addr = ptbr + vpn;
    uint16_t pte      = mem[pte_addr];

    if(!(pte & (1u << PTE_VALID_BIT))) return 0;  // not allocated

    uint16_t pfn = (pte >> PTE_PFN_SHIFT) & 0x1F;
    mem[pte_addr] &= ~(uint16_t)(1u << PTE_VALID_BIT);  // clear valid bit only
    bitmap_set(pfn, 1);
    return 1;
}

uint16_t allocMem(uint16_t ptbr, uint16_t vpn, uint16_t read, uint16_t write) {
    uint16_t pte_addr = ptbr + vpn;

    if(mem[pte_addr] & (1u << PTE_VALID_BIT)) return 0;  // already allocated

    // linear search for first free frame
    int pfn = -1;
    for(int f = 0; f < TOTAL_FRAMES; f++){
        if(bitmap_get(f)){ pfn = f; break; }
    }
    if(pfn == -1) return 0;  // no free frame

    bitmap_set(pfn, 0);

    // build PTE: PFN in [15:11], flags in [2:0]
    uint16_t pte = (uint16_t)((pfn & 0x1F) << PTE_PFN_SHIFT);
    if(write == UINT16_MAX) pte |= (uint16_t)(1u << PTE_WRITE_BIT);
    if(read  == UINT16_MAX) pte |= (uint16_t)(1u << PTE_READ_BIT);
    pte |= (uint16_t)(1u << PTE_VALID_BIT);
    mem[pte_addr] = pte;

    return (uint16_t)pfn;
}

// instructions to implement
static inline void tbrk() {
    uint16_t r0       = reg[R0];
    uint16_t vpn      = (r0 >> 11) & 0x1F;
    uint16_t wr       = (r0 >> 2)  & 1;
    uint16_t rd       = (r0 >> 1)  & 1;
    uint16_t do_alloc = r0 & 1;

    uint16_t cur_pid = mem[Cur_Proc_ID];

    if(vpn < RESERVED_VPN){
        printf("Cannot allocate/free memory for the reserved segment.\n");
        thalt();
        return;
    }

    if(do_alloc){
        printf("Heap increase requested by process %u.\n", cur_pid);
        uint16_t pte = mem[reg[RPTBR] + vpn];
        if(pte & (1u << PTE_VALID_BIT)){
            printf("Cannot allocate memory for page %u of pid %u since it is already allocated.\n",
                   vpn, cur_pid);
            return;
        }
        uint16_t r_flag = rd ? UINT16_MAX : 0;
        uint16_t w_flag = wr ? UINT16_MAX : 0;
        if(!allocMem(reg[RPTBR], vpn, r_flag, w_flag)){
            printf("Cannot allocate more space for pid %u since there is no free page frames.\n",
                   cur_pid);
        }
    } else {
        printf("Heap decrease requested by process %u.\n", cur_pid);
        uint16_t pte = mem[reg[RPTBR] + vpn];
        if(!(pte & (1u << PTE_VALID_BIT))){
            printf("Cannot free memory of page %u of pid %u since it is not allocated.\n",
                   vpn, cur_pid);
            return;
        }
        freeMem(vpn, reg[RPTBR]);
    }
}

static inline void tyld() {
    uint16_t cur_pid = mem[Cur_Proc_ID];
    uint16_t pcb     = pcb_addr(cur_pid);

    int next = find_next_proc(cur_pid);
    if(next == -1 || (uint16_t)next == cur_pid) return;  // only one process, keep running

    // save CPU state only when actually switching
    mem[pcb + PC_PCB]   = reg[RPC];
    mem[pcb + PTBR_PCB] = reg[RPTBR];
    printf("We are switching from process %u to %u.\n", cur_pid, (uint16_t)next);
    loadProc((uint16_t)next);
}

// instructions to modify
static inline void thalt() {
    uint16_t cur_pid = mem[Cur_Proc_ID];
    uint16_t pcb     = pcb_addr(cur_pid);

    // free all allocated pages for this process
    for(uint16_t vpn = 0; vpn < PTE_PER_PROC; vpn++){
        if(mem[reg[RPTBR] + vpn] & (1u << PTE_VALID_BIT)){
            freeMem(vpn, reg[RPTBR]);
        }
    }

    mem[pcb + PID_PCB] = 0xffff;  // mark PCB as terminated

    int next = find_next_proc(cur_pid);
    if(next == -1){ running = false; return; }

    loadProc((uint16_t)next);  // silently load next process
}

static inline uint16_t mr(uint16_t address) {
    uint16_t vpn    = VA_VPN(address);
    uint16_t offset = VA_OFFSET(address);

    if(vpn < RESERVED_VPN){ fprintf(stdout, "Segmentation fault.\n"); exit(1); }

    uint16_t pte_addr = reg[RPTBR] + vpn;
    uint16_t pte      = mem[pte_addr];

    if(!(pte & (1u << PTE_VALID_BIT))){ fprintf(stdout, "Segmentation fault inside free space.\n"); exit(1); }
    if(!(pte & (1u << PTE_READ_BIT))){ fprintf(stdout, "Cannot read the page.\n"); exit(1); }

    uint16_t pfn  = (pte >> PTE_PFN_SHIFT) & 0x1F;
    uint16_t phys = (uint16_t)(pfn * PAGE_SIZE + offset);
    return mem[phys];
}

static inline void mw(uint16_t address, uint16_t val) {
    uint16_t vpn    = VA_VPN(address);
    uint16_t offset = VA_OFFSET(address);

    if(vpn < RESERVED_VPN){ fprintf(stdout, "Segmentation fault.\n"); exit(1); }

    uint16_t pte_addr = reg[RPTBR] + vpn;
    uint16_t pte      = mem[pte_addr];

    if(!(pte & (1u << PTE_VALID_BIT))){ fprintf(stdout, "Segmentation fault inside free space.\n"); exit(1); }
    if(!(pte & (1u << PTE_WRITE_BIT))){ fprintf(stdout, "Cannot write to a read-only page.\n"); exit(1); }

    uint16_t pfn  = (pte >> PTE_PFN_SHIFT) & 0x1F;
    uint16_t phys = (uint16_t)(pfn * PAGE_SIZE + offset);
    mem[phys] = val;
}

// YOUR CODE ENDS HERE