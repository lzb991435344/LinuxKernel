/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sys.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <signal.h>

//取信号nr在位图中对应的二进制数值，信号编号是1-32，信号5的计算方式是 1<<(5 - 1) = 16 = 2^4
#define _S(nr) (1<<((nr)-1))

//除了SIGKILL和SIGSTOP之外，其他都是可阻塞的
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

//内核调试函数
//显示任务号nr的进程号，进程状态，内核堆栈空闲字节数
void show_task(int nr,struct task_struct * p)
{
	int i,j = 4096-sizeof(struct task_struct);

	printk("%d: pid=%d, state=%d, ",nr,p->pid,p->state);
	i = 0;
	while (i < j && !((char *)(p + 1))[i]) //检测指定任务数据结构之后等于0的字节数
		i++;
	printk("%d (of %d) chars free in kernel stack\n\r",i,j);
}

//
void show_stat(void)
{
	int i;

	for (i=0;i<NR_TASKS;i++)
		if (task[i])
			show_task(i,task[i]);
}


//设置8253芯片的初值
//8253定时芯片输入时钟频率大约为1193180，linux希望定时器发出的中断频率是100hz,
//即每10ms发一次时钟中断
#define LATCH (1193180/HZ)

extern void mem_use(void);

extern int timer_interrupt(void);
extern int system_call(void);

union task_union {
	struct task_struct task;
	char stack[PAGE_SIZE];
};

static union task_union init_task = {INIT_TASK,};//定义初始任务的数据

//volatile 该变量是易变的，不稳定的，向编译器说明该变量会被其他的程序修改而发生变化
//程序申明变量时，通常放在通用寄存器
//要求gcc不要对该变量进行优化处理，引用该变量时一定要从指定的内存读取该变量，中断过程
//会修改该变量值
long volatile jiffies = 0;
long startup_time = 0; //开机时间
struct task_struct *current = &(init_task.task);//当前任务的指针
struct task_struct *last_task_used_math = NULL;//使用过协处理器的指针

struct task_struct * task[NR_TASKS] = {&(init_task.task), };//定义任务指针数组

//设置用户堆栈大小为1K，容量是4k
//内核初始化的过程中被用作内核栈，初始化完成之后被用作任务0的用户态堆栈。在运行任务0
//之前它是内核栈，以后被用作0和1的用户态栈。
long user_stack [ PAGE_SIZE>>2 ] ;


//结构体用于设置堆栈ss:esp(数据选择符，指针)，在head.s中
//ss被设置为内核数据段选择符（0x10）,指针esp指向user_stack数组最后一项后面
//原因：Intel cpu执行堆栈操作时先递减堆栈指针sp值，在指针sp处保存入栈内容
struct {
	long * a;
	short b;
	} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };
/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 */
/*
 *将当前的协处理器内容保存到老协处理器的状态数组中，并将当前任务的协处理器
 *内容加载进协处理器
 *
 */
//当任务被调度被交换以后，该函数用于保存原任务的协处理器状态（上下文）并恢复新调度
//进来的当前任务的协处理器状态	
void math_state_restore()
{
	//刚被交换出去的任务没变，直接返回
	if (last_task_used_math == current)
		return;
	__asm__("fwait");//在发送协处理器命令之前先发WAIT指令
	if (last_task_used_math) {//上个任务使用了协处理器，直接保存状态
		__asm__("fnsave %0"::"m" (last_task_used_math->tss.i387));
	}
	//设置为当前任务
	last_task_used_math=current;
	if (current->used_math) {//当前任务用过协处理器
		__asm__("frstor %0"::"m" (current->tss.i387));
	} else {//第一次使用
		__asm__("fninit"::);//向协处理器发送初始化命令
		current->used_math=1;//设置使用协处理器的标志
	}
}

/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */
//0任务是个闲置的任务，只有当没有其他任务可以运行时才调用，不能被杀死和睡眠，状态
//信息是未使用的
void schedule(void)
{
	int i,next,c;
	struct task_struct ** p;//任务结构指针的指针

/* check alarm, wake up any interruptible tasks that have got a signal */
//检查alarm（进程报警定时器），唤醒任何已得到信号的可中断任务

	//从最后一个任务开始循环检测alarm，跳过空指针
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
		if (*p) {
			//设置过任务的定时值alarm且已经过期alarm < jiffies，则信号位图设置为SIGALRM，向任务
			//发送SIGALRM信号，然后清零。信号默认操作是终止进程
			//jiffies 是从开机算起的滴答数 10ms/滴答
			if ((*p)->alarm && (*p)->alarm < jiffies) {
					(*p)->signal |= (1<<(SIGALRM-1));
					(*p)->alarm = 0;
				}
			//除被阻塞的信号之外还有其他信号，且任务处于可中断状态，则任务状态设置为就绪态	
			//~(_BLOCKABLE & (*p)->blocked) 用于被阻塞的信号
			if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) &&
			(*p)->state==TASK_INTERRUPTIBLE)
				(*p)->state=TASK_RUNNING;//置为就绪状态
		}

/* this is the scheduler proper: */
//调度程序的主要程序
	while (1) {
		c = -1;
		next = 0;
		//下标值设置为数组的最后的值，NR_TASKS = 64
		//#define FIRST_TASK task[0]
		//#define LAST_TASK task[NR_TASKS-1]
		i = NR_TASKS;
		p = &task[NR_TASKS];
		while (--i) {
			if (!*--p)
				continue;
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
				c = (*p)->counter, next = i;
		}
		if (c) break;
		for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
			if (*p)
				//计算每个任务的优先权值，更新每一个任务的counter值
				(*p)->counter = ((*p)->counter >> 1) +
						(*p)->priority;
	}
	//系统中没有其他任务运行时，next始终为0，任务0仅执行pause()系统调用，并调用
	//本函数
	//切换任务号，并运行
	switch_to(next);
}

//pause系统调用，转换当前任务状态为可中断的等待状态，并重新调度
//系统调用导致进程进入睡眠状态，直到收到信号（用于终止进程或者使进程获得一个信号
//捕获函数）。当捕获信号，且处理函数返回，pause（）才返回。此时pause()返回值是-1，errno
//被设置为EINTR(0.95实现)
int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return 0;
}

void sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	if (!p)
		return;
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");
	tmp = *p;
	*p = current;
	current->state = TASK_UNINTERRUPTIBLE;
	schedule();
	if (tmp)
		tmp->state=0;
}

void interruptible_sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	if (!p)
		return;
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");
	tmp=*p;
	*p=current;
repeat:	current->state = TASK_INTERRUPTIBLE;
	schedule();
	if (*p && *p != current) {
		(**p).state=0;
		goto repeat;
	}
	*p=NULL;
	if (tmp)
		tmp->state=0;
}

void wake_up(struct task_struct **p)
{
	if (p && *p) {
		(**p).state=0;
		*p=NULL;
	}
}

/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
static struct task_struct * wait_motor[4] = {NULL,NULL,NULL,NULL};
static int  mon_timer[4]={0,0,0,0};
static int moff_timer[4]={0,0,0,0};
unsigned char current_DOR = 0x0C;

int ticks_to_floppy_on(unsigned int nr)
{
	extern unsigned char selected;
	unsigned char mask = 0x10 << nr;

	if (nr>3)
		panic("floppy_on: nr>3");
	moff_timer[nr]=10000;		/* 100 s = very big :-) */
	cli();				/* use floppy_off to turn it off */
	mask |= current_DOR;
	if (!selected) {
		mask &= 0xFC;
		mask |= nr;
	}
	if (mask != current_DOR) {
		outb(mask,FD_DOR);
		if ((mask ^ current_DOR) & 0xf0)
			mon_timer[nr] = HZ/2;
		else if (mon_timer[nr] < 2)
			mon_timer[nr] = 2;
		current_DOR = mask;
	}
	sti();
	return mon_timer[nr];
}

void floppy_on(unsigned int nr)
{
	cli();
	while (ticks_to_floppy_on(nr))
		sleep_on(nr+wait_motor);
	sti();
}

void floppy_off(unsigned int nr)
{
	moff_timer[nr]=3*HZ;
}

void do_floppy_timer(void)
{
	int i;
	unsigned char mask = 0x10;

	for (i=0 ; i<4 ; i++,mask <<= 1) {
		if (!(mask & current_DOR))
			continue;
		if (mon_timer[i]) {
			if (!--mon_timer[i])
				wake_up(i+wait_motor);
		} else if (!moff_timer[i]) {
			current_DOR &= ~mask;
			outb(current_DOR,FD_DOR);
		} else
			moff_timer[i]--;
	}
}

#define TIME_REQUESTS 64

static struct timer_list {
	long jiffies;
	void (*fn)();
	struct timer_list * next;
} timer_list[TIME_REQUESTS], * next_timer = NULL;

void add_timer(long jiffies, void (*fn)(void))
{
	struct timer_list * p;

	if (!fn)
		return;
	cli();
	if (jiffies <= 0)
		(fn)();
	else {
		for (p = timer_list ; p < timer_list + TIME_REQUESTS ; p++)
			if (!p->fn)
				break;
		if (p >= timer_list + TIME_REQUESTS)
			panic("No more time requests free");
		p->fn = fn;
		p->jiffies = jiffies;
		p->next = next_timer;
		next_timer = p;
		while (p->next && p->next->jiffies < p->jiffies) {
			p->jiffies -= p->next->jiffies;
			fn = p->fn;
			p->fn = p->next->fn;
			p->next->fn = fn;
			jiffies = p->jiffies;
			p->jiffies = p->next->jiffies;
			p->next->jiffies = jiffies;
			p = p->next;
		}
	}
	sti();
}

void do_timer(long cpl)
{
	extern int beepcount;
	extern void sysbeepstop(void);

	if (beepcount)
		if (!--beepcount)
			sysbeepstop();

	if (cpl)
		current->utime++;
	else
		current->stime++;

	if (next_timer) {
		next_timer->jiffies--;
		while (next_timer && next_timer->jiffies <= 0) {
			void (*fn)(void);
			
			fn = next_timer->fn;
			next_timer->fn = NULL;
			next_timer = next_timer->next;
			(fn)();
		}
	}
	if (current_DOR & 0xf0)
		do_floppy_timer();
	if ((--current->counter)>0) return;
	current->counter=0;
	if (!cpl) return;
	schedule();
}

int sys_alarm(long seconds)
{
	int old = current->alarm;

	if (old)
		old = (old - jiffies) / HZ;
	current->alarm = (seconds>0)?(jiffies+HZ*seconds):0;
	return (old);
}

int sys_getpid(void)
{
	return current->pid;
}

int sys_getppid(void)
{
	return current->father;
}

int sys_getuid(void)
{
	return current->uid;
}

int sys_geteuid(void)
{
	return current->euid;
}

int sys_getgid(void)
{
	return current->gid;
}

int sys_getegid(void)
{
	return current->egid;
}

int sys_nice(long increment)
{
	if (current->priority-increment>0)
		current->priority -= increment;
	return 0;
}

void sched_init(void)
{
	int i;
	struct desc_struct * p;

	if (sizeof(struct sigaction) != 16)
		panic("Struct sigaction MUST be 16 bytes");
	set_tss_desc(gdt+FIRST_TSS_ENTRY,&(init_task.task.tss));
	set_ldt_desc(gdt+FIRST_LDT_ENTRY,&(init_task.task.ldt));
	p = gdt+2+FIRST_TSS_ENTRY;
	for(i=1;i<NR_TASKS;i++) {
		task[i] = NULL;
		p->a=p->b=0;
		p++;
		p->a=p->b=0;
		p++;
	}
/* Clear NT, so that we won't have troubles with that later on */
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
	ltr(0);
	lldt(0);
	outb_p(0x36,0x43);		/* binary, mode 3, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	set_intr_gate(0x20,&timer_interrupt);
	outb(inb_p(0x21)&~0x01,0x21);
	set_system_gate(0x80,&system_call);
}
