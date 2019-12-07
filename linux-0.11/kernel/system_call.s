/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 *
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - %fs
 *	14(%esp) - %es
 *	18(%esp) - %ds
 *	1C(%esp) - %eip
 *	20(%esp) - %cs
 *	24(%esp) - %eflags
 *	28(%esp) - %oldesp
 *	2C(%esp) - %oldss
 */

SIG_CHLD	= 17  #定义SIG_CHLD信号（子进程停止或结束）

EAX		= 0x00  #堆栈中寄存器的偏移位置
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
FS		= 0x10
ES		= 0x14
DS		= 0x18
EIP		= 0x1C
CS		= 0x20
EFLAGS		= 0x24
OLDESP		= 0x28  #特权级发生变化时栈会切换，用户栈指针被保存在内核态栈中
OLDSS		= 0x2C

#定义在task_struct中变量的偏移

state	= 0		# these are offsets into the task-struct. 状态码
counter	= 4     #任务运行时的计数（递减）滴答数，运行时间片
priority = 8    #运行优先数，任务开始时，counter = priority，越大运行时间越长
signal	= 12    #信号位图，每一个比特位代表一种信号
sigaction = 16		# MUST be 16 (=len of sigaction)  sigaction必须是16字节
blocked = (33*16)  #受阻信号位图的偏移量

# offsets within sigaction
sa_handler = 0    #信号处理的句柄
sa_mask = 4       #信号屏蔽码
sa_flags = 8      #信号集
sa_restorer = 12  #恢复指针函数

nr_system_calls = 72  #0.11中系统调用总数

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */

 #定义入口点
.globl _system_call,_sys_fork,_timer_interrupt,_sys_execve
.globl _hd_interrupt,_floppy_interrupt,_parallel_interrupt
.globl _device_not_available, _coprocessor_error

#错误的系统调用号
.align 2   #内存4字节对齐
bad_sys_call:
	movl $-1,%eax  #eax中置为-1，退出中断
	iret

#重新执行调度程序入口，调度程序schedule()返回时就在ret_from_sys_call处继续执行	
.align 2
reschedule:
	pushl $ret_from_sys_call  #ret_from_sys_call返回的地址入栈
	jmp _schedule

# int 0x80 ---Linux系统调用入口点（调用中断 int0x80,eax中是调用号）
.align 2
_system_call:
	cmpl $nr_system_calls-1,%eax  #调用号超出范围的话就在eax中置-1并退出
	ja bad_sys_call
	push %ds  #保留原段寄存器值
	push %es
	push %fs

#一个系统调用最多可带3个参数，也可以不带参数
#寄存器入栈的顺序是GNU规定的
#ebx放第一个参数，ecx第二个，edx第三个
	pushl %edx
	pushl %ecx		# push %ebx,%ecx,%edx as parameters
	pushl %ebx		# to the system call
	movl $0x10,%edx		# set up ds,es to kernel space
	mov %dx,%ds         #ds,es指向内核数据段（全局描述符表中数据段描述符）
	mov %dx,%es

#fs指向局部的数据段（局部描述符表中数据段描述符），即指向执行本次系统调用的用户程序
#的数据段，内核给任务分配的代码和数据内存段是重叠的，他们段基址和段限长相同
	movl $0x17,%edx		# fs points to local data space
	mov %dx,%fs
	call _sys_call_table(,%eax,4)
	pushl %eax
	movl _current,%eax
	cmpl $0,state(%eax)		# state
	jne reschedule
	cmpl $0,counter(%eax)		# counter
	je reschedule
ret_from_sys_call:
	movl _current,%eax		# task[0] cannot have signals
	cmpl _task,%eax
	je 3f
	cmpw $0x0f,CS(%esp)		# was old code segment supervisor ?
	jne 3f
	cmpw $0x17,OLDSS(%esp)		# was stack segment = 0x17 ?
	jne 3f
	movl signal(%eax),%ebx
	movl blocked(%eax),%ecx
	notl %ecx
	andl %ebx,%ecx
	bsfl %ecx,%ecx
	je 3f
	btrl %ecx,%ebx
	movl %ebx,signal(%eax)
	incl %ecx
	pushl %ecx
	call _do_signal
	popl %eax
3:	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds
	iret

.align 2
_coprocessor_error:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	jmp _math_error

.align 2
_device_not_available:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	clts				# clear TS so that we can use math
	movl %cr0,%eax
	testl $0x4,%eax			# EM (math emulation bit)
	je _math_state_restore
	pushl %ebp
	pushl %esi
	pushl %edi
	call _math_emulate
	popl %edi
	popl %esi
	popl %ebp
	ret

.align 2
_timer_interrupt:
	push %ds		# save ds,es and put kernel data space
	push %es		# into them. %fs is used by _system_call
	push %fs
	pushl %edx		# we save %eax,%ecx,%edx as gcc doesn't
	pushl %ecx		# save those across function calls. %ebx
	pushl %ebx		# is saved as we use that in ret_sys_call
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	incl _jiffies
	movb $0x20,%al		# EOI to interrupt controller #1
	outb %al,$0x20
	movl CS(%esp),%eax
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	call _do_timer		# 'do_timer(long CPL)' does everything from
	addl $4,%esp		# task switching to accounting ...
	jmp ret_from_sys_call

#sys_execve()系统调用。取中断调用程序的代码指针作为参数调用C语言do_execve()
#do_execve()在（fs/exec.c）
.align 2
_sys_execve:
	lea EIP(%esp),%eax  #eax指向堆栈中保存用户程序eip指针处
	pushl %eax
	call _do_execve
	addl $4,%esp  #丢弃调用时压入栈的EIP值
	ret



.align 2
_sys_fork:
	call _find_empty_process
	testl %eax,%eax
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call _copy_process
	addl $20,%esp
1:	ret

_hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# EOI to interrupt controller #1
	jmp 1f			# give port chance to breathe
1:	jmp 1f
1:	xorl %edx,%edx
	xchgl _do_hd,%edx
	testl %edx,%edx
	jne 1f
	movl $_unexpected_hd_interrupt,%edx
1:	outb %al,$0x20
	call *%edx		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

# int 38 --int 0x26 
#软盘驱动器中断处理程序，硬件响应中断请求 IRQ6
#首先向8259A中断控制器主芯片发送EOI指令，然后取变量_do_floppy
#中的函数指针放入eax中，并置do_floppy为空，接着判断eax函数指针是否为空
#（1）空，eax = _unexpected_floppy_interrupt(),显示出错信息
#（2）调用eax指向的函数 rw_interrupt,seek_interrupt,recal_interrupt，
#reset_interrupt或 _unexpected_floppy_interrupt
_floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax  #ds,es置为内核数据段
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax  #fs置为调用程序局部数据段
	mov %ax,%fs
	movb $0x20,%al     #送主8259A中断控制器EOI指令
	outb %al,$0x20		# EOI to interrupt controller #1
	

	xorl %eax,%eax
	#换指令XCHG是两个寄存器，寄存器和内存变量之间内容的交换指令，
	#两个操作数的数据类型要相同，可以是一个字节，也可以是一个字，
	#也可以是双字
	xchgl _do_floppy,%eax
	testl %eax,%eax  #测试指令对否为空
	jne 1f  #ZF = 0转移，指针指向_unexpected_floppy_interrupt（）
	movl $_unexpected_floppy_interrupt,%eax
1:	call *%eax		# "interesting" way of handling intr. #间接调用
	pop %fs         
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret
##int 39--int 0x27并行口处理程序，对应硬件中断强请求IRQ7
#未实现，只是发送EOI指令
_parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret
