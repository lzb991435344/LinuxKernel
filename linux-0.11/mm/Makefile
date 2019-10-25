CC	=gcc
CFLAGS	=-O -Wall -fstrength-reduce -fcombine-regs -fomit-frame-pointer \
	-finline-functions -nostdinc -I../include
#C编译程序选项，-wall显示所有警告信息；-o为优化选项，优化代码长度和执行时间
#-fstrength-reduce 优化循环执行代码，排除重复变量
#-fomit-frame-pointer 省略保存不必要的框架指针	
#-fcombine-regs 合并寄存器，，减少寄存器类的使用
#-finline-functions 将所有的简单短小的代码嵌入调用程序中
#-nostdinc -I../include 不使用默认路径中的包含文件，使用指定目录中的(../include)
AS	=gas  #GNU的汇编程序
AR	=gar  #GNU的二进制文件处理程序，用于创建，修改以及归档文件中的抽取文件
LD	=gld  #GNU的连接程序
CPP	=gcc -E -nostdinc -I../include
          #c前处理选项，-E只运行c前处理，对所有指定的C程序进行预处理并将结果输出到
		  #标准输出设备或指定的输出文件
		  
.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<
.s.o:
	$(AS) -o $*.o $<
.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<

OBJS	= memory.o page.o

all: mm.o

#在有了先决条件OBJS的情况下使用下面的命令连接成mm.o文件
mm.o: $(OBJS)  
               
	$(LD) -r -o mm.o $(OBJS)
#用于清理工作，去除所有编译生成的连接文件 
clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done

#目标或规则用于检查各文件之间的依赖关系
#字符串编辑程序sed对MakeFile文件进行处理输出为删除MakeFile
#对mm下的每一个C文件执行Gcc预处理操作
#-M 告诉预处理程序输出每个目标文件相关性规则，使这些规则符合make语法
#对于每一个源文件，预处理程序输出一个make规则，结果形式是相应源程序文件的目标
#文件名加上其依赖关系--该文件包含的所有头文件列表。把预处理结果都添加到临时文件tmp_make中
#然后将临时文件复制到新的Makefile中
dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in *.c;do $(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile

### Dependencies:
memory.o : memory.c ../include/signal.h ../include/sys/types.h \
  ../include/asm/system.h ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/linux/mm.h ../include/linux/kernel.h 
