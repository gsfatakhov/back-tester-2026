#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace cmf
{

template <typename T>
class BlockingQueue
{
  public:
    explicit BlockingQueue(std::size_t capacity)
        : capacity_{capacity}
    {
    }

    bool push(T value)
    {
        std::unique_lock lock{mutex_};
        notFull_.wait(lock, [&]() { return closed_ || queue_.size() < capacity_; });

        if (closed_)
        {
            return false;
        }

        queue_.push_back(std::move(value));
        notEmpty_.notify_one();
        return true;
    }

    bool pop(T& value)
    {
        std::unique_lock lock{mutex_};
        notEmpty_.wait(lock, [&]() { return closed_ || !queue_.empty(); });

        if (queue_.empty())
        {
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop_front();
        notFull_.notify_one();
        return true;
    }

    void close()
    {
        {
            std::lock_guard lock{mutex_};
            closed_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

  private:
    std::size_t capacity_;
    std::deque<T> queue_;
    std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    bool closed_ = false;
};

} // namespace cmf
