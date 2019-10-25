!
! SYS_SIZE is the number of clicks (16 bytes) to be loaded.
! 0x3000 is 0x30000 bytes = 196kB, more than enough for current
! versions of linux   
!
SYSSIZE = 0x3000
!
!	bootsect.s		(C) 1991 Linus Torvalds
!
! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
! iself out of the way to address 0x90000, and jumps there.
!
! It then loads 'setup' directly after itself (0x90200), and the system
! at 0x10000, using BIOS interrupts. 
!
! NOTE! currently system is at most 8*65536 bytes long. This should be no
! problem, even in the future. I want to keep it simple. This 512 kB
! kernel size should be enough, especially as this doesn't contain the
! buffer cache as in minix
!
! The loader has been made as simple as possible, and continuos
! read errors will result in a unbreakable loop. Reboot by hand. It
! loads pretty fast by getting whole sectors at a time whenever possible.


! bootsect.s 被 bios-启动子程序加载至 0x7c00 (31k)处，并将自己
! 移到了地址 0x90000 (576k)处，并跳转至那里。
!
! 它然后使用 BIOS 中断将'setup'直接加载到自己的后面(0x90200)(576.5k)，
! 并将 system 加载到地址 0x10000 处。
!
! 注意! 目前的内核系统最大长度限制为(8*65536)(512k)字节，即使是在
! 将来这也应该没有问题的。我想让它保持简单明了。这样 512k 的最大内核长度应该
! 足够了，尤其是这里没有象 minix 中一样包含缓冲区高速缓冲。
!
! 加载程序已经做的够简单了，所以持续的读出错将导致死循环。只能手工重启。
! 只要可能，通过一次取取所有的扇区，加载过程可以做的很快的。



.globl begtext, begdata, begbss, endtext, enddata, endbss  ! 定义了 6 个全局标识符；
.text       ! 文本段；
begtext:
.data       ! 数据段；
begdata:
.bss         ! 堆栈段；
begbss:
.text       ! 文本段；

SETUPLEN = 4				! nr of setup-sectors   ! setup 程序的扇区数(setup-sectors)值；
BOOTSEG  = 0x07c0			! original address of boot-sector  bootsect 的原始地址（是段地址，以下同）；
INITSEG  = 0x9000			! we move boot here - out of the way  ! 将 bootsect 移到这里 -- 避开；
SETUPSEG = 0x9020			! setup starts here  ! setup 程序从这里开始；
SYSSEG   = 0x1000			! system loaded at 0x10000 (65536). ! system 模块加载到 0x10000（64 kB）处；
ENDSEG   = SYSSEG + SYSSIZE		! where to stop loading   ! 停止加载的段地址；
0
! ROOT_DEV:	0x000 - same type of floppy as boot.  根文件系统设备使用与引导时同样的软驱设备；
!		0x301 - first partition on first drive etc 根文件系统设备在第一个硬盘的第一个分区上，等等；
ROOT_DEV = 0x306                            
! 指定根文件系统设备是第 2 个硬盘的第 1 个分区。这是 Linux 老式的硬盘命名
! 方式,具体值的含义如下：
! 设备号=主设备号*256 + 次设备号（也即 dev_no = (major<<8) + minor ）
! （主设备号：1-内存,2-磁盘,3-硬盘,4-ttyx,5-tty,6-并行口,7-非命名管道）
! 0x300 - /dev/hd0 - 代表整个第 1 个硬盘；
! 0x301 - /dev/hd1 - 第 1 个盘的第 1 个分区；
! …
! 0x304 - /dev/hd4 - 第 1 个盘的第 4 个分区；
! 0x305 - /dev/hd5 - 代表整个第 2 个硬盘盘；
! 0x306 - /dev/hd6 - 第 2 个盘的第 1 个分区；
! …
! 0x309 - /dev/hd9 - 第 2 个盘的第 4 个分区；
! 从 linux 内核 0.95 版后已经使用与现在相同的命名方法了。         
entry start   ! 告知连接程序，程序从 start 标号开始执行。
               ! 47--56 行作用是将自身(bootsect)从目前段位置 0x07c0(31k)
               ! 移动到 0x9000(576k)处，共 256 字（512 字节），然后跳转到
               ! 移动后代码的 go 标号处，也即本程序的下一语句处。

start:
	mov	ax,#BOOTSEG  ! 将 ds 段寄存器置为 0x7C0；
	mov	ds,ax
	mov	ax,#INITSEG  ! 将 es 段寄存器置为 0x9000；
	mov	es,ax
	mov	cx,#256     ! 移动计数值=256 字；
	sub	si,si      ! 源地址 ds:si = 0x07C0:0x0000
	sub	di,di    ! 目的地址 es:di = 0x9000:0x0000
	rep          ! 重复执行，直到 cx = 0
	movw           ! 移动 1 个字；
	jmpi	go,INITSEG   ! 间接跳转。这里 INITSEG 指出跳转到的段地址。
go:	mov	ax,cs     ! 将 ds、es 和 ss 都置成移动后代码所在的段处(0x9000)。
	mov	ds,ax     ！由于程序中有堆栈操作(push,pop,call)，因此必须设置堆栈。
	mov	es,ax    
! put stack at 0x9ff00.  ! 将堆栈指针 sp 指向 0x9ff00(即 0x9000:0xff00)处
	mov	ss,ax
	mov	sp,#0xFF00		! arbitrary value >>512
                          ! 由于代码段移动过了，所以要重新设置堆栈段的位置。
                 ! sp 只要指向远大于 512 偏移（即地址 0x90200）处
          ! 都可以。因为从 0x90200 地址开始处还要放置 setup 程序，
          ! 而此时 setup 程序大约为 4 个扇区，因此 sp 要指向大
          ! 于（0x200 + 0x200 * 4 + 堆栈大小） 处。
          ! load the setup-sectors directly after the bootblock.
          ! Note that 'es' is already set up.
		  
		  
! 在 bootsect 程序块后紧根着加载 setup 模块的代码数据。
! 注意 es 已经设置好了。（在移动代码时 es 已经指向目的段地址处 0x9000）。

! 68--77 行的用途是利用 BIOS 中断 INT 0x13 将 setup 模块从磁盘第 2 个扇区
! 开始读到 0x90200 开始处，共读 4 个扇区。如果读出错，则复位驱动器，并
! 重试，没有退路。INT 0x13 的使用方法如下：
! 读扇区：
! ah = 0x02 - 读磁盘扇区到内存；al = 需要读出的扇区数量；
! ch = 磁道(柱面)号的低 8 位； cl = 开始扇区(0-5 位)，磁道号高 2 位(6-7)；
! dh = 磁头号； dl = 驱动器号（如果是硬盘则要置位 7）；
! es:bx Æ指向数据缓冲区； 如果出错则 CF 标志置位。
load_setup:
	mov	dx,#0x0000		! drive 0, head 0
	mov	cx,#0x0002		! sector 2, track 0
	mov	bx,#0x0200		! address = 512, in INITSEG
	mov	ax,#0x0200+SETUPLEN	! service 2, nr of sectors
	int	0x13			! read it
	jnc	ok_load_setup		! ok - continue
	mov	dx,#0x0000
	mov	ax,#0x0000		! reset the diskette
	int	0x13
	j	load_setup

ok_load_setup:

! Get disk drive parameters, specifically nr of sectors/track
! 取磁盘驱动器的参数，特别是每道的扇区数量。
! 取磁盘驱动器参数 INT 0x13 调用格式和返回信息如下：
! ah = 0x08 dl = 驱动器号（如果是硬盘则要置位 7 为 1）。
! 返回信息：
! 如果出错则 CF 置位，并且 ah = 状态码。
! ah = 0， al = 0， bl = 驱动器类型（AT/PS2）
! ch = 最大磁道号的低 8 位，cl = 每磁道最大扇区数(位 0-5)，最大磁道号高 2 位(位 6-7)
! dh = 最大磁头数， dl = 驱动器数量，
! es:di -Æ 软驱磁盘参数表。
	mov	dl,#0x00
	mov	ax,#0x0800		! AH=8 is get drive parameters
	int	0x13
	mov	ch,#0x00
	seg cs   ! 表示下一条语句的操作数在 cs 段寄存器所指的段中。
	mov	sectors,cx  ! 保存每磁道扇区数。
	mov	ax,#INITSEG
	mov	es,ax   ! 因为上面取磁盘参数中断改掉了 es 的值，这里重新改回。

! Print some inane message  ! 在显示一些信息('Loading system ...'回车换行，共 24 个字符)。

	mov	ah,#0x03		! read cursor pos
	xor	bh,bh           ! 读光标位置。
	int	0x10
	
	mov	cx,#24          ! 共 24 个字符。
	mov	bx,#0x0007		! page 0, attribute 7 (normal)
	mov	bp,#msg1        ! 指向要显示的字符串。
	mov	ax,#0x1301		! write string, move cursor
	int	0x10            ! 写字符串并移动光标。

! ok, we've written the message, now
! we want to load the system (at 0x10000)
! 现在开始将 system 模块加载到 0x10000(64k)处。

	mov	ax,#SYSSEG
	mov	es,ax		! segment of 0x010000 ! es = 存放 system 的段地址。
	call	read_it  ! 读磁盘上 system 模块，es 为输入参数。
	call	kill_motor ! 关闭驱动器马达，这样就可以知道驱动器的状态了

! After that we check which root-device to use. If the device is
! defined (!= 0), nothing is done and the given device is used.
! Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
! on the number of sectors that the BIOS reports currently.
! 此后，我们检查要使用哪个根文件系统设备（简称根设备）。如果已经指定了设备(!=0)
! 就直接使用给定的设备。否则就需要根据 BIOS 报告的每磁道扇区数来
! 确定到底使用/dev/PS0 (2,28) 还是 /dev/at0 (2,8)。
! 上面一行中两个设备文件的含义：
! 在 Linux 中软驱的主设备号是 2(参见第 43 行的注释)，次设备号 = type*4 + nr，其中
! nr 为 0-3 分别对应软驱 A、B、C 或 D；type 是软驱的类型（2Æ1.2M 或 7Æ1.44M 等）。
! 因为 7*4 + 0 = 28，所以 /dev/PS0 (2,28)指的是 1.44M A 驱动器,其设备号是 0x021c
! 同理 /dev/at0 (2,8)指的是 1.2M A 驱动器，其设备号是 0x0208。

	seg cs
	mov	ax,root_dev  ! 将根设备号
	cmp	ax,#0
	jne	root_defined
	seg cs
	mov	bx,sectors  ! 取上面第 88 行保存的每磁道扇区数。如果 sectors=15
                    ! 则说明是 1.2Mb 的驱动器；如果 sectors=18，则说明是
                    ! 1.44Mb 软驱。因为是可引导的驱动器，所以肯定是 A 驱。
	mov	ax,#0x0208		! /dev/ps0 - 1.2Mb
	cmp	bx,#15       ! 判断每磁道扇区数是否=15
	je	root_defined  ! 如果等于，则 ax 中就是引导驱动器的设备号。
	mov	ax,#0x021c		! /dev/PS0 - 1.44Mb
	cmp	bx,#18
	je	root_defined    ! 如果都不一样，则死循环（死机）。
undef_root:
	jmp undef_root
root_defined:
	seg cs
	mov	root_dev,ax     ! 将检查过的设备号保存起来。


! after that (everyting loaded), we jump to
! the setup-routine loaded directly after
! the bootblock:
  ! 到此，所有程序都加载完毕，我们就跳转到被
  ! 加载在 bootsect 后面的 setup 程序去。
	jmpi	0,SETUPSEG   ! 跳转到 0x9020:0000(setup.s 程序的开始处)。

! This routine loads the system at address 0x10000, making sure
! no 64kB boundaries are crossed. We try to load it as fast as
! possible, loading whole tracks whenever we can.
!
! in:	es - starting address segment (normally 0x1000)
!
 ! 该子程序将系统模块加载到内存地址 0x10000 处，并确定没有跨越 64KB 的内存边界。我们试图尽快
 ! 地进行加载，只要可能，就每次加载整条磁道的数据。
 ! 输入：es – 开始内存地址段值（通常是 0x1000）
sread:	.word 1+SETUPLEN	! sectors read of current track
                         ! 当前磁道中已读的扇区数。开始时已经读进 1 扇区的引导扇区
                         ! bootsect 和 setup 程序所占的扇区数 SETUPLEN。
head:	.word 0			! current head   !当前磁头号
track:	.word 0			! current track   !当前磁道号。
              ! 测试输入的段值。必须位于内存地址 64KB 边界处，否则进入死循环。清 bx 寄存器，用于表示当前段内
              ! 存放数据的开始位置。
read_it:
	mov ax,es
	test ax,#0x0fff
die:	jne die			! es must be at 64kB boundary  ! es 值必须位于 64KB 地址边界!
	xor bx,bx		! bx is starting address within segment   ! bx 为段内偏移位置
rp_read:
   ! 判断是否已经读入全部数据。比较当前所读段是否就是系统数据末端所处的段(#ENDSEG)，如果不是就
   ! 跳转至下面 ok1_read 标号处继续读数据。否则退出子程序返回。
	mov ax,es
	cmp ax,#ENDSEG		! have we loaded all yet?
	jb ok1_read
	ret
ok1_read:
	seg cs
	mov ax,sectors   ! 取每磁道扇区数。
	sub ax,sread     ! 减去当前磁道已读扇区数。
	mov cx,ax        ! cx = ax = 当前磁道未读扇区数。
	shl cx,#9        ! cx = cx * 512 字节。
	add cx,bx        ! cx = cx + 段内当前偏移值(bx)
                     ! = 此次读操作后，段内共读入的字节数。
	jnc ok2_read     ! 若没有超过 64KB 字节，则跳转至 ok2_read
	je ok2_read      
	xor ax,ax        ! 若加上此次将读磁道上所有未读扇区时会超过 64KB，则计算
	sub ax,bx        ! 此时最多能读入的字节数(64KB – 段内读偏移位置)， 再转换
	shr ax,#9          ! 成需要读取的扇区数。
ok2_read:
	call read_track
	mov cx,ax      ! cx = 该次操作已读取的扇区数。
	add ax,sread   ! 当前磁道上已经读取的扇区数。
	seg cs
	cmp ax,sectors  ! 如果当前磁道上的还有扇区未读， 则跳转到 ok3_read 处。
	jne ok3_read
	mov ax,#1   ! 读该磁道的下一磁头面(1 号磁头)上的数据。如果已经完成，则去读下一磁道。
	sub ax,head    ! 判断当前磁头号。
	jne ok4_read    ! 如果是 0 磁头，则再去读 1 磁头面上的扇区数据。
	inc track      ! 否则去读下一磁道。
ok4_read:
	mov head,ax    ! 保存当前磁头号。
	xor ax,ax       ! 清当前磁道已读扇区数。
ok3_read:
	mov sread,ax   ! 保存当前磁道已读扇区数。
	shl cx,#9      ! 上次已读扇区数*512 字节
	add bx,cx      ! 调整当前段内数据开始位置。
	jnc rp_read   ! 若小于 64KB 边界值，则跳转到 rp_read(156 行)处，继续读数据。
                  ! 否则调整当前段，为读下一段数据作准备。
	mov ax,es    
	add ax,#0x1000  ! 将段基址调整为指向下一个 64KB 段内存
	mov es,ax
	xor bx,bx     ! 清段内数据开始偏移值。
	jmp rp_read   ! 跳转至 rp_read(156 行)处，继续读数据。

! 读当前磁道上指定开始扇区和需读扇区数的数据到 es:bx 开始处。参见第 67 行下对 BIOS 磁盘读中断
! int 0x13，ah=2 的说明。
! al – 需读扇区数；es:bx – 缓冲区开始位置。
read_track:
	push ax
	push bx
	push cx
	push dx
	mov dx,track   ! 取当前磁道号。
	mov cx,sread   ! 取当前磁道上已读扇区数。
	inc cx         ! cl = 开始读扇区。
	mov ch,dl       ! ch = 当前磁道号。
	mov dx,head     ! 取当前磁头号。
	mov dh,dl      ! dh = 磁头号。
	mov dl,#0     ! dl = 驱动器号(为 0 表示当前驱动器)。
	and dx,#0x0100   ! 磁头号不大于 1。
	mov ah,#2      ! ah = 2，读磁盘扇区功能号
	int 0x13
	jc bad_rt   ! 若出错，则跳转至 bad_rt。
	pop dx
	pop cx
	pop bx
	pop ax
	ret
	! 执行驱动器复位操作（磁盘中断功能号 0），再跳转到 read_track 处重试。
bad_rt:	mov ax,#0
	mov dx,#0
	int 0x13
	pop dx
	pop cx
	pop bx
	pop ax
	jmp read_track

/*
 * This procedure turns off the floppy drive motor, so
 * that we enter the kernel in a known state, and
 * don't have to worry about it later.
 */
 ! 这个子程序用于关闭软驱的马达，这样我们进入内核后它处于已知状态，以后也就无须担心它了。
kill_motor:
	push dx
	mov dx,#0x3f2  ! 软驱控制卡的驱动端口，只写。
	mov al,#0   ! A 驱动器，关闭 FDC，禁止 DMA 和中断请求，关闭马达。
	outb         ! 将 al 中的内容输出到 dx 指定的端口去。
	pop dx
	ret

sectors:
	.word 0   ! 存放当前启动软盘每磁道的扇区数。

msg1:
	.byte 13,10   ! 回车、换行的 ASCII 码。
	.ascii "Loading system ..."
	.byte 13,10,13,10  ! 共 24 个 ASCII 码字符。

.org 508   ! 表示下面语句从地址 508(0x1FC)开始，所以 root_dev
  ! 在启动扇区的第 508 开始的 2 个字节中。
root_dev:
	.word ROOT_DEV  ! 这里存放根文件系统所在的设备号(init/main.c 中会用)。
boot_flag:
	.word 0xAA55    ! 硬盘有效标识。

.text
endtext:
.data
enddata:
.bss
endbss:
