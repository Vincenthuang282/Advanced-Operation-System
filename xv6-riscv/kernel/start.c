#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

/* Adil */
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[STSIZE * NCPU];

// a scratch area per CPU for machine-mode timer interrupts.
uint64 timer_scratch[NCPU][5];

// assembly code in kernelvec.S for machine-mode timer interrupt.
extern void timervec();

// entry.S jumps here in machine mode on stack0.
void
start()
{
  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);

  // set M Previous Privilege mode to Supervisor, for mret.
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  w_mepc((uint64)main);

  // disable paging for now.
  w_satp(0);

  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // ask for clock interrupts.
  timerinit();

#if 0
  // Adil: enable printf
  if (id == 0) {
    consoleinit();
    printfinit();

    // Adil: testing reading from the ramdisk
    uint64 ramdisk_size = 273176;
    int total_blocks = (ramdisk_size/BSIZE) + 1;

    /* First 4096 bytes are ELF headers */
    uint64 kern_load_addr = RAMDISKBASE - 0x1000;
    for (int i = 0; i < total_blocks; i++) {
      struct buf b;
      b.blockno = i;
      b.valid = 0;
      ramdiskrw(&b);
      memmove((void*) kern_load_addr, (void*) &(b.data), BSIZE);
      
      if (kern_load_addr == RAMDISKBASE)
        dump_hex((void*) kern_load_addr, BSIZE);

      kern_load_addr+=BSIZE;
    }

    // w_mepc((uint64)0x8100001a);

    // Directly jumping to main works too.
    // w_mepc((uint64)0x81000fb0);

    printf("\n");
    printf("bootloader code is executing\n");
  }
#endif

  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}

// arrange to receive timer interrupts.
// they will arrive in machine mode at
// at timervec in kernelvec.S,
// which turns them into software interrupts for
// devintr() in trap.c.
void
timerinit()
{
  // each CPU has a separate source of timer interrupts.
  int id = r_mhartid();

  // ask the CLINT for a timer interrupt.
  int interval = 1000000; // cycles; about 1/10th second in qemu.
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

  // prepare information in scratch[] for timervec.
  // scratch[0..2] : space for timervec to save registers.
  // scratch[3] : address of CLINT MTIMECMP register.
  // scratch[4] : desired interval (in cycles) between timer interrupts.
  uint64 *scratch = &timer_scratch[id][0];
  scratch[3] = CLINT_MTIMECMP(id);
  scratch[4] = interval;
  w_mscratch((uint64)scratch);

  // set the machine-mode trap handler.
  w_mtvec((uint64)timervec);

  // enable machine-mode interrupts.
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // enable machine-mode timer interrupts.
  w_mie(r_mie() | MIE_MTIE);
}