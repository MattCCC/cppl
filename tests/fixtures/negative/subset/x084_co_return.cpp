// SPEC: CONSTRUCT-084
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// co_return, is refused.

#include <coroutine>

struct Task {
    struct promise_type {
        unsigned value = 0u;
        Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(unsigned produced) noexcept {
            value = produced;
            return {};
        }
        void return_value(unsigned produced) noexcept { value = produced; }
        void unhandled_exception() noexcept {}
    };
    std::coroutine_handle<promise_type> handle;
    explicit Task(std::coroutine_handle<promise_type> owned) : handle(owned) {}
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() {
        if (handle) {
            handle.destroy();
        }
    }
};

verified Task probe(unsigned x)
    ensures (true)
{
    co_return x;
}

int main() { return probe(2u).handle.promise().value == 2u ? 0 : 1; }
