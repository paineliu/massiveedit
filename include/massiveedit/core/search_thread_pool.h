#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

namespace massiveedit::core {

class SearchThreadPool {
 public:
  using JobId = std::uint64_t;
  using JobFn = std::function<void(std::atomic_bool&)>;

  explicit SearchThreadPool(std::size_t worker_count = 0);
  ~SearchThreadPool();

  SearchThreadPool(const SearchThreadPool&) = delete;
  SearchThreadPool& operator=(const SearchThreadPool&) = delete;

  JobId submit(JobFn fn);
  void cancel(JobId job_id);
  void cancelAll();

 private:
  struct QueuedJob {
    JobId job_id = 0;
    JobFn fn;
    std::shared_ptr<std::atomic_bool> cancel_flag;
  };

  void workerLoop();

  std::atomic<JobId> next_job_id_{1};
  std::vector<std::thread> workers_;
  std::queue<QueuedJob> queue_;
  std::unordered_map<JobId, std::shared_ptr<std::atomic_bool>> active_flags_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
};

}  // namespace massiveedit::core

