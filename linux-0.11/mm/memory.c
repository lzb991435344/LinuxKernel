/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */
//定义信号符号常量，信号结构，以及信号操作函数原型。
#include <signal.h>

//系统头文件，定义了设置或修改描述符/中断门等的嵌入式汇编宏
#include <asm/system.h>

//程序调度头文件，定义了任务结构task_struct，初始任务0的数据
//和一些关于描述符参数设置和获取的嵌入式汇编函数宏语句。
#include <linux/sched.h>

//定义段描述符的简单结构，和几个选择符常量
#include <linux/head.h>

//含有内核常用函数原型
#include <linux/kernel.h>

//进程退出函数
volatile void do_exit(long code);

//显示内存已经用完出错信息，退出
static inline volatile void oom(void)
{
	printk("out of memory\n\r");
	//使用了信号SIGSEGV(11),表示为资源暂时不可用
	do_exit(SIGSEGV);
}
//刷新页变换高速缓冲宏函数
//为提高地址转换的效率，cpu将最近使用的页表数据存在高速缓冲中，
//修改信息后刷新缓冲区。这里使用重新加载页目录基址寄存器cr3的方法刷新。
//下面是eax=0,是页目录的基址。
#define invalidate() \
__asm__("movl %%eax,%%cr3"::"a" (0))

/* these are not to be changed without changing head.s etc */
//Linux0.11内核默认内存支持最大容量是16M,修改该定义获取更大的内存
#define LOW_MEM 0x100000 //低端内存1MB
#define PAGING_MEMORY (15*1024*1024) //分页内存15MB，主存最大15M
#define PAGING_PAGES (PAGING_MEMORY>>12)//分页后的物理内存页数
#define MAP_NR(addr) (((addr)-LOW_MEM)>>12)//指定内存地址映射为页号
#define USED 100 //页面被占用标志 

//该宏用于判断给定的地址是否在当前进程的代码中
#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

//全局变量，存放实际物理内存最高端地址
static long HIGH_MEMORY = 0;

//复制1页内存（4K字节）
#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")

//内存映射字节图（1字节代表1页内存），每个页面对应的字节用于标志档期内页面被引用（占用）次数。
static unsigned char mem_map [ PAGING_PAGES ] = {0,};

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
 
 /**
    获取首个空闲页面（实际上是最后一个），并标记为已使用。无空闲页面返回0。
取空闲页面，无可用内存返回0。
输入：%1(ax=0)-0;%2(LOW_MEM);%3(cx=PAGES);%4(edi=mem_map+PAGING_PAGES-1).
输出：返回%0(ax=页面其实地址) 	
 --%4寄存器指向mem_map[]内存字节图的最后一个字节。本函数从字节末端开始向前扫描所有页面标志
 (页面总数为PAGING_PAGES)，若有页面空闲(其内存映射字节为0)则返回页面地址。
 --本函数只是指出在主内存区的一页空闲页面，但并没有映射到某个进程的线性地址去。
 
 */
unsigned long get_free_page(void)
{
register unsigned long __res asm("ax");

//方向位置位，将a1(0)与对应每个页面的(di)内容比较。
__asm__("std ; repne ; scasb\n\t"
	"jne 1f\n\t" //没有等于0的字节，返回0
	"movb $1,1(%%edi)\n\t" //将对应页面的内存映像位置1
	"sall $12,%%ecx\n\t" //页面数*4K=相对页面起始地址
	"addl %2,%%ecx\n\t" //加上低端内存地址，获得页面实际物理起始地址
	"movl %%ecx,%%edx\n\t"//页面起始地址赋值给edx寄存器
	"movl $1024,%%ecx\n\t" //寄存器ecx置计数值1024
	"leal 4092(%%edx),%%edi\n\t"//4092+edx位置赋值给edi（该页面末端）
	"rep ; stosl\n\t" //edi所指内存清零
	"movl %%edx,%%eax\n" //页面起始地址赋值给eax
	"1:"
	:"=a" (__res)
	:"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
	"D" (mem_map+PAGING_PAGES-1)
	:"di","cx","dx");
return __res; //返回空闲页面地址(无空闲返回0)
}

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
 //释放物理地址addr开始的一页面内存
 //1Mb以下的内存空间用于内核程序和缓冲，不作为分配页面的内存空间
void free_page(unsigned long addr)
{
	//物理地址小于内存低端1MB
	if (addr < LOW_MEM) return;
	//大于物理内存最高端
	if (addr >= HIGH_MEMORY)
		panic("trying to free nonexistent page");
	addr -= LOW_MEM;//(物理内存-低端内存)/4kb,得到页面号
	addr >>= 12;//2^12=4kb
	//对应内存页面映射字节不等于0，减1返回
	if (mem_map[addr]--) return;
	//对应页面映射字节设为0，死机
	mem_map[addr]=0;
	panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */
 //释放物理地址addr开始的一页内存
 /**
  释放页表连续的内存块，仅处理4MB的内存块。
根据指定的线性地址和线长(页表个数)，释放对应的内存页表所指向的内存块并置表项空闲
页目录位于物理地址为0开始的地方，共1024项，占4K字节，每个目录项指定一个页表
页表从物理地址0X1000处开始，紧接着是目录空间，每个页表有1024项，也占4k内存
每个页表项对应一个物理内存(4K),目录项和页表项均为4个字节
//参数：from-起始基地址;size-释放的长度   
   
 */
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long * dir, nr;

	if (from & 0x3fffff)//要释放的内存块的地址以4M为边界
		panic("free_page_tables called with wrong alignment");
	if (!from)//出错
		panic("Trying to free up swapper memory space");
	//计算占页目录项数（4Mb的进位整数倍），即所占的页表数
	size = (size + 0x3fffff) >> 22;
	/**
	  计算起始的目录项，对应的目录项号=from>>22,因每项占4个字节，并且页目录是从
物理地址0开始，实际的目录项指针=目录项号<<2,from>>20,与上0xffc保证指针有效	  
	*/
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	for ( ; size-->0 ; dir++) {//size为被释放的内存目录项数
		if (!(1 & *dir))//目录项无效
			continue;//目录项的位p位对应页表是否存在？
		//取目录项中的页表地址
		pg_table = (unsigned long *) (0xfffff000 & *dir);
		for (nr=0 ; nr<1024 ; nr++) { //每个页表有1024个页项
			//该页表项有效，释放对应的内存页。
			if (1 & *pg_table) //p位为1
				free_page(0xfffff000 & *pg_table);
			*pg_table = 0;//页表项内容清零
			pg_table++;//指向页表的下一项
		}
		//释放该页表所占的内存页面，页表在物理地址1M以内，什么都不做。
		free_page(0xfffff000 & *dir);
		*dir = 0; //对应页表的目录项清零
	}
	//刷新页变换高速缓存
	invalidate();
	return 0;
}

/*    ---复制内存页面来拷贝一定范围内线性地址的内容
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */
 /**
    复制指定的线性地址和长度(页面个数)内存对应的页目录项和页表，从而
被复制的页目录项和页表对应的原物理内存区域被共享
    复制指定地址和长度的内存对应的页目录项和页表项，需要申请页面来存放
页表，原内存被共享，此后两个进程共享内存区，直到有一个进程执行写操作时，
才分配新的内存页（写时复制）。	
 
 */
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
	unsigned long * from_page_table;
	unsigned long * to_page_table;
	unsigned long this_page;
	unsigned long * from_dir, * to_dir;
	unsigned long nr;
    //源地址和目的地址都要在4Mb的内存边界上，否则死机
	if ((from&0x3fffff) || (to&0x3fffff))
		panic("copy_page_tables called with wrong alignment");
	//取得源地址和目的地址的目录项
	from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	to_dir = (unsigned long *) ((to>>20) & 0xffc);
	//计算复制的内存块占用的页表数目
	size = ((unsigned) (size+0x3fffff)) >> 22;
	//对占用的页表进行复制
	for( ; size-->0 ; from_dir++,to_dir++) {
		//目录项指定的页表存在，死机
		if (1 & *to_dir)
			panic("copy_page_tables: already exist");
		//源目录项未被使用，不复制对应的页表，跳过
		if (!(1 & *from_dir))
			continue;
		//取源目录项中页表的地址
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
		//为目的页表取一页空闲内存，返回0说明没有申请到内存空闲页面，返回值=-1
		if (!(to_page_table = (unsigned long *) get_free_page()))
			return -1;	/* Out of memory, see freeing */
		//设置目的目录项信息，7是标志信息。表示(Usr，R/W,Present)
		*to_dir = ((unsigned long) to_page_table) | 7;
		//针对当前处理的页表，设置需要复制的页面数。内核空间，仅需要复制头160页
		//否则需要复制1个页表中所有的1024页面
		nr = (from==0)?0xA0:1024;
		//当前页表，复制nr个内存页面
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
			this_page = *from_page_table; //取源页表内容
			if (!(1 & this_page)) //无内容，不再复制
				continue;
				/**
				复位页表项中R/W标志（置0）
				u/s为0，R/W无作用，（1，0）读，都置位则为写
				*/
			this_page &= ~2;
			*to_page_table = this_page;//页表项复制到目的页表
			
			//页表项所指页面地址在1M以上，需要设置内存页面映射数组
			//mem_map[],计算页面号，设置为索引在页面映射数组中增加引用次数
			if (this_page > LOW_MEM) {
				*from_page_table = this_page;//源页表项也只读
				
				/**
				目前有两个进程共用内存区，其中一个内存需要进行写操作，则
				可以通过页异常的写保护处理，为执行写操作的进程分配一页行的空闲页面
				也即进行写时复制操作
				*/
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++;
			}
		}
	}
	invalidate();
	return 0;
}

/*将内存页面放置在指定的地址处，返回页面的物理地址，内存不够返回0
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
 //把一物理内存页面映射到指定的线性地址
 //在页目录和页表中设置指定的页面信息，成功返回页面的地址
unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */
    
	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n",page,address);
	//申请的页面在内存页面映射字节图中没有置位
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
	//指定地址在页目录表中对应的目录项指针
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		//目录项有效，页表在内存里，获取页表的地址
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		//申请空闲页面给页表使用，在目录项中设置标志位7(User,U/S,R/W)
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp|7;
		//赋值页表地址
		page_table = (unsigned long *) tmp;
	}
	//页表中设置指定地址的物理内存页面页表项内容，页表有1024项即0x3ff
	page_table[(address>>12) & 0x3ff] = page | 7;
/* no need for invalidate */
	return page; //返回页面地址
}
//取消页面写保护函数，用于页异常中断过程中写保护异常处理(写时复制)，参数为表项指针
void un_wp_page(unsigned long * table_entry)
{//取消页面写保护
	unsigned long old_page,new_page;
    //取原页面对应的目录项号
	old_page = 0xfffff000 & *table_entry;
	//大于页面地段内存，在页面映射字节图数组中值为1(仅引用一次，页面没有被共享)
	//该页面的页表项中置R/W标志，可写
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		return;
	}
	//在主存中申请一空闲页面
	if (!(new_page=get_free_page()))
		oom();//内存不足
	if (old_page >= LOW_MEM) //大于低端内存，mem_map[]>1页面共享
		//原页面映射数组值减1
		mem_map[MAP_NR(old_page)]--;
	//设置可读写等标志位
	*table_entry = new_page | 7;
	invalidate();
	//原页面复制到新页面
	copy_page(old_page,new_page);
}	

/*用户在共享页面上写时，处理已存在的页面内存，写时复制是通过将页面
  复制到一个新地址上并递减原页面的共享页面计数值实现。在代码空间中返回段错误。
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
 //页异常中断处理的函数。写共享页面处理函数，在page.s程序中被调用
 //参数1是cpu产生，2是页面线性地址
 //写共享页面时，需复制页面
void do_wp_page(unsigned long error_code,unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
	if (CODE_SPACE(address))
		do_exit(SIGSEGV);//地址位于代码空间
#endif
   //处理取消页面保护，对共享页面进行复制，参数是在页表中页表项指针
   //1.计算指定地址的页面在页表中的偏移地址
   //2.计算页面所在页表的目录项指针
   //3.和即为指定地址对应页面的页表项指针
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 &
		*((unsigned long *) ((address>>20) &0xffc)))));
}
//写页面验证，页面不可写，则复制页面
void write_verify(unsigned long address)
{
	unsigned long page;
     //指定地址的页目录项的页表是否存在，p=0返回
	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
	//取页表的地址，加上偏移量，得到物理页面对应的页表项指针
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);
	//不可写(标志R/W没有置位)，执行共享检验和复制页面操作
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page);
	return;
}
/**
  取一页空闲内存并映射到指定线性地址处
  get_free_page()函数仅是申请主内存的一页物理地址
  put_page物理页面映射到指定线性地址
*/
void get_empty_page(unsigned long address)
{
	unsigned long tmp;
     //取不到空闲页面，或者不能将页面放到指定的位置处，显示内存不足
	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */
		//前一个函数返回0也可，后面的函数会再次申请空闲物理页面
		oom();
	}
}

/*在任务p中检查位于地址address中的页面，页面是否存在和干净，干净就共享
  已设定p不为当前任务，并且他们共享同一个执行程序
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable.
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;
    //指定内存的页目录项
	from_page = to_page = ((address>>20) & 0xffc);
	//p进程的代码的起始地址对应的页目录项
	from_page += ((p->start_code>>20) & 0xffc);
	//当前进程中代码起始地址对应的页目录项
	to_page += ((current->start_code>>20) & 0xffc);
/* is there a page-directory at from? */ //from处是否存在页目录
//对p进程页面进行操作
	from = *(unsigned long *) from_page;
	//取页目录项内容，p无效，返回
	//有效则取目录项对应的页表地址
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	//计算对应地址的页表项指针值，取出内容
	from_page = from + ((address>>10) & 0xffc);
	phys_addr = *(unsigned long *) from_page;
/* is the page clean and present? *///页面干净且存在？
    //0x41对应页表项中的dirty和present标志，不干净或无效返回
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	//取页面地址
	phys_addr &= 0xfffff000;
	//页面地址不存在和小于内存低端（1M）返回退出
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;
	
	//对当前页面进程进行操作
	//取页目录项内容，无效则取空闲页面，更新to_page指向的目录项
	to = *(unsigned long *) to_page;
	if (!(to & 1))
		if (to = get_free_page())
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	//取页表地址	
	to &= 0xfffff000;
	//页表项地址
	to_page = to + ((address>>10) & 0xffc);
	//存在，死机
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
  //共享处理，写保护
   //对p进程页面置写保护标志（只读），当前进程中对应的页表项指向它
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();
	//计算操作页面的页面号，对应页面的映射数组引用增加1
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;
	return 1;
}

/* 试图找到一个进程，它可以与当前的进程共享页面
   参数是数据空间中期望共享某页面的地址
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
 //共享页面，在缺页处理时看看能否共享页面。
static int share_page(unsigned long address)
{
	struct task_struct ** p;
       //如果是不可执行，返回
	if (!current->executable)//executable是执行进程的内存i节点结构
		return 0;
		//只能单独执行，也返回
	if (current->executable->i_count < 2)
		return 0;
	//搜索任务数组中的所有任务，寻找与当前进程可共享页面的进程
	//尝试对指定地址的页面进行共享
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p) //任务项空闲，继续找
			continue;
		if (current == *p)//当前任务继续查找
			continue;
			//executable不等，继续
		if ((*p)->executable != current->executable)
			continue;
		//共享页面
		if (try_to_share(address,*p))
			return 1;
	}
	return 0;
}
//页面异常中断处理，处理页面异常情况，在page.s中被调用
void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;

	address &= 0xfffff000;//页面地址
	//计算指定线性地址空间在进程空间中相对应于进程基地址的偏移长度值
	tmp = address - current->start_code;
	/*
	 进程的executable为空或者指定地址超出代码+数据长度
	内核中代码段和数据段的起始基地址
	*/
	if (!current->executable || tmp >= current->end_data) {
		//申请物理内存，并映射到指定的线性地址
		get_empty_page(address);
		return;
	}
	//尝试共享页面
	if (share_page(tmp))
		return;
	//取共享页面，内存不够，终止进程
	if (!(page = get_free_page()))
		oom();
/* remember that 1 block is used for header */
  //程序头要用一个数据块，计算缺页所在的数据块项，BlockSize=1024，
  //一共需要4个数据块
	block = 1 + tmp/BLOCK_SIZE;
	//根据i节点号，取数据块在设备上的逻辑块号
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(current->executable,block);
	//读设备上的一个页面数据（4个逻辑块）到指定的物理地址page处
	bread_page(page,current->executable->i_dev,nr);
	//增加一页内存之后，该页的内存的部分可能会超过进程的end_data位置
	//下面循环对超出的部分进行清零
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	//物理页面映射到线性地址成功，返回
	if (put_page(page,address))
		return;
	free_page(page);
	oom();
}

/**
 物理内存页面初始化
 参数1：可用作分页处理的物理内存的起始位置
 参数2：物理内存的最大地址
 0-1M属于内核空间0-640K
*/
void mem_init(long start_mem, long end_mem)
{
	int i;

	HIGH_MEMORY = end_mem;//设置内存最高端
	for (i=0 ; i<PAGING_PAGES ; i++)
		mem_map[i] = USED;//所有页面设置为已占用状态（used=100）
	i = MAP_NR(start_mem);//计算可使用起始内存的页面号
	end_mem -= start_mem; //分页处理的内存块大小
	end_mem >>= 12; //计算可用于分页处理的页面数
	while (end_mem-->0)//将可用的页面对应的页面映射数组清零
		mem_map[i++]=0;
}

//计算内存空闲页面并显示
void calc_mem(void)
{
	int i,j,k,free=0;
	long * pg_tbl;
    //扫描内存页面映射数组，获取空闲页面并显示
	for(i=0 ; i<PAGING_PAGES ; i++)
		if (!mem_map[i]) free++;
	printk("%d pages free (of %d)\n\r",free,PAGING_PAGES);
	//扫描页目录项(0,1外)有效则统计对应页表中有效页面数，显示
	for(i=2 ; i<1024 ; i++) {
		if (1&pg_dir[i]) {
			pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
			for(j=k=0 ; j<1024 ; j++)
				if (pg_tbl[j]&1)
					k++;
			printk("Pg-dir[%d] uses %d pages\n",i,k);
		}
	}
}
