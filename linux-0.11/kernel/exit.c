/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */
//进程销毁
//1. 释放进程的代码段和数据段占用的内存
//2. 关闭进程打开的所有文件，对当前目录和i节点进行同步（文件操作）
//3. 如果当前要销毁的进程有子进程，就让1号进程作为新的父进程
//4. 如果当前进程是一个会话头进程，则会终止会话中的所有进程
//5. 改变当前进程的运行状态，变成TASK_ZOMBIE状态，并且向其父进程发送SIGCHLD信号，说明自己要死了
//
//1. 父进程在运行子进程时一般都会运行wait waitpid这两个函数，用来父进程等待子进程终止
//2. 当父进程收到SIGCHLD信号时，父进程会终止僵死状态的子进程
//3. 父进程会把子进程的运行时间累加到自己的运行时间上
//4. 把对应子进程的进程描述结构体进行释放，置空数组空槽


#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_pause(void);
int sys_close(int fd);
//清空task[]中对应task
//释放对应内存页
void release(struct task_struct * p)
{
	int i;

	if (!p)
		return;
	for (i=1 ; i<NR_TASKS ; i++)//在task[]中进行遍历
		if (task[i]==p) {
			task[i]=NULL;
			free_page((long)p);//释放内存页
			schedule();//重新进行进程调度
			return;
		}
	panic("trying to release non-existent task");
}
//给指定的p进程发送信号
static inline int send_sig(long sig,struct task_struct * p,int priv)
{
	if (!p || sig<1 || sig>32)
		return -EINVAL;
	if (priv || (current->euid==p->euid) || suser())
		p->signal |= (1<<(sig-1));
	else
		return -EPERM;
	return 0;
}
//关闭session
static void kill_session(void)
{
	struct task_struct **p = NR_TASKS + task;
	
	while (--p > &FIRST_TASK) {//从最后一个开始扫描（不包括0进程）
		if (*p && (*p)->session == current->session)
			(*p)->signal |= 1<<(SIGHUP-1);
	}
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 */
// 系统调用 向任何进程 发送任何信号（类比shell中的kill命令也是发送信号的意思）
int sys_kill(int pid,int sig)
{
	struct task_struct **p = NR_TASKS + task;//指向最后
	int err, retval = 0;

	if (!pid) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pgrp == current->pid) //如果等于进程组号
			if (err=send_sig(sig,*p,1))
				retval = err;
	} else if (pid>0) while (--p > &FIRST_TASK) {//pid>0给对应进程发送信号
		if (*p && (*p)->pid == pid) 
			if (err=send_sig(sig,*p,0))
				retval = err;
	} else if (pid == -1) while (--p > &FIRST_TASK)//pid=-1给任何进程发送
		if (err = send_sig(sig,*p,0))
			retval = err;
	else while (--p > &FIRST_TASK)//pid<-1 给进程组发送信息
		if (*p && (*p)->pgrp == -pid)
			if (err = send_sig(sig,*p,0))
				retval = err;
	return retval;
}
//告诉父进程要死了
static void tell_father(int pid)
{
	int i;

	if (pid)
		for (i=0;i<NR_TASKS;i++) {
			if (!task[i])
				continue;
			if (task[i]->pid != pid)
				continue;
			task[i]->signal |= (1<<(SIGCHLD-1));//找到父亲发送SIGCHLD信号
			return;
		}
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
	printk("BAD BAD - no father found\n\r");
	release(current);//释放子进程
}
//命名规则
//以do开头 以syscall开头基本都是终端调用函数
int do_exit(long code)
{
	int i;
	//释放内存页
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	//current->pid就是当前需要关闭的进程
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->father == current->pid) {//如果当前进程是某个进程的父进程
			task[i]->father = 1;//就让1号进程作为新的父进程
			if (task[i]->state == TASK_ZOMBIE)//如果是僵死状态
				/* assumption task[1] is always init */
				(void) send_sig(SIGCHLD, task[1], 1);//给父进程发送SIGCHLD
		}
	for (i=0 ; i<NR_OPEN ; i++)//每个进程能打开的最大文件数NR_OPEN=20
		if (current->filp[i])
			sys_close(i);//关闭文件
	iput(current->pwd);
	current->pwd=NULL;
	iput(current->root);
	current->root=NULL;
	iput(current->executable);
	current->executable=NULL;
	if (current->leader && current->tty >= 0)
		tty_table[current->tty].pgrp = 0;//清空终端
	if (last_task_used_math == current)
		last_task_used_math = NULL;//清空协处理器
	if (current->leader)
		kill_session();//清空session
	current->state = TASK_ZOMBIE;//设为僵死状态
	current->exit_code = code;
	tell_father(current->father);
	schedule();
	return (-1);	/* just to suppress warnings */
}
// 定义系统调用
int sys_exit(int error_code)
{
	return do_exit((error_code&0xff)<<8);
}

int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
	int flag, code;
	struct task_struct ** p;

	verify_area(stat_addr,4);//验证区域是否可以用
repeat:
	flag=0;
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p || *p == current)
			continue;
		if ((*p)->father != current->pid)
			continue;
		if (pid>0) {
			if ((*p)->pid != pid)
				continue;
		} else if (!pid) {
			if ((*p)->pgrp != current->pgrp)
				continue;
		} else if (pid != -1) {
			if ((*p)->pgrp != -pid)
				continue;
		}
		switch ((*p)->state) {
			case TASK_STOPPED:
				if (!(options & WUNTRACED))
					continue;
				put_fs_long(0x7f,stat_addr);
				return (*p)->pid;
			case TASK_ZOMBIE:
				current->cutime += (*p)->utime;
				current->cstime += (*p)->stime;
				flag = (*p)->pid;
				code = (*p)->exit_code;
				release(*p);
				put_fs_long(code,stat_addr);
				return flag;
			default:
				flag=1;
				continue;
		}
	}
	if (flag) {
		if (options & WNOHANG)
			return 0;
		current->state=TASK_INTERRUPTIBLE;
		schedule();
		if (!(current->signal &= ~(1<<(SIGCHLD-1))))
			goto repeat;
		else
			return -EINTR;
	}
	return -ECHILD;
}


