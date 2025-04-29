#include "scheduler.h"
#include "interop.hpp"
#include "openre.h"
#include "re2.h"

#include <cstring>

namespace openre
{
    // 0x00508CE0
    void task_sleep(int frames)
    {
        auto& task = gGameTable.tasks[gGameTable.task_no];
        task.sleep = frames;
        task.status = TaskStatus::sleeping;
        task.var_13 = 1;
    }

    // 0x00508D10
    void task_exit()
    {
        auto& task = gGameTable.tasks[gGameTable.task_no];
        task.status = TaskStatus::killed;
        task.fn = task_exit;
        task.var_13 = 1;
    }

    // 0x00508CC0
    void task_execute(int index, TaskFn fn)
    {
        auto& task = gGameTable.tasks[index];
        task.status = TaskStatus::starting;
        task.fn = fn;
    }

    // 0x00508D40
    void task_kill(int index)
    {
        auto& task = gGameTable.tasks[index];
        task.status = TaskStatus::killed;
        task.fn = task_exit;
    }

    // 0x00508D60
    void task_chain(TaskFn fn)
    {
        auto& task = gGameTable.tasks[gGameTable.task_no];
        task.status = TaskStatus::starting;
        task.fn = fn;
        task.var_13 = 1;
    }

    // 0x00508D90
    void task_suspend(int index)
    {
        auto& task = gGameTable.tasks[index];
        task.status |= TaskStatus::signal;
    }

    // 0x00508DA0
    void task_signal(int index)
    {
        auto& task = gGameTable.tasks[index];
        task.status &= ~TaskStatus::signal;
    }

    // 0x00508DB0
    int task_status(int index)
    {
        auto& task = gGameTable.tasks[index];
        return task.status;
    }

    // 0x00508B60
    void scheduler_init()
    {
        std::memset(gGameTable.tasks, 0, sizeof(gGameTable.tasks));
        for (auto& task : gGameTable.tasks)
        {
            task.sleep = 1;
            task.fn = task_exit;
        }
        gGameTable.task_disable = 1;
    }

    // 0x00508BC0
    void scheduler()
    {
        gGameTable.task_disable = 0;
        gGameTable.task_no = 0;
        for (auto& task : gGameTable.tasks)
        {
            gGameTable.ctcb = &task;
            switch (task.status)
            {
            case TaskStatus::starting:
                task.sleep = 1;
                task.var_08 = 0;
                task.var_09 = 0;
                task.var_0A = 0;
                task.var_0B = 0;
                task.var_0C = 0;
                task.var_0D = 0;
                task.var_0E = 0;
                task.var_0F = 0;
                task.var_10 = 0;
                task.var_11 = 0;
                task.var_14 = 0;
                task.var_18 = 0;
                task.var_1C = 0;
                task.var_20 = 0;
                [[fallthrough]];
            case TaskStatus::sleeping:
                task.sleep--;
                if (task.sleep != 0)
                    break;
                [[fallthrough]];
            case TaskStatus::running:
                task.var_13 = 0;
                task.status = TaskStatus::executing;
                reinterpret_cast<TaskFn>(task.fn)();
                if (task.status == TaskStatus::executing)
                {
                    task.status = TaskStatus::sleeping;
                    task.sleep = 1;
                }
                break;
            }
            gGameTable.task_no++;
        }
    }

    void scheduler_init_hooks()
    {
        interop::writeJmp(0x00508CE0, task_sleep);
        interop::writeJmp(0x00508D10, task_exit);
        interop::writeJmp(0x00508CC0, task_execute);
        interop::writeJmp(0x00508D40, task_kill);
        interop::writeJmp(0x00508D60, task_chain);
        interop::writeJmp(0x00508D90, task_suspend);
        interop::writeJmp(0x00508DA0, task_signal);
        interop::writeJmp(0x00508DB0, task_status);
        interop::writeJmp(0x00508B60, scheduler_init);
        interop::writeJmp(0x00508BC0, scheduler);
    }
}
