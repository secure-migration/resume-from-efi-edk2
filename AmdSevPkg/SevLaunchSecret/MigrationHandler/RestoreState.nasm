#include "StateLayout.h"

%define ENABLE_DEBUG

  DEFAULT REL
  SECTION .text

extern ASM_PFX(gRelocatedRestoreRegisters)
extern ASM_PFX(gRelocatedRestoreRegistersData)
extern ASM_PFX(gTempPGT)
extern ASM_PFX(gMMUCR4Features)
extern ASM_PFX(gRelocatedRestoreStep2)

%define X86_CR4_PGE     BIT7

%define FLAG_POSITION   0xf00
%define FLAG_RESTORE_MEMORY_AND_DEVICES_FINISHED 0x3c3c3c3c3c3c3c3c

;
;arg 1:Char to print
;dx must be already set to the debug console port (0x402)
;
%macro DBG_PUT_CHAR   1
    mov     al, %1
    out     dx, al
%endmacro

%macro DBG_PUT_REG 1
%ifdef ENABLE_DEBUG
    mov     dx, 0x402
    DBG_PUT_CHAR 'H'
    DBG_PUT_CHAR 'E'
    DBG_PUT_CHAR 'X'
    DBG_PUT_CHAR '|'
    mov     rax, %1
    %rep 8
        out     dx, al
        shr     rax, 8
    %endrep
    DBG_PUT_CHAR '|'
    DBG_PUT_CHAR 0x0d
    DBG_PUT_CHAR 0x0a
%endif
%endmacro

%macro DBG_PUT_TIME 0
%ifdef ENABLE_DEBUG
    rdtsc
    mov     r14, rdx
    shl     r14, 32
    or      r14, rax
    DBG_PUT_REG r14
%endif
%endmacro


%macro DBG_PUT_CHARS 1
    %strlen len %1
    %assign i 0
    %rep len
        %assign i i+1
        %substr curr_char %1 i
        DBG_PUT_CHAR curr_char
    %endrep
%endmacro

;
;arg 1: String to print (CRLF is added)
;
%macro DBG_PRINT 1
%ifdef ENABLE_DEBUG
    mov     dx, 0x402
    DBG_PUT_CHARS %1
    DBG_PUT_CHAR 0x0d
    DBG_PUT_CHAR 0x0a
%endif
%endmacro

;
; arg 1: MSR address
; arg 2: Offset of value in CPU_DATA
;
%macro RESTORE_MSR 2
    mov     ecx, %1                   ; MSR address
    mov     eax, [CPU_DATA + %2]      ; Load low 32-bits into eax
    mov     edx, [CPU_DATA + %2 + 4]  ; Load high 32-bits into edx
    wrmsr                             ; Write edx:eax into the MSR
%endmacro

;
; arg 1: MSR address
; arg 2: Lower 32-bit value to set
;
%macro WRITE_MSR32 2
    mov     ecx, %1            ; MSR address
    mov     eax, %2            ; Load low 32-bits into eax
    xor     edx, edx           ; Load high 32-bits into edx (=0)
    wrmsr                      ; Write edx:eax into the MSR
%endmacro

;
; Phase 1 prepares copies all needed values from global variables into
; registers so that phase 2 and 3 don't need to use the stack.
;
global ASM_PFX(RestoreStep1)
ASM_PFX(RestoreStep1):

    DBG_PRINT 'RSTR1:74'
    mov     r8, qword [gRelocatedRestoreRegisters]
    mov     r10, qword [gRelocatedRestoreRegistersData]

    DBG_PRINT 'RSTR1:78'
    mov     r11, qword [gTempPGT]
    mov     rbx, qword [gMMUCR4Features]

    DBG_PRINT 'RSTR1:81'
    mov     rcx, qword [gRelocatedRestoreStep2]
    jmp     rcx

;
; Phase 2 switches from the OVMF page tables to the intermediate page tables. It
; is relocated to a page which has the same virtual address in both the OVMF
; page tables and the intermediate page tables, so it keeps executing after CR3
; is switched.
;
; We don't set up proper space for stack, so this code doesn't use the stack at
; all.
;
; Inputs:
;   r11 - Intermediate PGD
;   rbx - Value of CR4
;   r8  - Address of the relocated RestoreRegisters (must be mapped both
;         in intermediate and target page tables)
;
ALIGN EFI_PAGE_SIZE
global ASM_PFX(RestoreStep2)
ASM_PFX(RestoreStep2):

    ; Switch to intermediate PGD (from r11)
    DBG_PRINT 'RSTR2:96'
    mov     cr3, r11
    DBG_PRINT 'RSTR2:98'

    ; Turn off PGE (Page Global Enabled)
    DBG_PRINT 'RSTR2:101'
    mov     rcx, rbx
    DBG_PRINT 'RSTR2:103'
    and     rcx, ~X86_CR4_PGE
    DBG_PRINT 'RSTR2:105'
    mov     cr4, rcx
    DBG_PRINT 'RSTR2:107'

    ; Force flush TLB
    mov     rcx, cr3
    mov     cr3, rcx

    ; Enable PGE back on
    mov     cr4, rbx

    ; Jump to the relocated RestoreRegisters
    jmp	r8


; The CpuStateDataPage (which holds the struct cpu_state) is
; positioned exactly 1 page (0x1000 bytes) after RestoreRegisters. From any
; instruction inside RestoreRegisters we can use this RIP-relative addressing
; to access the cpu_state struct.
%define CPU_DATA rel RestoreRegisters + 0x1000 + CPU_STATE_OFFSET_IN_PAGE

;
; Phase 3 switches from the intermediate page table to the target page table
; (the same one that was active in the source VM), and then continues to
; restore all the CPU registers.
;
; We don't set up proper space for stack, so this code doesn't use the stack at
; all.
;
; Inputs:
;   As explained above in CPU_DATA, this code expects the CpuStateDataPage
;   which holds struct cpu_state to be exactly one page (0x1000 bytes) after
;   the beginning of this function.
;
;   rbx - Value of CR4
;
ALIGN EFI_PAGE_SIZE
global ASM_PFX(RestoreRegisters)
ASM_PFX(RestoreRegisters):
RestoreRegistersStart:

    ; Prevent interrupts during this restore; the final iretq will restore
    ; RFLAGS, which will return the interrupt flag back to its original state
    cli

    ; Switch to the target page tables
    DBG_PRINT 'DBG:70'
    mov     rcx, [CPU_DATA + STATE_CR3]
    mov     cr3, rcx
    DBG_PRINT 'DBG:cr3='
    DBG_PUT_REG rcx

    ; Turn off PGE (Page Global Enabled)
    DBG_PRINT 'DBG:PGEOFF'
    mov     rcx, rbx
    and     rcx, ~X86_CR4_PGE
    DBG_PRINT 'DBG:SETCR4_A'
    mov     cr4, rcx

    ; Force flush TLB
    DBG_PRINT 'DBG:FLUSHTLB'
    mov     rcx, cr3
    mov     cr3, rcx

    ; Restore CR4: Enable PGE back on
    DBG_PRINT 'DBG:SETCR4_B'
    mov     cr4, rbx

    ; Restore CR0 (but with the WP turned off)
    DBG_PRINT 'DBG:110'
    mov     r9, qword [CPU_DATA + STATE_CR0]
    ; Clear WP (Write-Protect) bit of CR0 - this is needed to allow
    ; executing `ltr` later in the process
    btr     r9, 16
    mov     cr0, r9

    ; Restore CR2
    DBG_PRINT 'DBG:120'
    mov     r9, qword [CPU_DATA + STATE_CR2]
    mov     cr2, r9

    DBG_PRINT 'DBG:EFER'
    RESTORE_MSR 0xc0000080, STATE_EFER

    ; --- Start memory and devices restore
    ;
    ; At this point the handler stalls in a busy loop until the memory
    ; location of the flag holds the a value that indicates the restore is
    ; finished (FLAG_RESTORE_MEMORY_AND_DEVICES_FINISHED).
    ;
    ; Out tooling instructions QEMU to load the the target VM's memory and
    ; devices state.  After these are restored, QEMU sets the flag memory
    ; location to the expected flag value which causes the resume state
    ; code to continue running.
    ;
    DBG_PRINT 'TIME1'
    DBG_PUT_TIME

    DBG_PRINT 'DBG:STALL'
    mov     r11, FLAG_RESTORE_MEMORY_AND_DEVICES_FINISHED
    lea     r10, [CPU_DATA + FLAG_POSITION]
    DBG_PUT_REG r10
    mov     r10, [CPU_DATA + FLAG_POSITION]
    DBG_PUT_REG r10

.loop_wait_for_memory_restore:
    mov     r10, [CPU_DATA + FLAG_POSITION]
    cmp     r10, r11        ; Check if memory was already restored
    jz      .wait_done
    pause
    jmp     .loop_wait_for_memory_restore

.wait_done:
    DBG_PRINT 'TIME2'
    DBG_PUT_TIME
    ;
    ; --- End memory and devices restore

    ; Force flush TLB again because memory was reloaded
    mov     rcx, cr3
    mov     cr3, rcx

    ; Restore all registers except rax
    mov     rsp, [CPU_DATA + STATE_REGS_SP]
    mov     rbp, [CPU_DATA + STATE_REGS_BP]
    mov     rsi, [CPU_DATA + STATE_REGS_SI]
    mov     rdi, [CPU_DATA + STATE_REGS_DI]
    mov     rbx, [CPU_DATA + STATE_REGS_BX]
    mov     rcx, [CPU_DATA + STATE_REGS_CX]
    mov     rdx, [CPU_DATA + STATE_REGS_DX]
    mov     r8,  [CPU_DATA + STATE_REGS_R8]
    mov     r9,  [CPU_DATA + STATE_REGS_R9]
    mov     r10, [CPU_DATA + STATE_REGS_R10]
    mov     r11, [CPU_DATA + STATE_REGS_R11]
    mov     r12, [CPU_DATA + STATE_REGS_R12]
    mov     r13, [CPU_DATA + STATE_REGS_R13]
    mov     r14, [CPU_DATA + STATE_REGS_R14]
    mov     r15, [CPU_DATA + STATE_REGS_R15]

    DBG_PRINT 'DBG:260'
    ; Restore GDT
    lgdt    [CPU_DATA + STATE_GDT_DESC]
    DBG_PRINT 'DBG:270'
    ; Restore IDT
    lidt    [CPU_DATA + STATE_IDT]
    DBG_PRINT 'DBG:LDT'
    ; Restore LDT to zero (note: maybe need to restore from the saved state)
    xor     ax, ax
    lldt    ax

    ; Restore segment registers
    DBG_PRINT 'DBG:SEG_DS'
    mov     ax, [CPU_DATA + STATE_DS]
    mov     ds, ax
    DBG_PRINT 'DBG:SEG_ES'
    mov     ax, [CPU_DATA + STATE_ES]
    mov     es, ax
    DBG_PRINT 'DBG:SEG_FS'
    mov     ax, [CPU_DATA + STATE_FS]
    mov     fs, ax
    DBG_PRINT 'DBG:SEG_GS'
    mov     ax, [CPU_DATA + STATE_GS]
    mov     gs, ax

    ; Restore selected MSRs
    DBG_PRINT 'DBG:STAR'
    RESTORE_MSR 0xc0000081, STATE_STAR
    DBG_PRINT 'DBG:LSTAR'
    RESTORE_MSR 0xc0000082, STATE_LSTAR
    DBG_PRINT 'DBG:CSTAR'
    RESTORE_MSR 0xc0000083, STATE_CSTAR
    DBG_PRINT 'DBG:FMASK'
    RESTORE_MSR 0xc0000084, STATE_FMASK
    DBG_PRINT 'DBG:FS_BASE'
    RESTORE_MSR 0xc0000100, STATE_FS_BASE
    DBG_PRINT 'DBG:GS_BASE'
    RESTORE_MSR 0xc0000101, STATE_GS_BASE
    DBG_PRINT 'DBG:KERNELGS_BASE'
    RESTORE_MSR 0xc0000102, STATE_KERNELGS_BASE
    DBG_PRINT 'DBG:TSC_AUX'
    RESTORE_MSR 0xc0000103, STATE_TSC_AUX

    ; Restore task register
    DBG_PRINT 'DBG:TR'
    mov     ax, [CPU_DATA + STATE_TR]
    ltr     ax

    ; Re-enable the original CR0 with WP (Write-Protect) bit turned on
    DBG_PRINT 'DBG:CR0'
    mov     r9, qword [CPU_DATA + STATE_CR0]
    mov     cr0, r9

    ; ----------------------------------
    ;
    ; Note: This section contains hard-coded values that eventually should be
    ; extracted from the source VM state.
    ;

    DBG_PRINT 'DBG:TSC'
    mov     ecx, 0x10          ; MSR address 10h TSC
    mov     eax, 0             ; Load low 32-bits into eax
    mov     edx, 9             ; Load high 32-bits into edx
    wrmsr                      ; Write edx:eax into the MSR

    DBG_PRINT 'DBG:APIC0'
    WRITE_MSR32 0x835, 0x10700 ; APIC register 350h LVT0
    WRITE_MSR32 0x80f, 0x1ff   ; APIC register 0f0h SPIV Spurious Interrupt Vector Register
    WRITE_MSR32 0x838, 0x3cf53 ; APIC register 380h Timer Initial Count Register
    WRITE_MSR32 0x832, 0xec    ; APIC register 320h Timer Local Vector Table Entry

    ; Clear any waiting EOIs
%define APIC_IR_REGS 8
%define APIC_IR_BITS (APIC_IR_REGS * 32)
%define REPETITIONS  16
    mov     ecx, 0x80b         ; MSR address = APIC register 0b0h End Of Interrupt (EOI)
    xor     eax, eax           ; Load low 32-bits into eax
    xor     edx, edx           ; Load high 32-bits into edx
    mov     rdi, (REPETITIONS * APIC_IR_BITS)
.innerloop:
    wrmsr                      ; Write edx:eax into the MSR
    dec     rdi
    cmp     rdi, 0
    jne     .innerloop

    ;
    ; TODO End of hard-coded section
    ;
    ; ----------------------------------

    DBG_PRINT 'DBG:400'
    ; Restored clobbered registers
    mov     rdi, [CPU_DATA + STATE_REGS_DI]
    mov     rcx, [CPU_DATA + STATE_REGS_CX]
    mov     rdx, [CPU_DATA + STATE_REGS_DX]
    mov     rax, [CPU_DATA + STATE_REGS_ORIG_AX]
    ; Point RSP to to the iretq frame structure
    lea     rsp, [CPU_DATA + STATE_REGS_IP]

    ; Atomically restore SS, RSP, RFLAGS, CS and RIP - and resume execution
    ; from the saved CPU state
    iretq

    jmp     $                    ; Never reached

RestoreRegistersEnd:

;
; Verify that RestoreRegisters fits in one page
;
%if (RestoreRegistersEnd - RestoreRegistersStart) >= EFI_PAGE_SIZE
  %assign rr_size RestoreRegistersEnd - RestoreRegistersStart
  %error Size of RestoreRegisters ( rr_size bytes ) is bigger than one page ( EFI_PAGE_SIZE bytes )
%endif


  SECTION .data

;
; The CpuStateDataPage is located exactly 4096 bytes after the step 3 page
; such that all the references to it inside step 3 are relative.
;
ALIGN EFI_PAGE_SIZE
CpuStateDataPage:
    TIMES EFI_PAGE_SIZE DB 0

global ASM_PFX(pgd)
global ASM_PFX(pud)
global ASM_PFX(pmd)
global ASM_PFX(pte)

ALIGN EFI_PAGE_SIZE
ASM_PFX(pgd):
    TIMES EFI_PAGE_SIZE DB 0

ALIGN EFI_PAGE_SIZE
ASM_PFX(pud):
    TIMES EFI_PAGE_SIZE DB 0

ALIGN EFI_PAGE_SIZE
ASM_PFX(pmd):
    TIMES EFI_PAGE_SIZE DB 0

ALIGN EFI_PAGE_SIZE
ASM_PFX(pte):
    TIMES EFI_PAGE_SIZE DB 0
