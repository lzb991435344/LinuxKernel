/*
 *  linux/kernel/traps.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 */


/**
  主要包含一些在处理异常故障（硬件中断）底层代码asm.s文件中调用相应的
C函数。用于显示出错的位置和出错号调试信息。
  die() 显示出错信息
  trap_init()  在init/main.c中被调用，用于初始化硬件异常处理中断向量
  并设置允许中断请求信号的到来

*/

/**
  在程序asm.s中保存了一些状态后，本程序用来处理硬件陷阱和故障，主要用于
 调试的目的，以后将扩展用来杀死遭损坏的进程（发信号或直接杀死）

*/

#include <string.h> //定义内存或字符串操作的嵌入函数

#include <linux/head.h> //段描述符的简单结构，和几个选择符常量
#include <linux/sched.h> //调度程序头文件，定义任务结构 task_struct,初始化
                         //任务0的数据，及描述符参数设置和获取嵌入式汇编函数宏语句
#include <linux/kernel.h> //内核常用函数的定义
#include <asm/system.h>  //定义设置或修改描述符、中断门等的嵌入式汇编函数
#include <asm/segment.h> //段操作头函数，定义段寄存器操作的嵌入式汇编函数
#include <asm/io.h>  //硬件端口的输入输出宏汇编语句


//指定寄存器
//register char __res asm("ax");

//取段地址中addr处的一个字节
//参数： 段选择符；段内指定地址
//输出：%0 -eax(__res);
//输入：%1 -eax(seg);%2 - 内存地址(*(addr))
#define get_seg_byte(seg,addr) ({ \
register char __res; \
__asm__("push %%fs;mov %%ax,%%fs;movb %%fs:%2,%%al;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})


//取段地址中addr处的一个长字（4个字节）
//参数： 段选择符；段内指定地址
//输出：%0 -eax(__res);
//输入：%1 -eax(seg);%2 - 内存地址(*(addr))
#define get_seg_long(seg,addr) ({ \
register unsigned long __res; \  
__asm__("push %%fs;mov %%ax,%%fs;movl %%fs:%2,%%eax;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})


//取fs寄存器的值
//输出：%0 -eax(__res);
#define _fs() ({ \
register unsigned short __res; \
__asm__("mov %%fs,%%ax":"=a" (__res):); \
__res;})

int do_exit(long code); //退出处理

void page_exception(void); //页异常，page_fault

void divide_error(void);  //int0
void debug(void);         //int1
void nmi(void);           //int2
void int3(void);          //int3
void overflow(void);      //int4
void bounds(void);        //int5
void invalid_op(void);    //int6
void device_not_available(void);  //int7
void double_fault(void);   //int8
void coprocessor_segment_overrun(void);  //int9
void invalid_TSS(void);       //int10
void segment_not_present(void);  //int11
void stack_segment(void);        //int12
void general_protection(void);  //int13
void page_fault(void);     //int14
void coprocessor_error(void);  //int16
void reserved(void);           //int15
void parallel_interrupt(void);  //int39
void irq13(void);              //int45 协处理器中断处理

//子程序用于打印出错中断名称，出错号，调用程序的EIP,EFLAGS,ESP,fs段寄存器值
//段的基地址，段的长度，进程号PID,任务号，10字节指令码，
//堆栈在用户数据段，还打印16字节的堆栈内容

static void die(char * str,long esp_ptr,long nr)
{
	long * esp = (long *) esp_ptr;
	int i;

	printk("%s: %04x\n\r",str,nr&0xffff);
	//
	//（1）EIP:\t%04x:%p\n --esp[1]是段选择符（CS）,esp[0]是eip
	//（2）EFLAGS:\t%p\n  --eap[2]是eflags
	//（3）ESP:\t%04x:%p\n --esp[4]是原ss,eap[3]是原esp
	printk("EIP:\t%04x:%p\nEFLAGS:\t%p\nESP:\t%04x:%p\n",
		esp[1],esp[0],esp[2],esp[4],esp[3]);
	printk("fs: %04x\n",_fs());
	printk("base: %p, limit: %p\n",get_base(current->ldt[1]),get_limit(0x17));
	if (esp[4] == 0x17) {
		printk("Stack: ");
		for (i=0;i<4;i++)
			printk("%p ",get_seg_long(0x17,i+(long *)esp[3]));
		printk("\n");
	}
	str(i);//取当前任务的任务号
	printk("Pid: %d, process nr: %d\n\r",current->pid,0xffff & i);
	for(i=0;i<10;i++)
		printk("%02x ",0xff & get_seg_byte(esp[1],(i+(char *)esp[0])));
	printk("\n\r");
	do_exit(11);		/* play segment exception */
}


//下面这些do_开头的函数是asm.s中对应中断处理程序调用的C函数
void do_double_fault(long esp, long error_code)
{
	die("double fault",esp,error_code);
}

void do_general_protection(long esp, long error_code)
{
	die("general protection",esp,error_code);
}

void do_divide_error(long esp, long error_code)
{
	die("divide error",esp,error_code);
}

//参数是进入中断后被顺序压入堆栈的寄存器
void do_int3(long * esp, long error_code,
		long fs,long es,long ds,
		long ebp,long esi,long edi,
		long edx,long ecx,long ebx,long eax)
{
	int tr;
	//取任务寄存器值tr
	__asm__("str %%ax":"=a" (tr):"0" (0));
	printk("eax\t\tebx\t\tecx\t\tedx\n\r%8x\t%8x\t%8x\t%8x\n\r",
		eax,ebx,ecx,edx);
	printk("esi\t\tedi\t\tebp\t\tesp\n\r%8x\t%8x\t%8x\t%8x\n\r",
		esi,edi,ebp,(long) esp);
	printk("\n\rds\tes\tfs\ttr\n\r%4x\t%4x\t%4x\t%4x\n\r",
		ds,es,fs,tr);
	printk("EIP: %8x   CS: %4x  EFLAGS: %8x\n\r",esp[0],esp[1],esp[2]);
}

void do_nmi(long esp, long error_code)
{
	die("nmi",esp,error_code);
}

void do_debug(long esp, long error_code)
{
	die("debug",esp,error_code);
}

void do_overflow(long esp, long error_code)
{
	die("overflow",esp,error_code);
}

void do_bounds(long esp, long error_code)
{
	die("bounds",esp,error_code);
}

void do_invalid_op(long esp, long error_code)
{
	die("invalid operand",esp,error_code);
}

void do_device_not_available(long esp, long error_code)
{
	die("device not available",esp,error_code);
}

void do_coprocessor_segment_overrun(long esp, long error_code)
{
	die("coprocessor segment overrun",esp,error_code);
}

void do_invalid_TSS(long esp,long error_code)
{
	die("invalid TSS",esp,error_code);
}

void do_segment_not_present(long esp,long error_code)
{
	die("segment not present",esp,error_code);
}

void do_stack_segment(long esp,long error_code)
{
	die("stack segment",esp,error_code);
}

void do_coprocessor_error(long esp, long error_code)
{
	if (last_task_used_math != current)
		return;
	die("coprocessor error",esp,error_code);
}

void do_reserved(long esp, long error_code)
{
	die("reserved (15,17-47) error",esp,error_code);
}


//异常中断程序初始化子程序，设置中断调用门或（中断向量）。
//set_trap_gate与set_system_gate
//同：使用了中断描述符IDT的陷阱门（TrapGate）
//异：set_trap_gate 特权级为0
//    set_system_gate 特权级为3
// 断点陷阱中断int3、溢出中断overflow和边界出错中断bounds可以由任何中断产生
void trap_init(void)
{
	int i;

	set_trap_gate(0,&divide_error);//设置操作出错的中断向量值
	set_trap_gate(1,&debug);
	set_trap_gate(2,&nmi);
	set_system_gate(3,&int3);	/* int3-5 can be called from all */
	set_system_gate(4,&overflow);
	set_system_gate(5,&bounds);
	set_trap_gate(6,&invalid_op);
	set_trap_gate(7,&device_not_available);
	set_trap_gate(8,&double_fault);
	set_trap_gate(9,&coprocessor_segment_overrun);
	set_trap_gate(10,&invalid_TSS);
	set_trap_gate(11,&segment_not_present);
	set_trap_gate(12,&stack_segment);
	set_trap_gate(13,&general_protection);
	set_trap_gate(14,&page_fault);
	set_trap_gate(15,&reserved);
	set_trap_gate(16,&coprocessor_error);
	//将int17-47的陷阱门先设置为reserved,硬件初始化之后会重新设置陷阱门
	for (i=17;i<48;i++)
		set_trap_gate(i,&reserved);
	set_trap_gate(45,&irq13);
	outb_p(inb_p(0x21)&0xfb,0x21);//允许8259A主芯片的IRQ2中断请求
	outb(inb_p(0xA1)&0xdf,0xA1);//允许8259A从芯片的IRQ13中断请求
	set_trap_gate(39,&parallel_interrupt);//设置并行口1的中断0x27陷阱门描述符
}
