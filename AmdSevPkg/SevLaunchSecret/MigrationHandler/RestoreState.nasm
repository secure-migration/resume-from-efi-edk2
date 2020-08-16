#include "StateLayout.h"

%define ENABLE_DEBUG

  DEFAULT REL
  SECTION .text

;extern ASM_PFX(MyTarget)
;extern ASM_PFX(gSavedCR0)
;extern ASM_PFX(gSavedCR2)
;extern ASM_PFX(gSavedCR3)
;extern ASM_PFX(gSavedCR4)
;extern ASM_PFX(gSavedRIP)
;extern ASM_PFX(gSavedGDTDesc)
;extern ASM_PFX(gSavedContext)
extern ASM_PFX(gRelocatedRestoreRegisters)
extern ASM_PFX(gRelocatedRestoreRegistersData)
extern ASM_PFX(gTempPGT)
extern ASM_PFX(gMMUCR4Features)
extern ASM_PFX(gRelocatedRestoreStep2)

%define X86_CR4_PGE     BIT7

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

    ; i think this may have been a mistake?
    ; mov     rbx, cr4

    ; Turn PGE back on
    mov     cr4, rbx

    ; Note: Not using the pbe loop from linux kernel

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

    ;mov     rcx, [CPU_DATA + STATE_MAGIC]
    ;DBG_PRINT 'DBG:magic='
    ;DBG_PUT_REG rcx

    DBG_PRINT 'DBG:70'
    mov     rcx, [CPU_DATA + STATE_CR3]
    mov     cr3, rcx
    DBG_PRINT 'DBG:cr3='
    DBG_PUT_REG rcx

    ;; Turn off PGE (Page Global Enabled)
    DBG_PRINT 'DBG:PGEOFF'
    mov     rcx, rbx
    and     rcx, ~X86_CR4_PGE
    DBG_PRINT 'DBG:SETCR4_A'
    mov     cr4, rcx

    ; Force flush TLB
    DBG_PRINT 'DBG:FLUSHTLB'
    mov     rcx, cr3
    mov     cr3, rcx

    ;; Turn PGE back on
    DBG_PRINT 'DBG:SETCR4_B'
    mov     cr4, rbx

    ;lea     r10, [CPU_DATA]
    ;DBG_PRINT 'DBG:data r10_A='
    ;DBG_PUT_REG r10

    ; Currently the cpu_state is store in the next page in
    ; [rel RestoreRegisters + 0x1000]. If (in the future) we find out that
    ; we need to fit everything in one page, we can put the cpu_state in the
    ; last 1KB of this page as follows:
    ;lea      r10, [rel RestoreRegisters + 0xC00]
    ;DBG_PRINT 'DBG:data r10_B='
    ;DBG_PUT_REG r10

    ;DBG_PRINT 'DBG:90'
    ;mov     r9, qword [CPU_DATA + STATE_MAGIC]
    ;DBG_PRINT 'DBG:magic='
    ;DBG_PUT_REG r9

    ;DBG_PRINT 'DBG:95'
    ;mov     r9, cr4
    ;DBG_PRINT 'DBG:cr4='
    ;DBG_PUT_REG r9

    DBG_PRINT 'DBG:110'
    mov     r9, qword [CPU_DATA + STATE_CR0]
    mov     cr0, r9
    DBG_PRINT 'DBG:120'
    mov     r9, qword [CPU_DATA + STATE_CR2]
    mov     cr2, r9

    ; Restore EFER
    DBG_PRINT 'DBG:EFER'
    mov     ecx, 0xc0000080                   ; EFER MSR number
    mov     eax, [CPU_DATA + STATE_EFER]      ; Load low 32-bits into eax
    mov     edx, [CPU_DATA + STATE_EFER + 4]  ; Load high 32-bits into edx
    wrmsr                                     ; Write edx:eax into the EFER MSR

    DBG_PRINT 'DBG:160'
    mov     r14, [CPU_DATA + STATE_REGS_IP]
    DBG_PRINT 'DBG:t.rip='
    DBG_PUT_REG r14
    and     r14, ~0x7  ; align to 8-byte boundary because mov r15, [r14] fetches 8-bytes from memory
    DBG_PRINT 'DBG:t.rip&~0x7='
    DBG_PUT_REG r14
    mov     r15, [r14]
    DBG_PUT_REG r15

    ;DBG_PRINT 'DBG:180'
    ; Restore all registers except rax
    mov     rsp, [CPU_DATA + STATE_REGS_SP]
    mov     rbp, [CPU_DATA + STATE_REGS_BP]
    mov     rsi, [CPU_DATA + STATE_REGS_SI]
    mov     rdi, [CPU_DATA + STATE_REGS_DI]
    ;DBG_PRINT 'DBG:190'
    mov     rbx, [CPU_DATA + STATE_REGS_BX]
    mov     rcx, [CPU_DATA + STATE_REGS_CX]
    mov     rdx, [CPU_DATA + STATE_REGS_DX]
    mov     r8,  [CPU_DATA + STATE_REGS_R8]
    ;DBG_PRINT 'DBG:200'
    mov     r9,  [CPU_DATA + STATE_REGS_R9]
    mov     r10, [CPU_DATA + STATE_REGS_R10]
    mov     r11, [CPU_DATA + STATE_REGS_R11]
    mov     r12, [CPU_DATA + STATE_REGS_R12]
    ;DBG_PRINT 'DBG:210'
    mov     r13, [CPU_DATA + STATE_REGS_R13]
    mov     r14, [CPU_DATA + STATE_REGS_R14]
    mov     r15, [CPU_DATA + STATE_REGS_R15]
    ;DBG_PRINT 'DBG:220'

    ; Restore flags
    ; disabled because this will be performed as part of iret
    ;push    qword [CPU_DATA + STATE_REGS_FLAGS]
    ;popf

    DBG_PRINT 'DBG:260'
    ; Restore GDT
    lgdt    [CPU_DATA + STATE_GDT_DESC]
    DBG_PRINT 'DBG:270'
    ; Restore IDT
    lidt    [CPU_DATA + STATE_IDT]

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

    cli

    DBG_PRINT 'DBG:400'
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

    ;mov     rax, [rax + STATE_REGS_IP]
    ;DBG_PRINT 'DBG:410'
    ;jmp     rax
    ;DBG_PRINT 'DBG:420'
    ;ret

%define TestTargetOffset 0xd00
TIMES TestTargetOffset - (_end_of_RestoreRegisters - ASM_PFX(RestoreRegisters)) DB 0x90

ASM_PFX(TestTarget):
_here_tt:
    lea     rcx, [rel _here_tt]     ; RIP + 0
    DBG_PRINT 'DBG:RIP_TT='
    DBG_PUT_REG rcx
    DBG_PRINT 'DBG:REACHED:TestTarget'
    hlt

%if (ASM_PFX(TestTarget) - ASM_PFX(RestoreRegisters)) != TestTargetOffset
  %assign rr_size _end_of_RestoreRegisters - ASM_PFX(RestoreRegisters)
  %error Size of RestoreRegisters ( rr_size bytes ) is more than TestTargetOffset bytes and it will clash with TestTarget
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
