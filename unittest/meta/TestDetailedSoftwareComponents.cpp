#include "TestLegacy.h"

#include "sai_serialize.h"
#include "sairediscommon.h"

#include <arpa/inet.h>

#include <vector>

#include <gtest/gtest.h>

using namespace TestLegacy;
using namespace sairediscommon;

namespace
{
    const sai_attr_metadata_t* mustHaveAttr(
            _In_ sai_object_type_t objectType,
            _In_ sai_attr_id_t attrId)
    {
        auto md = sai_metadata_get_attr_metadata(objectType, attrId);
        EXPECT_NE(md, nullptr);
        return md;
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

TEST(DetailedSoftwareComponents, adapterEncodingDecodingObjectIdRoundTrip)
{
    auto switchId = create_dummy_object_id(SAI_OBJECT_TYPE_SWITCH, SAI_NULL_OBJECT_ID);

    auto s = sai_serialize_object_id(switchId);
    EXPECT_NE(std::string::npos, s.find("oid:0x"));

    sai_object_id_t decoded = SAI_NULL_OBJECT_ID;
    sai_deserialize_object_id(s, decoded);
    EXPECT_EQ(switchId, decoded);

    auto ot = sai_serialize_object_type(SAI_OBJECT_TYPE_PORT);
    EXPECT_EQ("SAI_OBJECT_TYPE_PORT", ot);
}

TEST(DetailedSoftwareComponents, sairedisSyncdCommandAndChannelContract)
{
    EXPECT_STREQ("ASIC_STATE", ASIC_STATE_TABLE);
    EXPECT_STREQ("NOTIFICATIONS", REDIS_TABLE_NOTIFICATIONS);
    EXPECT_STREQ("GETRESPONSE", REDIS_TABLE_GETRESPONSE);

    EXPECT_STREQ("create", REDIS_ASIC_STATE_COMMAND_CREATE);
    EXPECT_STREQ("set", REDIS_ASIC_STATE_COMMAND_SET);
    EXPECT_STREQ("get", REDIS_ASIC_STATE_COMMAND_GET);
    EXPECT_STREQ("bulkcreate", REDIS_ASIC_STATE_COMMAND_BULK_CREATE);
    EXPECT_STREQ("notify", REDIS_ASIC_STATE_COMMAND_NOTIFY);

    EXPECT_EQ(std::string("NOTIFICATIONS"), REDIS_TABLE_NOTIFICATIONS_PER_DB("ASIC_DB"));
    EXPECT_EQ(std::string("STATE_DB_NOTIFICATIONS"), REDIS_TABLE_NOTIFICATIONS_PER_DB("STATE_DB"));
}

TEST(DetailedSoftwareComponents, orchObjectCoverageRouteNeighborPortAclQosNhg)
{
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_PORT));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_NEIGHBOR_ENTRY));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_NEXT_HOP_GROUP));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_ACL_TABLE));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_QUEUE));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_SCHEDULER));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_ROUTE_ENTRY));

    mustHaveAttr(SAI_OBJECT_TYPE_PORT, SAI_PORT_ATTR_SPEED);
    mustHaveAttr(SAI_OBJECT_TYPE_NEIGHBOR_ENTRY, SAI_NEIGHBOR_ENTRY_ATTR_DST_MAC_ADDRESS);
    mustHaveAttr(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID);
    mustHaveAttr(SAI_OBJECT_TYPE_ACL_TABLE, SAI_ACL_TABLE_ATTR_ACL_STAGE);
    mustHaveAttr(SAI_OBJECT_TYPE_QUEUE, SAI_QUEUE_ATTR_PARENT_SCHEDULER_NODE);
    mustHaveAttr(SAI_OBJECT_TYPE_SCHEDULER, SAI_SCHEDULER_ATTR_SCHEDULING_ALGORITHM);
    mustHaveAttr(SAI_OBJECT_TYPE_ROUTE_ENTRY, SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID);
}

TEST(DetailedSoftwareComponents, orchDependencyAndBulkExecutionWithNhg)
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
    nhAttrs[1].value.ipaddr.addr.ip4 = htonl(0x0a00000a);
    nhAttrs[2].id = SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID;
    nhAttrs[2].value.oid = rifId;

    sai_object_id_t nhId = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP, &nhId, switchId, 3, nhAttrs));

    sai_object_id_t nhgId = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP_GROUP, &nhgId, switchId, 0, nullptr));

    sai_attribute_t nhgmAttrs[2] = {};
    nhgmAttrs[0].id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID;
    nhgmAttrs[0].value.oid = nhgId;
    nhgmAttrs[1].id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID;
    nhgmAttrs[1].value.oid = nhId;

    sai_object_id_t nhgmId = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, &nhgmId, switchId, 2, nhgmAttrs));

    std::vector<sai_route_entry_t> routes(8);
    std::vector<uint32_t> attrCount(8, 1);
    std::vector<const sai_attribute_t*> attrList(8, nullptr);
    std::vector<sai_status_t> status(8, SAI_STATUS_FAILURE);

    sai_attribute_t routeAttr = {};
    routeAttr.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    routeAttr.value.oid = nhgId;

    for (uint32_t i = 0; i < routes.size(); ++i)
    {
        routes[i] = makeRoute(switchId, vrId, 0x14000000 + i);
        attrList[i] = &routeAttr;
    }

    EXPECT_EQ(
            SAI_STATUS_SUCCESS,
            g_meta->bulkCreate(
                static_cast<uint32_t>(routes.size()),
                routes.data(),
                attrCount.data(),
                attrList.data(),
                SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
                status.data()));

    for (uint32_t i = 0; i < routes.size(); ++i)
    {
        EXPECT_EQ(SAI_STATUS_SUCCESS, status[i]);
        EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&routes[i]));
    }

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, nhgmId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP_GROUP, nhgId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP, nhId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rifId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, portId));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, vrId));

    remove_switch(switchId);
}

TEST(DetailedSoftwareComponents, mappingTypeDefaultAndConstraintSignals)
{
    auto speedMeta = mustHaveAttr(SAI_OBJECT_TYPE_PORT, SAI_PORT_ATTR_SPEED);
    ASSERT_NE(speedMeta, nullptr);
    EXPECT_EQ(SAI_ATTR_VALUE_TYPE_UINT32, speedMeta->attrvaluetype);

    auto laneMeta = mustHaveAttr(SAI_OBJECT_TYPE_PORT, SAI_PORT_ATTR_HW_LANE_LIST);
    ASSERT_NE(laneMeta, nullptr);
    EXPECT_TRUE(SAI_HAS_FLAG_MANDATORY_ON_CREATE(laneMeta->flags));
    EXPECT_TRUE(SAI_HAS_FLAG_CREATE_ONLY(laneMeta->flags));

    auto adminMeta = mustHaveAttr(SAI_OBJECT_TYPE_PORT, SAI_PORT_ATTR_ADMIN_STATE);
    ASSERT_NE(adminMeta, nullptr);
    EXPECT_TRUE(adminMeta->defaultvaluetype != SAI_DEFAULT_VALUE_TYPE_NONE);
}
