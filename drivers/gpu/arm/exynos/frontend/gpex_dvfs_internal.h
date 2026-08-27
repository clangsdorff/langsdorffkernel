/* SPDX-License-Identifier: GPL-2.0 */

/*
 * (C) COPYRIGHT 2021 Samsung Electronics Inc. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 */

#ifndef _GPEX_DVFS_INTERNAL_H_
#define _GPEX_DVFS_INTERNAL_H_

#include <linux/atomic.h>
#include <linux/ktime.h>

#include <gpex_clock.h>

#define DVFS_ASSERT(x)                                                                             \
	do {                                                                                       \
		if (x)                                                                             \
			break;                                                                     \
		printk(KERN_EMERG "### ASSERTION FAILED %s: %s: %d: %s\n", __FILE__, __func__,     \
		       __LINE__, #x);                                                              \
		dump_stack();                                                                      \
	} while (0)

#define GPEX_DVFS_MIN_POLLING_SPEED 4
#define GPEX_DVFS_MAX_POLLING_SPEED 1000

/* A fragment job this long alone eats half of a 60Hz frame budget, which is
 * enough evidence that the frame will not land on time.
 */
#define GPEX_DVFS_FRAME_BOOST_JOB_US 8000
/* Kept short enough that the clock is back at min before the GPU would have
 * power gated anyway, long enough to bridge the gap between two frames.
 */
#define GPEX_DVFS_FRAME_BOOST_RELEASE_MS 150
#define GPEX_DVFS_FRAME_BOOST_MAX_JOB_US 33000
#define GPEX_DVFS_FRAME_BOOST_MAX_RELEASE_MS 1000

typedef struct _gpu_dvfs_env_data {
	int utilization;
} gpu_dvfs_env_data;

typedef struct _gpu_dvfs_governor_info {
	int id;
	char *name;
	void *governor;
	int table_size;
	int start_clk;
} gpu_dvfs_governor_info;

typedef struct _dvfs_clock_info {
	unsigned int clock;
	unsigned int voltage;
	int min_threshold;
	int max_threshold;
	int down_staycount;
} dvfs_clock_info;

struct dvfs_info {
	struct kbase_device *kbdev;
	int step;
	dvfs_clock_info *table;
	int table_size;
	bool timer_active;
	bool status;
	int governor_type;
	int gpu_dvfs_config_clock; // starting clock when enabling dvfs using sysfs
	int polling_speed;
	spinlock_t spinlock;
	gpu_dvfs_env_data env_data;

	struct mutex handler_lock;

	struct delayed_work dvfs_work;
	struct workqueue_struct *dvfs_wq;
	int down_requirement;

	/* For the interactive governor */
	struct {
		int highspeed_clock;
		int highspeed_load;
		int highspeed_delay;
		int delay_count;
	} interactive;

	/* Deadline driven boost. Written from the job completion path (atomics
	 * only, called under hwaccess_lock), consumed by the dvfs work item.
	 */
	struct {
		int clock; /* 0 disables the boost */
		int job_us;
		int release_ms;
		atomic64_t last_late_job;
		atomic_t late_job_cnt;
	} frame_boost;
};

int gpex_dvfs_sysfs_init(struct dvfs_info *_dvfs);
int gpex_dvfs_external_init(struct dvfs_info *_dvfs);
int gpu_dvfs_governor_init(struct dvfs_info *_dvfs);

#endif /* _GPEX_DVFS_INTERNAL_H_ */
