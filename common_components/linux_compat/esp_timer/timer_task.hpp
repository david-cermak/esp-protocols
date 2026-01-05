/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>

typedef void (*cb_t)(void *arg);

class TimerTaskMock {
public:
    TimerTaskMock(cb_t cb): cb(cb), active(false), periodic(false), timeout_ms(0), stop_requested(false)
    {
        t = std::thread(run_static, this);
    }
    ~TimerTaskMock(void)
    {
        Stop();
        {
            std::lock_guard<std::mutex> lk(mtx);
            stop_requested = true;
        }
        cv.notify_one();
        if (t.joinable()) {
            t.join();
        }
    }

    void SetTimeoutPeriodic(uint32_t m)
    {
        Arm(m, true);
    }

    void SetTimeoutOnce(uint32_t m)
    {
        Arm(m, false);
    }

    void Stop(void)
    {
        {
            std::lock_guard<std::mutex> lk(mtx);
            active = false;
        }
        cv.notify_one();
    }

private:

    void Arm(uint32_t m, bool is_periodic)
    {
        {
            std::lock_guard<std::mutex> lk(mtx);
            timeout_ms = m;
            periodic = is_periodic;
            active = true;
        }
        cv.notify_one();
    }

    static void run_static(TimerTaskMock *timer)
    {
        timer->run();
    }

    void run(void)
    {
        std::unique_lock<std::mutex> lk(mtx);
        while (!stop_requested) {
            cv.wait(lk, [&] { return stop_requested || active; });
            if (stop_requested) {
                break;
            }

            const uint32_t local_timeout_ms = timeout_ms;
            const bool local_periodic = periodic;

            // Release the lock while waiting/calling the callback so Arm/Stop can proceed.
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(local_timeout_ms));

            // Re-check if still active before firing.
            lk.lock();
            if (!active || stop_requested) {
                continue;
            }
            lk.unlock();
            cb(nullptr);
            lk.lock();

            if (!local_periodic) {
                active = false;
            }
        }
    }

    cb_t cb;
    std::thread t;
    std::mutex mtx;
    std::condition_variable cv;
    bool active;
    bool periodic;
    uint32_t timeout_ms;
    bool stop_requested;

};
