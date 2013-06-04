/* arch/arm/mach-rk30/rk_pm_tests.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


/*************************************************************************/
/*	  COPYRIGHT (C)  ROCK-CHIPS FUZHOU . ALL RIGHTS RESERVED.*/
/*************************************************************************
FILE		:	  rk_pm_tests.c
DESC		:	  Power management in dynning state
AUTHOR		:	  chenxing
DATE		:	  2012-7-2
NOTES		:
$LOG: GPIO.C,V $
REVISION 0.01
#include <linux/clk.h>
#include <linux/kobject.h>
 ***************************************************************************/
#include <linux/string.h>
#include <linux/resume-trace.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/freezer.h>

#include <linux/dma-mapping.h>
#include <linux/regulator/machine.h>
#include <plat/dma-pl330.h>
#include <linux/mfd/wm831x/core.h>
#include <linux/sysfs.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/clk.h>

#include <linux/io.h>
#include <linux/gpio.h>

//#include <mach/ddr.h>
#if 0
#include "rk_pm_tests.h"

#include "clk_rate.h"
#include "clk_volt.h"
#include "maxfreq.h"
#include "freq_limit.h"
#include "cpu_usage.h"
#include "rk_suspend_test.h"
#include "clk_auto_volt.h"
#include "delayline.h"
#endif

#define ft_printk(fmt, arg...) \
	printk(KERN_EMERG fmt, ##arg)
 //KERN_DEBUG
#define ft_printk_dbg(fmt, arg...) \
	printk(KERN_EMERG fmt, ##arg)

 //KERN_DEBUG
#define ft_printk_info(fmt, arg...) \
	printk(KERN_EMERG fmt, ##arg)



static unsigned long arm_setup2_rate=1608*1000*1000;

//0-15 :test setup1
#define CPU_TST_L1 (1<<0)
#define CPU_TST_L2 (1<<1)

//16-31 :test setup2
#define CPU_TST_L1_STP2 (1<<16)
#define CPU_TST_L2_STP2 (1<<17)

static DEFINE_PER_CPU(int, cpu_tst_flags)=(CPU_TST_L1|CPU_TST_L2|CPU_TST_L1_STP2|CPU_TST_L2_STP2);

static struct clk *arm_clk;


#define FT_CLIENT_READY_PIN    RK30_PIN3_PB3
#define FT_CLIENT_IDLE_PIN     RK30_PIN0_PA3


static DEFINE_PER_CPU(wait_queue_head_t, wait_rate);

/************************************l1 tst***************************************/
void test_cpus_l1(u32 *data);
void test_cpus_l0(u32 *data);

static struct semaphore sem = __SEMAPHORE_INITIALIZER(sem, 0);
 
void ft_cpu_l1_test(u32 cnt)
{
	u32 cpu = smp_processor_id();
	int test_array[100];
	int i;

	ft_printk_dbg("l1_test cpu=%d start\n",cpu);	

	for(i=0;i<cnt;i++)
	{
		test_cpus_l1(&test_array[0]);
		barrier();
		
	}
	for(i=0;i<cnt;i++)
	{
		test_cpus_l0(&test_array[0]);
		barrier();
		
	}
		
	ft_printk_dbg("l1_test cpu=%d end\n",cpu);	

}


/************************************l2 tst***************************************/
static struct semaphore sem_step2 = __SEMAPHORE_INITIALIZER(sem_step2, 0);

int test_cpus_l2(char *data_s,char *data_e,u32 data);

#define l1_DCACHE_SIZE (32*1024)
#define l1_DCACHE_SIZE_M (l1_DCACHE_SIZE*32)

#define BUF_SIZE (l1_DCACHE_SIZE_M*4)// tst buf size

static char memtest_buf0[BUF_SIZE] __attribute__((aligned(4096)));
#if (NR_CPUS>=2)
static char memtest_buf1[BUF_SIZE] __attribute__((aligned(4096)));
#if (NR_CPUS>=4)
static char memtest_buf2[BUF_SIZE] __attribute__((aligned(4096)));
static char memtest_buf3[BUF_SIZE] __attribute__((aligned(4096)));
#if (NR_CPUS>=8)
static char memtest_buf4[BUF_SIZE] __attribute__((aligned(4096)));
static char memtest_buf5[BUF_SIZE] __attribute__((aligned(4096)));
static char memtest_buf6[BUF_SIZE] __attribute__((aligned(4096)));
static char memtest_buf7[BUF_SIZE] __attribute__((aligned(4096)));
#endif
#endif
#endif

static char *l2_test_buf[NR_CPUS]=
{
	memtest_buf0,
#if (NR_CPUS>=2)
	memtest_buf1,
#if (NR_CPUS>=4)
	memtest_buf2,
	memtest_buf3,
#if (NR_CPUS>=8)
	memtest_buf4,
	memtest_buf5,
	memtest_buf6,
	memtest_buf7,
#endif
#endif
#endif

};

int ft_test_cpus_l2(char *data_s,u32 buf_size,u32 cnt)
{
	int i,j;
	int ret=0;
	int test_size=l1_DCACHE_SIZE;
	for(j=0;j<cnt;j++)	
	{	
		for(i=0;i<buf_size;)
		{
			
			ret=test_cpus_l2(data_s+i,data_s+i+test_size,0xffffffff);
			if(ret)
				return -1;
			ret=test_cpus_l2(data_s+i,data_s+i+test_size,0);
			if(ret)
				return -1;
			ret=test_cpus_l2(data_s+i,data_s+i+test_size,0xaaaaaaaa);
			if(ret)
				return -1;
			
			i+=test_size;	

			if((i+test_size)>buf_size)
				break;	
			
		}
	}

	return 0;
}

/*************************************816 tst case**************************************/

void ft_cpu_test_step1(void)
{
	u32 temp=-1;
	u32 cpu = smp_processor_id();

	//arm rate init
	ft_cpu_l1_test(10);
	per_cpu(cpu_tst_flags, cpu)&=~CPU_TST_L1;

	temp=ft_test_cpus_l2(l2_test_buf[cpu],sizeof(char)*BUF_SIZE,18);

	if(!temp)
	per_cpu(cpu_tst_flags, cpu)&=~CPU_TST_L2;

	up(&sem);

}
/*************************************hight rate tst case**************************************/

void ft_cpu_test_step2(void)
{
	u32 temp=-1;
	u32 cpu = smp_processor_id();

	//arm rate init
	ft_cpu_l1_test(10*2);
	per_cpu(cpu_tst_flags, cpu)&=~CPU_TST_L1_STP2;

	temp=ft_test_cpus_l2(l2_test_buf[cpu],sizeof(char)*BUF_SIZE,20*2);

	if(!temp)
	per_cpu(cpu_tst_flags, cpu)&=~CPU_TST_L2_STP2;

	up(&sem_step2);

}

// tst thread callback for per cpu
static int ft_cpu_test(void *data)
{	
	u32 cpu = smp_processor_id();
	
	ft_cpu_test_step1();

	//arm hight rate
	wait_event_freezable(per_cpu(wait_rate, cpu),  (clk_get_rate(arm_clk)==arm_setup2_rate)||kthread_should_stop());
	
	ft_cpu_test_step2();

	return 0;
}


static int __init rk_ft_tests_init(void)
{
	int cpu, ret = 0;
	struct sched_param param = { .sched_priority = 0 };	

	arm_clk=clk_get(NULL, "cpu");
	if (IS_ERR(arm_clk))
		arm_setup2_rate=0;
	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		init_waitqueue_head(&per_cpu(wait_rate, cpu));
	}

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		struct task_struct *task = kthread_create(ft_cpu_test, NULL, "ft_cpu_test%d", cpu);
		sched_setscheduler_nocheck(task, SCHED_RR, &param);
		get_task_struct(task);
		kthread_bind(task, cpu);
		wake_up_process(task);
	}

	return ret;
}
core_initcall(rk_ft_tests_init);

static int rk_ft_tests_over(void)
{
		int cpu, ret = 0;


		for (cpu = 0; cpu < NR_CPUS; cpu++)
			down(&sem);
		
		ft_printk_info("arm=%lu,ddr=%lu\n",clk_get_rate(arm_clk),clk_get_rate(clk_get(NULL, "ddr")));
	
		ret=per_cpu(cpu_tst_flags, 0)|per_cpu(cpu_tst_flags, 1)|per_cpu(cpu_tst_flags, 2)|per_cpu(cpu_tst_flags, 3);
		ret&=0xffff;// test setup1
		
		ft_printk_dbg("per cpu setup1=%x\n",ret);
		
		if(ret)
			ft_printk("#R01KERNEL*\n");
		else
			ft_printk("#R00KERNEL*\n");
	
	
		if(arm_setup2_rate)
		{		
			ft_printk("#SHSPEED*\n");

			#if 0  
			// send msg to ctr board to up the volt
			gpio_direction_output(FT_CLIENT_READY_PIN, GPIO_HIGH);
			gpio_direction_input(FT_CLIENT_IDLE_PIN);
			
			// waiting for volt upping ok 
			while( 0 == gpio_get_value(FT_CLIENT_IDLE_PIN));

			gpio_set_value(FT_CLIENT_READY_PIN, GPIO_LOW);    
			#endif
		
		
			clk_set_rate(arm_clk,arm_setup2_rate);
	
			for (cpu = 0; cpu < NR_CPUS; cpu++)
				wake_up(&per_cpu(wait_rate, cpu));
	
	
			for (cpu = 0; cpu < NR_CPUS; cpu++)
				down(&sem_step2);
				
			ft_printk_info("arm=%lu,ddr=%lu\n",clk_get_rate(arm_clk),clk_get_rate(clk_get(NULL, "ddr")));
			
			ret=per_cpu(cpu_tst_flags, 0)|per_cpu(cpu_tst_flags, 1)|per_cpu(cpu_tst_flags, 2)|per_cpu(cpu_tst_flags, 3);
			ret&=(0xffff<<16);// test setup1
			
			ft_printk_dbg("per cpu setup2=%x\n",ret);
			
			if(ret)
				ft_printk("#R01HSPEED*\n");
			else
				ft_printk("#R00HSPEED*\n");
			
		}
		
	
	
	ft_printk("#END1F*\n");

	while(1);

	return ret;
}


late_initcall_sync(rk_ft_tests_over);




