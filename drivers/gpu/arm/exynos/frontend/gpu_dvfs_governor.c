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

#include <gpex_dvfs.h>
#include <gpex_clock.h>
#include <gpex_pm.h>
#include <gpex_utils.h>
#include <gpex_clboost.h>
#include <gpex_tsg.h>

#include "gpex_dvfs_internal.h"
#include "gpu_dvfs_governor.h"

static struct dvfs_info *dvfs;

/* TODO: This should be moved to DVFS module */
int gpex_dvfs_set_clock_callback()
{
	unsigned long flags;
	int level = 0;

	int cur_clock = 0;
	cur_clock = gpex_clock_get_cur_clock();

	level = gpex_clock_get_table_idx(cur_clock);
	if (level >= 0) {
		spin_lock_irqsave(&dvfs->spinlock, flags);

		if (dvfs->step != level)
			dvfs->down_requirement = dvfs->table[level].down_staycount;

		if (dvfs->step < level)
			dvfs->interactive.delay_count = 0;

		dvfs->step = level;
		//gpex_dvfs_set_step(level);

		spin_unlock_irqrestore(&dvfs->spinlock, flags);
	} else {
		GPU_LOG(MALI_EXYNOS_ERROR, "%s: invalid dvfs level returned %d gpu power %d\n",
			__func__, cur_clock, gpex_pm_get_status(false));
		return -1;
	}
	return 0;
}

typedef int (*GET_NEXT_LEVEL)(int utilization);
static GET_NEXT_LEVEL gpu_dvfs_get_next_level;

static int gpu_dvfs_governor_default(int utilization);
static int gpu_dvfs_governor_interactive(int utilization);
static int gpu_dvfs_governor_joint(int utilization);
static int gpu_dvfs_governor_static(int utilization);
static int gpu_dvfs_governor_booster(int utilization);
static int gpu_dvfs_governor_dynamic(int utilization);

static gpu_dvfs_governor_info governor_info[G3D_MAX_GOVERNOR_NUM] = {
	{
		G3D_DVFS_GOVERNOR_DEFAULT,
		"Default",
		gpu_dvfs_governor_default,
	},
	{
		G3D_DVFS_GOVERNOR_INTERACTIVE,
		"Interactive",
		gpu_dvfs_governor_interactive,
	},
	{
		G3D_DVFS_GOVERNOR_JOINT,
		"Joint",
		gpu_dvfs_governor_joint,
	},
	{
		G3D_DVFS_GOVERNOR_STATIC,
		"Static",
		gpu_dvfs_governor_static,
	},
	{
		G3D_DVFS_GOVERNOR_BOOSTER,
		"Booster",
		gpu_dvfs_governor_booster,
	},
	{
		G3D_DVFS_GOVERNOR_DYNAMIC,
		"Dynamic",
		gpu_dvfs_governor_dynamic,
	},
};

void gpu_dvfs_update_start_clk(int governor_type, int clk)
{
	governor_info[governor_type].start_clk = clk;
}

void *gpu_dvfs_get_governor_info(void)
{
	return &governor_info;
}

static int gpu_dvfs_governor_default(int utilization)
{
	if ((dvfs->step > gpex_clock_get_table_idx(gpex_clock_get_max_clock())) &&
	    (utilization > dvfs->table[dvfs->step].max_threshold)) {
		dvfs->step--;
		if (dvfs->table[dvfs->step].clock > gpex_clock_get_max_clock_limit())
			dvfs->step = gpex_clock_get_table_idx(gpex_clock_get_max_clock_limit());
		dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
	} else if ((dvfs->step < gpex_clock_get_table_idx(gpex_clock_get_min_clock())) &&
		   (utilization < dvfs->table[dvfs->step].min_threshold)) {
		dvfs->down_requirement--;
		if (dvfs->down_requirement == 0) {
			dvfs->step++;
			dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
		}
	} else {
		dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
	}
	DVFS_ASSERT((dvfs->step >= gpex_clock_get_table_idx(gpex_clock_get_max_clock())) &&
		    (dvfs->step <= gpex_clock_get_table_idx(gpex_clock_get_min_clock())));

	return 0;
}

static int gpu_dvfs_governor_interactive(int utilization)
{
	int max_clock_lev = gpex_clock_get_table_idx(gpex_clock_get_max_clock());
	int min_clock_lev = gpex_clock_get_table_idx(gpex_clock_get_min_clock());
	int highspeed_level = gpex_clock_get_table_idx(dvfs->interactive.highspeed_clock);
	/* >= and not >, so that naming the topmost table entry as highspeed_clock
	 * still arms the jump. A bare > silently disables it there, which is how
	 * the stock <highspeed_clock = gpu_min_clock> ends up unreachable from the
	 * other side of the table.
	 */
	bool highspeed_wanted = (highspeed_level >= max_clock_lev) &&
				(dvfs->step > highspeed_level) &&
				(utilization > dvfs->interactive.highspeed_load);

	/* The highspeed jump is evaluated on its own load threshold instead of
	 * being nested inside the per-level step-up branch. Nested, it is only
	 * reached while the governor is already stepping up, so on bursty render
	 * loads that never cross max_threshold it stays dead code no matter what
	 * highspeed_clock/highspeed_load are set to.
	 */
	if (highspeed_wanted &&
	    (dvfs->interactive.delay_count >= dvfs->interactive.highspeed_delay)) {
		dvfs->step = highspeed_level;
		dvfs->interactive.delay_count = 0;
		if (dvfs->table[dvfs->step].clock > gpex_clock_get_max_clock_limit())
			dvfs->step = gpex_clock_get_table_idx(gpex_clock_get_max_clock_limit());
		dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;

		DVFS_ASSERT(dvfs->step <= min_clock_lev);

		return 0;
	}

	/* Waiting out highspeed_delay must not also hold the clock down: keep
	 * counting, but let the normal ladder run in the meantime.
	 */
	if (highspeed_wanted)
		dvfs->interactive.delay_count++;
	else
		dvfs->interactive.delay_count = 0;

	if ((dvfs->step > max_clock_lev) &&
	    (utilization > dvfs->table[dvfs->step].max_threshold)) {
		dvfs->step--;
		if (dvfs->table[dvfs->step].clock > gpex_clock_get_max_clock_limit())
			dvfs->step = gpex_clock_get_table_idx(gpex_clock_get_max_clock_limit());
		dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
	} else if ((dvfs->step < min_clock_lev) &&
		   (utilization < dvfs->table[dvfs->step].min_threshold)) {
		dvfs->down_requirement--;
		if (dvfs->down_requirement == 0) {
			dvfs->step++;
			dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
		}
	} else {
		dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
	}

	DVFS_ASSERT(dvfs->step <= min_clock_lev);

	return 0;
}

static int gpu_dvfs_governor_joint(int utilization)
{
	int i;
	int min_value;
	int weight_util;
	int utilT;
	int weight_fmargin_clock;
	int next_clock;
	int diff_clock;

	min_value = gpex_clock_get_min_clock();
	next_clock = gpex_clock_get_cur_clock();

	weight_util = gpu_weight_prediction_utilisation(utilization);
	utilT = ((long long)(weight_util)*gpex_clock_get_cur_clock() / 100) >> 10;
	weight_fmargin_clock =
		utilT + ((gpex_clock_get_max_clock() - utilT) / 1000) * gpex_tsg_get_freq_margin();

	if (weight_fmargin_clock > gpex_clock_get_max_clock()) {
		dvfs->step = gpex_clock_get_table_idx(gpex_clock_get_max_clock());
	} else if (weight_fmargin_clock < gpex_clock_get_min_clock()) {
		dvfs->step = gpex_clock_get_table_idx(gpex_clock_get_min_clock());
	} else {
		for (i = gpex_clock_get_table_idx(gpex_clock_get_max_clock());
		     i <= gpex_clock_get_table_idx(gpex_clock_get_min_clock()); i++) {
			diff_clock = (dvfs->table[i].clock - weight_fmargin_clock);
			if (diff_clock < min_value) {
				if (diff_clock >= 0) {
					min_value = diff_clock;
					next_clock = dvfs->table[i].clock;
				} else {
					break;
				}
			}
		}
		dvfs->step = gpex_clock_get_table_idx(next_clock);
	}

	GPU_LOG(MALI_EXYNOS_DEBUG,
		"%s: F_margin[%d] weight_util[%d] utilT[%d] weight_fmargin_clock[%d] next_clock[%d], step[%d]\n",
		__func__, gpex_tsg_get_freq_margin(), weight_util, utilT, weight_fmargin_clock,
		next_clock, dvfs->step);
	DVFS_ASSERT((dvfs->step >= gpex_clock_get_table_idx(gpex_clock_get_max_clock())) &&
		    (dvfs->step <= gpex_clock_get_table_idx(gpex_clock_get_min_clock())));

	return 0;
}
#define weight_table_size 12
#define WEIGHT_TABLE_MAX_IDX 11
int gpu_weight_prediction_utilisation(int utilization)
{
	int i;
	int idx;
	int t_window = weight_table_size;
	static int weight_sum[2] = { 0, 0 };
	static int weight_table_idx[2];
	int weight_table[WEIGHT_TABLE_MAX_IDX][weight_table_size] = {
		{ 48, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8, 4 },
		{ 100, 10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 200, 40, 8, 2, 1, 0, 0, 0, 0, 0, 0, 0 },
		{ 300, 90, 27, 8, 2, 1, 0, 0, 0, 0, 0, 0 },
		{ 400, 160, 64, 26, 10, 4, 2, 1, 0, 0, 0, 0 },
		{ 500, 250, 125, 63, 31, 16, 8, 4, 2, 1, 0, 0 },
		{ 600, 360, 216, 130, 78, 47, 28, 17, 10, 6, 4, 2 },
		{ 700, 490, 343, 240, 168, 118, 82, 58, 40, 28, 20, 14 },
		{ 800, 640, 512, 410, 328, 262, 210, 168, 134, 107, 86, 69 },
		{ 900, 810, 729, 656, 590, 531, 478, 430, 387, 349, 314, 282 },
		{ 48, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8, 4 }
	};
	int normalized_util;
	int util_conv;
	int table_idx[2];

	table_idx[0] = gpex_tsg_get_weight_table_idx(0);
	table_idx[1] = gpex_tsg_get_weight_table_idx(1);

	if ((weight_table_idx[0] != table_idx[0]) || (weight_table_idx[1] != table_idx[1])) {
		weight_table_idx[0] = (table_idx[0] < WEIGHT_TABLE_MAX_IDX) ?
						    table_idx[0] :
						    WEIGHT_TABLE_MAX_IDX - 1;
		weight_table_idx[1] = (table_idx[1] < WEIGHT_TABLE_MAX_IDX) ?
						    table_idx[1] :
						    WEIGHT_TABLE_MAX_IDX - 1;
		weight_sum[0] = 0;
		weight_sum[1] = 0;
	}

	if ((weight_sum[0] == 0) || (weight_sum[1] == 0)) {
		weight_sum[0] = 0;
		weight_sum[1] = 0;
		for (i = 0; i < t_window; i++) {
			weight_sum[0] += weight_table[table_idx[0]][i];
			weight_sum[1] += weight_table[table_idx[1]][i];
		}
	}

	normalized_util = ((long long)(utilization * gpex_clock_get_cur_clock()) << 10) /
			  gpex_clock_get_max_clock();

	GPU_LOG(MALI_EXYNOS_DEBUG, "%s: util[%d] cur_clock[%d] max_clock[%d] normalized_util[%d]\n",
		__func__, utilization, gpex_clock_get_cur_clock(), gpex_clock_get_max_clock(),
		normalized_util);

	for (idx = 0; idx < 2; idx++) {
		gpex_tsg_set_weight_util(idx, 0);
		gpex_tsg_set_weight_freq(0);

		for (i = t_window - 1; i >= 0; i--) {
			if (0 == i) {
				gpex_tsg_set_util_history(idx, 0, normalized_util);
				gpex_tsg_set_weight_util(
					idx, gpex_tsg_get_weight_util(idx) +
						     gpex_tsg_get_util_history(idx, i) *
							     weight_table[table_idx[idx]][i]);
				gpex_tsg_set_weight_util(idx, gpex_tsg_get_weight_util(idx) /
								      weight_sum[idx]);
				gpex_tsg_set_en_signal(true);
				break;
			}

			gpex_tsg_set_util_history(idx, i, gpex_tsg_get_util_history(idx, i - 1));
			gpex_tsg_set_weight_util(idx,
						 gpex_tsg_get_weight_util(idx) +
							 gpex_tsg_get_util_history(idx, i) *
								 weight_table[table_idx[idx]][i]);
		}

		/* Check history */
		GPU_LOG(MALI_EXYNOS_DEBUG,
			"%s: cur_util[%d]_cur_freq[%d]_weight_util[%d]_window[%d]\n", __func__,
			utilization, gpex_clock_get_cur_clock(), gpex_tsg_get_weight_util(idx),
			t_window);
	}
	if (gpex_tsg_get_weight_util(0) < gpex_tsg_get_weight_util(1)) {
		gpex_tsg_set_weight_util(0, gpex_tsg_get_weight_util(1));
	}

	if (gpex_tsg_get_en_signal() == true)
		util_conv = (long long)(gpex_tsg_get_weight_util(0)) * gpex_clock_get_max_clock() /
			    gpex_clock_get_cur_clock();
	else
		util_conv = utilization << 10;

	return util_conv;
}

#define G3D_GOVERNOR_STATIC_PERIOD 10
static int gpu_dvfs_governor_static(int utilization)
{
	static bool step_down = true;
	static int count;

	if (count == G3D_GOVERNOR_STATIC_PERIOD) {
		if (step_down) {
			if (dvfs->step > gpex_clock_get_table_idx(gpex_clock_get_max_clock()))
				dvfs->step--;
			if (((gpex_clock_get_max_lock() > 0) &&
			     (dvfs->table[dvfs->step].clock == gpex_clock_get_max_lock())) ||
			    (dvfs->step == gpex_clock_get_table_idx(gpex_clock_get_max_clock())))
				step_down = false;
		} else {
			if (dvfs->step < gpex_clock_get_table_idx(gpex_clock_get_min_clock()))
				dvfs->step++;
			if (((gpex_clock_get_min_lock() > 0) &&
			     (dvfs->table[dvfs->step].clock == gpex_clock_get_min_lock())) ||
			    (dvfs->step == gpex_clock_get_table_idx(gpex_clock_get_min_clock())))
				step_down = true;
		}

		count = 0;
	} else {
		count++;
	}

	return 0;
}

static int gpu_dvfs_governor_booster(int utilization)
{
	static int weight;
	int cur_weight, booster_threshold, dvfs_table_lock;

	cur_weight = gpex_clock_get_cur_clock() * utilization;
	/* booster_threshold = current clock * set the percentage of utilization */
	booster_threshold = gpex_clock_get_cur_clock() * 50;

	dvfs_table_lock = gpex_clock_get_table_idx(gpex_clock_get_max_clock());

	if ((dvfs->step >= dvfs_table_lock + 2) && ((cur_weight - weight) > booster_threshold)) {
		dvfs->step -= 2;
		dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
		GPU_LOG(MALI_EXYNOS_WARNING, "Booster Governor: G3D level 2 step\n");
	} else if ((dvfs->step > gpex_clock_get_table_idx(gpex_clock_get_max_clock())) &&
		   (utilization > dvfs->table[dvfs->step].max_threshold)) {
		dvfs->step--;
		dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
	} else if ((dvfs->step < gpex_clock_get_table_idx(gpex_clock_get_min_clock())) &&
		   (utilization < dvfs->table[dvfs->step].min_threshold)) {
		dvfs->down_requirement--;
		if (dvfs->down_requirement == 0) {
			dvfs->step++;
			dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
		}
	} else {
		dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
	}

	DVFS_ASSERT((dvfs->step >= gpex_clock_get_table_idx(gpex_clock_get_max_clock())) &&
		    (dvfs->step <= gpex_clock_get_table_idx(gpex_clock_get_min_clock())));

	weight = cur_weight;

	return 0;
}

static int gpu_dvfs_governor_dynamic(int utilization)
{
	int max_clock_lev = gpex_clock_get_table_idx(gpex_clock_get_max_clock());
	int min_clock_lev = gpex_clock_get_table_idx(gpex_clock_get_min_clock());

	if ((dvfs->step > max_clock_lev) && (utilization > dvfs->table[dvfs->step].max_threshold)) {
		if (dvfs->table[dvfs->step].clock * utilization >
		    dvfs->table[dvfs->step - 1].clock * dvfs->table[dvfs->step - 1].max_threshold) {
			dvfs->step -= 2;
			if (dvfs->step < max_clock_lev) {
				dvfs->step = max_clock_lev;
			}
		} else {
			dvfs->step--;
		}

		if (dvfs->table[dvfs->step].clock > gpex_clock_get_max_clock_limit())
			dvfs->step = gpex_clock_get_table_idx(gpex_clock_get_max_clock_limit());

		dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
	} else if ((dvfs->step < min_clock_lev) &&
		   (utilization < dvfs->table[dvfs->step].min_threshold)) {
		dvfs->down_requirement--;
		if (dvfs->down_requirement == 0) {
			if (dvfs->table[dvfs->step].clock * utilization <
			    dvfs->table[dvfs->step + 1].clock *
				    dvfs->table[dvfs->step + 1].min_threshold) {
				dvfs->step += 2;
				if (dvfs->step > min_clock_lev) {
					dvfs->step = min_clock_lev;
				}
			} else {
				dvfs->step++;
			}
			dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
		}
	} else {
		dvfs->down_requirement = dvfs->table[dvfs->step].down_staycount;
	}

	DVFS_ASSERT(dvfs->step <= gpex_clock_get_table_idx(gpex_clock_get_min_clock()));

	return 0;
}

/* Deadline driven clock floor, fed by gpex_dvfs_notify_render_job().
 *
 * Utilization is a throughput metric with no notion of "too late": a GPU busy
 * 45% of a polling window while missing every 16.7ms frame deadline looks
 * exactly like one with plenty of headroom, so no threshold tuning can make the
 * governor react to it. When a fragment job has recently been measured
 * overrunning its share of the frame budget, clamp whatever the governor picked
 * to the frame boost level. An idle GPU stops refreshing the timestamp, the
 * clamp expires and the table walks the clock back down to min as usual.
 */
static void gpu_dvfs_apply_frame_boost(void)
{
	unsigned long flags;
	int level;
	ktime_t last_late;

	if (dvfs->frame_boost.clock <= 0)
		return;

	/* The clamp reasons about the interactive table's thresholds. The other
	 * governors compute their target from a different model (joint uses the
	 * frame margin, static/booster ignore utilization), so overriding their
	 * decision here would be meaningless rather than conservative.
	 */
	if (dvfs->governor_type != G3D_DVFS_GOVERNOR_INTERACTIVE)
		return;

	last_late = (ktime_t)atomic64_read(&dvfs->frame_boost.last_late_job);
	if (!last_late)
		return;

	if (ktime_ms_delta(ktime_get_boottime(), last_late) > dvfs->frame_boost.release_ms)
		return;

	level = gpex_clock_get_table_idx(dvfs->frame_boost.clock);
	if (level < 0)
		return;

	spin_lock_irqsave(&dvfs->spinlock, flags);

	if (dvfs->table[level].clock > gpex_clock_get_max_clock_limit())
		level = gpex_clock_get_table_idx(gpex_clock_get_max_clock_limit());

	if (level >= 0 && dvfs->step > level) {
		dvfs->step = level;
		/* gpex_dvfs_set_clock_callback() only refreshes down_requirement
		 * when it notices the step moved, and it runs after this, so it
		 * sees no change and would leave the previous level's staycount
		 * in place. Apply the new level's here.
		 *
		 * The thermal ceiling is not applied here on purpose: TMU holds a
		 * max lock and gpu_check_target_clock() enforces it on the way to
		 * the hardware, after this clamp.
		 */
		dvfs->down_requirement = dvfs->table[level].down_staycount;
	}

	spin_unlock_irqrestore(&dvfs->spinlock, flags);
}

int gpu_dvfs_decide_next_freq(int utilization)
{
	unsigned long flags;

	if (gpex_tsg_get_migov_mode() == 1 && gpex_tsg_get_is_gov_set() != 1) {
		gpu_dvfs_governor_setting_locked(G3D_DVFS_GOVERNOR_JOINT);
		gpex_tsg_set_saved_polling_speed(gpex_dvfs_get_polling_speed());
		gpex_dvfs_set_polling_speed(16);
		gpex_tsg_set_is_gov_set(1);
		gpex_tsg_set_en_signal(false);
		gpex_tsg_set_pmqos(true);
	} else if (gpex_tsg_get_migov_mode() != 1 && gpex_tsg_get_is_gov_set() != 0) {
		gpu_dvfs_governor_setting_locked(gpex_tsg_get_governor_type_init());
		gpex_dvfs_set_polling_speed(gpex_tsg_get_saved_polling_speed());
		gpex_tsg_set_is_gov_set(0);
		gpex_tsg_set_pmqos(false);
		gpex_tsg_reset_acc_count();
	}

	gpex_tsg_input_nr_acc_cnt();

	spin_lock_irqsave(&dvfs->spinlock, flags);
	gpu_dvfs_get_next_level(utilization);
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	if (gpex_clboost_check_activation_condition())
		dvfs->step = gpex_clock_get_table_idx(gpex_clock_get_max_clock());
	else
		gpu_dvfs_apply_frame_boost();

	return dvfs->table[dvfs->step].clock;
}

int gpu_dvfs_governor_setting(int governor_type)
{
	unsigned long flags;

	if ((governor_type < 0) || (governor_type >= G3D_MAX_GOVERNOR_NUM)) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid governor type (%d)\n", __func__,
			governor_type);
		return -1;
	}

	spin_lock_irqsave(&dvfs->spinlock, flags);
	dvfs->step = gpex_clock_get_table_idx(governor_info[governor_type].start_clk);
	gpu_dvfs_get_next_level = (GET_NEXT_LEVEL)(governor_info[governor_type].governor);

	dvfs->env_data.utilization = 80;

	dvfs->down_requirement = 1;
	dvfs->governor_type = governor_type;

	/* TODO: why set the cur_clock here? cur_clock should be set when the actual clock is changed */
	//gpex_clock_get_cur_clock() = dvfs->table[dvfs->step].clock;

	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	return 0;
}

int gpu_dvfs_governor_setting_locked(int governor_type)
{
	unsigned long flags;

	if ((governor_type < 0) || (governor_type >= G3D_MAX_GOVERNOR_NUM)) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid governor type (%d)\n", __func__,
			governor_type);
		return -1;
	}

	spin_lock_irqsave(&dvfs->spinlock, flags);
	dvfs->step = gpex_clock_get_table_idx(governor_info[governor_type].start_clk);
	gpu_dvfs_get_next_level = (GET_NEXT_LEVEL)(governor_info[governor_type].governor);

	dvfs->governor_type = governor_type;

	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	return 0;
}

int gpu_dvfs_governor_init(struct dvfs_info *_dvfs)
{
	int governor_type = G3D_DVFS_GOVERNOR_DEFAULT;

	dvfs = _dvfs;

	governor_type = dvfs->governor_type;

	if (gpu_dvfs_governor_setting(governor_type) < 0) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: fail to initialize governor\n", __func__);
		return -1;
	}

	return 0;
}
