/**
 *    Copyright (C) 2021-present MongoDB, Inc.
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

#include "mongo/db/namespace_string.h"
#include "mongo/db/repl/primary_only_service.h"
#include "mongo/db/s/sharding_ddl_coordinator.h"

namespace mongo {

class ShardingDDLCoordinatorService final : public repl::PrimaryOnlyService {
public:
    static constexpr StringData kServiceName = "ShardingDDLCoordinator"_sd;

    explicit ShardingDDLCoordinatorService(ServiceContext* serviceContext)
        : PrimaryOnlyService(serviceContext) {}

    ~ShardingDDLCoordinatorService() = default;

    static ShardingDDLCoordinatorService* getService(OperationContext* opCtx);

    StringData getServiceName() const override {
        return kServiceName;
    }

    NamespaceString getStateDocumentsNS() const override {
        return NamespaceString::kShardingDDLCoordinatorsNamespace;
    }

    ThreadPool::Limits getThreadPoolLimits() const override {
        return ThreadPool::Limits();
    }

    std::shared_ptr<Instance> constructInstance(BSONObj initialState) override;

    std::shared_ptr<Instance> getOrCreateInstance(OperationContext* opCtx, BSONObj initialState);

    // TODO SERVER-53283 remove the following function after 5.0 became last LTS
    void waitForAllCoordinatorsToComplete(OperationContext* opCtx) const;

private:
    std::shared_ptr<ShardingDDLCoordinator> _constructCoordinator(BSONObj initialState) const;

    ExecutorFuture<void> _rebuildService(std::shared_ptr<executor::ScopedTaskExecutor> executor,
                                         const CancellationToken& token) override;

    void _waitForRecoveryCompletion(OperationContext* opCtx) const;
    void _afterStepDown() override;
    size_t _countCoordinatorDocs(OperationContext* opCtx);

    // TODO SERVER-53283 remove the following 3 variables after 5.0 became last LTS
    mutable Mutex _completionMutex =
        MONGO_MAKE_LATCH("ShardingDDLCoordinatorService::_completionMutex");
    size_t _numActiveCoordinators{0};
    mutable stdx::condition_variable _completedCV;

    mutable Mutex _mutex = MONGO_MAKE_LATCH("ShardingDDLCoordinatorService::_mutex");

    // When the node stepDown the state is set to kPaused and all the new DDL operation will be
    // blocked. On step-up we set _coordinatorsToWait to the numbers of coordinators that needs to
    // be recovered and we enter in kRecovering state. Once all coordinators have been recovered we
    // move to kRecovered state and we unblock all new incoming DDL.
    enum class State {
        kPaused,
        kRecovering,
        kRecovered,
    };

    State _state = State::kPaused;

    mutable stdx::condition_variable _recoveredCV;

    // This counter is set up at stepUp and reprensent the number of coordinator instances
    // that needs to be recovered from disk.
    size_t _numCoordinatorsToWait{0};
};

}  // namespace mongo
