#pragma once
#include <cstdint>
#include <type_traits>
#include <utility>
#include <new>

// base class：Z-Task (stackless)
struct z_Task {
    virtual ~z_Task() = default;

    // resume task
    // @return true(DONE), false(SUSPENDED)
    virtual bool resume() = 0;
};

// z_task struct fields (`z_call` is available)
// @param ... `SubTask a; SubTask b; ...`
#define z_task_fields(...) \
    union z_SubTaskU; \
    void (*_z_subtask_deinit)(z_SubTaskU *u) = nullptr; \
    union z_SubTaskU { __VA_ARGS__; z_SubTaskU(){} ~z_SubTaskU(){} } _z_subtask_u; \
    int32_t _z_resume_point = 0

// z_task (leaf) struct fields (`z_call` is not available)
#define z_task_leaf_fields() \
    int32_t _z_resume_point = 0

// implement the `z_Task::resume` method
#define z_impl_resume() \
    virtual bool resume() override { \
        /* resume z_function(result, z_task) */ \
        return this->operator()(nullptr, this); \
    }

// deinit the active `subtask` (if present).
// must be placed at the beginning of `~z_task()`
#define z_subtask_deinit() do { \
    if (this->_z_subtask_deinit) { \
        this->_z_subtask_deinit(&this->_z_subtask_u); \
        this->_z_subtask_deinit = nullptr; \
    } \
} while (0)

// definition of the z_task coroutine function
#define z_function(Result, ...) bool operator()(Result *_z_result, z_Task *_z_task, ##__VA_ARGS__)

#define Z_CONCAT_(a, b) a##b
#define Z_CONCAT(a, b) Z_CONCAT_(a, b)
#define Z_LABEL Z_CONCAT(z_label_, __LINE__)
#define z_label_addr ((int32_t)((intptr_t)&&Z_LABEL - (intptr_t)&&z_label_base))
#define z_resume_point ((void *)((intptr_t)&&z_label_base + (intptr_t)this->_z_resume_point))

// place this at the beginning of the body of `z_function`
#define z_begin() \
    goto *z_resume_point; \
    z_label_base:

#define z_suspend() do { \
    this->_z_resume_point = z_label_addr; \
    return false; \
    Z_LABEL: ; \
} while (0)

#define z_return(val, ...) do { \
    if (_z_result) *_z_result = std::move(val); \
    this->_z_resume_point = 0; \
    __VA_ARGS__; \
    return true; \
} while (0)

#define z_ret(...) do { \
    this->_z_resume_point = 0; \
    __VA_ARGS__; \
    return true; \
} while (0)

// @param result `Result *`, use nullptr to ignore
// @param ... the arguments passed to z_function (pinned)
#define z_call(taskname, result, ...) do { \
    using z_SubTask = std::remove_reference_t<decltype(this->_z_subtask_u.taskname)>; \
    new (&this->_z_subtask_u.taskname) z_SubTask(); \
    this->_z_subtask_deinit = [] (z_SubTaskU *u) { u->taskname.~z_SubTask(); }; \
Z_LABEL: \
    if (!this->_z_subtask_u.taskname((result), _z_task, ##__VA_ARGS__)) { \
        this->_z_resume_point = z_label_addr; \
        return false; \
    } \
    this->_z_subtask_u.taskname.~z_SubTask(); \
    this->_z_subtask_deinit = nullptr; \
} while (0)
