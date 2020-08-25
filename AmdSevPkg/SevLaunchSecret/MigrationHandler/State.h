/* 
 * TARGET STATE 
 *
 * For now, we store the target state in OVMF using 
 * the following structures.
 *
 * pr_regs is from Linux kernel: arch/x86/include/asm/ptrace.h
 */
struct pt_regs {
/*
 * C ABI says these regs are callee-preserved. They aren't saved on kernel entry
 * unless syscall needs a complete, fully filled "struct pt_regs".
 */
	unsigned long r15;
	unsigned long r14;
	unsigned long r13;
	unsigned long r12;
	unsigned long bp;
	unsigned long bx;
/* These regs are callee-clobbered. Always saved on kernel entry. */
	unsigned long r11;
	unsigned long r10;
	unsigned long r9;
	unsigned long r8;
	unsigned long ax;
	unsigned long cx;
	unsigned long dx;
	unsigned long si;
	unsigned long di;
/*
 * On syscall entry, this is syscall#. On CPU exception, this is error code.
 * On hw interrupt, it's IRQ number:
 */
	unsigned long orig_ax;
/* Return frame for iretq */
	unsigned long ip;
	unsigned long cs;
	unsigned long flags;
	unsigned long sp;
	unsigned long ss;
/* top of stack page */
};

// This is from Linux kernel: arch/x86/include/asm/desc_defs.h
struct desc_ptr {
	unsigned short size;
	unsigned long address;
} __attribute__((packed)) ;

#pragma pack(1)
struct cpu_state {
  char magic[8];
  UINT64 version;
  struct pt_regs regs;
  UINT16 ds;
  UINT16 es;
  UINT16 fs;
  UINT16 gs;
  UINT16 tr;
  char qword_pad[6];
  UINT64 fs_base;
  UINT64 gs_base;
  UINT64 cr0;
  UINT64 cr2;
  UINT64 cr3;
  UINT64 cr4;
  UINT64 efer;
  char gdt_pad[6];
  struct desc_ptr gdt_desc;
  char idt_pad[6];
  struct desc_ptr idt;
  char end_magic[8];
};
#pragma pack()
