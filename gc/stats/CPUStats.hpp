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

#if !defined(CPUSTATS_HPP_)
#define CPUSTATS_HPP_

#include "Base.hpp"

class  MM_CollectionStatistics;
class  MM_EnvironmentBase;

class MM_CPUStats : public MM_Base {
public:
	int64_t prev_userTime;
	int64_t prev_systemTime;
	int64_t prev_cpuIdleTime;
	int64_t prev_cpuUserTime;
	int64_t prev_cpuSystemTime;
	int64_t prev_cpuNiceTime;
	int64_t prev_cpuIrqTime;
	int64_t prev_cpuSoftirqTime;
	int64_t prev_cpuTime;
	int64_t prev_elapsedTimeNew;
	int64_t prev_elapsedTime;
	double weighted_avg_interval;
	double weighted_avg_cpuUtil;
	double weighted_avg_procUtil;
	double weighted_avg_cpuIdle;
	bool ifCpuDiff;

	MM_CPUStats() :
		MM_Base()
		,prev_userTime(0)
		,prev_systemTime(0)
		,prev_cpuIdleTime(0)
		,prev_cpuUserTime(0)
		,prev_cpuSystemTime(0)
		,prev_cpuNiceTime(0)
		,prev_cpuIrqTime(0)
		,prev_cpuSoftirqTime(0)
		,prev_cpuTime(0)
		,prev_elapsedTimeNew(0)
		,prev_elapsedTime(0)
		,weighted_avg_interval(0)
		,weighted_avg_cpuUtil(0.0)
		,weighted_avg_procUtil(0.0)
		,weighted_avg_cpuIdle(0.0)
	{}

	void recordProcessAndCpuUtilization(MM_EnvironmentBase *env, MM_CollectionStatistics *stats);
	void calculateProcessAndCpuUtilizationDelta(MM_EnvironmentBase *env, MM_CollectionStatistics *stats);
};

#endif /* CPUSTATS_HPP_ */


