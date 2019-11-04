/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */
 
 

/*

 *asm.s 汇编程序中包括大部分 CPU 探测到的异常故障处理的底层代码，也包括数学协处理器（ FPU）
 *的异常处理。该程序与 kernel/traps.c 程序有着密切的关系。该程序的主要处理方式是在中断处理程序中
 *调用相应的 C 函数程序，显示出错位置和出错号，然后退出中断。
 * asm.s contains the low-level code for most hardware faults.
 * page_exception is handled by the mm, so that isn't here. This
 * file also handles (hopefully) fpu-exceptions due to TS-bit, as
 * the fpu must be properly saved/resored. This hasn't been tested.
 */
 /*
* asm.s 程序中包括大部分的硬件故障（或出错）处理的底层次代码。页异常是由内存管理程序
* mm 处理的，所以不在这里。此程序还处理（希望是这样）由于 TS-位而造成的 fpu 异常，
* 因为 fpu 必须正确地进行保存/恢复处理，这些还没有测试过。
*/
# 本代码文件主要涉及对 Intel 保留的中断 int0--int16 的处理（ int17-int31 留作今后使用）。
# 以下是一些全局函数名的声明，其原形在 traps.c 中说明。
.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_coprocessor_error,_irq13,_reserved

# int0 --（下面这段代码的含义参见图 4.1(a)）。
# 下面是被零除出错(divide_error)处理代码。标号'_divide_error'实际上是 C 语言函
# 数 divide_error()编译后所生成模块中对应的名称。'_do_divide_error'函数在 traps.c 中。
_divide_error:
	pushl $_do_divide_error # 首先把将要调用的函数地址入栈。这段程序的出错号为 0。
no_error_code:      # 这里是无出错号处理的入口处，
	xchgl %eax,(%esp) # _do_divide_error 的地址 Î eax，eax 被交换入栈。
	pushl %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds     # ！！16 位的段寄存器入栈后也要占用 4 个字节
	push %es
	push %fs
	pushl $0		# "error code" # 将出错码入栈。
	lea 44(%esp),%edx   # 取原调用返回地址处堆栈指针位置，并压入堆栈。
	pushl %edx
	movl $0x10,%edx # 内核代码数据段选择符。
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs
	call *%eax   # 调用 C 函数 do_divide_error()。
	addl $8,%esp  # 让堆栈指针重新指向寄存器 fs入栈处。
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax # 弹出原来 eax 中的内容。
	iret  #子程序返回
	
# int1 -- debug 调试中断入口点。处理过程同上。
_debug:
	pushl $_do_int3		# _do_debug # _do_debug C 函数指针入栈。
	jmp no_error_code

	# int2 -- 非屏蔽中断调用入口点。
_nmi:
	pushl $_do_nmi
	jmp no_error_code

# int3 -- 同_debug。
_int3:
	pushl $_do_int3
	jmp no_error_code
# int4 -- 溢出出错处理中断入口点。
_overflow:
	pushl $_do_overflow
	jmp no_error_code

# int5 -- 边界检查出错中断入口点。
_bounds:
	pushl $_do_bounds
	jmp no_error_code
	
# int6 -- 无效操作指令出错中断入口点。
_invalid_op:
	pushl $_do_invalid_op
	jmp no_error_code
	
# int9 -- 协处理器段超出出错中断入口点。
_coprocessor_segment_overrun:
	pushl $_do_coprocessor_segment_overrun
	jmp no_error_code
	
# int15 – 保留。
_reserved:
	pushl $_do_reserved
	jmp no_error_code

# int45 -- ( = 0x20 + 13 ) 数学协处理器（ Coprocessor）发出的中断。
# 当协处理器执行完一个操作时就会发出 IRQ13 中断信号，以通知 CPU 操作完成。
_irq13:
	pushl %eax
	xorb %al,%al
	outb %al,$0xF0
	movb $0x20,%al
	outb %al,$0x20  #向8259主中断控制芯片发送EOI（中断结束）信号。
	jmp 1f          #跳转指令延时
1:	jmp 1f
1:	outb %al,$0xA0  #再向8259主中断控制芯片发送EOI（中断结束）信号。
	popl %eax
	jmp _coprocessor_error #原本在程序中，现在放到了system_call.s中


#以下中断在调用时cpu会在中断返回之前将出错号压入堆栈
#返回的时候也需要弹出

#int8 -双出错故障。类型：放弃  有错误码
#通常当cpu在调用前一个异常的处理程序而又检测到一个新异常的时候，两个异常
#将会被串行处理，但很少碰到这种情况，cpu不能进行这样的串行处理操作，此时引发中断

_double_fault:
	pushl $_do_double_fault #C函数地址入栈
error_code:
	xchgl %eax,4(%esp)		# error code <-> %eax，eax原来的值被保存在堆栈上
	xchgl %ebx,(%esp)		# &function <-> %ebx，ebx原来的值被保存在堆栈上
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl %eax			# error code,出错号入栈
	lea 44(%esp),%eax	# offset，程序返回地址处堆栈指针位置值入栈
	pushl %eax
	movl $0x10,%eax  #置内核数据段选择符
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	call *%ebx    #间接调用，调用相应的C函数，其参数已经入栈
	addl $8,%esp  #丢弃入栈的2个用作C函数的参数
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret

#int 10 --无效的任务状态段（TSS） 类型：错误：有出错码
#CPu企图切换一个进程，而进程的TSS无效。根据TSS中的那一部分引起了异常，
#当TSS的长度超过104字节时，这个异常在当前的任务中产生，因而切换被终止。
#其他问题则会导致在切换后的新任务中产生本异常。

_invalid_TSS:
	pushl $_do_invalid_TSS
	jmp error_code


#int11  --段不存在 类型：错误  有出错码
#被引用的段不在内存内，段描述符不在内存内
_segment_not_present:
	pushl $_do_segment_not_present
	jmp error_code


#int12 --堆栈段错误  类型：错误；有出错码
#指令操作试图超出堆栈段范围内，或者堆栈不在内存中
_stack_segment:
	pushl $_do_stack_segment
	jmp error_code


#int13 一般性保护错误  类型：错误；有出错码
#表明是不属于任何类的错误。若一个异常产生时没有对应的处理向量
#0-16，通常就会归到此类
_general_protection:
	pushl $_do_general_protection
	jmp error_code


#int7  设备不存在（_device_not_available）
#int14  页错误(_page_fault)
#int16  协处理错误（_coprocessor_error）
#时钟中断 int 0x20(_time_interrupt)
#系统调用 int 0x80(_system_call)
