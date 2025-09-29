/*******************************************************************************
 * Copyright IBM Corp. and others 2016
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

#include "ModronAssertions.h"

#include "EnvironmentBase.hpp"
#include "GCExtensionsBase.hpp"
#include "ParallelDispatcher.hpp"
#include "ScavengerStats.hpp"

#include "ScavengerCopyScanRatio.hpp"

void
MM_ScavengerCopyScanRatio::reset(MM_EnvironmentBase* env, bool resetHistory)
{
	_accumulatingSamples = 0;
	_accumulatedSamples = SCAVENGER_COUNTER_DEFAULT_ACCUMULATOR;
	_threadCount = env->getExtensions()->dispatcher->activeThreadCount();
	if (resetHistory) {
		OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());
		_resetTimestamp = omrtime_hires_clock();
		_scalingUpdateCount = 0;
		_overflowCount = 0;
		_historyFoldingFactor = 1;
		_historyTableIndex = 0;
		_majorUpdateThreadEnv = 0;
		memset(_historyTable, 0, SCAVENGER_UPDATE_HISTORY_SIZE * sizeof(UpdateHistory));
	}

//	OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());
//	omrtty_printf("reset envID %zu _historyTableIndex %zu _majorUpdateThreadEnvId %zu\n", env->getEnvironmentId(), _historyTableIndex, (_majorUpdateThreadEnv ? ((MM_EnvironmentBase *)_majorUpdateThreadEnv)->getEnvironmentId() : 0));

}

uintptr_t
MM_ScavengerCopyScanRatio::record(MM_EnvironmentBase* env, uintptr_t nonEmptyScanLists, uintptr_t cachesQueued)
{
	OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());
	//omrtty_printf("record 1 envID %zu _historyTableIndex %zu\n", env->getEnvironmentId(), _historyTableIndex);

//	if ((SCAVENGER_UPDATE_HISTORY_SIZE == _historyTableIndex) && (0 == rand() % 10)) {
//		omrthread_sleep(1);
//	}

	if (SCAVENGER_UPDATE_HISTORY_SIZE <= _historyTableIndex) {
		Assert_MM_true(SCAVENGER_UPDATE_HISTORY_SIZE == _historyTableIndex);
		/* table full -- sum adjacent pairs of records and shift results to top half of table */
		UpdateHistory *head = &(_historyTable[0]);
		UpdateHistory *tail = &(_historyTable[1]);
		UpdateHistory *stop = &(_historyTable[SCAVENGER_UPDATE_HISTORY_SIZE]);
		while (tail < stop) {
			UpdateHistory *prev = tail - 1;
			prev->waits += tail->waits;
			prev->copied += tail->copied;
			prev->scanned += tail->scanned;
			prev->updates += tail->updates;
			prev->threads += tail->threads;
			prev->majorUpdates += tail->majorUpdates;
			prev->lists += tail->lists;
			prev->caches += tail->caches;
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
			prev->readObjectBarrierUpdate = tail->readObjectBarrierUpdate;
			prev->readObjectBarrierCopy = tail->readObjectBarrierCopy;
#endif /* OMR_GC_CONCURRENT_SCAVENGER */
			prev->time = tail->time;
			if (prev > head) {
				memcpy(head, prev, sizeof(UpdateHistory));
			}
			head += 1;
			tail += 2;
		}
		_historyFoldingFactor <<= 1;
		_historyTableIndex = SCAVENGER_UPDATE_HISTORY_SIZE >> 1;
		//omrtty_printf("record 2 envID %zu _historyTableIndex %zu\n", env->getEnvironmentId(), _historyTableIndex);

		uintptr_t zeroBytes = (SCAVENGER_UPDATE_HISTORY_SIZE >> 1) * sizeof(UpdateHistory);
		memset(&(_historyTable[_historyTableIndex]), 0, zeroBytes);
	}

	/* update record at current table index from fields in current acculumator */
	uintptr_t threadCount = env->getExtensions()->dispatcher->activeThreadCount();
	UpdateHistory *historyRecord = &(_historyTable[_historyTableIndex]);
	uint64_t accumulatedSamples = _accumulatedSamples;
	historyRecord->waits += waits(accumulatedSamples);
	historyRecord->copied += copied(accumulatedSamples);
	historyRecord->scanned += scanned(accumulatedSamples);
	historyRecord->updates += updates(accumulatedSamples);
	historyRecord->threads += threadCount;
	historyRecord->majorUpdates += 1;
	historyRecord->lists += nonEmptyScanLists;
	historyRecord->caches += cachesQueued;
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
	/* record current read barries values (we do not want to sum them up and average, we want last value) */
	MM_GCExtensionsBase *ext = env->getExtensions();
	historyRecord->readObjectBarrierUpdate = ext->scavengerStats._readObjectBarrierUpdate;
	historyRecord->readObjectBarrierCopy = ext->scavengerStats._readObjectBarrierCopy;
#endif /* OMR_GC_CONCURRENT_SCAVENGER */
	historyRecord->time = omrtime_hires_clock();

	/* advance table index if current record is maxed out */
	if (historyRecord->updates >= (_historyFoldingFactor * SCAVENGER_THREAD_UPDATES_PER_MAJOR_UPDATE)) {
		_historyTableIndex += 1;
//		//uintptr_t index = _historyTableIndex;
//		omrtty_printf("record 3 envID %zu _historyTableIndex %zu\n", env->getEnvironmentId(), _historyTableIndex);
//		if (0 == rand() % 10) {
//			omrthread_sleep(1);
//			//omrtty_printf("record 4 envID %zu _historyTableIndex %zu\n", env->getEnvironmentId(), _historyTableIndex);
//			//Assert_MM_true(index == _historyTableIndex);
//		}
	}
	return threadCount;
}

uint64_t
MM_ScavengerCopyScanRatio::getSpannedMicros(MM_EnvironmentBase* env, UpdateHistory *historyRecord)
{
	OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());
	uint64_t start = (historyRecord == _historyTable) ? _resetTimestamp : (historyRecord - 1)->time;
	return omrtime_hires_delta(start, historyRecord->time, OMRPORT_TIME_DELTA_IN_MICROSECONDS);
}

void
MM_ScavengerCopyScanRatio::failedUpdate(MM_EnvironmentBase* env, uint64_t copied, uint64_t scanned)
{
	Assert_GC_true_with_message2(env, copied <= scanned, "MM_ScavengerCopyScanRatio::getScalingFactor(): copied (=%llu) exceeds scanned (=%llu) -- non-atomic 64-bit read\n", copied, scanned);
}

void
MM_ScavengerCopyScanRatio::majorUpdate(MM_EnvironmentBase* env, uint64_t updateResult, uintptr_t nonEmptyScanLists, uintptr_t cachesQueued)
{
//	OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());
	if (0 == (SCAVENGER_COUNTER_OVERFLOW & updateResult)) {
		/* no overflow so latch updateResult into _accumulatedSamples and record the update */
		MM_AtomicOperations::setU64(&_accumulatedSamples, updateResult);
		_scalingUpdateCount += 1;
//		omrtty_printf("majorUpdate envID %zu _historyTableIndex %zu _majorUpdateThreadEnvId %zu\n", env->getEnvironmentId(), _historyTableIndex, (_majorUpdateThreadEnv ? ((MM_EnvironmentBase *)_majorUpdateThreadEnv)->getEnvironmentId() : 0));
		_threadCount = record(env, nonEmptyScanLists, cachesQueued);
	} else {
		/* one or more counters overflowed so discard this update */
		_overflowCount += 1;
	}
	/* Ensure updates are visible to other threads that go on to do a major update */
	MM_AtomicOperations::storeSync();

	_majorUpdateThreadEnv = 0;
//	omrtty_printf("majorUpdate 2 envID %zu _historyTableIndex %zu _majorUpdateThreadEnvId %zu\n", env->getEnvironmentId(), _historyTableIndex, (_majorUpdateThreadEnv ? ((MM_EnvironmentBase *)_majorUpdateThreadEnv)->getEnvironmentId() : 0));
}
/**
 * Flush major update which may be discarded: accumulator may not of reached threshold to perform a major update.
 * Progress stats in the accumulator will be discarded if threshold is not reached and cycle completes.
 * Cached Scan queue metrics are used for flushing, a snapshot of these metrics is taken during minor update which would
 * of triggered a major update had it reached the threshold.
 *
 * This is not thread safe and should be called at the end of scan completion routine by the last blocking thread
 */
void
MM_ScavengerCopyScanRatio::flush(MM_EnvironmentBase* env, uintptr_t nonEmptyScanLists, uintptr_t cachesQueued)
{
	uint64_t updateResult = _accumulatingSamples;
	_accumulatingSamples = 0;
	if (0 != updateResult) {
		if (0 == (SCAVENGER_COUNTER_OVERFLOW & updateResult)) {
			/* no overflow so latch updateResult into _accumulatedSamples and record the update */
			MM_AtomicOperations::setU64(&_accumulatedSamples, updateResult);
			_scalingUpdateCount += 1;
//			OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());
//			omrtty_printf("flush envID %zu _historyTableIndex %zu _majorUpdateThreadEnvId %zu\n", env->getEnvironmentId(), _historyTableIndex, (_majorUpdateThreadEnv ? ((MM_EnvironmentBase *)_majorUpdateThreadEnv)->getEnvironmentId() : 0));
			_threadCount = record(env, nonEmptyScanLists, cachesQueued);
		} else {
			_overflowCount += 1;
		}
	}
	reset(env, false);
}


uint64_t
MM_ScavengerCopyScanRatio::update(MM_EnvironmentBase* env, uint64_t *slotsScanned, uint64_t *slotsCopied, uint64_t waitingCount, uintptr_t *copyScanUpdates, bool flush)
{
	if (SCAVENGER_SLOTS_SCANNED_PER_THREAD_UPDATE <= *slotsScanned || flush) {
		uint64_t scannedCount =  *slotsScanned;
		uint64_t copiedCount =  *slotsCopied;
		*slotsScanned = *slotsCopied = 0;

		/* this thread may have scanned a long array segment resulting in scanned/copied slot counts that must be scaled down to avoid overflow in the accumulator */
		while ((SCAVENGER_SLOTS_SCANNED_PER_THREAD_UPDATE << 1) < scannedCount) {
			/* scale scanned and copied counts identically */
			scannedCount >>= 1;
			copiedCount >>= 1;
		}

		/* add this thread's samples to the accumulating register */
		uint64_t updateSample = sample(scannedCount, copiedCount, waitingCount);
		uint64_t updateResult = atomicAddThreadUpdate(updateSample);
		uintptr_t updateCount = updates(updateResult);
		(*copyScanUpdates)++;

		/* this next section includes a critical region for the thread that increments the update counter to threshold */
		if (SCAVENGER_THREAD_UPDATES_PER_MAJOR_UPDATE == updateCount) {
			/* make sure that every other thread knows that a specific thread is performing the major update. if
			 * this thread gets timesliced in the section below while other free-running threads work up another major
			 * update, that update will be discarded */

			if  (0 == MM_AtomicOperations::lockCompareExchange(&_majorUpdateThreadEnv, 0, (uintptr_t)env)) {
//				OMRPORT_ACCESS_FROM_OMRPORT(env->getPortLibrary());
//				omrtty_printf("update envID %zu _historyTableIndex %zu _majorUpdateThreadEnvId %zu\n", env->getEnvironmentId(), _historyTableIndex, ((MM_EnvironmentBase *)_majorUpdateThreadEnv)->getEnvironmentId());
				return updateResult;
			}
		}
	}

	return 0;
}

