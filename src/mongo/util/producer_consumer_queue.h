
/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <boost/optional.hpp>
#include <deque>
#include <list>
#include <numeric>
#include <queue>
#include <stack>

#include "mongo/db/operation_context.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/concurrency/with_lock.h"
#include "mongo/util/interruptible.h"
#include "mongo/util/scopeguard.h"

namespace mongo {

namespace producer_consumer_queue_detail {

/**
 * The default cost function for the producer consumer queue.
 *
 * By default, all items in the queue have equal weight.
 */
struct DefaultCostFunction {
    template <typename T>
    size_t operator()(const T&) const {
        return 1;
    }
};

template <bool b, typename T>
struct Holder;

template <typename T>
struct Holder<true, T> {
    std::shared_ptr<T> data;

    template <typename U>
    Holder(const std::shared_ptr<U>& u) : data(std::make_shared<T>(u)) {}

    const T& operator->() const {
        return *data;
    }
};

template <typename T>
struct Holder<false, T> {
    T data;

    template <typename U>
    Holder(const std::shared_ptr<U>& u) : data(u) {}

    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;

    Holder(Holder&&) = default;
    Holder& operator=(Holder&&) = default;

    const T& operator->() const {
        return data;
    }
};

template <bool>
class ConsumerState;

template <>
class ConsumerState<false> {
public:
    stdx::condition_variable& cv() {
        return _cv;
    }

    operator size_t() const {
        return data ? 1 : 0;
    }

    class RAII {
    public:
        RAII(ConsumerState& x) : _x(x.data) {
            invariant(!_x);
            _x = true;
        }

        ~RAII() {
            _x = false;
        }

    private:
        bool& _x;
    };

private:
    bool data = false;
    stdx::condition_variable _cv;
};

template <>
class ConsumerState<true> {
public:
    stdx::condition_variable& cv() {
        return _cv;
    }

    operator size_t() const {
        return data;
    }

    class RAII {
    public:
        RAII(ConsumerState& x) : _x(x.data) {
            ++_x;
        }

        ~RAII() {
            --_x;
        }

    private:
        size_t& _x;
    };

private:
    size_t data = 0;
    stdx::condition_variable _cv;
};

template <bool>
class ProducerState;

template <>
class ProducerState<false> {
public:
    stdx::condition_variable& cv() {
        return _cv;
    }

    size_t wants() {
        return data;
    }

    operator size_t() const {
        return data ? 1 : 0;
    }

    size_t queueDepth() const {
        return data;
    }

    explicit operator bool() const {
        return data;
    }

    class RAII {
    public:
        RAII(ProducerState& x, size_t wants) : _x(x) {
            invariant(!_x);
            _x.data = wants;
        }

        stdx::condition_variable& cv() {
            return _x._cv;
        }

        explicit operator bool() const {
            return true;
        }

        ~RAII() {
            _x.data = 0;
        }

    public:
        ProducerState& _x;
    };

private:
    size_t data = 0;

    stdx::condition_variable _cv;
};

template <>
class ProducerState<true> {
    // One of these is allocated for each producer that blocks on pushing to the queue
    struct ProducerWants {
        ProducerWants(size_t s) : wants(s) {}

        size_t wants;
        // Each producer has their own cv, so that they can be woken individually in FIFO order
        stdx::condition_variable cv;
    };

public:
    stdx::condition_variable& cv() {
        return data.front().cv;
    }

    size_t wants() {
        return data.front().wants;
    }

    operator size_t() const {
        return data.size();
    }

    size_t queueDepth() const {
        size_t depth = 0;
        for (const auto& x : data) {
            depth += x.wants;
        }

        return depth;
    }

    explicit operator bool() const {
        return data.size();
    }

    class RAII {
    public:
        RAII(ProducerState& x, size_t wants) : _x(x.data) {
            _x.emplace_back(wants);
            _iter = --_x.end();
        }

        stdx::condition_variable& cv() {
            return _iter->cv;
        }

        explicit operator bool() const {
            return _x.begin() == _iter;
        }

        ~RAII() {
            _x.erase(_iter);
        }

    private:
        std::list<ProducerWants>& _x;
        std::list<ProducerWants>::iterator _iter;
    };

private:
    // A list of producers that want to push to the queue
    std::list<ProducerWants> data;
};

template <typename T,
          bool isMultiProducer,
          bool isMultiConsumer,
          typename CostFunc = producer_consumer_queue_detail::DefaultCostFunction>
class ProducerConsumerQueue {
    using Consumers = ConsumerState<isMultiConsumer>;
    using Producers = ProducerState<isMultiProducer>;

public:
    struct Stats {
        size_t queueDepth;
        size_t waitingConsumers;
        size_t waitingProducers;
        size_t producerQueueDepth;
    };

    // By default the queue depth is unlimited
    ProducerConsumerQueue()
        : ProducerConsumerQueue(std::numeric_limits<size_t>::max(), CostFunc{}) {}

    // Or it can be measured in whatever units your size function returns
    explicit ProducerConsumerQueue(size_t size) : ProducerConsumerQueue(size, CostFunc{}) {}

    // If your cost function has meaningful state, you may also pass a non-default constructed
    // instance
    explicit ProducerConsumerQueue(size_t size, CostFunc costFunc)
        : _max(size), _costFunc(std::move(costFunc)) {}

    ProducerConsumerQueue(const ProducerConsumerQueue&) = delete;
    ProducerConsumerQueue& operator=(const ProducerConsumerQueue&) = delete;

    ProducerConsumerQueue(ProducerConsumerQueue&&) = delete;
    ProducerConsumerQueue& operator=(ProducerConsumerQueue&&) = delete;

    ~ProducerConsumerQueue() {
        invariant(!_producers);
        invariant(!_consumers);
    }

    // Pushes the passed T into the queue
    //
    // Leaves T unchanged if an interrupt exception is thrown while waiting for space
    void push(T&& t, Interruptible* interruptible = Interruptible::notInterruptible()) {
        _pushRunner([&](stdx::unique_lock<stdx::mutex>& lk) {
            auto cost = _invokeCostFunc(t, lk);
            uassert(ErrorCodes::ProducerConsumerQueueBatchTooLarge,
                    str::stream() << "cost of item (" << cost
                                  << ") larger than maximum queue size ("
                                  << _max
                                  << ")",
                    cost <= _max);

            _waitForSpace(lk, cost, interruptible);
            _push(lk, std::move(t));
        });
    }

    // Pushes all Ts into the queue
    //
    // Blocks until all of the Ts can be pushed at once
    //
    // StartIterator must be ForwardIterator
    //
    // Leaves the values underneath the iterators unchanged if an interrupt exception is thrown
    // while waiting for space
    //
    // Lifecycle methods of T must not throw if you want to use this method, as there's no obvious
    // mechanism to see what was and was not pushed if those do throw
    template <typename StartIterator, typename EndIterator>
    void pushMany(StartIterator start,
                  EndIterator last,
                  Interruptible* interruptible = Interruptible::notInterruptible()) {
        return _pushRunner([&](stdx::unique_lock<stdx::mutex>& lk) {
            size_t cost = 0;
            for (auto iter = start; iter != last; ++iter) {
                cost += _invokeCostFunc(*iter, lk);
            }

            uassert(ErrorCodes::ProducerConsumerQueueBatchTooLarge,
                    str::stream() << "cost of items in batch (" << cost
                                  << ") larger than maximum queue size ("
                                  << _max
                                  << ")",
                    cost <= _max);

            _waitForSpace(lk, cost, interruptible);

            for (auto iter = start; iter != last; ++iter) {
                _push(lk, std::move(*iter));
            }
        });
    }

    // Attempts a non-blocking push of a value
    //
    // Leaves T unchanged if it fails
    bool tryPush(T&& t) {
        return _pushRunner(
            [&](stdx::unique_lock<stdx::mutex>& lk) { return _tryPush(lk, std::move(t)); });
    }

    // Pops one T out of the queue
    T pop(Interruptible* interruptible = Interruptible::notInterruptible()) {
        return _popRunner([&](stdx::unique_lock<stdx::mutex>& lk) {
            _waitForNonEmpty(lk, interruptible);
            return _pop(lk);
        });
    }

    // Waits for at least one item in the queue, then pops items out of the queue until it would
    // block
    //
    // OutputIterator must not throw on move assignment to *iter or popped values may be lost
    // TODO: add sfinae to check to enforce
    //
    // Returns the cost value of the items extracted, along with the updated output iterator
    template <typename OutputIterator>
    std::pair<size_t, OutputIterator> popMany(
        OutputIterator iterator, Interruptible* interruptible = Interruptible::notInterruptible()) {
        return popManyUpTo(_max, iterator, interruptible);
    }

    // Waits for at least one item in the queue, then pops items out of the queue until it would
    // block, or we've exceeded our budget
    //
    // OutputIterator must not throw on move assignment to *iter or popped values may be lost
    // TODO: add sfinae to check to enforce
    //
    // Returns the cost value of the items extracted, along with the updated output iterator
    template <typename OutputIterator>
    std::pair<size_t, OutputIterator> popManyUpTo(
        size_t budget,
        OutputIterator iterator,
        Interruptible* interruptible = Interruptible::notInterruptible()) {
        return _popRunner([&](stdx::unique_lock<stdx::mutex>& lk) {
            size_t cost = 0;

            _waitForNonEmpty(lk, interruptible);

            while (auto out = _tryPop(lk)) {
                cost += _invokeCostFunc(*out, lk);
                *iterator = std::move(*out);
                ++iterator;

                if (cost >= budget) {
                    break;
                }
            }

            return std::make_pair(cost, iterator);
        });
    }

    // Attempts a non-blocking pop of a value
    boost::optional<T> tryPop() {
        return _popRunner([&](stdx::unique_lock<stdx::mutex>& lk) { return _tryPop(lk); });
    }

    // Closes the producer end. Consumers will continue to consume until the queue is exhausted, at
    // which time they will begin to throw with an interruption dbexception
    void closeProducerEnd() {
        stdx::lock_guard<stdx::mutex> lk(_mutex);

        _producerEndClosed = true;

        _notifyIfNecessary(lk);
    }

    // Closes the consumer end. This causes all callers to throw with an interruption dbexception
    void closeConsumerEnd() {
        stdx::lock_guard<stdx::mutex> lk(_mutex);

        _consumerEndClosed = true;
        _producerEndClosed = true;

        _notifyIfNecessary(lk);
    }

    Stats getStats() const {
        stdx::lock_guard<stdx::mutex> lk(_mutex);
        Stats stats;
        stats.queueDepth = _current;
        stats.waitingConsumers = _consumers;
        stats.waitingProducers = _producers;
        stats.producerQueueDepth = _producers.queueDepth();
        return stats;
    }

    struct Pipe {
        class Producer {
            struct Closer {
                Closer(const std::shared_ptr<ProducerConsumerQueue>& parent) : _parent(parent) {}

                ~Closer() {
                    _parent->closeProducerEnd();
                }

                ProducerConsumerQueue* operator->() const {
                    return _parent.get();
                }

                std::shared_ptr<ProducerConsumerQueue> _parent;
            };

        public:
            explicit Producer(const std::shared_ptr<ProducerConsumerQueue>& parent)
                : _parent(parent) {}

            void push(T&& t,
                      Interruptible* interruptible = Interruptible::notInterruptible()) const {
                _parent->push(std::move(t), interruptible);
            }

            template <typename StartIterator, typename EndIterator>
            void pushMany(StartIterator&& start,
                          EndIterator&& last,
                          Interruptible* interruptible = Interruptible::notInterruptible()) const {
                _parent->pushMany(std::forward<StartIterator>(start),
                                  std::forward<EndIterator>(last),
                                  interruptible);
            }

            bool tryPush(T&& t) const {
                return _parent->tryPush(std::move(t));
            }

            void close() const {
                _parent->closeProducerEnd();
            }

        private:
            producer_consumer_queue_detail::Holder<isMultiProducer, Closer> _parent;
        };

        class Consumer {
            struct Closer {
                Closer(const std::shared_ptr<ProducerConsumerQueue>& parent) : _parent(parent) {}

                ~Closer() {
                    _parent->closeConsumerEnd();
                }

                ProducerConsumerQueue* operator->() const {
                    return _parent.get();
                }

                std::shared_ptr<ProducerConsumerQueue> _parent;
            };

        public:
            explicit Consumer(const std::shared_ptr<ProducerConsumerQueue>& parent)
                : _parent(parent) {}

            T pop(Interruptible* interruptible = Interruptible::notInterruptible()) const {
                return _parent->pop(interruptible);
            }

            template <typename OutputIterator>
            std::pair<size_t, OutputIterator> popMany(
                OutputIterator&& iterator,
                Interruptible* interruptible = Interruptible::notInterruptible()) const {
                return _parent->popMany(std::forward<OutputIterator>(iterator), interruptible);
            }

            template <typename OutputIterator>
            std::pair<size_t, OutputIterator> popManyUpTo(
                size_t budget,
                OutputIterator&& iterator,
                Interruptible* interruptible = Interruptible::notInterruptible()) const {
                return _parent->popManyUpTo(
                    budget, std::forward<OutputIterator>(iterator), interruptible);
            }

            boost::optional<T> tryPop() const {
                return _parent->tryPop();
            }

            void close() const {
                _parent->closeConsumerEnd();
            }

        private:
            producer_consumer_queue_detail::Holder<isMultiConsumer, Closer> _parent;
        };

        class Controller {
        public:
            explicit Controller(const std::shared_ptr<ProducerConsumerQueue>& parent)
                : _parent(parent) {}

            void closeConsumerEnd() const {
                _parent->closeConsumerEnd();
            }

            void closeProducerEnd() const {
                _parent->closeProducerEnd();
            }

            Stats getStats() const {
                return _parent->getStats();
            }

        private:
            std::shared_ptr<ProducerConsumerQueue> _parent;
        };

        explicit Pipe(const std::shared_ptr<ProducerConsumerQueue>& parent)
            : producer(parent), controller(parent), consumer(parent) {}

        Pipe() : Pipe(std::make_shared<ProducerConsumerQueue>()) {}
        Pipe(size_t size) : Pipe(std::make_shared<ProducerConsumerQueue>(size)) {}
        Pipe(size_t size, CostFunc costFunc)
            : Pipe(std::make_shared<ProducerConsumerQueue>(size, std::move(costFunc))) {}

        Producer producer;
        Controller controller;
        Consumer consumer;
    };

private:
    size_t _invokeCostFunc(const T& t, WithLock) {
        auto cost = _costFunc(t);
        invariant(cost);
        return cost;
    }

    void _checkProducerClosed(WithLock) {
        uassert(
            ErrorCodes::ProducerConsumerQueueEndClosed, "Producer end closed", !_producerEndClosed);
        uassert(
            ErrorCodes::ProducerConsumerQueueEndClosed, "Consumer end closed", !_consumerEndClosed);
    }

    void _checkConsumerClosed(WithLock) {
        uassert(
            ErrorCodes::ProducerConsumerQueueEndClosed, "Consumer end closed", !_consumerEndClosed);
        uassert(ErrorCodes::ProducerConsumerQueueEndClosed,
                "Producer end closed and values exhausted",
                !(_producerEndClosed && _queue.empty()));
    }

    void _notifyIfNecessary(WithLock) {
        // If we've closed the consumer end, or if the production end is closed and we've exhausted
        // the queue, wake everyone up and get out of here
        if (_consumerEndClosed || (_queue.empty() && _producerEndClosed)) {
            if (_consumers) {
                _consumers.cv().notify_all();
            }

            if (_producers) {
                _producers.cv().notify_one();
            }

            return;
        }

        // If a producer is queued, and we have enough space for it to push its work
        if (_producers && _current + _producers.wants() <= _max) {
            _producers.cv().notify_one();

            return;
        }

        // If we have consumers and anything in the queue, notify consumers
        if (_consumers && _queue.size()) {
            _consumers.cv().notify_one();

            return;
        }
    }

    template <typename Callback>
    auto _pushRunner(Callback&& cb) {
        stdx::unique_lock<stdx::mutex> lk(_mutex);

        _checkProducerClosed(lk);

        const auto guard = MakeGuard([&] { _notifyIfNecessary(lk); });

        return cb(lk);
    }

    template <typename Callback>
    auto _popRunner(Callback&& cb) {
        stdx::unique_lock<stdx::mutex> lk(_mutex);

        _checkConsumerClosed(lk);

        const auto guard = MakeGuard([&] { _notifyIfNecessary(lk); });

        return cb(lk);
    }

    bool _tryPush(WithLock wl, T&& t) {
        size_t cost = _invokeCostFunc(t, wl);
        if (_current + cost <= _max) {
            _queue.emplace(std::move(t));
            _current += cost;
            return true;
        }

        return false;
    }

    void _push(WithLock wl, T&& t) {
        size_t cost = _invokeCostFunc(t, wl);
        invariant(_current + cost <= _max);

        _queue.emplace(std::move(t));
        _current += cost;
    }

    boost::optional<T> _tryPop(WithLock wl) {
        boost::optional<T> out;

        if (!_queue.empty()) {
            out.emplace(std::move(_queue.front()));
            _queue.pop();
            _current -= _invokeCostFunc(*out, wl);
        }

        return out;
    }

    T _pop(WithLock wl) {
        invariant(_queue.size());

        auto t = std::move(_queue.front());
        _queue.pop();

        _current -= _invokeCostFunc(t, wl);

        return t;
    }

    void _waitForSpace(stdx::unique_lock<stdx::mutex>& lk,
                       size_t cost,
                       Interruptible* interruptible) {
        _checkProducerClosed(lk);

        if (!_producers && _current + cost <= _max) {
            return;
        }

        typename Producers::RAII raii(_producers, cost);

        _waitFor(lk,
                 raii.cv(),
                 [&] {
                     _checkProducerClosed(lk);

                     if (!raii) {
                         return false;
                     }

                     return _current + cost <= _max;
                 },
                 interruptible);
    }

    void _waitForNonEmpty(stdx::unique_lock<stdx::mutex>& lk, Interruptible* interruptible) {
        typename Consumers::RAII raii(_consumers);

        _waitFor(lk,
                 _consumers.cv(),
                 [&] {
                     _checkConsumerClosed(lk);
                     return _queue.size();
                 },
                 interruptible);
    }

    template <typename Callback>
    void _waitFor(stdx::unique_lock<stdx::mutex>& lk,
                  stdx::condition_variable& condvar,
                  Callback&& pred,
                  Interruptible* interruptible) {
        interruptible->waitForConditionOrInterrupt(condvar, lk, pred);
    }

    mutable stdx::mutex _mutex;

    // Max size of the queue
    const size_t _max;

    // User's cost function
    CostFunc _costFunc;

    // Current size of the queue
    size_t _current = 0;

    std::queue<T> _queue;

    Consumers _consumers;
    Producers _producers;

    // Flags that we're shutting down the queue
    bool _consumerEndClosed = false;
    bool _producerEndClosed = false;
};

}  // namespace producer_consumer_queue_detail

template <typename T, typename CostFunc = producer_consumer_queue_detail::DefaultCostFunction>
using MultiProducerMultiConsumerQueue =
    producer_consumer_queue_detail::ProducerConsumerQueue<T, true, true, CostFunc>;

template <typename T, typename CostFunc = producer_consumer_queue_detail::DefaultCostFunction>
using MultiProducerSingleConsumerQueue =
    producer_consumer_queue_detail::ProducerConsumerQueue<T, true, false, CostFunc>;

template <typename T, typename CostFunc = producer_consumer_queue_detail::DefaultCostFunction>
using SingleProducerMultiConsumerQueue =
    producer_consumer_queue_detail::ProducerConsumerQueue<T, false, true, CostFunc>;

template <typename T, typename CostFunc = producer_consumer_queue_detail::DefaultCostFunction>
using SingleProducerSingleConsumerQueue =
    producer_consumer_queue_detail::ProducerConsumerQueue<T, false, false, CostFunc>;

}  // namespace mongo
