/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>

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
static inline _syscall0(int,fork)
static inline _syscall0(int,pause)
static inline _syscall1(int,setup,void *,BIOS)
static inline _syscall0(int,sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 全局变量 都是在boot阶段读入的，然后放到宏定义中
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

static void time_init(void)//这一段代码起到从CMOS中读取时间信息的作用
{
	struct tm time;

	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);//把CMOS中读出来的数据进行转换
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);//存在startup_time这个全局变量中，并且之后会被JIFFIES使用
	
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

struct drive_info { char dummy[32]; } drive_info;

//main函数 linux引导成功后就从这里开始运行
void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
//前面这里做的所有事情都是在对内存进行拷贝
 	ROOT_DEV = ORIG_ROOT_DEV;//设置操作系统的根文件
 	drive_info = DRIVE_INFO;//设置操作系统驱动参数
	 //解析setup.s代码后获取系统内存参数
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	//取整4k的内存大小
	memory_end &= 0xfffff000;
	if (memory_end > 16*1024*1024)//控制操作系统的最大内存为16M
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;//设置高速缓冲区的大小，跟块设备有关，跟设备交互的时候，充当缓冲区，写入到块设备中的数据先放在缓冲区里，只有执行sync时才真正写入；这也是为什么要区分块设备驱动和字符设备驱动；块设备写入需要缓冲区，字符设备不需要是直接写入的
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;
	main_memory_start = buffer_memory_end;
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
//内存控制器初始化
	mem_init(main_memory_start,memory_end);
	//异常函数初始化
	trap_init();
	//块设备驱动初始化
	blk_dev_init();
	//字符型设备出动初始化
	chr_dev_init();
	//控制台设备初始化
	tty_init();
	//加载定时器驱动
	time_init();
	//进程间调度初始化
	sched_init();
	//缓冲区初始化
	buffer_init(buffer_memory_end);
	//硬盘初始化
	hd_init();
	//软盘初始化
	floppy_init();
	sti();
	//从内核态切换到用户态，上面的初始化都是在内核态运行的
	//内核态无法被抢占，不能在进程间进行切换，运行不会被干扰
	move_to_user_mode();
	if (!fork()) {	//创建0号进程 fork函数就是用来创建进程的函数	/* we count on this going ok */
		//0号进程是所有进程的父进程
		init();
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
//0号进程永远不会结束，他会在没有其他进程调用的时候调用，只会执行for(;;) pause();
	for(;;) pause();
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

void init(void)
{
	int pid,i;
	//设置了驱动信息
	setup((void *) &drive_info);
	//打开标准输入控制台 句柄为0
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0);//打开标准输入控制台 这里是复制句柄的意思
	(void) dup(0);//打开标准错误控制台
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);
	if (!(pid=fork())) {//这里创建1号进程
		close(0);//关闭了0号进程的标准输入输出
		if (open("/etc/rc",O_RDONLY,0))//如果1号进程创建成功打开/etc/rc这里面保存的大部分是系统配置文件 开机的时候要什么提示信息全部写在这个里面
			_exit(1);
		execve("/bin/sh",argv_rc,envp_rc);//运行shell程序
		_exit(2);
	}
	if (pid>0)//如果这个不是0号进程
		while (pid != wait(&i))//就等待父进程退出
			/* nothing */;
	while (1) {
		if ((pid=fork())<0) {//如果创建失败
			printf("Fork failed in init\r\n");
			continue;
		}
		//如果创建成功
		if (!pid) {//这个分支里面是进行进程的再一次创建
			close(0);close(1);close(2);//关闭上面那几个输入输出错误的句柄
			setsid();//重新设置id
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);//重新打开
			_exit(execve("/bin/sh",argv,envp));//这里不是上面的argv_rc和envp_rc了是因为怕上面那种创建失败，换了一种环境变量来创建，过程和上面是一样的其实
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();
	}
	_exit(0);	/* NOTE! _exit, not exit() */
}
