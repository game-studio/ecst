// Copyright (c) 2015-2016 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: http://opensource.org/licenses/AFL-3.0
// http://vittorioromeo.info | vittorio.romeo@outlook.com

#pragma once

#include <ecst/hardware.hpp>
#include "./instance.hpp"
#include "./executor_proxy.hpp"
#include "./data_proxy.hpp"

ECST_CONTEXT_SYSTEM_NAMESPACE
{
    namespace impl
    {
        template <typename TSettings, typename TSystemSignature>
        auto& instance_base<TSettings, TSystemSignature>::system() noexcept
        {
            return _system;
        }

        template <typename TSettings, typename TSystemSignature>
        const auto& instance_base<TSettings, TSystemSignature>::system() const
            noexcept
        {
            return _system;
        }
    }

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
                s.as_state().execute_deferred_fns(proxy);
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
    template <typename TAcc, typename TF>
    auto instance<TSettings, TSystemSignature>::foldl_outputs(TAcc acc, TF && f)
    {
        for_outputs([&acc, xf = FWD(f) ](auto& system, auto& state_data)
            {
                xf(system, acc, state_data);
            });

        return acc;
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
    void instance<TSettings, TSystemSignature>::prepare_and_wait_n_subtasks(
        sz_t n, TF && f)
    {
        _sm.clear_and_prepare(n);
        counter_blocker b{n};

        // Runs the parallel executor and waits until the remaining subtasks
        // counter is zero.
        execute_and_wait_until_counter_zero(b, [this, &b, &f]
            {
                f(b);
            });
    }

    template <typename TSettings, typename TSystemSignature>
    template <typename TCounterBlocker, typename TData, typename TF>
    void instance<TSettings,
        TSystemSignature>::execute_subtask_and_decrement_counter( // .
        TCounterBlocker & cb, TData & data, TF && f               // .
        )
    {
        f(data);
        decrement_cv_counter_and_notify_all(cb);
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
        // Aggregates synchronization primitives.
        _parallel_executor.execute(*this, ctx, f);
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
        auto eh = executor_proxy::make(*this, execution_dispatch(ctx));
        f(*this, eh);
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
