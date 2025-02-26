/**
 *    Copyright (C) 2024-present MongoDB, Inc.
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

#include "mongo/db/default_max_time_ms_cluster_parameter.h"

#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/default_max_time_ms_cluster_parameter_gen.h"

namespace mongo {

boost::optional<Milliseconds> getRequestOrDefaultMaxTimeMS(
    OperationContext* opCtx,
    const boost::optional<mongo::IDLAnyType>& requestMaxTimeMS,
    const bool isReadOperation) {

    // Always uses the user-defined value if it's passed in.
    if (requestMaxTimeMS) {
        return Milliseconds{uassertStatusOK(parseMaxTimeMS(requestMaxTimeMS->getElement()))};
    }

    // Currently, defaultMaxTimeMS is only applicable to read operations.
    if (!isReadOperation) {
        return boost::none;
    }

    // If the feature flag is disabled, return early.
    const auto fcvSnapshot = serverGlobalParams.featureCompatibility.acquireFCVSnapshot();
    if (!fcvSnapshot.isVersionInitialized() ||
        !gFeatureFlagDefaultReadMaxTimeMS.isEnabled(fcvSnapshot)) {
        return boost::none;
    }

    const boost::optional<auth::ValidatedTenancyScope>& vts =
        auth::ValidatedTenancyScope::get(opCtx);
    auto tenantId = vts && vts->hasTenantId() ? boost::make_optional(vts->tenantId()) : boost::none;

    // Check if the defaultMaxTimeMS can be bypassed.
    const auto bypassDefaultMaxTimeMS =
        AuthorizationSession::get(opCtx->getClient())
            ->isAuthorizedForActionsOnResource(ResourcePattern::forClusterResource(boost::none),
                                               ActionType::bypassDefaultMaxTimeMS);
    if (bypassDefaultMaxTimeMS) {
        return boost::none;
    }

    auto* clusterParameters = ServerParameterSet::getClusterParameterSet();
    auto* defaultMaxTimeMSParam =
        clusterParameters->get<ClusterParameterWithStorage<DefaultMaxTimeMSParam>>(
            "defaultMaxTimeMS");
    // Uses the tenant specific default value if one was set.
    if (tenantId) {
        auto tenantDefaultReadMaxTimeMS =
            defaultMaxTimeMSParam->getValue(tenantId).getReadOperations();
        if (tenantDefaultReadMaxTimeMS) {
            return Milliseconds{tenantDefaultReadMaxTimeMS};
        }
    }

    // Uses the global default maxTimeMS for read operations.
    auto globalDefaultReadMaxTimeMS =
        defaultMaxTimeMSParam->getValue(boost::none).getReadOperations();
    return Milliseconds{globalDefaultReadMaxTimeMS};
}
}  // namespace mongo
