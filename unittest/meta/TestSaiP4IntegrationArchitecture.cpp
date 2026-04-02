#include "TestLegacy.h"

#include "sairediscommon.h"

#include <arpa/inet.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace TestLegacy;
using namespace sairediscommon;

namespace
{
    enum class P4ManagerKind
    {
        Acl,
        Route,
        Unknown
    };

    P4ManagerKind classifyP4TableKey(
            _In_ const std::string& key)
    {
        static const std::string kAcl = "P4RT_TABLE:ACL_TABLE:";
        static const std::string kRoute = "P4RT_TABLE:IPV4_TABLE:";

        if (key.rfind(kAcl, 0) == 0)
        {
            return P4ManagerKind::Acl;
        }

        if (key.rfind(kRoute, 0) == 0)
        {
            return P4ManagerKind::Route;
        }

        return P4ManagerKind::Unknown;
    }
}

TEST(SaiP4IntegrationArchitecture, routeBulkProgrammingRespectsDependencies)
{
    clear_local();

    sai_object_id_t switchId = create_switch();
    sai_object_id_t vrId = create_virtual_router(switchId);
    sai_object_id_t nhId = create_next_hop(switchId);

    sai_route_entry_t routes[2] = {};
    for (size_t i = 0; i < 2; ++i)
    {
        routes[i].switch_id = switchId;
        routes[i].vr_id = vrId;
        routes[i].destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        routes[i].destination.addr.ip4 = htonl(0x0e000000 + static_cast<uint32_t>(i));
        routes[i].destination.mask.ip4 = htonl(0xffffff00);
    }

    sai_attribute_t routeAttrOk = {};
    routeAttrOk.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    routeAttrOk.value.oid = nhId;

    sai_attribute_t routeAttrBad = {};
    routeAttrBad.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
    routeAttrBad.value.oid = create_dummy_object_id(SAI_OBJECT_TYPE_NEXT_HOP, switchId);

    uint32_t attrCount[2] = {1, 1};
    const sai_attribute_t* attrList[2] = {&routeAttrOk, &routeAttrBad};
    sai_status_t objectStatuses[2] = {SAI_STATUS_FAILURE, SAI_STATUS_FAILURE};

    auto rc = g_meta->bulkCreate(
            2,
            routes,
            attrCount,
            attrList,
            SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
            objectStatuses);

    EXPECT_EQ(SAI_STATUS_FAILURE, rc);
    EXPECT_EQ(SAI_STATUS_SUCCESS, objectStatuses[0]);
    EXPECT_NE(SAI_STATUS_SUCCESS, objectStatuses[1]);

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(&routes[0]));

    remove_switch(switchId);
}

TEST(SaiP4IntegrationArchitecture, demuxContractForP4TableKeys)
{
    const std::string aclKey =
        "P4RT_TABLE:ACL_TABLE:{\"vrf_id\":\"vrf-0\",\"rule\":\"allow\"}";
    const std::string routeKey =
        "P4RT_TABLE:IPV4_TABLE:{\"vrf_id\":\"vrf-0\",\"dst_ip\":\"10.0.0.1/32\"}";
    const std::string unknownKey =
        "P4RT_TABLE:COUNTER_TABLE:{\"name\":\"c1\"}";

    EXPECT_EQ(P4ManagerKind::Acl, classifyP4TableKey(aclKey));
    EXPECT_EQ(P4ManagerKind::Route, classifyP4TableKey(routeKey));
    EXPECT_EQ(P4ManagerKind::Unknown, classifyP4TableKey(unknownKey));
}

TEST(SaiP4IntegrationArchitecture, redisDataContractKeyLayout)
{
    EXPECT_STREQ("ASIC_STATE", ASIC_STATE_TABLE);
    EXPECT_STREQ("create", REDIS_ASIC_STATE_COMMAND_CREATE);
    EXPECT_STREQ("bulkcreate", REDIS_ASIC_STATE_COMMAND_BULK_CREATE);

    auto switchId = create_dummy_object_id(SAI_OBJECT_TYPE_SWITCH, SAI_NULL_OBJECT_ID);
    auto objectType = sai_serialize_object_type(SAI_OBJECT_TYPE_SWITCH);
    auto objectId = sai_serialize_object_id(switchId);

    std::string protocolKey = objectType + ":" + objectId;
    std::string asicDbKey = std::string(ASIC_STATE_TABLE) + ":" + protocolKey;

    EXPECT_NE(std::string::npos, protocolKey.find("SAI_OBJECT_TYPE_SWITCH:oid:0x"));
    EXPECT_NE(std::string::npos, asicDbKey.find("ASIC_STATE:SAI_OBJECT_TYPE_SWITCH:oid:0x"));

    const std::string versionsKey = "VERSIONS:APP_DB";
    EXPECT_NE(std::string::npos, versionsKey.find(':'));
}

TEST(SaiP4IntegrationArchitecture, attributeMappingTypeAndEnumConstraints)
{
    auto packetActionMeta = sai_metadata_get_attr_metadata(
            SAI_OBJECT_TYPE_ROUTE_ENTRY,
            SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION);

    ASSERT_NE(packetActionMeta, nullptr);
    EXPECT_TRUE(packetActionMeta->isenum);
    EXPECT_EQ(SAI_ATTR_VALUE_TYPE_S32, packetActionMeta->attrvaluetype);
    EXPECT_TRUE(sai_metadata_is_allowed_enum_value(packetActionMeta, SAI_PACKET_ACTION_FORWARD));
    EXPECT_FALSE(sai_metadata_is_allowed_enum_value(packetActionMeta, 0x7fffffff));

    auto laneMeta = sai_metadata_get_attr_metadata(
            SAI_OBJECT_TYPE_PORT,
            SAI_PORT_ATTR_HW_LANE_LIST);

    ASSERT_NE(laneMeta, nullptr);
    EXPECT_TRUE(SAI_HAS_FLAG_MANDATORY_ON_CREATE(laneMeta->flags));
    EXPECT_TRUE(SAI_HAS_FLAG_CREATE_ONLY(laneMeta->flags));
}

TEST(SaiP4IntegrationArchitecture, readOnlyAndCreateOnlyWriteProtection)
{
    clear_local();

    sai_object_id_t switchId = create_switch();
    sai_object_id_t portId = create_port(switchId);

    sai_attribute_t laneSet = {};
    laneSet.id = SAI_PORT_ATTR_HW_LANE_LIST;
    uint32_t laneValues[1] = {999};
    laneSet.value.u32list.count = 1;
    laneSet.value.u32list.list = laneValues;

    // HW lane list is CREATE_ONLY and must reject post-create SET.
    EXPECT_NE(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_PORT, portId, &laneSet));

    sai_attribute_t cpuPortSet = {};
    cpuPortSet.id = SAI_SWITCH_ATTR_CPU_PORT;
    cpuPortSet.value.oid = SAI_NULL_OBJECT_ID;

    // CPU port is READ_ONLY and must reject write attempts.
    EXPECT_NE(SAI_STATUS_SUCCESS, g_meta->set(SAI_OBJECT_TYPE_SWITCH, switchId, &cpuPortSet));

    EXPECT_EQ(SAI_STATUS_SUCCESS, g_meta->remove(SAI_OBJECT_TYPE_PORT, portId));
    remove_switch(switchId);
}
