bits 32

global user_enter
global user_enter_eax

; ── Segment selectors (from gdt.h) ───────────────────────────────────────────
;
;  Index  Selector  Ring  Use
;  -----  --------  ----  ───────────────────────────────
;    3     0x18      3    User code  (DPL=3 in descriptor)
;    4     0x20      3    User data  (DPL=3 in descriptor)
;
; When loading a segment register or building an iret frame for ring 3,
; OR the base selector with RPL=3 so the CPU's privilege checks pass.
;
%define USER_CS  (0x18 | 3)   ; 0x1B — user code segment, RPL=3
%define USER_DS  (0x20 | 3)   ; 0x23 — user data segment, RPL=3

; ── void user_enter(uint32_t eip, uint32_t esp) ──────────────────────────────
;
; Transitions from ring 0 to ring 3 via iret.  Does NOT return.
;
; Arguments (cdecl):
;   [esp+4]  eip — user-mode instruction pointer
;   [esp+8]  esp — user-mode stack pointer
;
; What we do:
;   1. Load user data selector into DS/ES/FS/GS.  (SS is set by iret.)
;   2. Build an iret frame: SS, ESP, EFLAGS, CS, EIP — in that order
;      from high to low address (iret pops EIP first, SS last).
;   3. Execute iret.  The CPU:
;        • Pops EIP and CS → switches CPL to 3
;        • Pops EFLAGS  → restores interrupt flag (we push 0x202 = IF+reserved)
;        • Sees ring change → pops ESP and SS → switches to user stack
;
; Caller guarantees:
;   • The page directory already has user-accessible mappings at eip and esp.
;   • The TSS esp0 has been set to the top of this task's kernel stack so
;     future ring-3 → ring-0 transitions (interrupts, syscalls) land there.
;   • Interrupts are enabled (sti called before user_enter).
;
user_enter:
    mov  eax, [esp + 4]      ; eax = user EIP
    mov  ecx, [esp + 8]      ; ecx = user ESP

    ; Load user data segment into the data registers.
    ; CPL=0 can load a DPL=3 selector: max(CPL=0, RPL=3)=3 ≤ DPL=3. ✓
    mov  dx, USER_DS
    mov  ds, dx
    mov  es, dx
    mov  fs, dx
    mov  gs, dx

    ; Build the five-word iret frame (high → low on the current kernel stack).
    ;
    ;   iret pops in this order: EIP, CS, EFLAGS, ESP, SS
    ;   So we push:              SS, ESP, EFLAGS, CS, EIP  (last pushed = first popped)
    ;
    push USER_DS              ; SS    — user stack segment
    push ecx                  ; ESP   — user stack pointer
    push dword 0x202          ; EFLAGS: IF=1 (bit 9), reserved bit 1 always set
    push USER_CS              ; CS    — user code segment, RPL=3
    push eax                  ; EIP   — user entry point
    iret

; ── void user_enter_eax(uint32_t eip, uint32_t esp, uint32_t eax_val) ────────
;
; Like user_enter but also sets EAX to eax_val before iret.
; Used by user_task_trampoline so fork() children get EAX = 0.
;
; Arguments (cdecl):
;   [esp+4]  eip     — user-mode instruction pointer
;   [esp+8]  esp     — user-mode stack pointer
;   [esp+12] eax_val — value to place in EAX when user mode starts
;
user_enter_eax:
    mov  edx, [esp + 4]    ; edx = user EIP
    mov  ecx, [esp + 8]    ; ecx = user ESP
    mov  eax, [esp + 12]   ; eax = desired user EAX

    mov  bx, USER_DS
    mov  ds, bx
    mov  es, bx
    mov  fs, bx
    mov  gs, bx

    push USER_DS             ; SS
    push ecx                 ; ESP
    push dword 0x202         ; EFLAGS: IF=1, reserved bit 1
    push USER_CS             ; CS
    push edx                 ; EIP
    ; EAX already holds eax_val — iret does not touch it
    iret

; Tell the linker this object does not need an executable stack.
section .note.GNU-stack noalloc noexec nowrite progbits
