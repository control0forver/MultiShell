#pragma once
#include <ratio>
#include <chrono>
#include <thread>

template <std::intmax_t FPS>
class frame_rater
{
  public:
    frame_rater() :                         // initialize the object keeping the pace
                    time_between_frames{1}, // std::ratio<1, FPS> seconds
                    tp{std::chrono::steady_clock::now()}
    {
    }

    void sleep()
    {
        // add to time point
        tp += time_between_frames;

        // and sleep until that time point
        std::this_thread::sleep_until(tp);
    }

  private:
    // a duration with a length of 1/FPS seconds
    std::chrono::duration<double, std::ratio<1, FPS>> time_between_frames;

    // the time point we'll add to in every loop
    std::chrono::time_point<std::chrono::steady_clock, decltype(time_between_frames)> tp;
};

class frame_counter
{
    int frameCount = 0;
    std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point curTime = std::chrono::system_clock::now();

  public:
    bool noUpdateDelay = false;
    double updateDelay = 0.8;
    double fps = 0.0;

    void count()
    {
        using namespace std;
        using namespace std::chrono;

        curTime = system_clock::now();

        auto duration = duration_cast<microseconds>(curTime - lastTime);
        double duration_s = double(duration.count()) * microseconds::period::num / microseconds::period::den;

        if (duration_s > updateDelay || noUpdateDelay)
        {
            fps = frameCount / duration_s;
            frameCount = 0;
            lastTime = curTime;
        }

        ++frameCount;
    }
};
