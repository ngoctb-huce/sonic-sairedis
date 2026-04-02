#include "TestLegacy.h"

#include <arpa/inet.h>

#include <vector>

#include <gtest/gtest.h>

using namespace TestLegacy;

namespace
{
    bool isGenericFailureCode(_In_ sai_status_t status)
    {
        return status == SAI_STATUS_FAILURE ||
               status == SAI_STATUS_INVALID_PARAMETER ||
               status == SAI_STATUS_ITEM_NOT_FOUND ||
               status == SAI_STATUS_NOT_SUPPORTED;
    }
}

TEST(AssessmentResultsAndGaps, passCriteriaCoreObjectPathStable)
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
    nhAttrs[1].value.ipaddr.addr.ip4 = htonl(0x0a00000c);
    nhAttrs[2].id = SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID;
    nhAttrs[2].value.oid = rifId;

    sai_object_id_t nhId = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP, &nhId, switchId, 3, nhAttrs));

    sai_route_entry_t route = {};
    route.switch_id = switchId;
    route.vr_id = vrId;
    route.destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    route.destination.addr.ip4 = htonl(0x0d000000);
    route.destination.mask.ip4 = htonl(0xffffff00);

    sai_attribute_t routeAttr = {};
    routeAttr.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    routeAttr.value.oid = nhId;

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(&route, 1, &routeAttr));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&route));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP, nhId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rifId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, portId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, vrId));

    remove_switch(switchId);
}

TEST(AssessmentResultsAndGaps, gapGranularErrorReportingStillGeneric)
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

    sai_route_entry_t route = {};
    route.switch_id = switchId;
    route.vr_id = vrId;
    route.destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    route.destination.addr.ip4 = htonl(0x0d010000);
    route.destination.mask.ip4 = htonl(0xffff0000);

    sai_attribute_t routeAttrs[2] = {};
    routeAttrs[0].id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    routeAttrs[0].value.oid = create_dummy_object_id(SAI_OBJECT_TYPE_NEXT_HOP, switchId);
    routeAttrs[1].id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    routeAttrs[1].value.s32 = SAI_PACKET_ACTION_FORWARD;

    sai_status_t status = g_meta->create(&route, 2, routeAttrs);

    EXPECT_NE(status, SAI_STATUS_SUCCESS);
    EXPECT_TRUE(isGenericFailureCode(status));

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rifId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, portId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, vrId));

    remove_switch(switchId);
}

TEST(AssessmentResultsAndGaps, gapScaleBaselineRouteBurst)
{
    clear_local();

    const uint32_t routeCount = 512;

    sai_object_id_t switchId = create_switch();
    sai_object_id_t vrId = create_virtual_router(switchId);
    sai_object_id_t nhId = create_next_hop(switchId);

    std::vector<sai_route_entry_t> routes(routeCount);
    sai_attribute_t routeAttr = {};
    routeAttr.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    routeAttr.value.oid = nhId;

    for (uint32_t i = 0; i < routeCount; ++i)
    {
        routes[i] = {};
        routes[i].switch_id = switchId;
        routes[i].vr_id = vrId;
        routes[i].destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        routes[i].destination.addr.ip4 = htonl(0x0d100000 + i);
        routes[i].destination.mask.ip4 = htonl(0xffffff00);

        EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(&routes[i], 1, &routeAttr));
    }

    for (uint32_t i = 0; i < routeCount; ++i)
    {
        EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&routes[i]));
    }

    remove_switch(switchId);
}
