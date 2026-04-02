#include "TestLegacy.h"

#include <arpa/inet.h>

#include <vector>

#include <gtest/gtest.h>

using namespace TestLegacy;

namespace
{
    enum class Strategy
    {
        SaiOnly,
        SaiPlusP4rt,
    };

    struct SelectionInputs
    {
        bool needProgrammablePipeline = false;
        bool prioritizeOperationalSimplicity = false;
        bool prioritizeStability = true;
        bool needDynamicFeatureVelocity = false;
        bool canAffordControlPlaneOverhead = true;
    };

    Strategy selectStrategy(
            _In_ const SelectionInputs& in)
    {
        int saiOnlyScore = 0;
        int hybridScore = 0;

        // Stability and simplicity favor SAI-only.
        saiOnlyScore += in.prioritizeStability ? 3 : 0;
        saiOnlyScore += in.prioritizeOperationalSimplicity ? 3 : 0;

        // Programmability and fast feature iteration favor SAI+P4RT.
        hybridScore += in.needProgrammablePipeline ? 4 : 0;
        hybridScore += in.needDynamicFeatureVelocity ? 2 : 0;
        hybridScore += in.canAffordControlPlaneOverhead ? 1 : -2;

        return (hybridScore > saiOnlyScore) ? Strategy::SaiPlusP4rt : Strategy::SaiOnly;
    }

    sai_route_entry_t makeRoute(
            _In_ sai_object_id_t switchId,
            _In_ sai_object_id_t vrId,
            _In_ uint32_t prefix)
    {
        sai_route_entry_t route = {};
        route.switch_id = switchId;
        route.vr_id = vrId;
        route.destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        route.destination.addr.ip4 = htonl(prefix);
        route.destination.mask.ip4 = htonl(0xffffff00);
        return route;
    }
}

TEST(OptimizationSelectionCriteria, chooseHybridWhenProgrammabilityIsPrimary)
{
    SelectionInputs in;
    in.needProgrammablePipeline = true;
    in.needDynamicFeatureVelocity = true;
    in.prioritizeOperationalSimplicity = false;
    in.prioritizeStability = false;
    in.canAffordControlPlaneOverhead = true;

    EXPECT_EQ(Strategy::SaiPlusP4rt, selectStrategy(in));
}

TEST(OptimizationSelectionCriteria, chooseSaiOnlyWhenStabilityAndSimplicityDominate)
{
    SelectionInputs in;
    in.needProgrammablePipeline = false;
    in.needDynamicFeatureVelocity = false;
    in.prioritizeOperationalSimplicity = true;
    in.prioritizeStability = true;
    in.canAffordControlPlaneOverhead = false;

    EXPECT_EQ(Strategy::SaiOnly, selectStrategy(in));
}

TEST(OptimizationSelectionCriteria, tradeoffControlPlaneOverheadCanFlipDecision)
{
    SelectionInputs in;
    in.needProgrammablePipeline = true;
    in.needDynamicFeatureVelocity = true;
    in.prioritizeOperationalSimplicity = true;
    in.prioritizeStability = true;

    in.canAffordControlPlaneOverhead = true;
    EXPECT_EQ(Strategy::SaiPlusP4rt, selectStrategy(in));

    in.canAffordControlPlaneOverhead = false;
    EXPECT_EQ(Strategy::SaiOnly, selectStrategy(in));
}

TEST(OptimizationSelectionCriteria, sequentialAndBulkProduceConsistentRoutingState)
{
    clear_local();

    constexpr uint32_t kSequentialCount = 16;
    constexpr uint32_t kBulkCount = 16;

    sai_object_id_t switchId = create_switch();
    sai_object_id_t vrId = create_virtual_router(switchId);
    sai_object_id_t nhId = create_next_hop(switchId);

    sai_attribute_t routeAttr = {};
    routeAttr.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    routeAttr.value.oid = nhId;

    std::vector<sai_route_entry_t> sequentialRoutes(kSequentialCount);
    for (uint32_t i = 0; i < kSequentialCount; ++i)
    {
        sequentialRoutes[i] = makeRoute(switchId, vrId, 0x12000000 + i);
        EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(&sequentialRoutes[i], 1, &routeAttr));
    }

    std::vector<sai_route_entry_t> bulkRoutes(kBulkCount);
    std::vector<uint32_t> attrCounts(kBulkCount, 1);
    std::vector<const sai_attribute_t*> attrLists(kBulkCount, &routeAttr);
    std::vector<sai_status_t> statuses(kBulkCount, SAI_STATUS_FAILURE);

    for (uint32_t i = 0; i < kBulkCount; ++i)
    {
        bulkRoutes[i] = makeRoute(switchId, vrId, 0x13000000 + i);
    }

    EXPECT_EQ(
            SAI_STATUS_SUCCESS,
            g_meta->bulkCreate(
                kBulkCount,
                bulkRoutes.data(),
                attrCounts.data(),
                attrLists.data(),
                SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
                statuses.data()));

    for (uint32_t i = 0; i < kBulkCount; ++i)
    {
        EXPECT_EQ(SAI_STATUS_SUCCESS, statuses[i]);
    }

    for (uint32_t i = 0; i < kSequentialCount; ++i)
    {
        EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&sequentialRoutes[i]));
    }

    for (uint32_t i = 0; i < kBulkCount; ++i)
    {
        EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&bulkRoutes[i]));
    }

    remove_switch(switchId);
}

TEST(OptimizationSelectionCriteria, maintenanceFallbackForVersionDependentAttribute)
{
    // This test encodes the maintenance rule from 3.4: use fallback when attribute is unavailable.
    bool supportsUserTrapId = false;

#ifdef SAI_ROUTE_ENTRY_ATTR_USER_TRAP_ID
    auto md = sai_metadata_get_attr_metadata(
            SAI_OBJECT_TYPE_ROUTE_ENTRY,
            SAI_ROUTE_ENTRY_ATTR_USER_TRAP_ID);
    supportsUserTrapId = (md != nullptr);
#endif

#if SAI_API_VERSION >= 10200
    EXPECT_TRUE(supportsUserTrapId);
#else
    EXPECT_FALSE(supportsUserTrapId);
#endif
}
