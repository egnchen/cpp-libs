#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
  explicit ThreadPool(size_t thread_num = std::thread::hardware_concurrency())
      : thread_num(thread_num) {
    // initialize threads
    threads.reserve(thread_num);
    for (unsigned i = 0; i < thread_num; i++) {
      threads.push_back(std::thread([this]() { slave(); }));
    }
  }
  void enqueue(std::function<void()> task) {
    std::unique_lock<std::mutex> lk(mtx);
    if (over) {
      puts("thread poll already over");
      exit(-1);
    }
    tasks.push(std::move(task));
    cond.notify_one();
  }
  void waitFinish() {
    std::unique_lock<std::mutex> lk(mtx);
    cond_done.wait(lk, [this]() { return tasks.empty() && num_busy_threads == 0; });
  }
  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lk(mtx);
      over = true;
      cond.notify_all();
    }
    for (auto &t : threads) {
      t.join();
    }
  }
  size_t getThreadNum() { return thread_num; }

private:
  size_t thread_num;
  std::vector<std::thread> threads;

  std::mutex mtx;
  std::condition_variable cond;
  std::condition_variable cond_done;

  int num_busy_threads;
  std::queue<std::function<void()>> tasks;
  bool over = false;

  void slave() {
    while (1) {
      std::unique_lock<std::mutex> lk(this->mtx);
      cond.wait(lk, [this]() { return this->over || !this->tasks.empty(); });
      if (this->over)
        break;
      auto tsk = this->tasks.front();
      this->tasks.pop();
      this->num_busy_threads++;
      lk.unlock();

      tsk();

      lk.lock();
      this->num_busy_threads--;
      cond_done.notify_one();
    }
  }
};
