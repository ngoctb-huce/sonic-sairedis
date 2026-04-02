#include "TestLegacy.h"

#include <arpa/inet.h>

#include <cstring>

#include <gtest/gtest.h>

using namespace TestLegacy;

namespace
{
    const sai_attr_metadata_t* requireAttr(
            _In_ sai_object_type_t objectType,
            _In_ sai_attr_id_t attrId)
    {
        auto md = sai_metadata_get_attr_metadata(objectType, attrId);

        EXPECT_NE(md, nullptr);

        if (md != nullptr)
        {
            EXPECT_EQ(md->objecttype, objectType);
        }

        return md;
    }
}

TEST(SaiObjectModelAssessment, coreObjectsMetadataAndAttributes)
{
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_PORT));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_LAG));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_LAG_MEMBER));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_ROUTER_INTERFACE));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_ROUTE_ENTRY));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_NEXT_HOP));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_NEXT_HOP_GROUP));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER));

    requireAttr(SAI_OBJECT_TYPE_PORT, SAI_PORT_ATTR_SPEED);
    requireAttr(SAI_OBJECT_TYPE_PORT, SAI_PORT_ATTR_ADMIN_STATE);
    requireAttr(SAI_OBJECT_TYPE_PORT, SAI_PORT_ATTR_MTU);

    requireAttr(SAI_OBJECT_TYPE_LAG_MEMBER, SAI_LAG_MEMBER_ATTR_LAG_ID);
    requireAttr(SAI_OBJECT_TYPE_LAG_MEMBER, SAI_LAG_MEMBER_ATTR_PORT_ID);

    requireAttr(SAI_OBJECT_TYPE_ROUTER_INTERFACE, SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID);
    requireAttr(SAI_OBJECT_TYPE_ROUTER_INTERFACE, SAI_ROUTER_INTERFACE_ATTR_TYPE);
    requireAttr(SAI_OBJECT_TYPE_ROUTER_INTERFACE, SAI_ROUTER_INTERFACE_ATTR_SRC_MAC_ADDRESS);

    requireAttr(SAI_OBJECT_TYPE_NEXT_HOP, SAI_NEXT_HOP_ATTR_IP);
    requireAttr(SAI_OBJECT_TYPE_NEXT_HOP, SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID);

    requireAttr(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID);
    requireAttr(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID);

    requireAttr(SAI_OBJECT_TYPE_ROUTE_ENTRY, SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID);
}

TEST(SaiObjectModelAssessment, routeDependencyChainViaNextHopGroup)
{
    clear_local();

    sai_object_id_t switch_id = create_switch();
    sai_object_id_t port_id = create_port(switch_id);
    sai_object_id_t vr_id = create_virtual_router(switch_id);

    sai_attribute_t rif_attrs[3] = {};
    rif_attrs[0].id = SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID;
    rif_attrs[0].value.oid = vr_id;
    rif_attrs[1].id = SAI_ROUTER_INTERFACE_ATTR_TYPE;
    rif_attrs[1].value.s32 = SAI_ROUTER_INTERFACE_TYPE_PORT;
    rif_attrs[2].id = SAI_ROUTER_INTERFACE_ATTR_PORT_ID;
    rif_attrs[2].value.oid = port_id;

    sai_object_id_t rif_id = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_ROUTER_INTERFACE, &rif_id, switch_id, 3, rif_attrs));

    sai_attribute_t nh_attrs[3] = {};
    nh_attrs[0].id = SAI_NEXT_HOP_ATTR_TYPE;
    nh_attrs[0].value.s32 = SAI_NEXT_HOP_TYPE_IP;
    nh_attrs[1].id = SAI_NEXT_HOP_ATTR_IP;
    nh_attrs[1].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    nh_attrs[1].value.ipaddr.addr.ip4 = htonl(0x0a00000a);
    nh_attrs[2].id = SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID;
    nh_attrs[2].value.oid = rif_id;

    sai_object_id_t nh_id = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP, &nh_id, switch_id, 3, nh_attrs));

    sai_object_id_t nhg_id = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP_GROUP, &nhg_id, switch_id, 0, NULL));

    sai_attribute_t nhg_member_attrs[2] = {};
    nhg_member_attrs[0].id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID;
    nhg_member_attrs[0].value.oid = nhg_id;
    nhg_member_attrs[1].id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID;
    nhg_member_attrs[1].value.oid = nh_id;

    sai_object_id_t nhg_member_id = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, &nhg_member_id, switch_id, 2, nhg_member_attrs));

    sai_route_entry_t route_entry;
    memset(&route_entry, 0, sizeof(route_entry));
    route_entry.switch_id = switch_id;
    route_entry.vr_id = vr_id;
    route_entry.destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    route_entry.destination.addr.ip4 = htonl(0x0a030000);
    route_entry.destination.mask.ip4 = htonl(0xffff0000);

    sai_attribute_t route_attrs[2] = {};
    route_attrs[0].id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    route_attrs[0].value.oid = nhg_id;
    route_attrs[1].id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    route_attrs[1].value.s32 = SAI_PACKET_ACTION_FORWARD;

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(&route_entry, 2, route_attrs));

    sai_object_meta_key_t route_key = {
        .objecttype = SAI_OBJECT_TYPE_ROUTE_ENTRY,
        .objectkey = {
            .key = {
                .route_entry = route_entry,
            },
        },
    };

    EXPECT_TRUE(g_meta->objectExists(route_key));

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&route_entry));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, nhg_member_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP_GROUP, nhg_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP, nh_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rif_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, port_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, vr_id));

    remove_switch(switch_id);
}

TEST(SaiObjectModelAssessment, controlObjectsMetadataCoverage)
{
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_ACL_TABLE));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_ACL_ENTRY));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_ACL_COUNTER));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_ACL_RANGE));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_QUEUE));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_SCHEDULER));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_SCHEDULER_GROUP));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_BUFFER_PROFILE));
    EXPECT_TRUE(sai_metadata_is_object_type_valid(SAI_OBJECT_TYPE_WRED));

    requireAttr(SAI_OBJECT_TYPE_ACL_TABLE, SAI_ACL_TABLE_ATTR_ACL_STAGE);
    requireAttr(SAI_OBJECT_TYPE_ACL_ENTRY, SAI_ACL_ENTRY_ATTR_TABLE_ID);
    requireAttr(SAI_OBJECT_TYPE_ACL_ENTRY, SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP);
    requireAttr(SAI_OBJECT_TYPE_ACL_ENTRY, SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION);
    requireAttr(SAI_OBJECT_TYPE_ACL_COUNTER, SAI_ACL_COUNTER_ATTR_TABLE_ID);
    requireAttr(SAI_OBJECT_TYPE_ACL_RANGE, SAI_ACL_RANGE_ATTR_TYPE);
    requireAttr(SAI_OBJECT_TYPE_ACL_RANGE, SAI_ACL_RANGE_ATTR_LIMIT);

    requireAttr(SAI_OBJECT_TYPE_QUEUE, SAI_QUEUE_ATTR_TYPE);
    requireAttr(SAI_OBJECT_TYPE_QUEUE, SAI_QUEUE_ATTR_INDEX);
    requireAttr(SAI_OBJECT_TYPE_QUEUE, SAI_QUEUE_ATTR_PARENT_SCHEDULER_NODE);

    requireAttr(SAI_OBJECT_TYPE_SCHEDULER, SAI_SCHEDULER_ATTR_SCHEDULING_ALGORITHM);
    requireAttr(SAI_OBJECT_TYPE_SCHEDULER, SAI_SCHEDULER_ATTR_SCHEDULING_WEIGHT);
    requireAttr(SAI_OBJECT_TYPE_SCHEDULER, SAI_SCHEDULER_ATTR_MIN_BANDWIDTH_RATE);
    requireAttr(SAI_OBJECT_TYPE_SCHEDULER, SAI_SCHEDULER_ATTR_MAX_BANDWIDTH_RATE);

    requireAttr(SAI_OBJECT_TYPE_SCHEDULER_GROUP, SAI_SCHEDULER_GROUP_ATTR_PORT_ID);

#ifdef SAI_BUFFER_PROFILE_ATTR_RESERVED_BUFFER_SIZE
    requireAttr(SAI_OBJECT_TYPE_BUFFER_PROFILE, SAI_BUFFER_PROFILE_ATTR_RESERVED_BUFFER_SIZE);
#endif

#ifdef SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH
    requireAttr(SAI_OBJECT_TYPE_BUFFER_PROFILE, SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH);
#endif

#ifdef SAI_WRED_ATTR_YELLOW_DROP_PROBABILITY
    requireAttr(SAI_OBJECT_TYPE_WRED, SAI_WRED_ATTR_YELLOW_DROP_PROBABILITY);
#endif
}
