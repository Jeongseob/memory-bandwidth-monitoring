/*
 * Architectural shared resources (L3 and Memory bandwidth) info
 *
 * Author: Jeongseob Ahn (ahnjeong@umich.edu) 
 */
#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros
#include <linux/perf_event.h> // perf_event
#include <linux/hrtimer.h> 
#include <linux/ktime.h> 

#define AHN_DEBUG 1
#define TIMER_INTERVAL	1000000000	// 1s
#define DDR3_1600_MAX_BANDWIDTH	12800*1024*1024	// B/s

struct pcpu_shared_resources_info {
	
	int bw_reserve;
	int bw_limit;

	int cache_reserve;
	int cache_limit;

	struct perf_event* perf_l3c_miss_event;
	int l3c_miss_sample_period; 

	u64 credit;
	u64 credit_per_period;

	struct task_struct* throttled_task;
	bool throttled;
};

struct archmon_info {

	struct pcpu_shared_resources_info* __percpu pcpu_resources_info;
	struct hrtimer period_timer;
	ktime_t	period;

	int total_credit;
};

static struct archmon_info g_archmon_info;

/*
 *	L3 cache miss overflow callback
 */
static void perf_l3c_miss_overflow(struct perf_event* event, struct perf_sample_data* data, struct pt_regs* regs)
{
	struct pcpu_shared_resources_info* resource_info = this_cpu_ptr(g_archmon_info.pcpu_resources_info);
	u64 used_credit = local64_read(&event->count);

	if ( used_credit < resource_info->credit ) {
		return;
	}
	
	/* End up its credit! */
	resource_info->credit = 0;
	
	/* need to throttle process running on the cpu */
	if ( resource_info->throttled == true ) {
#ifdef AHN_DEBUG
		printk(KERN_ERR "[%d] a process %d is still throttled down!\n", smp_processor_id(), current->pid);
#endif
		return;
	}

	resource_info->throttled_task = current;
	resource_info->throttled = true;
	kill_pid(task_pid(current), SIGSTOP, 1);
#ifdef AHN_DEBUG
	printk("[%d] a process %d needs to be throttled down \n", smp_processor_id(), current->pid);
#endif
}

/*
 *	Create a performance counter (reference 'arch/x86/kvm/pmu.c')
 */
static struct perf_event* reprogram_counter(int cpu, u32 type, unsigned config, 
		bool exclude_user, bool exclude_kernel, int period,  perf_overflow_handler_t callback)
{
	struct perf_event *event = NULL;
	struct perf_event_attr attr = {
		.type = type,
		.config	= config,
		.size = sizeof(struct perf_event_attr),
		.exclude_user = exclude_user,
		.exclude_kernel = exclude_kernel,
		.pinned = true,
		.sample_period = period,
	};

	event = perf_event_create_kernel_counter(&attr, cpu, NULL, callback, NULL);

	if ( IS_ERR(event) ) {
		printk(KERN_ERR "pmu event creation failed %ld\n", PTR_ERR(event));
		return NULL;
	}
	
	return event;
}

/*
 * Release a performance counter
 */
static void stop_counter(struct perf_event* event)
{
	if ( event ) {
		perf_event_release_kernel(event);
	}
}


/*
 * Do something
 */
static void do_archmon_period_timer(void)
{
	int cpu_id = 0;
#ifdef AHN_DEBUG
	printk("[%d] timer invoked!\n", smp_processor_id());
#endif

	for_each_online_cpu(cpu_id) {
		struct pcpu_shared_resources_info* resource_info = per_cpu_ptr(g_archmon_info.pcpu_resources_info, cpu_id);
		struct perf_event* event = resource_info->perf_l3c_miss_event;
		
		/* Stop the perf event */
		event->pmu->stop(event, PERF_EF_UPDATE);

		/* Reset the credit */
		resource_info->credit = resource_info->credit_per_period;

		/* If there are throttled threads, then need to unlock */
		if ( resource_info->throttled ) {
			kill_pid(task_pid(resource_info->throttled_task), SIGCONT, 1);
#ifdef AHN_DEBUG
			printk("[%d] a process %d needs to be throttled up \n", cpu_id, resource_info->throttled_task->pid);
#endif
			resource_info->throttled = false;
		}
	
		/* 
		 * Reconfiguring the period to reflect new credit on the sampling period 
		 * and restart the perf event
		 */
		local64_set(&event->hw.period_left, resource_info->credit);
		event->pmu->start(event, PERF_EF_RELOAD);
	}

}

/*
 *	Periodic timer
 */
enum hrtimer_restart archmon_period_timer(struct hrtimer* timer)
{
	int overrun;
	ktime_t now;

	for (;;) {
		now = hrtimer_cb_get_time(timer);
		overrun = hrtimer_forward(timer, now, g_archmon_info.period);
		
		if (!overrun)
			break;
		
		do_archmon_period_timer();
	}

	return HRTIMER_RESTART;
}


void init_archmon_timer(struct hrtimer* timer, void* timer_callback)
{
	ktime_t interval = ktime_set(0, TIMER_INTERVAL);
	hrtimer_init(timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer->function = timer_callback;	
	hrtimer_start(timer, interval, HRTIMER_MODE_REL);
}

int init_archmon_percpu(struct pcpu_shared_resources_info* resource_info, int cpu_id)
{
	int credit_per_cpu = 0;

	//g_archmon_info.total_credit = div64_u64( (u64)DDR3_1600_MAX_BANDWIDTH, 64 * 1000) ;	// maximum # of l3c misses per 1ms
	g_archmon_info.total_credit = div64_u64( (u64)DDR3_1600_MAX_BANDWIDTH, 64 * 1) ;	// maximum # of l3c misses per 1s
	credit_per_cpu = g_archmon_info.total_credit / num_online_cpus();

	resource_info->l3c_miss_sample_period = credit_per_cpu;
	resource_info->credit = credit_per_cpu;
	resource_info->credit_per_period = credit_per_cpu;
	resource_info->throttled_task = NULL;
	resource_info->throttled = false;
		
	resource_info->perf_l3c_miss_event = reprogram_counter(cpu_id, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES, false, true, resource_info->l3c_miss_sample_period, (perf_overflow_handler_t)perf_l3c_miss_overflow);
	
	if ( NULL == resource_info->perf_l3c_miss_event ) {
		printk(KERN_ERR "[%d] cannot initialize PMUs\n", cpu_id);
		return -1;
	}

	return 0;
}

/*
 * Entry point
 */ 
int init_module(void)
{
	int cpu_id = 0;

	g_archmon_info.pcpu_resources_info = alloc_percpu(struct pcpu_shared_resources_info);
	g_archmon_info.period = ktime_set(0, TIMER_INTERVAL);

	for_each_online_cpu(cpu_id) {
		struct pcpu_shared_resources_info* resource_info = per_cpu_ptr(g_archmon_info.pcpu_resources_info, cpu_id);
		if ( init_archmon_percpu(resource_info, cpu_id) == -1 ) {
			return -1;
		}
	}
	
	init_archmon_timer(&g_archmon_info.period_timer, archmon_period_timer);
	
	printk(KERN_INFO "Archmon is loaded\n");

	return 0;    // Non-zero return means that the module couldn't be loaded.
}

void cleanup_module(void)
{
	int i = 0;

	for_each_online_cpu(i) {

		struct pcpu_shared_resources_info* resource_info = per_cpu_ptr(g_archmon_info.pcpu_resources_info, i);
		stop_counter(resource_info->perf_l3c_miss_event);
	}

	hrtimer_cancel(&g_archmon_info.period_timer);

	printk(KERN_INFO "Archmon is unloaded\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jeongseob Ahn");
MODULE_DESCRIPTION("Architectural shared resources monitoring module");

