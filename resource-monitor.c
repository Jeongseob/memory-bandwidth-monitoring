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

struct pcpu_shared_resources_info {
	
	int bw_reserve;
	int bw_limit;

	int cache_reserve;
	int cache_limit;

	struct perf_event* perf_l3c_miss_event;

};

static struct pcpu_shared_resources_info* __percpu pcpu_resources_info;

/*
 *	L3 cache miss overflow callback
 */
static void perf_l3c_miss_overflow(struct perf_event* event, struct perf_sample_data* data, struct pt_regs* regs)
{
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
		printk("pmu event creation failed %ld\n", PTR_ERR(event));
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
 *
 */

/*
 * Entry point
 */ 
int init_module(void)
{
	int cpu_id = 0;

	printk(KERN_INFO "Init architectural shared resources monitoring module\n");
	pcpu_resources_info = alloc_percpu(struct pcpu_shared_resources_info);

	for_each_online_cpu(cpu_id) {

		struct pcpu_shared_resources_info* resource_info = per_cpu_ptr(pcpu_resources_info, cpu_id);

		resource_info->perf_l3c_miss_event = reprogram_counter(cpu_id, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES, false, true, 1000, (perf_overflow_handler_t)perf_l3c_miss_overflow);

		/*
		if ( NULL == resource_info->perf_l3c_miss_event ) {
			printk("[%d] cannot initialize PMUs\n", cpu_id);
			break;
		}
		*/
		
		printk("[%d] core\n", cpu_id);
	}

	return 0;    // Non-zero return means that the module couldn't be loaded.
}

void cleanup_module(void)
{
	int i = 0;

	for_each_online_cpu(i) {

		struct pcpu_shared_resources_info* resource_info = per_cpu_ptr(pcpu_resources_info, i);
		stop_counter(resource_info->perf_l3c_miss_event);
	}
	printk(KERN_INFO "Cleaning architectural shared resources monitorting module\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jeongseob Ahn");
MODULE_DESCRIPTION("Architectural shared resources monitoring module");
