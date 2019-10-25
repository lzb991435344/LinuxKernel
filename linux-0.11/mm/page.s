/*
 *  linux/mm/page.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * page.s contains the low-level page-exception code.
 * the real work is done in mm.c
 */

 /**
   1.页异常中断处理程序。
   （1）缺页：do_no_page(error_code,address)处理，cpu发现
   对应的目录项或者页表项的存在位标志位0
   （2）页写保护：do_wp_page(error_code,address)处理，进程无访问页面权限
  出错码是CPU自动产生并压入堆栈的，出现异常时访问的线性地址
  是从寄存器cr2中取得，用于存放页出错时的线性地址。
   2.诊断和恢复
   （1）放在堆栈上的出错码。
   （2）cr2控制器。
 */
.globl _page_fault

_page_fault:
	xchgl %eax,(%esp) #取出出错码到eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%edx #置内核数据段选择符
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs
	movl %cr2,%edx  #取引起页面异常的线性地址
	pushl %edx  #将线性地址和出错码压入堆栈，作为调用函数的参数
	pushl %eax
	testl $1,%eax #测试标志P,不是缺页引起的异常则跳转
	jne 1f
	call _do_no_page #调用缺页处理函数
	jmp 2f
1:	call _do_wp_page #调用写保护处理函数
2:	addl $8,%esp   #丢弃压入栈的两个参数
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret   #子程序返回
