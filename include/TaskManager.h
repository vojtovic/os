#pragma once

#include <Arduino.h>

class TaskManager {
public:
    static constexpr size_t kMaxTasks = 6;

    struct TaskConfig {
        const char *name;
        void (*callback)(void *context);
        void *context;
        uint32_t periodMs;
        uint32_t stackWords;
        UBaseType_t priority;
        BaseType_t core;
    };

    bool addTask(const TaskConfig &config);
    bool start();
    size_t taskCount() const;
    void logStatus(Stream &out) const;

private:
    struct TaskState {
        TaskConfig config;
        TaskHandle_t handle = nullptr;
        bool started = false;
    };

    TaskState tasks_[kMaxTasks];
    size_t taskCount_ = 0;

    static void taskTrampoline(void *parameter);
    static const char *coreName(BaseType_t core);
};