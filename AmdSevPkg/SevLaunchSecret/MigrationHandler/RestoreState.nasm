
%define ENABLE_DEBUG

  DEFAULT REL
  SECTION .text

extern ASM_PFX(MyTarget)
extern ASM_PFX(gSavedCR0)
extern ASM_PFX(gSavedCR2)
extern ASM_PFX(gSavedCR3)
extern ASM_PFX(gSavedCR4)
extern ASM_PFX(gSavedRIP)
extern ASM_PFX(gSavedGDTDesc)
extern ASM_PFX(gSavedContext)
extern ASM_PFX(gRelocatedRestoreRegisters)
extern ASM_PFX(gRelocatedRestoreRegistersData)
extern ASM_PFX(gTempPGT)
extern ASM_PFX(gMMUCR4Features)
extern ASM_PFX(gRelocatedRestoreStep2)

; Based on struct pt_regs from Linux
%define PT_REGS_R15     0
%define PT_REGS_R14     8
%define PT_REGS_R13     16
%define PT_REGS_R12     24
%define PT_REGS_BP      32
%define PT_REGS_BX      40
%define PT_REGS_R11     48
%define PT_REGS_R10     56
%define PT_REGS_R9      64
%define PT_REGS_R8      72
%define PT_REGS_CX      88
%define PT_REGS_DX      96
%define PT_REGS_SI      104
%define PT_REGS_DI      112
%define PT_REGS_FLAGS   144
%define PT_REGS_SP      152

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

    DBG_PRINT 'RSTR1:74'
    mov     r8, qword [gRelocatedRestoreRegisters]
    mov     r9, qword [gSavedCR3]
    mov     r10, qword [gRelocatedRestoreRegistersData]

    DBG_PRINT 'RSTR1:78'
    mov     rax, qword [gTempPGT]
    ; in the kernel rbx is set to mmu_cr4_features(%rip)
    mov     rbx, qword [gMMUCR4Features]

    DBG_PRINT 'RSTR1:81'
    mov     rcx, qword [gRelocatedRestoreStep2]
    jmp     rcx

; Inputs:
;   rax - Temporary PGD
;   rbx - Content of CR4
;   r8  - Address of target RestoreRegisters (must be mapped both
;         in temporay and target page tables)
;
ALIGN EFI_PAGE_SIZE
global ASM_PFX(RestoreStep2)
ASM_PFX(RestoreStep2):
    ; Switch to temporary PGD (from rax)
    DBG_PRINT 'RSTR2:96'
    mov     cr3, rax
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

    ; Jump to RestoreRegisters
    jmp	r8

; Inputs:
;   r10 - Address of relocated CPU state structure
ALIGN EFI_PAGE_SIZE
global ASM_PFX(RestoreRegisters)
ASM_PFX(RestoreRegisters):

    DBG_PRINT 'DBG:r10='
    DBG_PUT_REG r10
    DBG_PRINT 'DBG:90'
    mov     r9, qword [r10 + 0x0]
    DBG_PRINT 'DBG:gggr9='
    DBG_PUT_REG r9

    DBG_PRINT 'DBG:100'
    mov     r9, qword [r10 + 0xd0]
    DBG_PRINT 'DBG:r9='
    DBG_PUT_REG r9
    DBG_PRINT 'DBG:101'
    mov     cr3, r9

    DBG_PRINT 'DBG:110'
    mov     r9, qword [r10 + 0xc0]
    mov     cr0, r9
    DBG_PRINT 'DBG:120'
    mov     r9, qword [r10 + 0xc8]
    mov     cr2, r9

    DBG_PRINT 'DBG:130'
    mov     rdx, qword [r10 + 0xd8]
    mov     rdx, rax
    and     rdx, ~X86_CR4_PGE
    mov     cr4, rdx    ; turn off PGE
    DBG_PRINT 'DBG:140'
    mov     rcx, cr3    ; flush TLB
    mov     cr3, rcx    ; flush TLB
    DBG_PRINT 'DBG:150'
    mov     cr4, rax    ; turn PGE back on
    DBG_PRINT 'DBG:160'

    DBG_PRINT 'DBG:170'
    lea     rax, [r10 + 0x08]

    DBG_PRINT 'DBG:180'
    ; Restore all registers except rax
    mov     rsp, [rax + PT_REGS_SP]
    mov     rbp, [rax + PT_REGS_BP]
    mov     rsi, [rax + PT_REGS_SI]
    mov     rdi, [rax + PT_REGS_DI]
    DBG_PRINT 'DBG:190'
    mov     rbx, [rax + PT_REGS_BX]
    mov     rcx, [rax + PT_REGS_CX]
    mov     rdx, [rax + PT_REGS_DX]
    mov     r8,  [rax + PT_REGS_R8]
    DBG_PRINT 'DBG:200'
    mov     r9,  [rax + PT_REGS_R9]
    mov     r10, [rax + PT_REGS_R10]
    mov     r11, [rax + PT_REGS_R11]
    mov     r12, [rax + PT_REGS_R12]
    DBG_PRINT 'DBG:210'
    mov     r13, [rax + PT_REGS_R13]
    mov     r14, [rax + PT_REGS_R14]
    mov     r15, [rax + PT_REGS_R15]
    DBG_PRINT 'DBG:220'

    ; Restore flags
    push    qword [rax + PT_REGS_FLAGS]
    popf

    DBG_PRINT 'DBG:230'
    mov     ax, 0x0010
    mov     cs, ax
    DBG_PRINT 'DBG:240'
    mov     ax, 0x0018
    mov     ss, ax
    DBG_PRINT 'DBG:250'

    DBG_PRINT 'DBG:260'
    xor     ax, ax
    mov     es, ax
    mov     ds, ax
    DBG_PRINT 'DBG:270'
    mov     fs, ax
    mov     gs, ax

    DBG_PRINT 'DBG:280'
    ; Restore GDT
    lgdt    [gSavedGDTDesc]

    DBG_PRINT 'DBG:290'
    mov     rax, [gSavedRIP]
    DBG_PRINT 'DBG:300'
    jmp     rax
    DBG_PRINT 'DBG:400'
    ret



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
