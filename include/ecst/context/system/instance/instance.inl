// Copyright (c) 2015-2016 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: http://opensource.org/licenses/AFL-3.0
// http://vittorioromeo.info | vittorio.romeo@outlook.com

#pragma once

#include "./instance.hpp"
#include "./executor_proxy.hpp"
#include "./data_proxy.hpp"

ECST_CONTEXT_SYSTEM_NAMESPACE
{
    template <typename TSettings, typename TSystemSignature>
    ECST_ALWAYS_INLINE auto& ECST_PURE_FN
    instance<TSettings, TSystemSignature>::subscribed() noexcept
    {
        return _subscribed;
    }

    template <typename TSettings, typename TSystemSignature>
    ECST_ALWAYS_INLINE const auto& ECST_PURE_FN
    instance<TSettings, TSystemSignature>::subscribed() const noexcept
    {
        return _subscribed;
    }

    template <typename TSettings, typename TSystemSignature>
    instance<TSettings, TSystemSignature>::instance()
        : _bitset{
              bitset::make_from_system_signature<TSystemSignature>(TSettings{})}
    {
        ELOG(                                                          // .
            debug::lo_system_bitset() << "(" << system_id()            // .
                                      << ") bitset: " << _bitset.str() // .
                                      << "\n";                         // .
            );
    }

    template <typename TSettings, typename TSystemSignature>
    template <typename TF>
    decltype(auto) instance<TSettings, TSystemSignature>::for_states(TF && f)
    {
        return _sm.for_states(FWD(f));
    }

    template <typename TSettings, typename TSystemSignature>
    template <typename TProxy>
    void instance<TSettings, TSystemSignature>::execute_deferred_fns(
        TProxy & proxy)
    {
        for_states([&proxy](auto& s)
            {
                s.as_state()._deferred_fns.execute_all(proxy);
            });
    }

    template <typename TSettings, typename TSystemSignature>
    template <typename TF>
    decltype(auto) instance<TSettings, TSystemSignature>::for_outputs(TF && f)
    {
        return for_states([this, &f](auto& s)
            {
                f(this->system(), s.as_data());
            });
    }

    template <typename TSettings, typename TSystemSignature>
    auto instance<TSettings, TSystemSignature>::is_subscribed(entity_id eid)
        const noexcept
    {
        return subscribed().has(eid);
    }

    template <typename TSettings, typename TSystemSignature>
    auto instance<TSettings, TSystemSignature>::subscribe(entity_id eid)
    {
        return subscribed().add(eid);
    }

    template <typename TSettings, typename TSystemSignature>
    auto instance<TSettings, TSystemSignature>::unsubscribe(entity_id eid)
    {
        return subscribed().erase(eid);
    }

    template <typename TSettings, typename TSystemSignature>
    template <typename TF>
    void instance<TSettings, TSystemSignature>::prepare_and_wait_subtasks(
        sz_t n_total, sz_t n_septhread, TF & f)
    {
        _sm.clear_and_prepare(n_total);
        counter_blocker b{n_septhread};

        // Function accepting a context and another function which will be
        // executed in a separated thread. Inteded to be called from inner
        // parllelism strategy executors.
        auto run_in_separate_thread = [this, &b](auto& ctx, auto& xf)
        {
            return [this, &b, &ctx, &xf](auto&&... xs) mutable
            {
                // Use of multithreading:
                // * Run subtask slices in separate threads.
                ctx.post_in_thread_pool([&xf, &b, xs...]() mutable
                    {
                        xf(FWD(xs)...);
                        decrement_cv_counter_and_notify_all(b);
                    });
            };
        };

        // Runs the parallel executor and waits until the remaining subtasks
        // counter is zero.
        execute_and_wait_until_counter_zero(b, [&f, &run_in_separate_thread]
            {
                f(run_in_separate_thread);
            });
    }

    template <typename TSettings, typename TSystemSignature>
    template <typename TContext, typename TF>
    void instance<TSettings, TSystemSignature>::execute_single( // .
        TContext & ctx, TF && f                                 // .
        )
    {
        _sm.clear_and_prepare(1);

        // Create single-subtask data proxy.
        auto dp = data_proxy::make_single<TSystemSignature>( // .
            data_proxy::make_single_functions(               // .
                make_all_entity_provider(),                  // .
                [this]() -> auto&                            // .
                {                                            // .
                    return this->_sm.get(0);                 // .
                }                                            // .
                ),                                           // .
            ctx,                                             // .
            all_entity_count()                               // .
            );

        f(dp);
    }

    template <typename TSettings, typename TSystemSignature>
    template <typename TContext, typename TF>
    void instance<TSettings, TSystemSignature>::execute_in_parallel( // .
        TContext & ctx, TF && f                                      // .
        )
    {
        auto st = [ this, &ctx, f = FWD(f) ] // .
            (auto split_idx, auto i_begin, auto i_end) mutable
        {
            // Create bound slice executor.
            auto bse = this->make_bound_slice_executor(
                ctx, split_idx, i_begin, i_end, f);

            // Execute the bound slice.
            bse();
        };

        _parallel_executor.execute(*this, ctx, std::move(st));
    }

    template <typename TSettings, typename TSystemSignature>
    template <typename TContext>
    auto instance<TSettings, TSystemSignature>::execution_dispatch(
        TContext & ctx) noexcept
    {
        return [this, &ctx](auto&& f)
        {
            return static_if(settings::inner_parallelism_allowed(TSettings{}))
                .then([this, &ctx](auto&& xf)
                    {
                        return this->execute_in_parallel(ctx, FWD(xf));
                    })
                .else_([this, &ctx](auto&& xf)
                    {
                        return this->execute_single(ctx, FWD(xf));
                    })(FWD(f));
        };
    }

    template <typename TSettings, typename TSystemSignature>
    template <typename TContext, typename TF>
    void instance<TSettings, TSystemSignature>::execute( // .
        TContext & ctx, TF && f                          // .
        )
    {
        // Bind the dispatch function to `ctx`.
        auto bound_dispatch = execution_dispatch(ctx);

        // Create an executor proxy.
        auto ep = executor_proxy::make(*this, std::move(bound_dispatch));

        // Call the user `(instance&, executor&)` function.
        f(*this, ep);
    }

    template <typename TSettings, typename TSystemSignature>
    const auto& instance<TSettings, TSystemSignature>::bitset() const noexcept
    {
        return _bitset;
    }

    template <typename TSettings, typename TSystemSignature>
    template <typename TBitset>
    auto ECST_PURE_FN instance<TSettings, TSystemSignature>::matches_bitset(
        const TBitset& b) const noexcept
    {
        return _bitset.contains(b);
    }

    template <typename TSettings, typename TSystemSignature>
    auto ECST_PURE_FN instance<TSettings, TSystemSignature>::subscribed_count()
        const noexcept
    {
        return this->subscribed().size();
    }
}
ECST_CONTEXT_SYSTEM_NAMESPACE_END
