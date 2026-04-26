#pragma once
#include <cstdint>
#include <type_traits>
#include <utility>
#include <new>

// task interface (stackless coroutine)
struct z_Task {
    // no atomic operations required
    uint32_t ref_count = 1;
    // cancellation signal received
    bool canceled = false;
    // the task has been terminated
    bool terminated = false;

    z_Task *ref() noexcept {
        ++ref_count;
        return this;
    }

    void unref() noexcept {
        if (--ref_count == 0)
            delete this;
    }

    // all task struct must have a destructor
    virtual ~z_Task() noexcept = default;

    // @return true(DONE), false(YIELD)
    virtual bool resume() noexcept = 0;

    // dispose the task, leaving only harmless zombies
    virtual void terminate() noexcept = 0;
};

// task fields (`z_call` is available)
// @param subtask_decls: SubTask a; SubTask b; ...
#define z_fields(subtask_decls...) \
    union z_SubTaskU; /* forward decl */ \
    void (*_z_subtask_deinit)(z_SubTaskU *u) = nullptr; \
    union z_SubTaskU { subtask_decls; z_SubTaskU(){} ~z_SubTaskU(){} } _z_subtask_u; \
    int32_t _z_resume_point = 0

// leaf task fields (`z_call` is not available)
#define z_leaf_fields() \
    int32_t _z_resume_point = 0

// for `struct RootTask : z_Task { ... }`
// implement the `z_Task::resume` method
// implement the `z_Task::~T() + terminate()` method
#define z_impl_deinit(T) \
    virtual bool resume() noexcept override { \
        /* resume z_function(result, z_task) */ \
        return this->operator()(nullptr, this); \
    } \
    virtual ~T() noexcept override { \
        this->terminate(); \
    } \
    virtual void terminate() noexcept override { \
        if (this->terminated) return; \
        this->terminated = true; \
        z_subtask_deinit(this); \
        this->deinit(); \
    } \
    void deinit() noexcept

// for `struct LogicTask { ... }`
#define z_def_deinit(T) \
    ~T() noexcept { \
        z_subtask_deinit(this); \
        this->deinit(); \
    } \
    void deinit() noexcept

// internal helper method
template <typename T>
inline void z_subtask_deinit(T *task) {
    if constexpr (requires { task->_z_subtask_deinit; }) {
        if (task->_z_subtask_deinit) {
            task->_z_subtask_deinit(&task->_z_subtask_u);
            task->_z_subtask_deinit = nullptr;
        }
    }
}

// task's coroutine function
#define z_function(Result, param_decls...) bool operator()(Result *_z_result, z_Task *_z_task, ##param_decls) noexcept

#define Z_CONCAT_(a, b) a##b
#define Z_CONCAT(a, b) Z_CONCAT_(a, b)
#define Z_LABEL Z_CONCAT(z_label_, __LINE__)
#define z_label_addr ((int32_t)((intptr_t)&&Z_LABEL - (intptr_t)&&z_label_base))
#define z_resume_point ((void *)((intptr_t)&&z_label_base + (intptr_t)this->_z_resume_point))

#define z_check_cancel() do { if (_z_task->canceled) [[unlikely]] z_ret(); } while (0)

// place this at the beginning of the body of `z_function`
#define z_begin() \
    goto *z_resume_point; \
    z_label_base: \
    z_check_cancel() \

#define z_yield() do { \
    this->_z_resume_point = z_label_addr; \
    return false; \
    Z_LABEL: \
    z_check_cancel(); \
} while (0)

#define z_return(result, final_logic...) do { \
    if (_z_result) *_z_result = std::move(result); \
    this->_z_resume_point = INT32_MIN; \
    final_logic; \
    return true; \
} while (0)

#define z_ret(final_logic...) do { \
    this->_z_resume_point = INT32_MIN; \
    final_logic; \
    return true; \
} while (0)

// @param result: `Result *`, use nullptr to ignore
// @param args: the arguments passed to z_function (pinned)
#define z_call(taskname, result, args...) do { \
    using z_SubTask = std::remove_reference_t<decltype(this->_z_subtask_u.taskname)>; \
    new (&this->_z_subtask_u.taskname) z_SubTask(); \
    this->_z_subtask_deinit = [] (z_SubTaskU *u) { u->taskname.~z_SubTask(); }; \
Z_LABEL: \
    if (!this->_z_subtask_u.taskname((result), _z_task, ##args)) { \
        this->_z_resume_point = z_label_addr; \
        return false; \
    } \
    this->_z_subtask_u.taskname.~z_SubTask(); \
    this->_z_subtask_deinit = nullptr; \
    z_check_cancel(); \
} while (0)

// do not touch the `task` object after resume
#define z_resume(task) do { \
    z_Task *__z_resume_task = static_cast<z_Task *>(task); \
    if (__z_resume_task->terminated) [[unlikely]] break; \
    if (__z_resume_task->resume()) { \
        __z_resume_task->terminate(); \
        __z_resume_task->unref(); \
    } \
} while (0)

#define z_cancel(task) do { \
    z_Task *__z_cancel_task = static_cast<z_Task *>(task); \
    if (__z_cancel_task->terminated || __z_cancel_task->canceled) [[unlikely]] break; \
    __z_cancel_task->canceled = true; \
    z_resume(__z_cancel_task); \
} while (0)

#define z_launch(T, ctor_args...) do { \
    z_Task *__z_launch_task = new (std::nothrow) T(ctor_args); \
    if (__z_launch_task) z_resume(__z_launch_task); \
} while (0)
