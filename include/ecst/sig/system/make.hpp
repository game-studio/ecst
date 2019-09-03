// Copyright (c) 2015-2016 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: http://opensource.org/licenses/AFL-3.0
// http://vittorioromeo.info | vittorio.romeo@outlook.com

#pragma once

#include "./data.hpp"
#include <ecst/config.hpp>
#include <ecst/mp/list.hpp>

namespace ecst::sig::system
{
    /// @brief Given a system tag `st`, creates a default system signature.
    template <typename TSystemTag>
    constexpr auto make(TSystemTag st);
} // namespace ecst::sig::system