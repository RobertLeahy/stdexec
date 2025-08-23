#include <asioexec/asio_config.hpp>

#include <catch2/catch.hpp>

#include <stdexec/execution.hpp>
#include "../test_common/receivers.hpp"

#include <boost/intrusive/slist.hpp>
#include <boost/intrusive/slist_hook.hpp>

#include <atomic>
#include <barrier>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <iostream>

using namespace asioexec;

template<typename T>
struct transform_signature;
template<typename... Args>
struct transform_signature<void(Args...)> {
  using type = ::stdexec::set_value_t(Args...);
};

template<typename... Signatures>
using transform_signatures = ::stdexec::completion_signatures<
  typename transform_signature<Signatures>::type...,
  ::stdexec::set_error_t(std::exception_ptr),
  ::stdexec::set_stopped_t()>;

template<typename, typename>
struct operation;

template<typename Receiver, typename Initiation>
struct frame : ::boost::intrusive::slist_base_hook<> {
  operation<Receiver, Initiation>* self_;
  explicit frame(operation<Receiver, Initiation>& self) noexcept
    : self_(&self)
  {
    self_->m_.lock();
    self_->frames_.push_front(*this);
  }
  frame(const frame&) = delete;
  frame& operator=(const frame&) = delete;
  ~frame() noexcept {
    if (!self_) {
      return;
    }
    assert(&self_->frames_.front() == this);
    self_->frames_.pop_front();
    const auto should_complete = self_->frames_.empty() && self_->abandoned_;
    self_->m_.unlock();
    if (!should_complete) {
      return;
    }
    self_->callback_.reset();
    if (self_->ex_) {
      ::stdexec::set_error(std::move(self_->r_), std::move(self_->ex_));
    } else {
      ::stdexec::set_stopped(std::move(self_->r_));
    }
  }
  void release() noexcept {
    assert(&self_->frames_.front() == this);
    self_->frames_.pop_front();
    self_->m_.unlock();
    self_ = nullptr;
  }
  constexpr explicit operator bool() const noexcept {
    return bool(self_);
  }
};

template<typename Receiver, typename Initiation>
struct completion_handler {
  constexpr completion_handler(operation<Receiver, Initiation>* self) noexcept
    : self_(self) {}
  constexpr completion_handler(completion_handler&& other) noexcept
    : self_(other.self_)
  {
    other.self_ = nullptr;
  }
  constexpr ~completion_handler() noexcept {
    if (!self_) {
      return;
    }
    const frame<Receiver, Initiation> f(*self_);
    self_->abandoned_ = true;
  }
  template<typename... Ts>
  constexpr void operator()(Ts&&... ts) noexcept {
    {
      const std::lock_guard l(self_->m_);
      while (!self_->frames_.empty()) {
        self_->frames_.front().release();
      }
    }
    self_->callback_.reset();
    auto&& r = self_->r_;
    self_ = nullptr;
    ::stdexec::set_value(std::move(r), std::forward<Ts>(ts)...);
  }
  operation<Receiver, Initiation>* self_;
};

template<typename Executor, typename Receiver, typename Initiation>
struct executor {
  operation<Receiver, Initiation>& self_;
  Executor ex_;
  constexpr bool operator==(const executor& other) const noexcept {
    return (ex_ == other.ex_) && (&self_ == &other.self_);
  }
  bool operator!=(const executor& other) const = default;
  template<typename F>
  void execute(F f) const noexcept {
    const frame<Receiver, Initiation> g(self_);
    try {
      ex_.execute([&self = self_, f = std::move(f)]() mutable noexcept {
        const frame<Receiver, Initiation> g(self);
        try {
          std::move(f)();
        } catch (...) {
          self.ex_ = std::current_exception();
        }
      });
    } catch (...) {
      self_.ex_ = std::current_exception();
    }
  }
  template<typename... Args>
    requires requires (const Executor& ex) {
      asio_impl::require(
        ex,
        std::declval<Args>()...);
    }
  decltype(auto) require(Args&&... args) const {
    auto ex = asio_impl::require(
      ex_,
      std::forward<Args>(args)...);
    return executor<decltype(ex), Receiver, Initiation>{
      self_,
      std::move(ex)};
  }
};

template<typename Receiver, typename Initiation>
struct operation {
  struct on_stop_request_ {
    void operator()() && noexcept {
      const std::lock_guard l(self_.m_);
      self_.signal_.emit(asio_impl::cancellation_type::all);
    }
    operation& self_;
  };
  Receiver r_;
  Initiation init_;
  std::exception_ptr ex_;
  ::boost::intrusive::slist<
    frame<Receiver, Initiation>,
    ::boost::intrusive::constant_time_size<false>,
    ::boost::intrusive::linear<true>> frames_;
  bool abandoned_{false};
  std::recursive_mutex m_;
  asio_impl::cancellation_signal signal_;
  std::optional<
    ::stdexec::stop_callback_for_t<
      ::stdexec::stop_token_of_t<
        ::stdexec::env_of_t<Receiver>>,
      on_stop_request_>> callback_;
  using operation_state_concept = ::stdexec::operation_state_t;
  void start() & noexcept {
    const frame<Receiver, Initiation> f(*this);
    try {
      std::invoke(
        std::move(init_),
        completion_handler<Receiver, Initiation>{this});
    } catch (...) {
      ex_ = std::current_exception();
    }
    if (f) {
      callback_.emplace(
        ::stdexec::get_stop_token(
          ::stdexec::get_env(r_)),
        on_stop_request_{*this});
    }
  }
};

template<typename Signatures, typename Initiation>
struct sender {
  using sender_concept = ::stdexec::sender_t;
  Initiation init_;
  template<typename Self, typename Env>
    requires
      std::is_constructible_v<Initiation, decltype(std::forward_like<Self>(std::declval<Initiation&>()))>
  Signatures get_completion_signatures(this Self&&, const Env&) noexcept {
    return {};
  }
  template<typename Self, typename Receiver>
    requires ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures_of_t<
        sender,
        ::stdexec::env_of_t<Receiver>>>
  constexpr auto connect(this Self&& self, Receiver r) {
    return operation<Receiver, Initiation>{std::move(r), std::forward<Self>(self).init_};
  }
};

struct completion_token_t {};

inline constexpr completion_token_t completion_token;

namespace ASIOEXEC_ASIO_NAMESPACE {

template <typename... Signatures>
struct async_result<completion_token_t, Signatures...> {
  template <typename Initiation, typename... Args>
  static constexpr auto initiate(
    Initiation init,
    const completion_token_t&,
    Args... args)
  {
    auto f = [init = std::move(init), ...args = std::move(args)](this auto&& self, auto h) {
      std::invoke(
        std::forward_like<decltype(self)>(init),
        std::move(h),
        std::forward_like<decltype(self)>(args)...);
    };
    return sender<
      transform_signatures<Signatures...>,
      decltype(f)>{std::move(f)};
  }
};

template<typename Receiver, typename Initiation, typename Executor>
struct associated_executor<completion_handler<Receiver, Initiation>, Executor> {
  using type = ::executor<Executor, Receiver, Initiation>;
  static constexpr type get(const completion_handler<Receiver, Initiation>& h, Executor ex = Executor()) noexcept {
    return type{*h.self_, std::move(ex)};
  }
};

template<typename Receiver, typename Initiation, typename CancellationSlot>
struct associated_cancellation_slot<completion_handler<Receiver, Initiation>, CancellationSlot> {
  using type = asio_impl::cancellation_slot;
  static constexpr type get(const completion_handler<Receiver, Initiation>& h, CancellationSlot slot = CancellationSlot()) noexcept {
    return h.self_->signal_.slot();
  }
};

}

namespace {

  template<typename Executor, typename Range, typename Function, typename CompletionToken>
  decltype(auto) async_for_each(const Executor& original_ex, Range& r, Function f, CompletionToken&& token) {
    return asio_impl::async_initiate<CompletionToken, void()>(
      [original_ex, &r, f = std::move(f)](auto h) mutable {
        const auto ex = asio_impl::require(
          asio_impl::get_associated_executor(h, original_ex),
          asio_impl::execution::blocking.never);
        ex.execute(
          [ex, &r, begin = std::ranges::begin(r), f = std::move(f), h = std::move(h)](this auto&& self) {
            if (begin == std::ranges::end(r)) {
              std::move(h)();
              return;
            } else {
              f(*begin++);
              ex.execute(std::move(self));
            }
          });
      },
      token);
  }

  template<typename Executor, typename A, typename B, typename Function, typename CompletionToken>
  decltype(auto) async_for_each_both(const Executor& original_ex, A& a, B& b, Function f, CompletionToken&& token) {
    return asio_impl::async_initiate<CompletionToken, void()>(
      [original_ex, &a, &b, f = std::move(f)](auto h) mutable {
        const auto ex = asio_impl::get_associated_executor(h, original_ex);
        struct state {
          decltype(h) handler;
          std::atomic<std::size_t> completed{0};
        };
        auto wrapped = [ptr = std::make_shared<state>(std::move(h))]() mutable {
          if (ptr->completed.fetch_add(1, std::memory_order_acq_rel) == 1) {
            std::move(ptr->handler)();
          }
        };
        ::async_for_each(ex, a, f, wrapped);
        ::async_for_each(ex, b, std::move(f), std::move(wrapped));
      },
      token);
  }

  TEST_CASE(
    "Integration with post",
    "[asioexec][use_sender]") {
    asio_impl::io_context ctx;
    auto sender = asio_impl::post(ctx, completion_token);
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_void_receiver{});
    ::stdexec::start(op);
    CHECK(ctx.poll() == 1);
  }

  TEST_CASE(
    "async_for_each",
    "[asioexec][use_sender]") {
    asio_impl::io_context ctx;
    std::size_t invoked = 0;
    const auto f = [&, expected = 1](const int i) mutable noexcept {
      ++invoked;
      CHECK(i == expected);
      ++expected;
    };
    std::vector<int> v;
    {
      auto sender = async_for_each(
        ctx.get_executor(),
        v,
        f,
        completion_token);
      auto op = ::stdexec::connect(
        std::move(sender),
        expect_void_receiver{});
      ::stdexec::start(op);
      CHECK(invoked == 0);
      CHECK(ctx.poll() == 1);
      CHECK(invoked == 0);
    }
    v.push_back(1);
    v.push_back(2);
    ctx.restart();
    {
      auto sender = async_for_each(
        ctx.get_executor(),
        v,
        f,
        completion_token);
      auto op = ::stdexec::connect(
        std::move(sender),
        expect_void_receiver{});
      ::stdexec::start(op);
      CHECK(invoked == 0);
      CHECK(ctx.poll() != 0);
      CHECK(invoked == 2);
    }
    ctx.restart();
    {
      auto sender = async_for_each(
        ctx.get_executor(),
        v,
        [](auto&&...) { throw std::logic_error("Test"); },
        completion_token);
      auto op = ::stdexec::connect(
        std::move(sender),
        expect_error_receiver{});
      ::stdexec::start(op);
      CHECK(ctx.poll() != 0);
    }
  }

  template <typename Receiver>
  class connect_shared_receiver {
    Receiver r_;
    std::shared_ptr<void>& ptr_;

    template <typename Tag, typename... Args>
    void complete_(const Tag& tag, Args&&... args) noexcept {
      CHECK(ptr_);
      CHECK(ptr_.use_count() == 1);
      tag(std::move(r_), std::forward<Args>(args)...);
      ptr_.reset();
    }
   public:
    using receiver_concept = ::stdexec::receiver_t;

    template <typename T>
      requires std::constructible_from<Receiver, T>
    constexpr explicit connect_shared_receiver(T&& t, std::shared_ptr<void>& ptr) noexcept
      : r_(std::forward<T>(t))
      , ptr_(ptr) {
    }

    constexpr void set_stopped() && noexcept
      requires ::stdexec::receiver_of<
        Receiver,
        ::stdexec::completion_signatures<::stdexec::set_stopped_t()>
      >
    {
      complete_(::stdexec::set_stopped);
    }

    template <typename T>
      requires ::stdexec::receiver_of<
        Receiver,
        ::stdexec::completion_signatures<::stdexec::set_error_t(T)>
      >
    constexpr void set_error(T&& t) && noexcept {
      complete_(::stdexec::set_error, std::forward<T>(t));
    }

    template <typename... Args>
      requires ::stdexec::receiver_of<
        Receiver,
        ::stdexec::completion_signatures<::stdexec::set_value_t(Args...)>
      >
    constexpr void set_value(Args&&... args) && noexcept {
      complete_(::stdexec::set_value, std::forward<Args>(args)...);
    }

    constexpr decltype(auto) get_env() const noexcept {
      return ::stdexec::get_env(r_);
    }
  };

  template <typename Sender, typename Receiver>
  class connect_shared_operation_state {
    using receiver_ = connect_shared_receiver<std::remove_cvref_t<Receiver>>;
    std::shared_ptr<void> self_;
    ::stdexec::connect_result_t<Sender, receiver_> op_;
   public:
    constexpr explicit connect_shared_operation_state(Sender&& s, Receiver&& r)
      : op_(
          ::stdexec::connect(
            std::forward<Sender>(s),
            receiver_(std::forward<Receiver>(r), self_))) {
    }

    void start(std::shared_ptr<connect_shared_operation_state>&& ptr) & noexcept {
      CHECK(ptr.get() == this);
      CHECK(ptr.use_count() == 1);
      self_ = std::move(ptr);
      ::stdexec::start(op_);
    }
  };

  template <typename Sender, typename Receiver>
  auto connect_shared(Sender&& sender, Receiver&& receiver) {
    return std::make_shared<connect_shared_operation_state<Sender, Receiver>>(
      std::forward<Sender>(sender), std::forward<Receiver>(receiver));
  }

  template <typename Sender, typename Receiver>
  void
    start_shared(std::shared_ptr<connect_shared_operation_state<Sender, Receiver>>&& ptr) noexcept {
    REQUIRE(ptr);
    auto&& state = *ptr;
    state.start(std::move(ptr));
  }

  TEST_CASE(
    "async_for_each_both",
    "[asioexec][use_sender]") {
    asio_impl::io_context ctx;
    std::size_t invoked = 0;
    const auto f = [&](int) mutable noexcept {
      ++invoked;
    };
    std::vector<int> a{5};
    std::vector<int> b{6, 7};
    {
      auto sender = async_for_each_both(
        ctx.get_executor(),
        a,
        b,
        f,
        completion_token);
      auto ptr = connect_shared(
        std::move(sender),
        expect_void_receiver{});
      start_shared(std::move(ptr));
      CHECK(invoked == 0);
      CHECK(ctx.poll() != 0);
      CHECK(invoked == 3);
    }
  }

  TEST_CASE(
    "async_for_each_both exception",
    "[asioexec][use_sender]") {
    asio_impl::io_context ctx;
    const auto f = [](auto&&...) {
      throw std::logic_error("Test");
    };
    std::vector<int> a{5};
    std::vector<int> b{6};
    {
      auto sender = async_for_each_both(
        ctx.get_executor(),
        a,
        b,
        f,
        completion_token);
      bool error = false;
      auto ptr = connect_shared(
        std::move(sender) | ::stdexec::upon_error([&](auto&&) noexcept {
          error = true;
        }),
        expect_void_receiver{});
      start_shared(std::move(ptr));
      CHECK(ctx.poll_one());
      CHECK(!error);
      CHECK(ctx.poll_one());
      CHECK(error);
    }
  }

  TEST_CASE(
    "Cancellation",
    "[asioexec][use_sender]") {
    ::stdexec::inplace_stop_source source;
    asio_impl::io_context ctx;
    asio_impl::system_timer timer(ctx);
    timer.expires_after(std::chrono::years(1));
    auto sender = timer.async_wait(completion_token);
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_value_receiver(
        env_tag{},
        ::stdexec::prop(
          ::stdexec::get_stop_token,
          source.get_token()),
        make_error_code(asio_impl::error::operation_aborted)));
    source.request_stop();
    ::stdexec::start(op);
    ctx.run();
  }

  TEST_CASE(
    "Abandoned by initiating function",
    "[asioexec][use_sender]") {
    const auto initiating_function = []<typename CompletionToken>(CompletionToken&& token) {
      return asio_impl::async_initiate<CompletionToken, void()>(
        [](auto&& ...) {},
        token);
    };
    auto sender = initiating_function(completion_token);
    auto ptr = connect_shared(
      std::move(sender),
      expect_stopped_receiver{});
    start_shared(std::move(ptr));
  }

  TEST_CASE(
    "Cancelled during abandonment",
    "[asioexec][use_sender]") {
    bool stopped = false;
    asio_impl::io_context ctx;
    std::barrier barrier(2);
    const auto initiating_function = [&]<typename CompletionToken>(CompletionToken&& token) {
      return asio_impl::async_initiate<CompletionToken, void()>(
        [&](auto h) {
          const auto ex = asio_impl::get_associated_executor(h, ctx.get_executor());
          asio_impl::post(ex, [&barrier, h = std::move(h)]() mutable {
            barrier.arrive_and_wait();
            barrier.arrive_and_wait();
            auto local = std::move(h);
            //  Abandoning
            (void)local;
          });
        },
        token);
    };
    ::stdexec::inplace_stop_source source;
    auto sender = initiating_function(completion_token);
    {
      auto ptr = connect_shared(
        std::move(sender) | ::stdexec::upon_stopped([&]() noexcept {
          stopped = true;
        }),
        expect_void_receiver(
          ::stdexec::prop(
            ::stdexec::get_stop_token,
            source.get_token())));
      std::thread t([&]() noexcept {
        barrier.arrive_and_wait();
        (void)barrier.arrive();
        source.request_stop();
      });
      start_shared(std::move(ptr));
      CHECK(ctx.run() != 0);
      //  Just in case
      (void)barrier.arrive();
      t.join();
      CHECK(stopped);
    }
  }

  TEST_CASE(
    "Cancelled during completion",
    "[asioexec][use_sender]") {
    bool complete = false;
    asio_impl::io_context ctx;
    std::barrier barrier(2);
    const auto initiating_function = [&]<typename CompletionToken>(CompletionToken&& token) {
      return asio_impl::async_initiate<CompletionToken, void()>(
        [&](auto h) {
          const auto ex = asio_impl::get_associated_executor(h, ctx.get_executor());
          asio_impl::post(ex, [&barrier, h = std::move(h)]() mutable {
            barrier.arrive_and_wait();
            barrier.arrive_and_wait();
            std::move(h)();
          });
        },
        token);
    };
    ::stdexec::inplace_stop_source source;
    auto sender = initiating_function(completion_token);
    {
      auto ptr = connect_shared(
        std::move(sender) | ::stdexec::then([&]() noexcept {
          complete = true;
        }),
        expect_void_receiver(
          ::stdexec::prop(
            ::stdexec::get_stop_token,
            source.get_token())));
      std::thread t([&]() noexcept {
        barrier.arrive_and_wait();
        (void)barrier.arrive();
        source.request_stop();
      });
      start_shared(std::move(ptr));
      CHECK(ctx.run() != 0);
      //  Just in case
      (void)barrier.arrive();
      t.join();
      CHECK(complete);
    }
  }

} // namespace
