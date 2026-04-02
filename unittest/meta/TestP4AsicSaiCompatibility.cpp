#include "TestLegacy.h"

#include <arpa/inet.h>

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

using namespace TestLegacy;

namespace
{
    bool hasAttr(
            _In_ sai_object_type_t objectType,
            _In_ sai_attr_id_t attrId)
    {
        return sai_metadata_get_attr_metadata(objectType, attrId) != nullptr;
    }
}

TEST(P4AsicSaiCompatibility, programmableForwardingModelCoverage)
{
    // P4 forwarding tables are typically mapped onto these standard SAI objects.
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_ROUTE_ENTRY));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_NEXT_HOP));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_NEXT_HOP_GROUP));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_ACL_TABLE));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_ACL_ENTRY));

    EXPECT_TRUE(hasAttr(SAI_OBJECT_TYPE_ROUTE_ENTRY, SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID));
    EXPECT_TRUE(hasAttr(SAI_OBJECT_TYPE_ROUTE_ENTRY, SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION));
    EXPECT_TRUE(hasAttr(SAI_OBJECT_TYPE_ACL_ENTRY, SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP));
    EXPECT_TRUE(hasAttr(SAI_OBJECT_TYPE_ACL_ENTRY, SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION));

#ifdef SAI_OBJECT_TYPE_GENERIC_PROGRAMMABLE
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_GENERIC_PROGRAMMABLE));
#endif
}

TEST(P4AsicSaiCompatibility, p4LikeAndSaiClassicObjectsCanCoexist)
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

    sai_attribute_t nhAttrs[3] = {};
    nhAttrs[0].id = SAI_NEXT_HOP_ATTR_TYPE;
    nhAttrs[0].value.s32 = SAI_NEXT_HOP_TYPE_IP;
    nhAttrs[1].id = SAI_NEXT_HOP_ATTR_IP;
    nhAttrs[1].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    nhAttrs[1].value.ipaddr.addr.ip4 = htonl(0x0a000001);
    nhAttrs[2].id = SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID;
    nhAttrs[2].value.oid = rifId;

    sai_object_id_t nhId = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP, &nhId, switchId, 3, nhAttrs));

    sai_route_entry_t route = {};
    route.switch_id = switchId;
    route.vr_id = vrId;
    route.destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    route.destination.addr.ip4 = htonl(0x0a0b0000);
    route.destination.mask.ip4 = htonl(0xffff0000);

    sai_attribute_t routeAttrs[2] = {};
    routeAttrs[0].id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    routeAttrs[0].value.oid = nhId;
    routeAttrs[1].id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    routeAttrs[1].value.s32 = SAI_PACKET_ACTION_FORWARD;

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(&route, 2, routeAttrs));

    sai_object_id_t aclTable = SAI_NULL_OBJECT_ID;
    sai_attribute_t tableAttr = {};
    tableAttr.id = SAI_ACL_TABLE_ATTR_ACL_STAGE;
    tableAttr.value.s32 = SAI_ACL_STAGE_INGRESS;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_ACL_TABLE, &aclTable, switchId, 1, &tableAttr));

    sai_object_id_t aclEntry = SAI_NULL_OBJECT_ID;
    sai_attribute_t entryAttrs[4] = {};
    entryAttrs[0].id = SAI_ACL_ENTRY_ATTR_TABLE_ID;
    entryAttrs[0].value.oid = aclTable;
    entryAttrs[1].id = SAI_ACL_ENTRY_ATTR_PRIORITY;
    entryAttrs[1].value.u32 = 100;
    entryAttrs[2].id = SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP;
    entryAttrs[2].value.aclfield.enable = true;
    entryAttrs[2].value.aclfield.data.ip4 = htonl(0xc0a80101);
    entryAttrs[2].value.aclfield.mask.ip4 = 0xffffffff;
    entryAttrs[3].id = SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION;
    entryAttrs[3].value.aclaction.enable = true;
    entryAttrs[3].value.aclaction.parameter.s32 = SAI_PACKET_ACTION_DROP;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_ACL_ENTRY, &aclEntry, switchId, 4, entryAttrs));

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ACL_ENTRY, aclEntry));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ACL_TABLE, aclTable));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&route));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP, nhId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rifId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, portId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, vrId));

    remove_switch(switchId);
}

TEST(P4AsicSaiCompatibility, resourceConstraintMetadataCoverage)
{
    // Resource-oriented attributes used to reason about TCAM/queues/buffers capacity.
#ifdef SAI_SWITCH_ATTR_AVAILABLE_IPV4_ROUTE_ENTRY
    EXPECT_TRUE(hasAttr(SAI_OBJECT_TYPE_SWITCH, SAI_SWITCH_ATTR_AVAILABLE_IPV4_ROUTE_ENTRY));
#endif
#ifdef SAI_SWITCH_ATTR_AVAILABLE_IPV6_ROUTE_ENTRY
    EXPECT_TRUE(hasAttr(SAI_OBJECT_TYPE_SWITCH, SAI_SWITCH_ATTR_AVAILABLE_IPV6_ROUTE_ENTRY));
#endif
#ifdef SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE
    EXPECT_TRUE(hasAttr(SAI_OBJECT_TYPE_SWITCH, SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE));
#endif
#ifdef SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE_GROUP
    EXPECT_TRUE(hasAttr(SAI_OBJECT_TYPE_SWITCH, SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE_GROUP));
#endif
#ifdef SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_ENTRY
    EXPECT_TRUE(hasAttr(SAI_OBJECT_TYPE_SWITCH, SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_ENTRY));
#endif

    EXPECT_TRUE(hasAttr(SAI_OBJECT_TYPE_QUEUE, SAI_QUEUE_ATTR_PARENT_SCHEDULER_NODE));

#ifdef SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH
    EXPECT_TRUE(hasAttr(SAI_OBJECT_TYPE_BUFFER_PROFILE, SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH));
#endif
}

TEST(P4AsicSaiCompatibility, bulkProgrammingLatencyBaseline)
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
        routes[i] = {};
        routes[i].switch_id = switchId;
        routes[i].vr_id = vrId;
        routes[i].destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        routes[i].destination.addr.ip4 = htonl(0x0d000000 + i);
        routes[i].destination.mask.ip4 = htonl(0xffffff00);
        attrLists[i] = &routeAttr;
    }

    auto start = std::chrono::steady_clock::now();

    EXPECT_EQ(
            SAI_STATUS_SUCCESS,
            g_meta->bulkCreate(
                kRouteCount,
                routes.data(),
                attrCounts.data(),
                attrLists.data(),
                SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
                statuses.data()));

    auto end = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    for (uint32_t i = 0; i < kRouteCount; ++i)
    {
        EXPECT_EQ(SAI_STATUS_SUCCESS, statuses[i]);
        EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&routes[i]));
    }

    // Keep threshold loose to avoid platform noise while still guarding regressions.
    EXPECT_LT(elapsedMs, 10000);

    remove_switch(switchId);
}
