#include "graphics/host_gpu/renderer/cache/uploadWorker.h"

#include "common/assert.h"
#include "common/liveControl.h"
#include "common/profiler.h"
#include "common/threads.h"
#include "graphics/host_gpu/memoryTracker.h"
#include "graphics/host_gpu/renderer/cache/streamBuffer.h"
#include "kernel/memory.h"

#include <algorithm>
#include <chrono>

namespace Libs::Graphics {

namespace {

// Per frame in `measure`: jobs, the waits of the submission thread for them and the
// microseconds those took, and the ranges another thread copied for a job before it wrote them.
Common::LiveControl::Counter g_upload_jobs("upload_jobs");
Common::LiveControl::Counter g_upload_waits("upload_waits");
Common::LiveControl::Counter g_upload_wait_us("upload_wait_us");
Common::LiveControl::Counter g_upload_captures("upload_captures");

} // namespace

UploadWorker::UploadWorker(): m_thread([this](std::stop_token stop) { Run(stop); }) {}

UploadWorker::~UploadWorker() {
	Wait(Pushed());
	m_thread.request_stop();
	m_available.notify_all();
	if (m_thread.joinable()) {
		m_thread.join();
	}
}

uint64_t UploadWorker::Push(Job&& job) {
	Queued queued;
	for (const auto& copy: job.copies) {
		queued.begin = std::min(queued.begin, copy.guest);
		queued.end   = std::max(queued.end, copy.guest + copy.size);
	}
	queued.job = std::move(job);
	uint64_t sequence = 0;
	{
		std::lock_guard lock(m_mutex);
		sequence        = m_pushed.load(std::memory_order_relaxed) + 1u;
		queued.sequence = sequence;
		m_queue.push_back(std::move(queued));
		m_pushed.store(sequence, std::memory_order_release);
	}
	m_available.notify_one();
	g_upload_jobs.Add();
	return sequence;
}

void UploadWorker::Wait(uint64_t sequence) {
	auto done = m_done.load(std::memory_order_acquire);
	if (done >= sequence) {
		return;
	}
	g_upload_waits.Add();
	const auto start = std::chrono::steady_clock::now();
	for (; done < sequence; done = m_done.load(std::memory_order_acquire)) {
		m_done.wait(done, std::memory_order_acquire);
	}
	g_upload_wait_us.Add(static_cast<uint64_t>(
	    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
	                                                           start)
	        .count()));
}

void UploadWorker::CopyGuest(uint64_t guest, uint8_t* destination, uint64_t size) {
	if (!LibKernel::Memory::CopySparseMemory(guest, destination, size)) {
		EXIT("UploadWorker: failed to read guest buffer memory at 0x%016" PRIx64 " size 0x%" PRIx64
		     "\n",
		     guest, size);
	}
}

void UploadWorker::CaptureRange(uint64_t vaddr, uint64_t size) {
	if (m_done.load(std::memory_order_acquire) == m_pushed.load(std::memory_order_acquire)) {
		return;
	}
	const auto      finish = vaddr + size;
	std::lock_guard lock(m_mutex);
	for (auto& queued: m_queue) {
		if (queued.begin >= finish || vaddr >= queued.end) {
			continue;
		}
		std::lock_guard copy_lock(m_copy_mutex);
		// A job's copies ascend per buffer, not across buffers: all that the worker has not made.
		for (size_t index = queued.copied; index < queued.job.copies.size(); index++) {
			const auto& copy  = queued.job.copies[index];
			const auto  begin = std::max(copy.guest, vaddr);
			const auto  end   = std::min(copy.guest + copy.size, finish);
			if (begin >= end) {
				continue;
			}
			CopyGuest(begin, copy.destination + (begin - copy.guest), end - begin);
			queued.captured.emplace_back(begin, end);
			g_upload_captures.Add();
		}
	}
}

void UploadWorker::Run(const std::stop_token& stop) {
	KYTY_PROFILER_THREAD("Thread_GpuUpload");
	Common::LiveControl::RegisterThread("upload");
	// Submissions wait for it, and it holds the tracking regions' locks while it protects.
	Common::Thread::RaiseCurrentThreadPriority();
	for (;;) {
		const Queued* queued = nullptr;
		{
			std::unique_lock lock(m_mutex);
			m_available.wait(lock, stop, [this] { return !m_queue.empty(); });
			if (m_queue.empty()) {
				return;
			}
			// The job stays at the front while it runs, for WaitForRange. A deque keeps the
			// element in place while later jobs are pushed behind it.
			queued = &m_queue.front();
		}
		for (auto* region: queued->job.regions) {
			MemoryTracker::ProtectTaken(region);
		}
		auto* running = const_cast<Queued*>(queued);
		for (size_t index = 0; index < running->job.copies.size(); index++) {
			const auto&     copy = running->job.copies[index];
			std::lock_guard copy_lock(m_copy_mutex);
			// The parts another thread copied before it wrote them are left as it copied them
			// (CaptureRange); they are few, and none overlaps another.
			auto       begin  = copy.guest;
			const auto finish = copy.guest + copy.size;
			while (begin < finish) {
				auto next_begin = finish;
				auto next_end   = finish;
				for (const auto& [captured_begin, captured_end]: running->captured) {
					if (captured_end > begin && captured_begin < next_begin) {
						next_begin = std::max(captured_begin, begin);
						next_end   = captured_end;
					}
				}
				if (next_begin > begin) {
					CopyGuest(begin, copy.destination + (begin - copy.guest), next_begin - begin);
				}
				begin = std::min(next_end, finish);
			}
			running->copied = index + 1u;
		}
		for (const auto& flush: queued->job.flushes) {
			flush.buffer->Flush(flush.offset, flush.size);
		}
		const auto sequence = queued->sequence;
		{
			std::lock_guard lock(m_mutex);
			m_queue.pop_front();
		}
		m_done.store(sequence, std::memory_order_release);
		m_done.notify_all();
	}
}

} // namespace Libs::Graphics
