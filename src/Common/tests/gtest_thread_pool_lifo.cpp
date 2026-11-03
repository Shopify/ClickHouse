#include <Common/CurrentMetrics.h>
#include <Common/ThreadPool.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace CurrentMetrics
{
    extern const Metric LocalThread;
    extern const Metric LocalThreadActive;
    extern const Metric LocalThreadScheduled;
}

TEST(ThreadPool, LIFOStartsManyBlockingJobs)
{
    constexpr size_t jobs = 64;
    ThreadPool pool(CurrentMetrics::LocalThread, CurrentMetrics::LocalThreadActive, CurrentMetrics::LocalThreadScheduled, jobs, jobs, jobs);

    std::mutex mutex;
    std::condition_variable cv;
    size_t started = 0;
    bool release = false;

    for (size_t i = 0; i < jobs; ++i)
    {
        pool.scheduleOrThrowOnError([&]
        {
            std::unique_lock lock(mutex);
            ++started;
            cv.notify_all();
            cv.wait(lock, [&]
            {
                return release;
            });
        });
    }

    bool all_started = false;
    {
        std::unique_lock lock(mutex);
        all_started = cv.wait_for(lock, std::chrono::seconds(30), [&]
        {
            return started == jobs;
        });
        release = true;
    }
    cv.notify_all();

    pool.wait();

    EXPECT_TRUE(all_started) << "Only " << started << " jobs started";
}

TEST(ThreadPool, LIFONotifyWithThreadSelfRemoval)
{
    constexpr size_t jobs = 128;

    for (size_t iteration = 0; iteration < 100; ++iteration)
    {
        ThreadPool pool(CurrentMetrics::LocalThread, CurrentMetrics::LocalThreadActive, CurrentMetrics::LocalThreadScheduled, 8, 0, jobs);

        std::atomic<size_t> counter = 0;
        for (size_t i = 0; i < jobs; ++i)
        {
            pool.scheduleOrThrowOnError([&]
            {
                ++counter;
            });
        }

        pool.wait();
        EXPECT_EQ(counter.load(), jobs);
    }
}

/// `scheduleImpl` selects an idle thread under the pool mutex but notifies its CV after
/// releasing the mutex. By then the selected thread may have woken up, decided to exit (with or
/// without running a job) and been destroyed, so the notification must not touch the thread
/// object. With `max_free_threads = 1` at most one worker stays parked, and a worker that
/// finds a spare one already parked exits. Several schedulers that each keep one short job in
/// flight make workers park, get selected and exit all the time, so this interleaving is
/// likely, especially when the CPUs are oversubscribed. A use-after-free here is reported by
/// ASan, and a lost wake-up makes a scheduler time out.
TEST(ThreadPool, LIFOSelectedThreadExitsWhileBeingNotified)
{
    constexpr size_t iterations = 3;
    constexpr size_t schedulers = 8;
    constexpr size_t jobs_per_scheduler = 5000;

    /// One per scheduler. Declared before the pool, so it outlives every job.
    struct Completion
    {
        std::mutex mutex;
        std::condition_variable cv;
        size_t completed = 0;
    };

    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        std::vector<Completion> completions(schedulers);
        std::atomic<size_t> timed_out = 0;

        /// Workers of this pool are counted in `LocalThread` from entering to leaving their loop.
        /// No other pool uses that metric during the test, so a drop below a value seen earlier
        /// means that a worker left its loop while the pool is alive, i.e. removed itself.
        std::atomic<CurrentMetrics::Value> peak_workers = 0;
        std::atomic<size_t> worker_exits_seen = 0;
        auto observe_workers = [&]
        {
            const auto workers = CurrentMetrics::get(CurrentMetrics::LocalThread);
            auto peak = peak_workers.load();
            while (workers > peak && !peak_workers.compare_exchange_weak(peak, workers))
            {
            }
            if (workers < peak)
                ++worker_exits_seen;
        };

        ThreadPool pool(CurrentMetrics::LocalThread, CurrentMetrics::LocalThreadActive, CurrentMetrics::LocalThreadScheduled, 4, 1, 0);

        /// Start the first worker from this thread, so that the implicit initialization of
        /// `GlobalThreadPool` does not race between the scheduler threads.
        pool.scheduleOrThrowOnError([] {});
        pool.wait();

        std::vector<std::thread> threads;
        threads.reserve(schedulers);
        for (size_t i = 0; i < schedulers; ++i)
        {
            threads.emplace_back([&, i]
            {
                Completion & completion = completions[i];
                for (size_t j = 1; j <= jobs_per_scheduler; ++j)
                {
                    pool.scheduleOrThrowOnError([&completion]
                    {
                        std::lock_guard lock(completion.mutex);
                        ++completion.completed;
                        completion.cv.notify_one();
                    });

                    std::unique_lock lock(completion.mutex);
                    if (!completion.cv.wait_for(lock, std::chrono::seconds(30), [&] { return completion.completed == j; }))
                    {
                        ++timed_out;
                        return;
                    }
                    lock.unlock();
                    observe_workers();
                }
            });
        }
        for (auto & thread : threads)
            thread.join();

        /// After a lost wake-up `pool.wait` could block forever. Fail instead; the destructor
        /// of the pool wakes all idle threads, so the stuck job still runs and it returns.
        ASSERT_EQ(timed_out.load(), 0) << "A scheduled job was not picked up, a wake-up was lost";

        pool.wait();
        for (auto & completion : completions)
            EXPECT_EQ(completion.completed, jobs_per_scheduler);

        /// The race needs workers that exit while the pool is in use. This checks that some do,
        /// not that one exited exactly between the selection and the notification.
        EXPECT_GT(worker_exits_seen.load(), 0u) << "No worker was seen removing itself from the pool";
    }
}

/// Verify the LIFO contract directly. Two properties are checked:
///  - while one worker is busy, a probe job runs on the single idle worker;
///  - once both workers are idle, a sequence of non-overlapping jobs
///    concentrates on a single worker instead of spreading across both.
/// The second property is the deterministic, observable consequence of LIFO
/// wake-up (a FIFO or arbitrary policy would alternate between the two threads).
TEST(ThreadPool, LIFOSchedulesOnMostRecentlyIdleThread)
{
    ThreadPool pool(CurrentMetrics::LocalThread, CurrentMetrics::LocalThreadActive, CurrentMetrics::LocalThreadScheduled, 2, 2, 2);

    std::mutex mutex;
    std::condition_variable cv;
    bool release_a = false;
    bool release_b = false;
    bool a_started = false;
    bool b_started = false;
    std::thread::id thread_a;
    std::thread::id thread_b;

    /// Start two blocking jobs that occupy both worker threads.
    pool.scheduleOrThrowOnError([&]
    {
        std::unique_lock lock(mutex);
        thread_a = std::this_thread::get_id();
        a_started = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release_a; });
    });

    pool.scheduleOrThrowOnError([&]
    {
        std::unique_lock lock(mutex);
        thread_b = std::this_thread::get_id();
        b_started = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release_b; });
    });

    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return a_started && b_started; });
    }

    ASSERT_NE(thread_a, thread_b);

    /// Release the first job. Its worker becomes the only idle thread.
    {
        std::lock_guard lock(mutex);
        release_a = true;
    }
    cv.notify_all();

    /// Probe with a quick job. With job B still running, only `thread_a` can
    /// run the probe, regardless of whether it has reached the idle stack at
    /// the moment of scheduling: if not yet idle, the probe is queued and
    /// `thread_a` picks it up when it returns to the worker loop.
    std::thread::id probe_x_thread;
    bool probe_x_done = false;
    pool.scheduleOrThrowOnError([&]
    {
        std::unique_lock lock(mutex);
        probe_x_thread = std::this_thread::get_id();
        probe_x_done = true;
        cv.notify_all();
    });
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return probe_x_done; });
    }
    EXPECT_EQ(probe_x_thread, thread_a);

    /// Release the second job so both workers become idle.
    {
        std::lock_guard lock(mutex);
        release_b = true;
    }
    cv.notify_all();

    /// `pool.wait` returns only after the worker that completed the last job
    /// has pushed itself onto the idle stack: the worker holds `pool.mutex`
    /// continuously from notifying `job_finished` through the idle push, and
    /// `pool.wait` reacquires that mutex before returning. So once `pool.wait`
    /// returns, both workers are parked in the idle stack.
    pool.wait();

    /// Now both threads are idle. We deliberately do not assert *which* of the
    /// two is on top of the LIFO stack: the relative order in which the two
    /// workers re-park after their jobs finish is an inherent race that no
    /// public API can pin down. What LIFO guarantees deterministically is that
    /// a sequence of non-overlapping jobs concentrates on a single worker.
    ///
    /// The `pool.wait()` after each probe is essential: it returns only once the
    /// probe's worker has re-parked onto the idle stack (the worker holds the
    /// pool mutex from decrementing `scheduled_jobs` through the idle push, and
    /// `pool.wait` reacquires it before returning). That worker is therefore the
    /// most recently idle thread when the next probe is scheduled, so LIFO wakes
    /// it again — all probes run on a single thread. Without the barrier the next
    /// probe could be scheduled before the worker re-parks and would be served by
    /// the other still-idle thread, which is not what is being tested here. A FIFO
    /// or arbitrary wake policy would instead alternate between the two idle
    /// threads and use both.
    constexpr size_t probes = 50;
    std::set<std::thread::id> probe_ids;
    for (size_t i = 0; i < probes; ++i)
    {
        std::mutex probe_mutex;
        std::condition_variable probe_cv;
        std::thread::id probe_thread;
        bool probe_done = false;
        pool.scheduleOrThrowOnError([&]
        {
            std::unique_lock lock(probe_mutex);
            probe_thread = std::this_thread::get_id();
            probe_done = true;
            probe_cv.notify_all();
        });
        {
            std::unique_lock lock(probe_mutex);
            probe_cv.wait(lock, [&] { return probe_done; });
        }
        pool.wait();
        probe_ids.insert(probe_thread);
    }

    EXPECT_EQ(probe_ids.size(), 1u)
        << "LIFO scheduling should concentrate sequential jobs on a single worker, but "
        << probe_ids.size() << " distinct threads were used";

    pool.wait();
}

/// Shrinking the pool via `setMaxThreads` must retire excess threads without
/// collapsing the surviving idle workers. After the shrink, subsequent jobs
/// must run only on a subset of the original threads (no new threads), and
/// at most `shrunk_threads` distinct workers should be observed.
TEST(ThreadPool, ShrinkMaxThreadsRetiresExcessWithoutCollapsingIdlePool)
{
    constexpr size_t initial_threads = 8;
    constexpr size_t shrunk_threads = 2;

    ThreadPool pool(CurrentMetrics::LocalThread, CurrentMetrics::LocalThreadActive, CurrentMetrics::LocalThreadScheduled, initial_threads, initial_threads, initial_threads);

    /// Force the pool to create all `initial_threads` workers by running that
    /// many simultaneously blocking jobs.
    std::mutex mutex;
    std::condition_variable cv;
    size_t started = 0;
    bool release = false;
    std::vector<std::thread::id> original_thread_ids;
    original_thread_ids.reserve(initial_threads);

    for (size_t i = 0; i < initial_threads; ++i)
    {
        pool.scheduleOrThrowOnError([&]
        {
            std::unique_lock lock(mutex);
            original_thread_ids.push_back(std::this_thread::get_id());
            ++started;
            cv.notify_all();
            cv.wait(lock, [&] { return release; });
        });
    }

    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return started == initial_threads; });
    }

    ASSERT_EQ(original_thread_ids.size(), initial_threads);
    std::set<std::thread::id> original_ids(original_thread_ids.begin(), original_thread_ids.end());
    ASSERT_EQ(original_ids.size(), initial_threads);

    {
        std::lock_guard lock(mutex);
        release = true;
    }
    cv.notify_all();
    pool.wait();

    /// At this point, all `initial_threads` workers are linked into the LIFO
    /// idle stack. `setMaxThreads` should pop and wake exactly the excess
    /// (oldest) threads; the surviving (newest) threads remain on the stack.
    pool.setMaxThreads(shrunk_threads);
    EXPECT_EQ(pool.getMaxThreads(), shrunk_threads);

    /// Run many sequential jobs and capture which thread runs each one. None
    /// of these jobs should be assigned to a thread that was popped during
    /// the shrink (those threads are no longer in the idle stack and exit on
    /// their own once they observe the new `max_threads`).
    constexpr size_t probes = 200;
    std::set<std::thread::id> post_shrink_ids;
    for (size_t i = 0; i < probes; ++i)
    {
        std::mutex local_mutex;
        std::condition_variable local_cv;
        std::thread::id id;
        bool done = false;
        pool.scheduleOrThrowOnError([&]
        {
            std::unique_lock lock(local_mutex);
            id = std::this_thread::get_id();
            done = true;
            local_cv.notify_all();
        });
        {
            std::unique_lock lock(local_mutex);
            local_cv.wait(lock, [&] { return done; });
        }
        EXPECT_TRUE(original_ids.contains(id)) << "Post-shrink job ran on a thread that did not exist before the shrink";
        post_shrink_ids.insert(id);
    }

    EXPECT_LE(post_shrink_ids.size(), shrunk_threads)
        << "Post-shrink jobs used " << post_shrink_ids.size() << " distinct threads, expected at most " << shrunk_threads;

    pool.wait();
}
