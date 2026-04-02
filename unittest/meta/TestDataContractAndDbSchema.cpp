#include "TestLegacy.h"

#include "sai_serialize.h"
#include "sairediscommon.h"

#include <string>

#include <gtest/gtest.h>

using namespace TestLegacy;
using namespace sairediscommon;

namespace
{
    bool versionCompatible(
            _In_ const std::string& expected,
            _In_ const std::string& actual)
    {
        return expected == actual;
    }
}

TEST(DataContractAndDbSchema, applDbP4rtKeySchemaContract)
{
    const std::string key =
        "P4RT_TABLE:IPV4_TABLE:{\"vrf_id\":\"vrf-0\",\"dst_ip\":\"10.0.0.1/32\"}";

    const auto firstSep = key.find(':');
    const auto secondSep = key.find(':', firstSep + 1);

    ASSERT_NE(std::string::npos, firstSep);
    ASSERT_NE(std::string::npos, secondSep);

    EXPECT_EQ("P4RT_TABLE", key.substr(0, firstSep));
    EXPECT_EQ("IPV4_TABLE", key.substr(firstSep + 1, secondSep - firstSep - 1));
    EXPECT_EQ('{', key[secondSep + 1]);
}

TEST(DataContractAndDbSchema, asicDbSchemaKeyUsesObjectTypeAndOid)
{
    const auto switchId = create_dummy_object_id(SAI_OBJECT_TYPE_SWITCH, SAI_NULL_OBJECT_ID);
    const auto objectType = sai_serialize_object_type(SAI_OBJECT_TYPE_SWITCH);
    const auto oid = sai_serialize_object_id(switchId);

    const std::string asicKey = std::string(ASIC_STATE_TABLE) + ":" + objectType + ":" + oid;

    EXPECT_NE(std::string::npos, asicKey.find("ASIC_STATE:SAI_OBJECT_TYPE_SWITCH:oid:0x"));
}

TEST(DataContractAndDbSchema, stateDbFeedbackSchemaPortStatus)
{
    const std::string key = "PORT_TABLE:Ethernet0";
    const std::string oper = "oper_status";
    const std::string speed = "speed";
    const std::string lanes = "lanes";

    EXPECT_EQ("PORT_TABLE", key.substr(0, key.find(':')));
    EXPECT_EQ("Ethernet0", key.substr(key.find(':') + 1));
    EXPECT_EQ("oper_status", oper);
    EXPECT_EQ("speed", speed);
    EXPECT_EQ("lanes", lanes);
}

TEST(DataContractAndDbSchema, versionsContractCompatibilityCheck)
{
    const std::string versionsKey = "VERSIONS";
    const std::string appDbField = "APP_DB";

    EXPECT_EQ("VERSIONS", versionsKey);
    EXPECT_EQ("APP_DB", appDbField);

    EXPECT_TRUE(versionCompatible("1.0.2", "1.0.2"));
    EXPECT_FALSE(versionCompatible("1.0.2", "1.0.1"));
}

TEST(DataContractAndDbSchema, mappingTypeDefaultAndConstraintMetadata)
{
    const auto speedMd = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_PORT, SAI_PORT_ATTR_SPEED);
    ASSERT_NE(speedMd, nullptr);
    EXPECT_EQ(SAI_ATTR_VALUE_TYPE_UINT32, speedMd->attrvaluetype);

    const auto adminMd = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_PORT, SAI_PORT_ATTR_ADMIN_STATE);
    ASSERT_NE(adminMd, nullptr);
    EXPECT_TRUE(adminMd->defaultvaluetype != SAI_DEFAULT_VALUE_TYPE_NONE);

    const auto laneMd = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_PORT, SAI_PORT_ATTR_HW_LANE_LIST);
    ASSERT_NE(laneMd, nullptr);
    EXPECT_TRUE(SAI_HAS_FLAG_MANDATORY_ON_CREATE(laneMd->flags));
    EXPECT_TRUE(SAI_HAS_FLAG_CREATE_ONLY(laneMd->flags));
}

TEST(DataContractAndDbSchema, mappingDeserializeStringToTypedAttributeValue)
{
    const auto speedMd = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_PORT, SAI_PORT_ATTR_SPEED);
    ASSERT_NE(speedMd, nullptr);

    sai_attribute_t speedAttr = {};
    speedAttr.id = SAI_PORT_ATTR_SPEED;
    sai_deserialize_attr_value("100000", *speedMd, speedAttr, false);
    EXPECT_EQ(100000u, speedAttr.value.u32);

    const auto switchId = create_dummy_object_id(SAI_OBJECT_TYPE_SWITCH, SAI_NULL_OBJECT_ID);
    const auto oidStr = sai_serialize_object_id(switchId);
    const auto cpuPortMd = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_SWITCH, SAI_SWITCH_ATTR_CPU_PORT);
    ASSERT_NE(cpuPortMd, nullptr);

    sai_attribute_t oidAttr = {};
    oidAttr.id = SAI_SWITCH_ATTR_CPU_PORT;
    sai_deserialize_attr_value(oidStr, *cpuPortMd, oidAttr, false);
    EXPECT_EQ(switchId, oidAttr.value.oid);
}
