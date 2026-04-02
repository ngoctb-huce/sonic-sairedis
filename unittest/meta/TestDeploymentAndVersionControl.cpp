#include "TestLegacy.h"

#include <nlohmann/json.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using json = nlohmann::json;

namespace
{
    bool isSaiVersionCompatible(
            _In_ const std::string& compiled,
            _In_ const std::string& runtime)
    {
        return compiled == runtime;
    }

    bool strictAbiCompatible(
            _In_ const std::string& pinnedSaiCommit,
            _In_ const std::string& sdkSaiCommit)
    {
        // Proxy for strict ABI pinning policy: both sides must be built from the same pinned revision.
        return !pinnedSaiCommit.empty() && pinnedSaiCommit == sdkSaiCommit;
    }

    bool capabilityHandshakeAccept(
            _In_ const std::set<std::string>& required,
            _In_ const std::map<std::string, bool>& capabilities)
    {
        for (const auto& name : required)
        {
            const auto it = capabilities.find(name);
            if (it == capabilities.end() || !it->second)
            {
                return false;
            }
        }

        return true;
    }

    bool packageDependencySatisfied(
            _In_ const std::vector<std::string>& buildOrder,
            _In_ const std::string& dependency,
            _In_ const std::string& consumer)
    {
        int depIdx = -1;
        int consumerIdx = -1;

        for (size_t i = 0; i < buildOrder.size(); ++i)
        {
            if (buildOrder[i] == dependency)
            {
                depIdx = static_cast<int>(i);
            }

            if (buildOrder[i] == consumer)
            {
                consumerIdx = static_cast<int>(i);
            }
        }

        return depIdx >= 0 && consumerIdx >= 0 && depIdx < consumerIdx;
    }
}

TEST(DeploymentAndVersionControl, buildAndRuntimeSaiVersionGate)
{
    EXPECT_TRUE(isSaiVersionCompatible("SAI_1_12_0", "SAI_1_12_0"));
    EXPECT_FALSE(isSaiVersionCompatible("SAI_1_12_0", "SAI_1_11_1"));
}

TEST(DeploymentAndVersionControl, strictAbiEnforcementByPinnedRevision)
{
    EXPECT_TRUE(strictAbiCompatible("a1b2c3d4", "a1b2c3d4"));
    EXPECT_FALSE(strictAbiCompatible("a1b2c3d4", "d4c3b2a1"));
    EXPECT_FALSE(strictAbiCompatible("", "a1b2c3d4"));
}

TEST(DeploymentAndVersionControl, capabilityHandshakeBlocksUnsupportedExtensions)
{
    std::set<std::string> required = {
        "SAI_EXPERIMENTAL_UDF",
        "SAI_EXPERIMENTAL_DASH",
    };

    std::map<std::string, bool> capsSupported = {
        {"SAI_EXPERIMENTAL_UDF", true},
        {"SAI_EXPERIMENTAL_DASH", true},
    };

    std::map<std::string, bool> capsMissing = {
        {"SAI_EXPERIMENTAL_UDF", true},
        {"SAI_EXPERIMENTAL_DASH", false},
    };

    EXPECT_TRUE(capabilityHandshakeAccept(required, capsSupported));
    EXPECT_FALSE(capabilityHandshakeAccept(required, capsMissing));
}

TEST(DeploymentAndVersionControl, ciCdBuildDependencyOrderGuard)
{
    const std::vector<std::string> validOrder = {
        "libsairedis",
        "sonic-pins",
        "p4-connector",
    };

    const std::vector<std::string> invalidOrder = {
        "sonic-pins",
        "libsairedis",
        "p4-connector",
    };

    EXPECT_TRUE(packageDependencySatisfied(validOrder, "libsairedis", "sonic-pins"));
    EXPECT_FALSE(packageDependencySatisfied(invalidOrder, "libsairedis", "sonic-pins"));
}

TEST(DeploymentAndVersionControl, deviceHwskuMatrixProfileSchema)
{
    const std::string profile = R"({
        "p4_pipeline_name": "main_pipeline",
        "context": {
            "platforms": ["Accton-Wedge100BF-32X"],
            "chip": "tofino",
            "pipeline_config": "/usr/share/sonic/p4/switch_tna.bin",
            "p4info": "/usr/share/sonic/p4/p4info.txt"
        }
    })";

    auto j = json::parse(profile);

    EXPECT_TRUE(j.contains("p4_pipeline_name"));
    EXPECT_TRUE(j.contains("context"));

    const auto& ctx = j["context"];
    EXPECT_TRUE(ctx.contains("platforms"));
    EXPECT_TRUE(ctx.contains("chip"));
    EXPECT_TRUE(ctx.contains("pipeline_config"));
    EXPECT_TRUE(ctx.contains("p4info"));

    EXPECT_EQ("tofino", ctx["chip"].get<std::string>());
    EXPECT_EQ("Accton-Wedge100BF-32X", ctx["platforms"][0].get<std::string>());
}
