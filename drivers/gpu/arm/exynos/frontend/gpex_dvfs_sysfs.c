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

#include <gpex_pm.h>
#include <gpex_dvfs.h>
#include <gpex_clock.h>
#include <gpex_utils.h>

#include "gpex_dvfs_internal.h"
#include "gpu_dvfs_governor.h"

static struct dvfs_info *dvfs;

static int gpu_dvfs_governor_change(int governor_type)
{
	mutex_lock(&dvfs->handler_lock);
	gpu_dvfs_governor_setting(governor_type);
	mutex_unlock(&dvfs->handler_lock);

	return 0;
}

static int gpu_get_dvfs_table(char *buf, size_t buf_size)
{
	int i, cnt = 0;

	if (buf == NULL)
		return 0;

	for (i = gpex_clock_get_table_idx(gpex_clock_get_max_clock());
	     i <= gpex_clock_get_table_idx(gpex_clock_get_min_clock()); i++)
		cnt += snprintf(buf + cnt, buf_size - cnt, " %d", dvfs->table[i].clock);

	cnt += snprintf(buf + cnt, buf_size - cnt, "\n");

	return cnt;
}

static ssize_t show_dvfs_table(char *buf)
{
	ssize_t ret = 0;

	ret += gpu_get_dvfs_table(buf + ret, (size_t)PAGE_SIZE - ret);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_DEVICE_READ_FUNCTION(show_dvfs_table);

static ssize_t show_dvfs(char *buf)
{
	ssize_t ret = 0;

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d", dvfs->status);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_DEVICE_READ_FUNCTION(show_dvfs);

static ssize_t set_dvfs(const char *buf, size_t count)
{
	if (sysfs_streq("0", buf))
		gpex_dvfs_disable();
	else if (sysfs_streq("1", buf))
		gpex_dvfs_enable();

	return count;
}
CREATE_SYSFS_DEVICE_WRITE_FUNCTION(set_dvfs);

static ssize_t show_governor(char *buf)
{
	ssize_t ret = 0;
	gpu_dvfs_governor_info *governor_info;
	int i;

	governor_info = (gpu_dvfs_governor_info *)gpu_dvfs_get_governor_info();

	for (i = 0; i < G3D_MAX_GOVERNOR_NUM; i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "%s\n", governor_info[i].name);

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "[Current Governor] %s",
			governor_info[dvfs->governor_type].name);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_DEVICE_READ_FUNCTION(show_governor);

/* Accepts either a governor name or its table index.
 *
 * Returns: governor type on success, -EINVAL on unparsable or out of range
 * input. Never falls back to a default: the previous version left the parsed
 * value untouched when kstrtoint() failed, so writing a governor name silently
 * switched the GPU to whatever index happened to be on the stack (in practice
 * 0, Default) while still reporting success.
 */
static int gpu_dvfs_governor_parse(const char *buf)
{
	gpu_dvfs_governor_info *governor_info;
	int governor_type;
	int i;

	governor_info = (gpu_dvfs_governor_info *)gpu_dvfs_get_governor_info();

	for (i = 0; i < G3D_MAX_GOVERNOR_NUM; i++)
		if (sysfs_streq(buf, governor_info[i].name))
			return i;

	if (kstrtoint(buf, 0, &governor_type))
		return -EINVAL;

	if ((governor_type < 0) || (governor_type >= G3D_MAX_GOVERNOR_NUM))
		return -EINVAL;

	return governor_type;
}

static ssize_t set_governor(const char *buf, size_t count)
{
	int next_governor_type = gpu_dvfs_governor_parse(buf);

	if (next_governor_type < 0) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid governor\n", __func__);
		return -EINVAL;
	}

	if (gpu_dvfs_governor_change(next_governor_type) < 0) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: fail to set the new governor (%d)\n", __func__,
			next_governor_type);
		return -ENOENT;
	}

	return count;
}
CREATE_SYSFS_DEVICE_WRITE_FUNCTION(set_governor);

#define MIN_DOWN_STAYCOUNT 1
#define MAX_DOWN_STAYCOUNT 10
static ssize_t show_down_staycount(char *buf)
{
	ssize_t ret = 0;
	unsigned long flags;
	int i = -1;

	spin_lock_irqsave(&dvfs->spinlock, flags);
	for (i = gpex_clock_get_table_idx(gpex_clock_get_max_clock());
	     i <= gpex_clock_get_table_idx(gpex_clock_get_min_clock()); i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Clock %d - %d\n", dvfs->table[i].clock,
				dvfs->table[i].down_staycount);
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	/* The write format is per level and does not match the lines above, which
	 * is why a bare "echo 3" is rejected. Spell it out here.
	 */
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "write format: <clock> <staycount %d-%d>\n",
			MIN_DOWN_STAYCOUNT, MAX_DOWN_STAYCOUNT);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_DEVICE_READ_FUNCTION(show_down_staycount);

static ssize_t set_down_staycount(const char *buf, size_t count)
{
	unsigned long flags;
	char tmpbuf[32];
	char *sptr, *tok;
	int ret = -1;
	int clock = -1, level = -1, down_staycount = 0;
	unsigned int len = 0;

	len = (unsigned int)min(count, sizeof(tmpbuf) - 1);
	memcpy(tmpbuf, buf, len);
	tmpbuf[len] = '\0';
	sptr = tmpbuf;

	tok = strsep(&sptr, " ,");
	if (tok == NULL) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid input\n", __func__);
		return -ENOENT;
	}

	ret = kstrtoint(tok, 0, &clock);
	if (ret) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid input %d\n", __func__, clock);
		return -ENOENT;
	}

	tok = strsep(&sptr, " ,");
	if (tok == NULL) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid input\n", __func__);
		return -ENOENT;
	}

	ret = kstrtoint(tok, 0, &down_staycount);
	if (ret) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid input %d\n", __func__, down_staycount);
		return -ENOENT;
	}

	level = gpex_clock_get_table_idx(clock);
	if (level < 0) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid clock value (%d)\n", __func__, clock);
		return -ENOENT;
	}

	if ((down_staycount < MIN_DOWN_STAYCOUNT) || (down_staycount > MAX_DOWN_STAYCOUNT)) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: down_staycount is out of range (%d, %d ~ %d)\n",
			__func__, down_staycount, MIN_DOWN_STAYCOUNT, MAX_DOWN_STAYCOUNT);
		return -ENOENT;
	}

	spin_lock_irqsave(&dvfs->spinlock, flags);
	dvfs->table[level].down_staycount = down_staycount;
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	return count;
}
CREATE_SYSFS_DEVICE_WRITE_FUNCTION(set_down_staycount);

static ssize_t show_highspeed_clock(char *buf)
{
	ssize_t ret = 0;
	unsigned long flags;
	int highspeed_clock = -1;

	spin_lock_irqsave(&dvfs->spinlock, flags);
	highspeed_clock = dvfs->interactive.highspeed_clock;
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d", highspeed_clock);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_DEVICE_READ_FUNCTION(show_highspeed_clock);

static ssize_t set_highspeed_clock(const char *buf, size_t count)
{
	ssize_t ret = 0;
	unsigned long flags;
	int highspeed_clock = -1;

	ret = kstrtoint(buf, 0, &highspeed_clock);
	if (ret) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid value\n", __func__);
		return -ENOENT;
	}

	ret = gpex_clock_get_table_idx(highspeed_clock);
	if ((ret < gpex_clock_get_table_idx(gpex_clock_get_max_clock())) ||
	    (ret > gpex_clock_get_table_idx(gpex_clock_get_min_clock()))) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid clock value (%d)\n", __func__,
			highspeed_clock);
		return -ENOENT;
	}

	if (highspeed_clock > gpex_clock_get_max_clock_limit())
		highspeed_clock = gpex_clock_get_max_clock_limit();

	spin_lock_irqsave(&dvfs->spinlock, flags);
	dvfs->interactive.highspeed_clock = highspeed_clock;
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	return count;
}
CREATE_SYSFS_DEVICE_WRITE_FUNCTION(set_highspeed_clock);

static ssize_t show_highspeed_load(char *buf)
{
	ssize_t ret = 0;
	unsigned long flags;
	int highspeed_load = -1;

	spin_lock_irqsave(&dvfs->spinlock, flags);
	highspeed_load = dvfs->interactive.highspeed_load;
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d", highspeed_load);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_DEVICE_READ_FUNCTION(show_highspeed_load);

static ssize_t set_highspeed_load(const char *buf, size_t count)
{
	ssize_t ret = 0;
	unsigned long flags;
	int highspeed_load = -1;

	ret = kstrtoint(buf, 0, &highspeed_load);
	if (ret) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid value\n", __func__);
		return -ENOENT;
	}

	if ((highspeed_load < 0) || (highspeed_load > 100)) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid load value (%d)\n", __func__,
			highspeed_load);
		return -ENOENT;
	}

	spin_lock_irqsave(&dvfs->spinlock, flags);
	dvfs->interactive.highspeed_load = highspeed_load;
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	return count;
}
CREATE_SYSFS_DEVICE_WRITE_FUNCTION(set_highspeed_load);

static ssize_t show_highspeed_delay(char *buf)
{
	ssize_t ret = 0;
	unsigned long flags;
	int highspeed_delay = -1;

	spin_lock_irqsave(&dvfs->spinlock, flags);
	highspeed_delay = dvfs->interactive.highspeed_delay;
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d", highspeed_delay);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_DEVICE_READ_FUNCTION(show_highspeed_delay);

static ssize_t set_highspeed_delay(const char *buf, size_t count)
{
	ssize_t ret = 0;
	unsigned long flags;
	int highspeed_delay = -1;

	ret = kstrtoint(buf, 0, &highspeed_delay);
	if (ret) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid value\n", __func__);
		return -ENOENT;
	}

	if ((highspeed_delay < 0) || (highspeed_delay > 5)) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid load value (%d)\n", __func__,
			highspeed_delay);
		return -ENOENT;
	}

	spin_lock_irqsave(&dvfs->spinlock, flags);
	dvfs->interactive.highspeed_delay = highspeed_delay;
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	return count;
}
CREATE_SYSFS_DEVICE_WRITE_FUNCTION(set_highspeed_delay);

static ssize_t show_polling_speed(char *buf)
{
	ssize_t ret = 0;

	int polling_speed = gpex_dvfs_get_polling_speed();
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d", polling_speed);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_DEVICE_READ_FUNCTION(show_polling_speed);

static ssize_t set_polling_speed(const char *buf, size_t count)
{
	int ret, polling_speed;

	ret = kstrtoint(buf, 0, &polling_speed);

	if (ret) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid value\n", __func__);
		return -ENOENT;
	}

	gpex_dvfs_set_polling_speed(polling_speed);

	return count;
}
CREATE_SYSFS_DEVICE_WRITE_FUNCTION(set_polling_speed);

static ssize_t show_utilization(char *buf)
{
	ssize_t ret = 0;

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d",
			gpex_pm_get_status(true) * dvfs->env_data.utilization);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_DEVICE_READ_FUNCTION(show_utilization);

static ssize_t show_kernel_sysfs_utilization(char *buf)
{
	ssize_t ret = 0;

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "%3d%%", dvfs->env_data.utilization);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_KOBJECT_READ_FUNCTION(show_kernel_sysfs_utilization)

#define BUF_SIZE 1000
static ssize_t show_kernel_sysfs_available_governor(char *buf)
{
	ssize_t ret = 0;
	gpu_dvfs_governor_info *governor_info;
	int i;

	governor_info = (gpu_dvfs_governor_info *)gpu_dvfs_get_governor_info();

	for (i = 0; i < G3D_MAX_GOVERNOR_NUM; i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "%s ", governor_info[i].name);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_KOBJECT_READ_FUNCTION(show_kernel_sysfs_available_governor)

static ssize_t show_kernel_sysfs_governor(char *buf)
{
	ssize_t ret = 0;
	gpu_dvfs_governor_info *governor_info = NULL;

	governor_info = (gpu_dvfs_governor_info *)gpu_dvfs_get_governor_info();

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "%s", governor_info[dvfs->governor_type].name);

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}
CREATE_SYSFS_KOBJECT_READ_FUNCTION(show_kernel_sysfs_governor)

static ssize_t set_kernel_sysfs_governor(const char *buf, size_t count)
{
	int next_governor_type = gpu_dvfs_governor_parse(buf);

	if (next_governor_type < 0) {
		GPU_LOG(MALI_EXYNOS_ERROR, "%s: invalid governor\n", __func__);
		return -EINVAL;
	}

	if (gpu_dvfs_governor_change(next_governor_type) < 0) {
		GPU_LOG(MALI_EXYNOS_ERROR, "%s: fail to set the new governor (%d)\n", __func__,
			next_governor_type);
		return -ENOENT;
	}

	return count;
}
CREATE_SYSFS_KOBJECT_WRITE_FUNCTION(set_kernel_sysfs_governor)

/* Deadline driven boost knobs. gpu_frame_boost_clock = 0 turns the whole thing
 * off and restores plain utilization based governing.
 */
static ssize_t show_kernel_sysfs_frame_boost_clock(char *buf)
{
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d", dvfs->frame_boost.clock);

	return gpex_utils_sysfs_endbuf(buf, len);
}
CREATE_SYSFS_KOBJECT_READ_FUNCTION(show_kernel_sysfs_frame_boost_clock)

static ssize_t set_kernel_sysfs_frame_boost_clock(const char *buf, size_t count)
{
	unsigned long flags;
	int clock = 0;
	int level;

	if (kstrtoint(buf, 0, &clock)) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid value\n", __func__);
		return -EINVAL;
	}

	if (clock < 0)
		return -EINVAL;

	if (clock > 0) {
		level = gpex_clock_get_table_idx(clock);
		if ((level < gpex_clock_get_table_idx(gpex_clock_get_max_clock())) ||
		    (level > gpex_clock_get_table_idx(gpex_clock_get_min_clock()))) {
			GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid clock value (%d)\n", __func__,
				clock);
			return -EINVAL;
		}
	}

	spin_lock_irqsave(&dvfs->spinlock, flags);
	dvfs->frame_boost.clock = clock;
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	return count;
}
CREATE_SYSFS_KOBJECT_WRITE_FUNCTION(set_kernel_sysfs_frame_boost_clock)

static ssize_t show_kernel_sysfs_frame_boost_job_us(char *buf)
{
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d", dvfs->frame_boost.job_us);

	return gpex_utils_sysfs_endbuf(buf, len);
}
CREATE_SYSFS_KOBJECT_READ_FUNCTION(show_kernel_sysfs_frame_boost_job_us)

static ssize_t set_kernel_sysfs_frame_boost_job_us(const char *buf, size_t count)
{
	unsigned long flags;
	int job_us = 0;

	if (kstrtoint(buf, 0, &job_us)) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid value\n", __func__);
		return -EINVAL;
	}

	if ((job_us <= 0) || (job_us > GPEX_DVFS_FRAME_BOOST_MAX_JOB_US)) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: out of range [1~%d] (%d)\n", __func__,
			GPEX_DVFS_FRAME_BOOST_MAX_JOB_US, job_us);
		return -EINVAL;
	}

	spin_lock_irqsave(&dvfs->spinlock, flags);
	dvfs->frame_boost.job_us = job_us;
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	return count;
}
CREATE_SYSFS_KOBJECT_WRITE_FUNCTION(set_kernel_sysfs_frame_boost_job_us)

static ssize_t show_kernel_sysfs_frame_boost_release_ms(char *buf)
{
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d", dvfs->frame_boost.release_ms);

	return gpex_utils_sysfs_endbuf(buf, len);
}
CREATE_SYSFS_KOBJECT_READ_FUNCTION(show_kernel_sysfs_frame_boost_release_ms)

static ssize_t set_kernel_sysfs_frame_boost_release_ms(const char *buf, size_t count)
{
	unsigned long flags;
	int release_ms = 0;

	if (kstrtoint(buf, 0, &release_ms)) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: invalid value\n", __func__);
		return -EINVAL;
	}

	if ((release_ms <= 0) || (release_ms > GPEX_DVFS_FRAME_BOOST_MAX_RELEASE_MS)) {
		GPU_LOG(MALI_EXYNOS_WARNING, "%s: out of range [1~%d] (%d)\n", __func__,
			GPEX_DVFS_FRAME_BOOST_MAX_RELEASE_MS, release_ms);
		return -EINVAL;
	}

	spin_lock_irqsave(&dvfs->spinlock, flags);
	dvfs->frame_boost.release_ms = release_ms;
	spin_unlock_irqrestore(&dvfs->spinlock, flags);

	return count;
}
CREATE_SYSFS_KOBJECT_WRITE_FUNCTION(set_kernel_sysfs_frame_boost_release_ms)

static ssize_t show_kernel_sysfs_frame_boost_state(char *buf)
{
	ssize_t len = 0;
	ktime_t last_late = (ktime_t)atomic64_read(&dvfs->frame_boost.last_late_job);
	int active = 0;

	if ((dvfs->frame_boost.clock > 0) && last_late &&
	    (ktime_ms_delta(ktime_get_boottime(), last_late) <= dvfs->frame_boost.release_ms))
		active = 1;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"clock %d job_us %d release_ms %d active %d late_jobs %d",
			dvfs->frame_boost.clock, dvfs->frame_boost.job_us,
			dvfs->frame_boost.release_ms, active,
			atomic_read(&dvfs->frame_boost.late_job_cnt));

	return gpex_utils_sysfs_endbuf(buf, len);
}
CREATE_SYSFS_KOBJECT_READ_FUNCTION(show_kernel_sysfs_frame_boost_state)

int gpex_dvfs_sysfs_init(struct dvfs_info *_dvfs)
{
	dvfs = _dvfs;

	GPEX_UTILS_SYSFS_DEVICE_FILE_ADD(dvfs, show_dvfs, set_dvfs);
	GPEX_UTILS_SYSFS_DEVICE_FILE_ADD(dvfs_governor, show_governor, set_governor);
	GPEX_UTILS_SYSFS_DEVICE_FILE_ADD(down_staycount, show_down_staycount, set_down_staycount);
	GPEX_UTILS_SYSFS_DEVICE_FILE_ADD(highspeed_clock, show_highspeed_clock,
					 set_highspeed_clock);
	GPEX_UTILS_SYSFS_DEVICE_FILE_ADD(highspeed_load, show_highspeed_load, set_highspeed_load);
	GPEX_UTILS_SYSFS_DEVICE_FILE_ADD(highspeed_delay, show_highspeed_delay,
					 set_highspeed_delay);
	GPEX_UTILS_SYSFS_DEVICE_FILE_ADD(polling_speed, show_polling_speed, set_polling_speed);
	GPEX_UTILS_SYSFS_DEVICE_FILE_ADD_RO(dvfs_table, show_dvfs_table);
	GPEX_UTILS_SYSFS_DEVICE_FILE_ADD_RO(utilization, show_utilization);

	GPEX_UTILS_SYSFS_KOBJECT_FILE_ADD(gpu_governor, show_kernel_sysfs_governor,
					  set_kernel_sysfs_governor);
	GPEX_UTILS_SYSFS_KOBJECT_FILE_ADD_RO(gpu_available_governor,
					     show_kernel_sysfs_available_governor);
	GPEX_UTILS_SYSFS_KOBJECT_FILE_ADD_RO(gpu_busy, show_kernel_sysfs_utilization);

	GPEX_UTILS_SYSFS_KOBJECT_FILE_ADD(gpu_frame_boost_clock,
					  show_kernel_sysfs_frame_boost_clock,
					  set_kernel_sysfs_frame_boost_clock);
	GPEX_UTILS_SYSFS_KOBJECT_FILE_ADD(gpu_frame_boost_job_us,
					  show_kernel_sysfs_frame_boost_job_us,
					  set_kernel_sysfs_frame_boost_job_us);
	GPEX_UTILS_SYSFS_KOBJECT_FILE_ADD(gpu_frame_boost_release_ms,
					  show_kernel_sysfs_frame_boost_release_ms,
					  set_kernel_sysfs_frame_boost_release_ms);
	GPEX_UTILS_SYSFS_KOBJECT_FILE_ADD_RO(gpu_frame_boost_state,
					     show_kernel_sysfs_frame_boost_state);

	return 0;
}
