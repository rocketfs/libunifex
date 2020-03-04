/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/config.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/async_trace.hpp>
#include <unifex/type_list.hpp>

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace unifex {
namespace tfx {
namespace detail {
  template <typename Result, typename = void>
  struct result_overload {
    using type = type_list<Result>;
  };
  template <typename Result>
  struct result_overload<Result, std::enable_if_t<std::is_void_v<Result>>> {
    using type = type_list<>;
  };
}

template <typename Receiver, typename Func>
struct receiver_ {
  struct type;
};
template <typename Receiver, typename Func>
using receiver = typename receiver_<Receiver, Func>::type;

template <typename Receiver, typename Func>
struct receiver_<Receiver, Func>::type {
  using receiver = type;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  template <typename... Values>
  void set_value(Values&&... values) && noexcept {
    using result_type = std::invoke_result_t<Func, Values...>;
    if constexpr (std::is_void_v<result_type>) {
      if constexpr (noexcept(std::invoke(
                        (Func &&) func_, (Values &&) values...))) {
        std::invoke((Func &&) func_, (Values &&) values...);
        unifex::set_value((Receiver &&) receiver_);
      } else {
        try {
          std::invoke((Func &&) func_, (Values &&) values...);
          unifex::set_value((Receiver &&) receiver_);
        } catch (...) {
          unifex::set_error((Receiver &&) receiver_, std::current_exception());
        }
      }
    } else {
      if constexpr (noexcept(std::invoke(
                        (Func &&) func_, (Values &&) values...))) {
        unifex::set_value(
            (Receiver &&) receiver_,
            std::invoke((Func &&) func_, (Values &&) values...));
      } else {
        try {
          unifex::set_value(
              (Receiver &&) receiver_,
              std::invoke((Func &&) func_, (Values &&) values...));
        } catch (...) {
          unifex::set_error((Receiver &&) receiver_, std::current_exception());
        }
      }
    }
  }

  template <typename Error>
  void set_error(Error&& error) && noexcept {
    unifex::set_error((Receiver &&) receiver_, (Error &&) error);
  }

  void set_done() && noexcept {
    unifex::set_done((Receiver &&) receiver_);
  }

  template <
      typename CPO,
      std::enable_if_t<!is_receiver_cpo_v<CPO>, int> = 0>
  friend auto tag_invoke(CPO cpo, const receiver& r) noexcept(
      std::is_nothrow_invocable_v<CPO, const Receiver&>)
      -> std::invoke_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

  template <typename Visit>
  friend void tag_invoke(tag_t<visit_continuations>, const receiver& r, Visit&& visit) {
    std::invoke(visit, r.receiver_);
  }
};

template <typename Predecessor, typename Func>
struct sender_ {
  struct type;
};
template <typename Predecessor, typename Func>
using sender = typename sender_<Predecessor, Func>::type;

template <typename Predecessor, typename Func>
struct sender_<Predecessor, Func>::type {
  using sender = type;
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

private:

  // This helper transforms an argument list into either
  // - type_list<type_list<Result>> - if Result is non-void, or
  // - type_list<type_list<>>       - if Result is void
  template<typename... Args>
  using result = type_list<
    typename detail::result_overload<std::invoke_result_t<Func, Args...>>::type>;

public:

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = type_list_nested_apply_t<
    typename Predecessor::template value_types<concat_type_lists_unique_t, result>,
    Variant,
    Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
    typename Predecessor::template error_types<type_list>,
    type_list<std::exception_ptr>>::template apply<Variant>;

  template <typename Receiver>
  using receiver = receiver<Receiver, Func>;

  friend constexpr auto tag_invoke(tag_t<blocking>, const sender& sender) {
    return blocking(sender.pred_);
  }

  template <typename Receiver>
  auto connect(Receiver&& r) &&
      -> operation_t<Predecessor, receiver<std::remove_cvref_t<Receiver>>> {
    return unifex::connect(
        std::forward<Predecessor>(pred_),
        receiver<std::remove_cvref_t<Receiver>>{
            std::forward<Func>(func_), std::forward<Receiver>(r)});
  }

  template <typename Receiver>
  auto connect(Receiver&& r) &
      -> operation_t<Predecessor&, receiver<std::remove_cvref_t<Receiver>>>{
    return unifex::connect(
        pred_,
        receiver<std::remove_cvref_t<Receiver>>{func_, std::forward<Receiver>(r)});
  }

  template <typename Receiver>
  auto connect(Receiver&& r) const &
      -> operation_t<const Predecessor&, receiver<std::remove_cvref_t<Receiver>>> {
    return unifex::connect(
        pred_,
        receiver<std::remove_cvref_t<Receiver>>{func_, std::forward<Receiver>(r)});
  }
};
} // namespace tfx

template <typename Sender, typename Func>
auto transform(Sender&& predecessor, Func&& func) {
  return tfx::sender<std::remove_cvref_t<Sender>, std::decay_t<Func>>{
      (Sender &&) predecessor, (Func &&) func};
}

} // namespace unifex
