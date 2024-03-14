/*******************************************************************************
 * Copyright IBM Corp. and others 2024
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] https://openjdk.org/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0-only WITH Classpath-exception-2.0 OR GPL-2.0-only WITH OpenJDK-assembly-exception-1.0
 *******************************************************************************/

/**
 * @file
 * @ingroup GC_Stats
 */

#include "CPUStats.hpp"

/* thread_api had to be before CollectionStatistics, but in reality probably CollectionStatistics should include it by itself (because it's also using it) */
#include "thread_api.h"
#include "CollectionStatistics.hpp"
#include "EnvironmentBase.hpp"
#include "ModronAssertions.h"
/* include for GC specific wrapper OMRPORT_ACCESS_FROM_ENVIRONMENT, but we could realy use OMRPORT_ACCESS_FROM_OMRPORT */
#include "omrgcconsts.h"


void
MM_CPUStats::recordProcessAndCpuUtilization(MM_EnvironmentBase *env, MM_CollectionStatistics *stats)
{
	OMRPORT_ACCESS_FROM_ENVIRONMENT(env);
	uint64_t CONST_DIVIDER = 1000000;
	stats->_endTime = omrtime_hires_clock();
	J9SysinfoCPUTime cpuTimeEnd;
	intptr_t portLibraryStatus = omrsysinfo_get_CPU_utilization(&cpuTimeEnd);
	if (portLibraryStatus < 0) {
		omrtty_printf("ERROR\n");
	}
	intptr_t rc = omrthread_get_process_times(&stats->_endProcessTimes);
	switch (rc){
	case -1: /* Error: Function un-implemented on architecture */
	case -2: /* Error: getrusage() or GetProcessTimes() returned error value */
		stats->_endProcessTimes._userTime = 0;
		stats->_endProcessTimes._systemTime = 0;
		break;
	case  0:
		break; /* Success */
	default:
		Assert_MM_unreachable();
	}
	if (ifCpuDiff) {
		prev_userTime = stats->_endProcessTimes._userTime/CONST_DIVIDER;
		prev_systemTime = stats->_endProcessTimes._systemTime/CONST_DIVIDER;
		prev_cpuIdleTime = cpuTimeEnd.idleTime/CONST_DIVIDER;
		prev_cpuTime = cpuTimeEnd.cpuTime/CONST_DIVIDER;
		prev_elapsedTime = cpuTimeEnd.elapsedTime/CONST_DIVIDER;
		prev_elapsedTimeNew = stats->_endTime;
	}

	/* Expose CPU stats data to hooks */
	stats->_cpuStats = *this;

}

void
MM_CPUStats::calculateProcessAndCpuUtilizationDelta(MM_EnvironmentBase *env, MM_CollectionStatistics *stats)
{
	OMRPORT_ACCESS_FROM_ENVIRONMENT(env);
	uint64_t CONST_DIVIDER = 1000000;
	intptr_t rc = omrthread_get_process_times(&stats->_startProcessTimes);
	switch (rc){
	case -1: /* Error: Function un-implemented on architecture */
	case -2: /* Error: getrusage() or GetProcessTimes() returned error value */
		stats->_startProcessTimes._userTime = I_64_MAX;
		stats->_startProcessTimes._systemTime = I_64_MAX;
		break;
	case  0:
		break; /* Success */
	default:
		Assert_MM_unreachable();
	}
	J9SysinfoCPUTime cpuTimeStart;
	intptr_t portLibraryStatus = omrsysinfo_get_CPU_utilization(&cpuTimeStart);
	if (portLibraryStatus < 0) {
		omrtty_printf("ERROR\n");
	}
	stats->_startTime = omrtime_hires_clock();
	ifCpuDiff = false;
	int64_t cpuTimeDiff = cpuTimeStart.cpuTime / CONST_DIVIDER - prev_cpuTime;
	// int64_t elapsedTime = cpuTimeStart.elapsedTime/CONST_DIVIDER - prev_elapsedTime;
	int64_t elapsedTime = omrtime_hires_delta(prev_elapsedTimeNew, stats->_startTime, OMRPORT_TIME_DELTA_IN_MILLISECONDS) * omrsysinfo_get_number_CPUs_by_type(OMRPORT_CPU_TARGET);
	// omrtty_printf("old cpu elapsed time: %llu\n", (cpuTimeStart.elapsedTime/CONST_DIVIDER - prev_elapsedTime));
	// omrtty_printf("new cpu elapsed time: %llu\n", (currentTime - prev_elapsedTimeNew)*cpuTimeStart.numberOfCpus);
	if (0 < cpuTimeDiff && 0 < elapsedTime) {
		ifCpuDiff = true;
		int64_t diffSumTime = stats->_startProcessTimes._systemTime / CONST_DIVIDER - prev_systemTime + stats->_startProcessTimes._userTime / CONST_DIVIDER - prev_userTime;
		int64_t diffIdleTime = cpuTimeStart.idleTime / CONST_DIVIDER - prev_cpuIdleTime;
		if (0 < prev_elapsedTimeNew) {
			int64_t originalCpuTimeDiff = cpuTimeDiff;
			if (diffSumTime > elapsedTime) {
				omrtty_printf("PROCESS TIME LARGER THAN ELAPSED TIME\n");
				diffSumTime = (diffSumTime + elapsedTime)/2;
				elapsedTime = diffSumTime;
			}
			if (cpuTimeDiff > elapsedTime) {
				omrtty_printf("CPU TIME LARGER THAN ELAPSED TIME\n");
				cpuTimeDiff = elapsedTime;
			}
			if (cpuTimeDiff < diffSumTime) {
				omrtty_printf("CPU TIME SMALLER THAN PROCESS TIME\n");
				cpuTimeDiff = diffSumTime;
			}
			weighted_avg_interval = weighted_avg_interval * 0.9 + elapsedTime * 0.1;
			const float baseWeight = 0.25;
			double weight = elapsedTime / weighted_avg_interval * baseWeight;
			omrtty_printf("current system and user time: %llu\n", diffSumTime);
			omrtty_printf("current cpu time diff before adjusted: %llu\n", originalCpuTimeDiff);
			omrtty_printf("current cpu time diff after adjusted: %llu\n", cpuTimeDiff);
			omrtty_printf("new elapsed time: %llu\n", elapsedTime);
			omrtty_printf("WA time: %f\n", weighted_avg_interval);
			omrtty_printf("weight: %f\n", weight);
			omrtty_printf("current cpu utilization percentage: %.4f %%\n", (double)(cpuTimeDiff) / elapsedTime*100);
			weighted_avg_cpuUtil = MM_Math::weightedAverage(weighted_avg_cpuUtil, (double)(cpuTimeDiff) / elapsedTime, 1.0 - weight);
			omrtty_printf("current process utilization percentage: %.4f %%\n", (double)(diffSumTime) / (cpuTimeDiff)*100);
			weighted_avg_procUtil = MM_Math::weightedAverage(weighted_avg_procUtil, (double)(diffSumTime) / (elapsedTime), 1.0 - weight);
			omrtty_printf("current cpu idle percentage: %.4f %%\n", (double)(diffIdleTime) / (elapsedTime)*100);
			weighted_avg_cpuIdle = MM_Math::weightedAverage(weighted_avg_cpuIdle, (double)(diffIdleTime) / (elapsedTime), 1.0 - weight);
			omrtty_printf("average cpu utilization percentage: %.4f %%\n", weighted_avg_cpuUtil*100);
			omrtty_printf("average process utilization percentage over elapsedTime: %.4f %%\n", weighted_avg_procUtil*100);
			omrtty_printf("average process utilization percentage over WeightedAverage cpuTime: %.4f %%\n", weighted_avg_procUtil/weighted_avg_cpuUtil*100);
			omrtty_printf("average cpu idle percentage: %.4f %%\n\n\n", weighted_avg_cpuIdle*100);
		}
	}

	/* Expose CPU stats data to hooks */
	stats->_cpuStats = *this;
}
