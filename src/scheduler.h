#pragma once

namespace openre
{
    using TaskFn = void (*)();

    namespace TaskStatus
    {
        constexpr int killed = 0;
        constexpr int sleeping = 1;
        constexpr int starting = 2;
        constexpr int running = 4;
        constexpr int signal = 64;
        constexpr int executing = 127;
    }

    void task_sleep(int frames);
    void task_exit();
    void task_execute(int index, TaskFn fn);
    void task_kill(int index);
    void task_chain(TaskFn fn);
    void task_suspend(int index);
    void task_signal(int index);
    int task_status(int index);
    void scheduler();
    void scheduler_init();
    void scheduler_init_hooks();
}
