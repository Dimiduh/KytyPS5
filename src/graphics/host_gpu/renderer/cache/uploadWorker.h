#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_UPLOADWORKER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_UPLOADWORKER_H_

#include "common/common.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace Libs::Graphics {

class RegionManager;
class Buffer;

// The CPU half of buffer uploads on a thread of its own. A synchronization of guest stores used
// to write-protect the stored pages again and copy them into the staging memory on the GPU
// thread, which then recorded the GPU copies: at Saros' resume spot about 1,500 protection
// requests and 16 MiB of copies a frame, a fifth of that thread's time. The GPU thread now only
// takes the pages (clears their modified state), reserves the staging memory and records the
// GPU copies; this worker protects the pages and fills the staging memory, in the order the jobs
// were pushed.
//
// What keeps that correct:
// - A job protects its pages before it copies them, as the GPU thread did: a guest store before
//   the protection is in the copy, one after it faults and marks the page again.
// - A submission is passed to the queue only once the jobs pushed before it are done
//   (CommandScheduler::Submit waits for their sequence), so the GPU never runs a copy from
//   staging memory that is not filled yet, and nothing the GPU does afterwards (labels, readbacks,
//   publications) can come before the bytes it depends on.
// - Before a store of the emulated command processor (a later command's, made on the GPU thread
//   at translation time) into pages a pending job still has to copy, the storing thread copies
//   those pages itself (CaptureRange, from BufferCache::InvalidateMemory) and the worker leaves
//   them out, so an earlier draw's upload never carries a later command's bytes. The same before
//   an unmap of such pages. The pages are invalidated right after, so a guest store the capture
//   came too early for reaches the buffer with their next upload.
//
// The idea is the upload worker of chenxiao07's KytyPS5 fork (src/local/async-upload.h).
class UploadWorker final {
public:
	struct Copy {
		uint64_t guest       = 0; // Guest address of the bytes.
		uint64_t size        = 0;
		uint8_t* destination = nullptr; // Reserved staging memory.
	};
	// Staging memory to flush once its copies are done (memory that is not host-coherent).
	struct Flush {
		Buffer*  buffer = nullptr;
		uint64_t offset = 0;
		uint64_t size   = 0;
	};
	struct Job {
		std::vector<RegionManager*> regions; // Protected first (MemoryTracker::ProtectTaken).
		std::vector<Copy>           copies;  // In ascending guest address order per buffer.
		std::vector<Flush>          flushes;
	};

	UploadWorker();
	~UploadWorker();
	KYTY_CLASS_NO_COPY(UploadWorker);

	// GPU thread: queues the job; the sequence covers it and every job pushed before.
	uint64_t Push(Job&& job);
	// The sequence of the last job pushed (0: none yet).
	[[nodiscard]] uint64_t Pushed() const noexcept {
		return m_pushed.load(std::memory_order_acquire);
	}
	// Any thread: returns once the jobs up to `sequence` are done.
	void Wait(uint64_t sequence);
	// Copies the bytes of [vaddr, vaddr + size) that pending jobs still have to copy into their
	// staging memory now, from the calling thread; the worker then leaves them out. For a range
	// that is about to be written (or unmapped) by something the jobs were recorded before.
	void CaptureRange(uint64_t vaddr, uint64_t size);

private:
	struct Queued {
		Job      job;
		uint64_t sequence = 0;
		// The lowest and one past the highest guest address the job copies.
		uint64_t begin = UINT64_MAX;
		uint64_t end   = 0;
		// Under m_copy_mutex: the copies the worker has made, and the guest ranges CaptureRange
		// copied for it.
		size_t                                     copied = 0;
		std::vector<std::pair<uint64_t, uint64_t>> captured; // [begin, end)
	};

	void        Run(const std::stop_token& stop);
	static void CopyGuest(uint64_t guest, uint8_t* destination, uint64_t size);

	std::mutex                  m_mutex;
	std::condition_variable_any m_available;
	std::deque<Queued>          m_queue;  // The front is the job being run, if any runs.
	// Held for each copy of the running job and by CaptureRange (after m_mutex there).
	std::mutex                  m_copy_mutex;
	std::atomic<uint64_t>       m_pushed {0};
	std::atomic<uint64_t>       m_done {0};
	std::jthread                m_thread;
};

} // namespace Libs::Graphics

#endif /* EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_UPLOADWORKER_H_ */
