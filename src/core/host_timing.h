// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common/common_types.h"
#include "common/spin_lock.h"
#include "common/thread.h"
#include "common/threadsafe_queue.h"
#include "common/wall_clock.h"

namespace Core::HostTiming {

/// A callback that may be scheduled for a particular core timing event.
using TimedCallback = std::function<void(u64 userdata, s64 cycles_late)>;

/// Contains the characteristics of a particular event.
struct EventType {
    EventType(TimedCallback&& callback, std::string&& name)
        : callback{std::move(callback)}, name{std::move(name)} {}

    /// The event's callback function.
    TimedCallback callback;
    /// A pointer to the name of the event.
    const std::string name;
};

/**
 * This is a system to schedule events into the emulated machine's future. Time is measured
 * in main CPU clock cycles.
 *
 * To schedule an event, you first have to register its type. This is where you pass in the
 * callback. You then schedule events using the type id you get back.
 *
 * The int cyclesLate that the callbacks get is how many cycles late it was.
 * So to schedule a new event on a regular basis:
 * inside callback:
 *   ScheduleEvent(periodInCycles - cyclesLate, callback, "whatever")
 */
class CoreTiming {
public:
    CoreTiming();
    ~CoreTiming();

    CoreTiming(const CoreTiming&) = delete;
    CoreTiming(CoreTiming&&) = delete;

    CoreTiming& operator=(const CoreTiming&) = delete;
    CoreTiming& operator=(CoreTiming&&) = delete;

    /// CoreTiming begins at the boundary of timing slice -1. An initial call to Advance() is
    /// required to end slice - 1 and start slice 0 before the first cycle of code is executed.
    void Initialize();

    /// Tears down all timing related functionality.
    void Shutdown();

    /// Pauses/Unpauses the execution of the timer thread.
    void Pause(bool is_paused);

    /// Pauses/Unpauses the execution of the timer thread and waits until paused.
    void SyncPause(bool is_paused);

    /// Checks if core timing is running.
    bool IsRunning();

    /// Checks if the timer thread has started.
    bool HasStarted() {
        return has_started;
    }

    /// Checks if there are any pending time events.
    bool HasPendingEvents();

    /// Schedules an event in core timing
    void ScheduleEvent(s64 ns_into_future, const std::shared_ptr<EventType>& event_type,
                       u64 userdata = 0);

    void UnscheduleEvent(const std::shared_ptr<EventType>& event_type, u64 userdata);

    /// We only permit one event of each type in the queue at a time.
    void RemoveEvent(const std::shared_ptr<EventType>& event_type);

    /// Returns current time in emulated CPU cycles
    u64 GetCPUTicks() const;

    /// Returns current time in emulated in Clock cycles
    u64 GetClockTicks() const;

    /// Returns current time in microseconds.
    std::chrono::microseconds GetGlobalTimeUs() const;

    /// Returns current time in nanoseconds.
    std::chrono::nanoseconds GetGlobalTimeNs() const;

private:
    struct Event;

    /// Clear all pending events. This should ONLY be done on exit.
    void ClearPendingEvents();

    static void ThreadEntry(CoreTiming& instance);
    void Advance();

    std::unique_ptr<Common::WallClock> clock;

    u64 global_timer = 0;

    std::chrono::nanoseconds start_point;

    // The queue is a min-heap using std::make_heap/push_heap/pop_heap.
    // We don't use std::priority_queue because we need to be able to serialize, unserialize and
    // erase arbitrary events (RemoveEvent()) regardless of the queue order. These aren't
    // accomodated by the standard adaptor class.
    std::vector<Event> event_queue;
    u64 event_fifo_id = 0;

    std::shared_ptr<EventType> ev_lost;
    Common::Event event{};
    Common::SpinLock basic_lock{};
    std::unique_ptr<std::thread> timer_thread;
    std::atomic<bool> paused{};
    std::atomic<bool> paused_set{};
    std::atomic<bool> wait_set{};
    std::atomic<bool> shutting_down{};
    std::atomic<bool> has_started{};
};

/// Creates a core timing event with the given name and callback.
///
/// @param name     The name of the core timing event to create.
/// @param callback The callback to execute for the event.
///
/// @returns An EventType instance representing the created event.
///
std::shared_ptr<EventType> CreateEvent(std::string name, TimedCallback&& callback);

} // namespace Core::HostTiming
