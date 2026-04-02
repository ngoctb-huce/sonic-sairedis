#include "TestLegacy.h"

#include <arpa/inet.h>

#include <cstring>

#include <gtest/gtest.h>

using namespace TestLegacy;

TEST(FunctionalCriteria, apiCorrectnessCoreObjectsCrud)
{
    clear_local();

    sai_object_id_t switch_id = create_switch();
    sai_object_id_t port_id = create_port(switch_id);

    sai_attribute_t attr;
    memset(&attr, 0, sizeof(attr));
    attr.id = SAI_PORT_ATTR_ADMIN_STATE;
    attr.value.booldata = true;

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    sai_attribute_t get_attr;
    memset(&get_attr, 0, sizeof(get_attr));
    get_attr.id = SAI_PORT_ATTR_ADMIN_STATE;

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->get(SAI_OBJECT_TYPE_PORT, port_id, 1, &get_attr));
    EXPECT_TRUE(get_attr.value.booldata);

    sai_object_id_t lag_id = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_LAG, &lag_id, switch_id, 0, NULL));

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
    nh_attrs[1].value.ipaddr.addr.ip4 = htonl(0x0a000001);
    nh_attrs[2].id = SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID;
    nh_attrs[2].value.oid = rif_id;

    sai_object_id_t nh_id = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP, &nh_id, switch_id, 3, nh_attrs));

    sai_object_id_t nhg_id = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP_GROUP, &nhg_id, switch_id, 0, NULL));

    sai_route_entry_t route_entry;
    memset(&route_entry, 0, sizeof(route_entry));
    route_entry.switch_id = switch_id;
    route_entry.vr_id = vr_id;
    route_entry.destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    route_entry.destination.addr.ip4 = htonl(0x0a010000);
    route_entry.destination.mask.ip4 = htonl(0xffff0000);

    sai_attribute_t route_attrs[2] = {};
    route_attrs[0].id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    route_attrs[0].value.oid = nh_id;
    route_attrs[1].id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    route_attrs[1].value.s32 = SAI_PACKET_ACTION_FORWARD;

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(&route_entry, 2, route_attrs));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&route_entry));

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP_GROUP, nhg_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP, nh_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rif_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_LAG, lag_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, port_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, vr_id));

    remove_switch(switch_id);
}

TEST(FunctionalCriteria, stateConsistencyAndDependencyOrdering)
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

    sai_route_entry_t route_entry;
    memset(&route_entry, 0, sizeof(route_entry));
    route_entry.switch_id = switch_id;
    route_entry.vr_id = vr_id;
    route_entry.destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    route_entry.destination.addr.ip4 = htonl(0x0a020000);
    route_entry.destination.mask.ip4 = htonl(0xffff0000);

    sai_attribute_t route_attrs[2] = {};
    route_attrs[0].id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    route_attrs[0].value.oid = create_dummy_object_id(SAI_OBJECT_TYPE_NEXT_HOP, switch_id);
    route_attrs[1].id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    route_attrs[1].value.s32 = SAI_PACKET_ACTION_FORWARD;

    // Route programming must fail when dependent Next Hop is not visible in meta DB.
    EXPECT_NE(SAI_STATUS_SUCCESS, g_meta->create(&route_entry, 2, route_attrs));

    sai_attribute_t nh_attrs[3] = {};
    nh_attrs[0].id = SAI_NEXT_HOP_ATTR_TYPE;
    nh_attrs[0].value.s32 = SAI_NEXT_HOP_TYPE_IP;
    nh_attrs[1].id = SAI_NEXT_HOP_ATTR_IP;
    nh_attrs[1].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    nh_attrs[1].value.ipaddr.addr.ip4 = htonl(0x0a000002);
    nh_attrs[2].id = SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID;
    nh_attrs[2].value.oid = rif_id;

    sai_object_id_t nh_id = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP, &nh_id, switch_id, 3, nh_attrs));

    route_attrs[0].value.oid = nh_id;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(&route_entry, 2, route_attrs));

    sai_object_meta_key_t key = {
        .objecttype = SAI_OBJECT_TYPE_ROUTE_ENTRY,
        .objectkey = {
            .key = {
                .route_entry = route_entry,
            },
        },
    };

    EXPECT_TRUE(g_meta->objectExists(key));

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&route_entry));
    EXPECT_FALSE(g_meta->objectExists(key));

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP, nh_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rif_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, port_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, vr_id));

    remove_switch(switch_id);
}

TEST(FunctionalCriteria, apiSemanticValidationAndNoOrphanOnFailure)
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
    nh_attrs[1].value.ipaddr.addr.ip4 = htonl(0x0a00000b);
    nh_attrs[2].id = SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID;
    nh_attrs[2].value.oid = rif_id;

    sai_object_id_t nh_id = SAI_NULL_OBJECT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->create(SAI_OBJECT_TYPE_NEXT_HOP, &nh_id, switch_id, 3, nh_attrs));

    sai_route_entry_t route_entry = {};
    route_entry.switch_id = switch_id;
    route_entry.vr_id = vr_id;
    route_entry.destination.addr_family = static_cast<sai_ip_addr_family_t>(10); // invalid family
    route_entry.destination.addr.ip4 = htonl(0x0a040000);
    route_entry.destination.mask.ip4 = htonl(0xffff0000);

    sai_attribute_t route_attrs[2] = {};
    route_attrs[0].id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    route_attrs[0].value.oid = nh_id;
    route_attrs[1].id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
    route_attrs[1].value.s32 = SAI_PACKET_ACTION_FORWARD;

    EXPECT_NE(SAI_STATUS_SUCCESS, g_meta->create(&route_entry, 2, route_attrs));

    sai_object_meta_key_t key = {
        .objecttype = SAI_OBJECT_TYPE_ROUTE_ENTRY,
        .objectkey = {
            .key = {
                .route_entry = route_entry,
            },
        },
    };

    // Failed create must not leave stale/orphaned route in meta DB.
    EXPECT_FALSE(g_meta->objectExists(key));

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_NEXT_HOP, nh_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rif_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, port_id));
    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, vr_id));

    remove_switch(switch_id);
}
