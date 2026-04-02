#include "TestLegacy.h"

#include <arpa/inet.h>

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

using namespace TestLegacy;

namespace
{
    sai_object_id_t createAclTable(_In_ sai_object_id_t switchId)
    {
        sai_object_id_t aclTable = SAI_NULL_OBJECT_ID;

        sai_attribute_t attrs[1] = {};
        attrs[0].id = SAI_ACL_TABLE_ATTR_ACL_STAGE;
        attrs[0].value.s32 = SAI_ACL_STAGE_INGRESS;

        EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_ACL_TABLE, &aclTable, switchId, 1, attrs));

        return aclTable;
    }
}

TEST(ProcessingFlowScenarios, applIntentToAsicObjectCrudFlow)
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

    sai_attribute_t setMtu = {};
    setMtu.id = SAI_ROUTER_INTERFACE_ATTR_MTU;
    setMtu.value.u32 = 9100;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rifId, &setMtu));

    sai_attribute_t getMtu = {};
    getMtu.id = SAI_ROUTER_INTERFACE_ATTR_MTU;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->get(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rifId, 1, &getMtu));
    EXPECT_EQ(getMtu.value.u32, 9100u);

    // Simulate link state transition at control-plane layer by toggling admin state.
    sai_attribute_t admin = {};
    admin.id = SAI_PORT_ATTR_ADMIN_STATE;
    admin.value.booldata = true;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_PORT, portId, &admin));

    admin.value.booldata = false;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_PORT, portId, &admin));

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rifId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, portId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, vrId));

    remove_switch(switchId);
}

TEST(ProcessingFlowScenarios, dependencyResolutionRouteNeedsNextHop)
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
    route.destination.addr.ip4 = htonl(0x0a0a0000);
    route.destination.mask.ip4 = htonl(0xffff0000);

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
    nhAttrs[1].value.ipaddr.addr.ip4 = htonl(0x0a0000fe);
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

TEST(ProcessingFlowScenarios, routeChurnCreateAndDelete)
{
    clear_local();

    const uint32_t routeCount = 256;

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
        routes[i].destination.addr.ip4 = htonl(0x0b000000 + i);
        routes[i].destination.mask.ip4 = htonl(0xffffff00);

        EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(&routes[i], 1, &routeAttr));
    }

    for (uint32_t i = 0; i < routeCount; ++i)
    {
        EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&routes[i]));
    }

    remove_switch(switchId);
}

TEST(ProcessingFlowScenarios, aclInPlaceActionUpdate)
{
    clear_local();

    sai_object_id_t switchId = create_switch();
    sai_object_id_t aclTable = createAclTable(switchId);

    sai_object_id_t aclEntry = SAI_NULL_OBJECT_ID;
    sai_attribute_t attrs[4] = {};

    attrs[0].id = SAI_ACL_ENTRY_ATTR_TABLE_ID;
    attrs[0].value.oid = aclTable;

    attrs[1].id = SAI_ACL_ENTRY_ATTR_PRIORITY;
    attrs[1].value.u32 = 100;

    attrs[2].id = SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP;
    attrs[2].value.aclfield.enable = true;
    attrs[2].value.aclfield.data.ip4 = htonl(0xc0a8010a);
    attrs[2].value.aclfield.mask.ip4 = 0xffffffff;

    attrs[3].id = SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION;
    attrs[3].value.aclaction.enable = true;
    attrs[3].value.aclaction.parameter.s32 = SAI_PACKET_ACTION_DROP;

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_ACL_ENTRY, &aclEntry, switchId, 4, attrs));

    // Hitless-like update path: modify action in place without re-creating ACL entry.
    sai_attribute_t updateAction = {};
    updateAction.id = SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION;
    updateAction.value.aclaction.enable = true;
    updateAction.value.aclaction.parameter.s32 = SAI_PACKET_ACTION_FORWARD;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_ACL_ENTRY, aclEntry, &updateAction));

    sai_attribute_t readAction = {};
    readAction.id = SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->get(SAI_OBJECT_TYPE_ACL_ENTRY, aclEntry, 1, &readAction));
    EXPECT_EQ(readAction.value.aclaction.parameter.s32, SAI_PACKET_ACTION_FORWARD);

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ACL_ENTRY, aclEntry));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ACL_TABLE, aclTable));
    remove_switch(switchId);
}
