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

#ifndef _MALI_EXYNOS_DVFS_H_
#define _MALI_EXYNOS_DVFS_H_
#ifndef CSTD_UNUSED
#define CSTD_UNUSED(x) ((void)(x))
#endif /* CSTD_UNUSED */

#include <mali_kbase.h>
#include <linux/device.h>

/*******************
 * INIT/TERM
 ******************/
/**
 * gpex_dvfs_init() - initializes and enables dvfs module
 *
 * Return: 0 on success
 */
int gpex_dvfs_init(struct device **dev);

/**
 * gpex_dvfs_term() - disables and terminates dvfs module
 */
void gpex_dvfs_term(void);

/*******************
 * CALLBACKS
 ******************/
/**
 * gpex_dvfs_set_clock_callback() - callback called by gpex_clock module
 *
 * Called by gpex_clock when setting gpu clock.
 */
int gpex_dvfs_set_clock_callback(void);

/**
 * gpex_dvfs_notify_render_job() - report how long a frame pipeline job ran
 * @ns_spent: job execution time in nanoseconds
 *
 * Called from the job completion path for vertex and fragment jobs, the two
 * that belong to a frame. A job that overruns its share of the frame budget
 * arms the deadline driven clock floor, which utilization based governing
 * cannot detect on its own. Jobs flagged BASE_JD_REQ_ONLY_COMPUTE are left out:
 * they carry no frame deadline.
 *
 * Context: called with hwaccess_lock held and interrupts disabled.
 */
void gpex_dvfs_notify_render_job(u64 ns_spent);

/*******************
 * SETTERS
 ******************/
/**
 * gpex_dvfs_enable() - enables dvfs module
 *
 * Returns: 0 on success
 */
int gpex_dvfs_enable(void);

/**
 * gpex_dvfs_disable() - disables dvfs module
 *
 * Returns: 0 on success
 */
int gpex_dvfs_disable(void);

/**
 * gpex_dvfs_start() - starts dvfs events/timer
 */
void gpex_dvfs_start(void);

/**
 * gpex_dvfs_stop() - stops dvfs events/timer
 */
void gpex_dvfs_stop(void);

/**
 * gpex_dvfs_set_polling_speed() - set polling speed for dvfs callback period
 */
int gpex_dvfs_set_polling_speed(int polling_speed);

/*******************
 * GETTERS
 ******************/
/**
 * gpex_dvfs_get_status() - check if DVFS is enabled.
 *
 * Returns: 0 if DVFS is disabled. 1 if DVFS is enabled.
 */
int gpex_dvfs_get_status(void);

/**
 * gpex_dvfs_get_governor_type() - get the current dvfs governor type
 *
 * G3D_DVFS_GOVERNOR_DEFAULT = 0,
 * G3D_DVFS_GOVERNOR_INTERACTIVE,
 * G3D_DVFS_GOVERNOR_JOINT,
 * G3D_DVFS_GOVERNOR_STATIC,
 * G3D_DVFS_GOVERNOR_BOOSTER,
 * G3D_DVFS_GOVERNOR_DYNAMIC,
 *
 * Returns: gpu_governor_type
 */
int gpex_dvfs_get_governor_type(void);

/**
 * gpex_dvfs_get_utilization() - get the current gpu utilization
 *
 * Returns: gpu utilization between 0 and 100
 */
int gpex_dvfs_get_utilization(void);

/**
 * gpex_dvfs_get_polling_speed() - get the dvfs polling speed in msec
 *
 * Returns: dvfs polling speed.
 */
int gpex_dvfs_get_polling_speed(void);

/******************
 * LOCKS & MUTEXES
 ******************/
/**
 * gpex_dvfs_spin_lock() - lock dvfs operations
 *
 * Hold this lock if you don't want dvfs event to change the clock
 * i.e: when manipulation GPU power state
 */
void gpex_dvfs_spin_lock(unsigned long *flags);

/**
 * gpex_dvfs_spin_unlock() - unlock dvfs operations
 */
void gpex_dvfs_spin_unlock(unsigned long *flags);

#endif /* _MALI_EXYNOS_DVFS_H_ */
