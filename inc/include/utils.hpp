#pragma once
#include <limits>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>

//#define NULL_PTR reinterpret_cast<void*>(0)
#define NULL_PTR \
    std::nullptr_t {}

typedef struct tagRect
{
    int left;

    int top;

    int right;

    int bottom;

} Rect;

struct SharedMutex
{
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic_bool is_locked{false};
    std::thread::id owner_thread_id{};

    void lock()
    {
        std::unique_lock<std::mutex> lock(mtx);
        while (is_locked && owner_thread_id != std::this_thread::get_id())
        {
            cv.wait(lock);
            if (owner_thread_id != std::thread::id() &&
                owner_thread_id != std::this_thread::get_id())
            {
                lock.unlock();
                lock.lock();
            }
        }
        is_locked = true;
        owner_thread_id = std::this_thread::get_id();
    }

    void unlock()
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (!is_locked || owner_thread_id != std::this_thread::get_id())
        {
            return;
        }
        is_locked = false;
        owner_thread_id = std::thread::id();
        cv.notify_one();
    }
};

template <typename T>
struct RangeQueue
{
    T *arr;
    size_t front = 0;
    size_t rear = 0;
    size_t capacity;

    RangeQueue(size_t capacity)
    {
        arr = new T[capacity];
        this->capacity = capacity;
    }

    ~RangeQueue()
    {
        delete[] arr;
    }

    void push(T val)
    {
        arr[rear] = val;
        rear = (rear + 1) % capacity;
        if (rear == front)
        {
            front = (front + 1) % capacity;
        }
    }

    void push(const T arr_vals[], size_t num_vals)
    {
        for (size_t i = 0; i < num_vals; i++)
        {
            push(arr_vals[i]);
        }
    }

    T pop()
    {
        T val = arr[front];
        front = (front + 1) % capacity;
        return val;
    }
};

template <typename T>
struct StayInRange
{
    T value;

    StayInRange()
    {
        value = static_cast<T>(0);
    }

    explicit StayInRange(T val)
    {
        if (val < static_cast<T>(0))
        {
            value = static_cast<T>(0);
        }
        else
        {
            value = val;
        }
    }

    StayInRange &operator+=(const StayInRange &other)
    {
        if (value < std::numeric_limits<T>::max() - other.value)
        {
            value += other.value;
        }
        else
        {
            value = std::numeric_limits<T>::max();
        }
        return *this;
    }

    StayInRange &operator-=(const StayInRange &other)
    {
        if (value > other.value)
        {
            value -= other.value;
        }
        else
        {
            value = static_cast<T>(0);
        }
        return *this;
    }

    StayInRange &operator++()
    {
        if (value < std::numeric_limits<T>::max())
        {
            ++value;
        }
        else
        {
            value = std::numeric_limits<T>::max();
        }
        return *this;
    }

    StayInRange operator++(int)
    {
        StayInRange temp(*this);
        ++(*this);
        return temp;
    }

    StayInRange &operator--()
    {
        if (value > static_cast<T>(0))
        {
            --value;
        }
        else
        {
            value = static_cast<T>(0);
        }
        return *this;
    }

    StayInRange operator--(int)
    {
        StayInRange temp(*this);
        --(*this);
        return temp;
    }

    operator bool() const
    {
        return value != static_cast<T>(0);
    }

    StayInRange &operator=(const T &val)
    {
        if (val < static_cast<T>(0))
        {
            value = static_cast<T>(0);
        }
        else
        {
            value = val;
        }
        return *this;
    }

    friend StayInRange operator+(StayInRange left, const StayInRange &right)
    {
        left += right;
        return left;
    }

    friend StayInRange operator-(StayInRange left, const StayInRange &right)
    {
        left -= right;
        return left;
    }
};

template <typename T>
class BlockingQueue
{
  public:
    void push(const T &value)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_queue.push(value);
        }
        m_condition.notify_one();
    }

    T front()
    {
        return m_queue.front();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_queue.empty())
        {
            m_condition.wait(lock);
        }
        T value = m_queue.front();
        m_queue.pop();
        return value;
    }

    bool peek(T *tRet, bool bNoRemove)
    {
        if (m_queue.empty())
            return false;

        *tRet = m_queue.front();
        if (!bNoRemove)
            m_queue.pop();
        return true;
    }

    bool empty()
    {
        return m_queue.empty();
    }

  private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_condition;
};
