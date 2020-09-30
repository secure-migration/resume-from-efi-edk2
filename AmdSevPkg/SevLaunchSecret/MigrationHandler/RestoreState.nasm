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

global ASM_PFX(RestoreStep1)
ASM_PFX(RestoreStep1):

_here_rs1:
    lea     rcx, [rel _here_rs1]     ; RIP + 0
    DBG_PRINT 'DBG:RIP_RS1='
    DBG_PUT_REG rcx

    DBG_PRINT 'RSTR1:74'
    mov     r8, qword [gRelocatedRestoreRegisters]
    mov     r10, qword [gRelocatedRestoreRegistersData]

    DBG_PRINT 'RSTR1:78'
    mov     r11, qword [gTempPGT]
    mov     rbx, qword [gMMUCR4Features]

    DBG_PRINT 'RSTR1:81'
    mov     rcx, qword [gRelocatedRestoreStep2]
    jmp     rcx

; Inputs:
;   r11 - Temporary PGD
;   rbx - Content of CR4
;   r8  - Address of target RestoreRegisters (must be mapped both
;         in temporay and target page tables)
;
ALIGN EFI_PAGE_SIZE
global ASM_PFX(RestoreStep2)
ASM_PFX(RestoreStep2):

_here_rs2:
    lea     rcx, [rel _here_rs2]     ; RIP + 0
    DBG_PRINT 'DBG:RIP_RS2='
    DBG_PUT_REG rcx

    ; Switch to temporary PGD (from r11)
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

    ; Turn PGE back on
    mov     cr4, rbx

    ; Jump to the relocated RestoreRegisters
    jmp	r8


; The RestoreRegistersData page (which holds the struct cpu_state) is
; positioned exactly 1 page (0x1000 bytes) after RestoreRegisters. So from any
; instruction we can use this RIP-relative addressing to access the cpu_state
; struct.
%define CPU_DATA rel RestoreRegisters + 0x1000 + CPU_STATE_OFFSET_IN_PAGE

; Inputs:
;   As explained above in CPU_DATA, this code expects the RestoreRegistersData
;   which holds struct cpu_state to be exactly one page (0x1000 bytes) after
;   the beginning of this function.
;
ALIGN EFI_PAGE_SIZE
global ASM_PFX(RestoreRegisters)
ASM_PFX(RestoreRegisters):

_here_rr:
    lea     rcx, [rel _here_rr]     ; RIP + 0
    DBG_PRINT 'DBG:RIP_RR='
    DBG_PUT_REG rcx

    cli

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

    ; Turn PGE back on
    DBG_PRINT 'DBG:SETCR4_B'
    mov     cr4, rbx

    DBG_PRINT 'DBG:110'
    mov     r9, qword [CPU_DATA + STATE_CR0]
    ; Clear WP (Write-Protect) bit of CR0 - this is needed to allow
    ; calling `ltr` later in the process
    btr     r9, 16
    mov     cr0, r9
    DBG_PRINT 'DBG:120'
    mov     r9, qword [CPU_DATA + STATE_CR2]
    mov     cr2, r9

    DBG_PRINT 'DBG:EFER'
    RESTORE_MSR 0xc0000080, STATE_EFER

    ;; --- Start memory restore
    DBG_PRINT 'TIME1'
    DBG_PUT_TIME

    DBG_PRINT 'DBG:STALL'
    mov     r11, 0x3c3c3c3c3c3c3c3c   ; Expected value
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
    ;; --- End memory restore

    ; Force flush TLB
    mov     rcx, cr3
    mov     cr3, rcx

    DBG_PRINT 'DBG:160'
    mov     r14, [CPU_DATA + STATE_REGS_IP]
    DBG_PRINT 'DBG:t.rip='
    DBG_PUT_REG r14
    and     r14, ~0x7  ; align to 8-byte boundary because mov r15, [r14] fetches 8-bytes from memory
    DBG_PRINT 'DBG:t.rip&~0x7='
    DBG_PUT_REG r14
    mov     r15, [r14]
    DBG_PUT_REG r15

    mov     r14, [CPU_DATA + STATE_REGS_SP]
    DBG_PUT_REG r14
    and     r14, ~0x7  ; align to 8-byte boundary because mov r15, [r14] fetches 8-bytes from memory
    DBG_PRINT 'DBG:t.rsp&~0x7='
    DBG_PUT_REG r14
    mov     r15, [r14]
    DBG_PUT_REG r15

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
    ; Restore LDT to zero - TODO maybe need to restore from State
    xor     ax, ax
    lldt    ax

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

    DBG_PRINT 'DBG:TR'
    mov     ax, [CPU_DATA + STATE_TR]
    ltr     ax

    ; Re-enable the original CR0 with WP (Write-Protect) bit turned on
    DBG_PRINT 'DBG:CR0'
    mov     r9, qword [CPU_DATA + STATE_CR0]
    mov     cr0, r9

; ----------------------------------
;
; TODO This section contains hard-coded values that should be extracted from the source state
;

    DBG_PRINT 'DBG:TSC'
    mov     ecx, 0x10                   ; MSR address
    mov     eax, 0      ; Load low 32-bits into eax
    mov     edx, 9  ; Load high 32-bits into edx
    wrmsr                             ; Write edx:eax into the MSR

    DBG_PRINT 'DBG:APIC0'
    mov     ecx, 0x835         ; MSR address = APIC register 350h LVT0
    mov     eax, 0x10700       ; Load low 32-bits into eax
    xor     edx, edx           ; Load high 32-bits into edx
    wrmsr                      ; Write edx:eax into the MSR
    DBG_PRINT 'DBG:APICS'
    mov     ecx, 0x80f         ; MSR address = APIC register 0f0h SPIV Spurious Interrupt Vector Register
    mov     eax, 0x1ff         ; Load low 32-bits into eax
    xor     edx, edx           ; Load high 32-bits into edx
    wrmsr                      ; Write edx:eax into the MSR
    DBG_PRINT 'DBG:APIC1'
    mov     ecx, 0x838         ; MSR address = APIC register 380h Timer Initial Count Register
    mov     eax, 0x3cf53       ; Load low 32-bits into eax
    xor     edx, edx           ; Load high 32-bits into edx
    wrmsr                      ; Write edx:eax into the MSR
    DBG_PRINT 'DBG:APIC2'
    mov     ecx, 0x832         ; MSR address = APIC register 320h Timer Local Vector Table Entry
    mov     eax, 0xec          ; Load low 32-bits into eax
    xor     edx, edx           ; Load high 32-bits into edx
    wrmsr                      ; Write edx:eax into the MSR
    DBG_PRINT 'DBG:APIC3'

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
    mov     rdi, [CPU_DATA + STATE_REGS_DI]
    mov     rcx, [CPU_DATA + STATE_REGS_CX]
    mov     rdx, [CPU_DATA + STATE_REGS_DX]
    mov     rax, [CPU_DATA + STATE_REGS_ORIG_AX]
    lea     rsp, [CPU_DATA + STATE_REGS_IP]
    iretq

_end_of_RestoreRegisters:

;
; Verify that RestoreRegisters fits in one page
;
%if (_end_of_RestoreRegisters - ASM_PFX(RestoreRegisters)) >= EFI_PAGE_SIZE
  %assign rr_size _end_of_RestoreRegisters - ASM_PFX(RestoreRegisters)
  %error Size of RestoreRegisters ( rr_size bytes ) is bigger than one page ( EFI_PAGE_SIZE bytes )
%endif


  SECTION .data

global ASM_PFX(RestoreRegistersData)
ALIGN EFI_PAGE_SIZE
ASM_PFX(RestoreRegistersData):
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
