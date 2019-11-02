/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>
/**
  （1）main.c 程序首先利用setup.s 程序取得的系统参数设置系统的根文件设备号以及一些内存全局变量
  （2）内核进行所有方面的硬件初始化工作。包括陷阱门、块设备、字符设备和tty，包括人工创建第一个任务（task 0）。
   待所有初始化工作完成就设置中断允许标志，开启中断
  （3）内核完成初始化后，内核将执行权切换到了用户模式，也即CPU 从0 特权级切换到了第3 特权级。然后系统
  第一次调用创建进程函数fork()，创建出一个用于运行init()的子进程。
  （4）在该进程（任务）中系统将运行控制台程序。如果控制台环境建立成功，则再生成一个子进程，用于
   运行shell 程序/bin/sh。若该子进程退出，父进程返回，则父进程进入一个死循环内，继续生成子进程，并
   在此子进程中再次执行shell 程序/bin/sh，而父进程则继续等待。

*/

/**
1.CMOS信息
  CMOS内存是电池供电的64或128字节内存块，是RTC的一部分，存放时钟和日期信息
 存放格式是BCD码
   地址空间在基本地址空间之外，不包含可执行代码，需要端口进行访问
   0x70 地址端口  
   0x71 数据端口 （IN）

   读取字节偏移，写数据
   OUT 0x70 送地址
   IN  0x71 读数据
*/



/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
/*
* 我们需要下面这些内嵌语句 - 从内核空间创建进程(forking)将导致没有写时复制（COPY ON WRITE）!!!
* 直到一个执行execve 调用。这对堆栈可能带来问题。处理的方法是在fork()调用之后不让main()使用
* 任何堆栈。因此就不能有函数调用 - 这意味着fork 也要使用内嵌的代码，否则我们在从fork()退出
* 时就要使用堆栈了。
* 实际上只有pause 和fork 需要使用内嵌方式，以保证从main()中不会弄乱堆栈，但是我们同时还
* 定义了其它一些函数。*/

//Linux在内核空间创建进程时不使用写时复制。main()函数在移动到用户模式（任务0）之后执行内嵌方式的
//fork()和pause(),因此保证不使用任务0的用户栈。在执行move_to_user_mode()之后，程序就以任务0的
//身份运行。任务0是所有将创建子进程的父进程。当创建init进程之后，由于任务1属于内核进程，无写时复制的
//功能。此时用户0的用户栈就是任务1的用户栈，他们共同使用一个栈空间。因此在执行任务0的时候不希望对
//堆栈有任何的操作。再执行fork + execve函数之后，已经不属于内核空间，可以使用写时复制。



/**
（1）static inline _syscall0(int,fork)
// 是unistd.h 中的内嵌宏代码。以嵌入汇编的形式调用
// Linux 的系统调用中断0x80。该中断是所有系统调用的
// 入口。该条语句实际上是int fork()创建进程系统调用。

（2）static inline _syscall0(int,pause)
  //int pause()系统调用，暂停进程的执行，直到收到一个信号

（3）static inline _syscall1(int,setup,void *,BIOS)
  // int setup(void * BIOS)系统调用，仅用于
  // linux 初始化（仅在这个程序中被调用）

（4）static inline _syscall0(int,sync)
  // int sync()系统调用：更新文件系统。

**/


/**
// 以下定义系统调用嵌入式汇编宏函数。
// 不带参数的系统调用宏函数。type name(void)。
// %0 - eax(__res)，%1 - eax(__NR_##name)。其中name 是系统调用的名称，与 __NR_ 组合形成上面
// 的系统调用符号常数，从而用来对系统调用表中函数指针寻址。
// 返回：如果返回值大于等于0，则返回该值，否则置出错号errno，并返回-1。
#define _syscall0(type,name) \
type name(void) \
{ \
long __res; \
__asm__ volatile ( "int $0x80" \        // 调用系统中断0x80。
:"=a" (__res) \                        // 返回值赋值给eax(__res)。
:"" (__NR_##name)); \                 // 输入为系统中断调用号__NR_name。
      if (__res >= 0) \                // 如果返回值>=0，则直接返回该值。
      return (type) __res; errno = -__res; \        // 否则置出错号，并返回-1。
      return -1;}

将代码展开得到：
static inline int fork(void)
{
	long __res;
	__asm__ volatile ( "int $0x80","=a"(__res),:"" (__NR_fork));
	if (__res >= 0)               
         return (type) __res;
    errno = -__res; 
    return -1;
} 

//gcc会把上述的语句直接插入fork()语句的代码处，执行fork()不会引起函数的调用
//系统中断调用执行中断指令INT时还是避免不了使用堆栈，但是使用的是内核栈，而不是
//用户栈，不会对用户栈产生影响。

//进程0和init进程（进程1）共用内核代码区（小于1MB的物理内存）相同的代码和物理内存页面（640KB），
//执行的代码不在一处，实际上用着相同的用户堆栈区。

//67
//238
*/
static inline _syscall0(int,fork)
static inline _syscall0(int,pause)
static inline _syscall1(int,setup,void *,BIOS)
static inline _syscall0(int,sync)

#include <linux/tty.h>  // tty 头文件，定义了有关tty_io，串行通信方面的参数、常数。
#include <linux/sched.h> // 调度程序头文件，定义了任务结构task_struct、第1 个初始任务
                         // 的数据。还有一些以宏的形式定义的有关描述符参数设置和获取的
                         // 嵌入式汇编函数程序。
#include <linux/head.h>  // head 头文件，定义了段描述符的简单结构，和几个选择符常量。
#include <asm/system.h>  // 系统头文件。以宏的形式定义了许多有关设置或修改
                         // 描述符/中断门等的嵌入式汇编子程序。
#include <asm/io.h>     // io 头文件。以宏的嵌入汇编程序形式定义对io 端口操作的函数。

#include <stddef.h>   // 标准定义头文件。定义了NULL, offsetof(TYPE, MEMBER)。
#include <stdarg.h>  // 标准参数头文件。以宏的形式定义变量参数列表。主要说明了-个
                     // 类型(va_list)和三个宏(va_start, va_arg 和va_end)，vsprintf、
                     // vprintf、vfprintf。
#include <unistd.h>  
#include <fcntl.h>  // 文件控制头文件。用于文件及其描述符的操作控制常数符号的定义。
#include <sys/types.h> // 类型头文件。定义了基本的系统数据类型

#include <linux/fs.h> // 文件系统头文件。定义文件表结构（file,buffer_head,m_inode 等）。

static char printbuf[1024]; // 静态字符串数组。

extern int vsprintf();  // 送格式化输出到一字符串中（在kernel/vsprintf.c，92 行）。
extern void init(void); // 函数原形，初始化（在168 行）。
extern void blk_dev_init(void);  // 块设备初始化子程序（kernel/blk_drv/ll_rw_blk.c,157 行）
extern void chr_dev_init(void); // 字符设备初始化（kernel/chr_drv/tty_io.c, 347 行）
extern void hd_init(void);  // 硬盘初始化程序（kernel/blk_drv/hd.c, 343 行）
extern void floppy_init(void); // 软驱初始化程序（kernel/blk_drv/floppy.c, 457 行）
extern void mem_init(long start, long end); // 内存管理初始化（mm/memory.c, 399 行）
extern long rd_init(long mem_start, int length); //虚拟盘初始化(kernel/blk_drv/ramdisk.c,52)
extern long kernel_mktime(struct tm * tm);  // 建立内核时间（秒）
extern long startup_time; // 内核启动时间（开机时间）（秒）

/*
 * This is set up by the setup-routine at boot-time
   以下这些数据是由setup.s 程序在引导时间设置的
 */

//将指定的线性地址强行转换为给定数据类型的指针，并获取指针的内容
//内核代码段被映射到从物理地址零开始的地方，现行地址刚好对应物理地址
#define EXT_MEM_K (*(unsigned short *)0x90002) // 1M 以后的扩展内存大小（KB）。
#define DRIVE_INFO (*(struct drive_info *)0x90080) // 硬盘参数表基址。
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC) // 根文件系统所在设备号。

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

//这段宏读取CMOS 实时时钟信息。
// 0x70 是写端口号，0x80|addr 是要读取的CMOS 内存地址。
// 0x71 是读端口号。
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

// 将BCD 码转换成二进制数字。
//使用半个字节（4Bit）表示一个十进制数
//1个字节表示2个10进制数，
//(val)&15表示10进制的个位数
//(val)>>4 表示10进制的十位数，再乘以10，
//两者之和就是一个字节的BCD码实际的二进制数
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

// 该子程序取CMOS 时钟，并设置开机时间成startup_time(秒)。
// 取CMOS实时时钟信息为开机时间，并保存到全局变量startup_time（秒）中。
//调用的kernel_mktime()用于计算从1970.1.1 0时起到开机当日经过的秒数作为
//开机时间
static void time_init(void)
{
	struct tm time;
    //CMOS访问速度慢，为了减小时间误差，在读取了循环所有的数值之后
    //如cmos中秒数发生了变化，重新读取，内核就能把时间控制在1s之内
	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));

	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;//0-11
	//计算机开机时间
	startup_time = kernel_mktime(&time);
}

//机器具有的物理内存容量（字节数）
static long memory_end = 0;

//高速缓冲区末端地址
static long buffer_memory_end = 0;

//主内存（将用于分页）开始的位置
static long main_memory_start = 0;

//结构体用于存放硬盘参数表信息
struct drive_info { char dummy[32]; } drive_info;

//内核初始化程序，初始化结束之后将以任务0（idle任务即空闲任务）的身份运行
void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
 /*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */

   //ROOT_DEV 根设备号  include/fs.h  extern int ROOT_DEV;
   //memory_end 机器内存数
   //buffer_memory_end 高速缓存末端地址
   //main_memory_start 主内存开始的地方
 	ROOT_DEV = ORIG_ROOT_DEV;  // fs/super.c int ROOT_DEV = 0;
 	drive_info = DRIVE_INFO;  //复制0x90080处的硬盘参数
	memory_end = (1<<20) + (EXT_MEM_K<<10); //内存大小 = 1MB + 扩展内存（k）*1024字节
	memory_end &= 0xfffff000; //忽略不到4KB(1页)的内存数
	if (memory_end > 16*1024*1024) //内存>16MB,按16MB算
		memory_end = 16*1024*1024;

	if (memory_end > 12*1024*1024) //内存 > 12MB,则设置缓冲区末端 = 2MB
		buffer_memory_end = 4*1024*1024;
	else if (memory_end > 6*1024*1024) //内存 > 6MB,则设置缓冲区末端 = 4MB
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024; //设置缓冲区末端为1M
	main_memory_start = buffer_memory_end;//主内存起始位置 = 缓冲区末端

//MakeFile文件中定义了内存虚拟盘符号RAMDISK
//则初始化虚拟盘，主内存将减少
//kernel/blk_drv/ramdisk.c
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
	mem_init(main_memory_start,memory_end); //主内存初始化 mm/memory.c
	trap_init(); //陷阱门（硬件中断向量）初始化 kernel/traps.c
	blk_dev_init();//块设备初始化 kernel/blk_dev/ll_rw_blk.c
	chr_dev_init();//字符设备初始化 kernel/chr_dev/tty_io.c
	tty_init();//tty初始化 kernel/chr_dev/tty_io.c
	time_init();//设置开机时间
	sched_init();//调度程序初始化（加载任务0的tr，ldtr）(kernel/sched.c)
	buffer_init(buffer_memory_end);//缓冲管理初始化建内存链表 fs/buffer.c
	hd_init();//硬盘初始化
	floppy_init();//软驱初始化
	sti();//开启中断
	//在堆栈中设置参数，利用中断返回指令启动任务0的执行
	move_to_user_mode();//移动到任务模式下 include/asm/system.h
	if (!fork()) {		/* we count on this going ok */
		init();//子进程中运行任务1
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */

	//pause()意味着必须等待收到一个信号才开始返回就绪态，但是任务0是特例
	//任务0在任何空闲时间都会被激活（没有其他任务在运行）
	//即任务0 pause()意味着我们返回来查看是否有其他任务在运行，没有就执行
	//死循环pause()

	//pause()系统调用（kernel/sched.c）将任务0切换成可中断等待状态，再执行
	//调度函数，调度函数发现系统中无任务时会切回任务0，不依赖于任务0的状态
	for(;;) pause();
}
//格式化信息，输出到标准输出设备 stdout(1)
//*fmt为格式
static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	//vsprintf格式化字符
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}
//读取并执行/etc/rc文件所使用的命令行参数和环境参数
static char * argv_rc[] = { "/bin/sh", NULL };//执行程序时参数字符数组
static char * envp_rc[] = { "HOME=/", NULL };//环境字符串数组

//登录shell使用的命名行参数和环境参数
// - 是传递给shell程序的sh的一个标志，识别这个标志，sh会作为登录shell
//执行，执行过程和在shell提示符下执行sh不一样
static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

//main()进行系统的初始化，包括内存管理，硬件设备和驱动程序，init()函数
//运行在任务0第一次创建的子进程（任务1）中，对第一个要执行的程序（shell）
//进行初始化，然后以登录shell的方式加载程序并执行

void init(void)
{
	int pid,i;
	//setup()是系统调用，用于读取硬盘参数包括分区表信息并加载虚拟盘（已定义）
	//和安装根文件系统设备。宏定义为 sys_setup(),块设备子目录 /kernel/blk_drv/hd.c
	setup((void *) &drive_info);//drive_info结构中的第二个硬盘的参数
	
	//以读写访问的方式打开设备‘/dev/tty’,对应终端控制台。第一次打开文件操作，
	//产生的文件描述符为0，及为标准输入设备stdin。读写方式打开是为了复制stdout和stderr
	//void表示强制函数无返回值
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0);//复制文件描述符，产生1（stdout）
	(void) dup(0);//复制文件描述符，产生2（stderr）
	//打印缓冲区块数和总字节数，主内存区空闲字节数
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);
	
	//close()--open()将stdin重定向到/etc/rc，
	if (!(pid=fork())) {   //child
		close(0); //关闭stdin
		if (open("/etc/rc",O_RDONLY,0)) //只读方式打开
			_exit(1); //操作未许可
		execve("/bin/sh",argv_rc,envp_rc);//替换成shell程序
		_exit(2);//文件目录不存在
	}
	if (pid>0)  //parent
		//wait等待子进程结束或终止，返回值为子进程的pid
		while (pid != wait(&i)) //空循环
			/* nothing */;


	while (1) {
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {  //child
			//关闭遗留的0,1,2
			close(0);close(1);close(2);
			//新创建会话组并设置进程组号
			setsid();
			//重新打开终端控制台作为stdin
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}

        //wait()处理孤儿进程？
        //一个进程的父进程先终止了，这个进程的父进程会被设置为
        //这里的init进程（进程1），由init进程负责释放终止进程资源
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();//同步操作，刷新缓冲区
	}
	_exit(0);	/* NOTE! _exit, not exit() */
}

//_exit() 直接是一个sys_exit()系统调用

//exit() 普通库函数，执行清除操作，执行各个终止处理程序，关闭标准IO,
//最后调用sys_exit()
