#include "TaskManager.h"

bool TaskManager::addTask(const TaskConfig &config) {
    if (taskCount_ >= kMaxTasks) {
        return false;
    }

    if (config.callback == nullptr || config.name == nullptr) {
        return false;
    }

    tasks_[taskCount_].config = config;
    tasks_[taskCount_].handle = nullptr;
    tasks_[taskCount_].started = false;
    taskCount_++;
    return true;
}

bool TaskManager::start() {
    bool allStarted = true;

    for (size_t index = 0; index < taskCount_; ++index) {
        TaskState &task = tasks_[index];
        if (task.started) {
            continue;
        }

        BaseType_t result = pdPASS;
        if (task.config.core == tskNO_AFFINITY) {
            result = xTaskCreate(
                taskTrampoline,
                task.config.name,
                task.config.stackWords,
                &task,
                task.config.priority,
                &task.handle);
        } else {
            result = xTaskCreatePinnedToCore(
                taskTrampoline,
                task.config.name,
                task.config.stackWords,
                &task,
                task.config.priority,
                &task.handle,
                task.config.core);
        }

        if (result != pdPASS) {
            Serial.print("TaskManager: fail start ");
            Serial.println(task.config.name);
            allStarted = false;
            continue;
        }

        task.started = true;
        Serial.print("TaskManager: started ");
        Serial.print(task.config.name);
        Serial.print(" on ");
        Serial.println(coreName(task.config.core));
    }

    return allStarted;
}

size_t TaskManager::taskCount() const {
    return taskCount_;
}

void TaskManager::logStatus(Stream &out) const {
    out.println("--- TaskManager ---");
    out.print("task count: ");
    out.println(taskCount_);

    for (size_t index = 0; index < taskCount_; ++index) {
        const TaskState &task = tasks_[index];
        out.print("name: ");
        out.print(task.config.name);
        out.print(", core: ");
        out.print(coreName(task.config.core));
        out.print(", periodMs: ");
        out.print(task.config.periodMs);
        out.print(", priority: ");
        out.print(task.config.priority);
        out.print(", started: ");
        out.println(task.started ? "yes" : "no");
    }
}

void TaskManager::taskTrampoline(void *parameter) {
    auto *task = static_cast<TaskState *>(parameter);
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        task->config.callback(task->config.context);

        if (task->config.periodMs == 0) {
            vTaskDelay(1);
            continue;
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(task->config.periodMs));
    }
}

const char *TaskManager::coreName(BaseType_t core) {
    if (core == 0) {
        return "core0";
    }

    if (core == 1) {
        return "core1";
    }

    return "any";
}