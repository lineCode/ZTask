;           Copyright Oliver Kowalke 2009.
;  Distributed under the Boost Software License, Version 1.0.
;     (See accompanying file LICENSE_1_0.txt or copy at
;           http://www.boost.org/LICENSE_1_0.txt)

;  ---------------------------------------------------------------------------------
;  |    0    |    1    |    2    |    3    |    4    |    5    |    6    |    7    |
;  ---------------------------------------------------------------------------------
;  |    0h   |   04h   |   08h   |   0ch   |   010h  |   014h  |   018h  |   01ch  |
;  ---------------------------------------------------------------------------------
;  | fc_mxcsr|fc_x87_cw| fc_strg |fc_deallo|  limit  |   base  |  fc_seh |   EDI   |
;  ---------------------------------------------------------------------------------
;  ---------------------------------------------------------------------------------
;  |    8    |    9    |   10    |    11   |    12   |    13   |    14   |    15   |
;  ---------------------------------------------------------------------------------
;  |   020h  |  024h   |  028h   |   02ch  |   030h  |   034h  |   038h  |   03ch  |
;  ---------------------------------------------------------------------------------
;  |   ESI   |   EBX   |   EBP   |   EIP   |   EXIT  |         | SEH NXT |SEH HNDLR|
;  ---------------------------------------------------------------------------------

.386
.XMM
.model flat, c
.code

jump_fcontext PROC BOOST_CONTEXT_EXPORT
    ; fourth arg of jump_fcontext() == flag indicating preserving FPU
    mov  ecx, [esp+010h] ;ecx保存参数4,检查是否需要保存浮点寄存器
	;把当前寄存器信息压入当前堆栈
    push  ebp  ; save EBP 
    push  ebx  ; save EBX 
    push  esi  ; save ESI 
    push  edi  ; save EDI 

    assume  fs:nothing
    ; load NT_TIB into ECX
    mov  edx, fs:[018h] ;FS段寄存器在内存中的镜像地址
    assume  fs:error

    ; 加载当前的异常处理列表,并压栈
    ;mov  eax, [edx]
    ;push  eax
    push [edx]

    ; load current stack base ;保存栈底
    ;mov  eax, [edx+04h]
    ;push  eax
    push [edx+04h]

    ; load current stack limit ;保存栈顶
    ;mov  eax, [edx+08h]
    ;push  eax
    push [edx+08h]

    ; load current deallocation stack
    ;mov  eax, [edx+0e0ch]
    ;push  eax
    push [edx+0e0ch]

    ; load fiber local storage
    ;mov  eax, [edx+010h] ;纤程数据
    ;push  eax
    push [edx+010h]

    ; prepare stack for FPU ;检查最后一个参数
    lea  esp, [esp-08h]

    ; test for flag preserve_fpu
    test  ecx, ecx
    je  nxt1

    ; save MMX control- and status-word
    stmxcsr  [esp]
    ; save x87 control-word
    fnstcw  [esp+04h]

nxt1:
    ; first arg of jump_fcontext() == context jumping from ;取第一个参数,用来保存跳转来自何处,esp指向的是fcontext_t结构
    mov  eax, [esp+030h]

    ; store ESP (pointing to context-data) in EAX ;将当前的esp保存到第一个参数的地址
    mov  [eax], esp

    ; second arg of jump_fcontext() == context jumping to ;取第二个参数,跳到的目标
    mov  edx, [esp+034h]

    ; third arg of jump_fcontext() == value to be returned after jump
    mov  eax, [esp+038h]

    ; restore ESP (pointing to context-data) from EDX ;把esp指向第一个参数的结构,后面按照入栈的顺序出栈即可
    mov  esp, edx

    ; test for flag preserve_fpu
    test  ecx, ecx
    je  nxt2

    ; restore MMX control- and status-word
    ldmxcsr  [esp]
    ; restore x87 control-word
    fldcw  [esp+04h]

nxt2:
    ; prepare stack for FPU //准备恢复堆栈
    lea  esp, [esp+08h]

    assume  fs:nothing
    ; load NT_TIB into ECX
    mov  edx, fs:[018h]  ;获取FS段寄存器在内存中的镜像地址
    assume  fs:error

    ; restore fiber local storage
    ;pop  ecx
    pop  [edx+010h] ;恢复纤程数据

    ; restore current deallocation stack
    ;pop  ecx
    pop  [edx+0e0ch]

    ; restore current stack limit
    ;pop  ecx
    pop  [edx+08h];恢复栈顶

    ; restore current stack base
    ;pop  ecx
    pop  [edx+04h] ;恢复栈底

    ; restore current SEH exception list
    ;pop  ecx
    ;mov  [edx], ecx ;恢复结构化异常处理指针
    pop [edx]
    
    ;恢复通用寄存器
    pop  edi  ; save EDI 
    pop  esi  ; save ESI 
    pop  ebx  ; save EBX 
    pop  ebp  ; save EBP 

    ; restore return-address ;返回地址
    pop  edx

    ; use value in EAX as return-value after jump
    ; use value in EAX as first arg in context function ;传递参数,上下文指针
    mov  [esp+04h], eax

    ; indirect jump to context
    jmp  edx
jump_fcontext ENDP
END
