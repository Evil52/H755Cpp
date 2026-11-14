#pragma once
#include <conc/concurrency.hpp>
#include <utility>
namespace h755::async_context
{
    // Async continuations are main-loop-only; ISR delivery stays in the USB event queue.
    struct MainLoopPolicy
    {
        template <class = void, class F, class... Predicates>
        static decltype(auto) call_in_critical_section(F&& action, Predicates&&... predicates)
        {
            if (!(... && predicates()))
            {
                __builtin_trap();
            }
            return std::forward<F>(action)();
        }
    };
}
template <> inline auto conc::injected_policy<> = h755::async_context::MainLoopPolicy{};
