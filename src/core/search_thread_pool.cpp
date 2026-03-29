#include "massiveedit/core/search_thread_pool.h"

#include <algorithm>

namespace massiveedit::core {

SearchThreadPool::SearchThreadPool(std::size_t worker_count) {
  if (worker_count == 0) {
    worker_count = std::max<std::size_t>(2, std::thread::hardware_concurrency());
  }
  workers_.reserve(worker_count);
  for (std::size_t i = 0; i < worker_count; ++i) {
    workers_.emplace_back([this]() { workerLoop(); });
  }
}

SearchThreadPool::~SearchThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    for (auto& [job_id, flag] : active_flags_) {
      (void)job_id;
      flag->store(true);
    }
  }
  cv_.notify_all();

  for (std::thread& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

SearchThreadPool::JobId SearchThreadPool::submit(JobFn fn) {
  if (!fn) {
    return 0;
  }

  const JobId job_id = next_job_id_.fetch_add(1);
  auto flag = std::make_shared<std::atomic_bool>(false);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) {
      return 0;
    }
    active_flags_[job_id] = flag;
    queue_.push(QueuedJob{
        .job_id = job_id,
        .fn = std::move(fn),
        .cancel_flag = std::move(flag),
    });
  }
  cv_.notify_one();
  return job_id;
}

void SearchThreadPool::cancel(JobId job_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = active_flags_.find(job_id);
  if (it != active_flags_.end()) {
    it->second->store(true);
  }
}

void SearchThreadPool::cancelAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [job_id, flag] : active_flags_) {
    (void)job_id;
    flag->store(true);
  }
}

void SearchThreadPool::workerLoop() {
  while (true) {
    QueuedJob job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
      if (stop_ && queue_.empty()) {
        return;
      }
      job = std::move(queue_.front());
      queue_.pop();
    }

    if (job.fn && !job.cancel_flag->load()) {
      job.fn(*job.cancel_flag);
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_flags_.erase(job.job_id);
    }
  }
}

}  // namespace massiveedit::core

