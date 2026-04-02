#include "TestLegacy.h"

#include <arpa/inet.h>

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

using namespace TestLegacy;

namespace
{
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

TEST(PerformanceConsistencyStrategy, bulkBatchingLargeRouteProgramming)
{
    clear_local();

    constexpr uint32_t kRouteCount = 512;

    sai_object_id_t switchId = create_switch();
    sai_object_id_t vrId = create_virtual_router(switchId);
    sai_object_id_t nhId = create_next_hop(switchId);

    std::vector<sai_route_entry_t> routes(kRouteCount);
    std::vector<uint32_t> attrCounts(kRouteCount, 1);
    std::vector<const sai_attribute_t*> attrLists(kRouteCount, nullptr);
    std::vector<sai_status_t> statuses(kRouteCount, SAI_STATUS_FAILURE);

    sai_attribute_t routeAttr = {};
    routeAttr.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    routeAttr.value.oid = nhId;

    for (uint32_t i = 0; i < kRouteCount; ++i)
    {
        routes[i] = makeRoute(switchId, vrId, 0x10000000 + i);
        attrLists[i] = &routeAttr;
    }

    EXPECT_EQ(
            SAI_STATUS_SUCCESS,
            g_meta->bulkCreate(
                kRouteCount,
                routes.data(),
                attrCounts.data(),
                attrLists.data(),
                SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
                statuses.data()));

    for (uint32_t i = 0; i < kRouteCount; ++i)
    {
        EXPECT_EQ(SAI_STATUS_SUCCESS, statuses[i]);
        EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&routes[i]));
    }

    remove_switch(switchId);
}

TEST(PerformanceConsistencyStrategy, coalescingStyleLastWriteWins)
{
    clear_local();

    sai_object_id_t switchId = create_switch();
    sai_object_id_t portId = create_port(switchId);

    sai_attribute_t admin = {};
    admin.id = SAI_PORT_ATTR_ADMIN_STATE;

    admin.value.booldata = true;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_PORT, portId, &admin));

    admin.value.booldata = false;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_PORT, portId, &admin));

    admin.value.booldata = true;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_PORT, portId, &admin));

    sai_attribute_t readAdmin = {};
    readAdmin.id = SAI_PORT_ATTR_ADMIN_STATE;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->get(SAI_OBJECT_TYPE_PORT, portId, 1, &readAdmin));
    EXPECT_TRUE(readAdmin.value.booldata);

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, portId));
    remove_switch(switchId);
}

TEST(PerformanceConsistencyStrategy, hitlessStyleRouteAttributeUpdate)
{
    clear_local();

    sai_object_id_t switchId = create_switch();
    sai_object_id_t vrId = create_virtual_router(switchId);
    sai_object_id_t nhId = create_next_hop(switchId);

    auto route = makeRoute(switchId, vrId, 0x0a550000);

    sai_attribute_t attrs[2] = {};
    attrs[0].id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    attrs[0].value.oid = nhId;
    attrs[1].id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    attrs[1].value.s32 = SAI_PACKET_ACTION_FORWARD;

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(&route, 2, attrs));

    sai_object_meta_key_t key = {
        .objecttype = SAI_OBJECT_TYPE_ROUTE_ENTRY,
        .objectkey = {
            .key = {
                .route_entry = route,
            },
        },
    };

    EXPECT_TRUE(g_meta->objectExists(key));

    sai_attribute_t update = {};
    update.id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    update.value.s32 = SAI_PACKET_ACTION_DROP;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(&route, &update));

    EXPECT_TRUE(g_meta->objectExists(key));

    update.value.s32 = SAI_PACKET_ACTION_FORWARD;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(&route, &update));
    EXPECT_TRUE(g_meta->objectExists(key));

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&route));
    remove_switch(switchId);
}

TEST(PerformanceConsistencyStrategy, idempotenceAndRetryOnDependencyFailure)
{
    clear_local();

    sai_object_id_t switchId = create_switch();
    sai_object_id_t portId = create_port(switchId);
    sai_object_id_t vrId = create_virtual_router(switchId);

    sai_attribute_t rifAttrs[3] = {};
    rifAttrs[0].id = SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID;
    rifAttrs[0].value.oid = vrId;
    rifAttrs[1].id = SAI_ROUTER_INTERFACE_ATTR_TYPE;
    rifAttrs[1].value.s32 = SAI_ROUTER_INTERFACE_TYPE_PORT;
    rifAttrs[2].id = SAI_ROUTER_INTERFACE_ATTR_PORT_ID;
    rifAttrs[2].value.oid = portId;

    sai_object_id_t rifId = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_ROUTER_INTERFACE, &rifId, switchId, 3, rifAttrs));

    sai_attribute_t admin = {};
    admin.id = SAI_PORT_ATTR_ADMIN_STATE;
    admin.value.booldata = true;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_PORT, portId, &admin));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_PORT, portId, &admin));

    auto route = makeRoute(switchId, vrId, 0x0a660000);

    sai_attribute_t routeAttrs[2] = {};
    routeAttrs[0].id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    routeAttrs[0].value.oid = create_dummy_object_id(SAI_OBJECT_TYPE_NEXT_HOP, switchId);
    routeAttrs[1].id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    routeAttrs[1].value.s32 = SAI_PACKET_ACTION_FORWARD;

    EXPECT_NE(SAI_STATUS_SUCCESS, g_meta->create(&route, 2, routeAttrs));

    sai_attribute_t nhAttrs[3] = {};
    nhAttrs[0].id = SAI_NEXT_HOP_ATTR_TYPE;
    nhAttrs[0].value.s32 = SAI_NEXT_HOP_TYPE_IP;
    nhAttrs[1].id = SAI_NEXT_HOP_ATTR_IP;
    nhAttrs[1].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    nhAttrs[1].value.ipaddr.addr.ip4 = htonl(0x0a660001);
    nhAttrs[2].id = SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID;
    nhAttrs[2].value.oid = rifId;

    sai_object_id_t nhId = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP, &nhId, switchId, 3, nhAttrs));

    routeAttrs[0].value.oid = nhId;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(&route, 2, routeAttrs));

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&route));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP, nhId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rifId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, portId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, vrId));

    remove_switch(switchId);
}

TEST(PerformanceConsistencyStrategy, rollbackSafetyNoOrphanOnFailedRouteCreate)
{
    clear_local();

    sai_object_id_t switchId = create_switch();
    sai_object_id_t vrId = create_virtual_router(switchId);
    sai_object_id_t nhId = create_next_hop(switchId);

    sai_route_entry_t route = {};
    route.switch_id = switchId;
    route.vr_id = vrId;
    route.destination.addr_family = static_cast<sai_ip_addr_family_t>(10);
    route.destination.addr.ip4 = htonl(0x0a770000);
    route.destination.mask.ip4 = htonl(0xffffff00);

    sai_attribute_t attrs[2] = {};
    attrs[0].id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    attrs[0].value.oid = nhId;
    attrs[1].id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    attrs[1].value.s32 = SAI_PACKET_ACTION_FORWARD;

    EXPECT_NE(SAI_STATUS_SUCCESS, g_meta->create(&route, 2, attrs));

    sai_object_meta_key_t key = {
        .objecttype = SAI_OBJECT_TYPE_ROUTE_ENTRY,
        .objectkey = {
            .key = {
                .route_entry = route,
            },
        },
    };

    EXPECT_FALSE(g_meta->objectExists(key));

    remove_switch(switchId);
}
