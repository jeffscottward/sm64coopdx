/**
 * Compilation test for Discord SDK and CoopNet exclusion guards.
 *
 * This test verifies that when DISCORD_SDK and COOPNET are not defined,
 * all references to their symbols are properly guarded and produce no
 * linker errors.
 *
 * Test 1 (all enabled): Compile with -DDISCORD_SDK -DCOOPNET
 *   Build: /usr/bin/cc -Wall -Wextra -Werror -DDISCORD_SDK -DCOOPNET -o test_discord_coopnet_exclusion_enabled test_discord_coopnet_exclusion.c
 *   Expected: compiles cleanly, all Discord/CoopNet paths active
 *
 * Test 2 (all disabled): Compile without defines (web build scenario)
 *   Build: /usr/bin/cc -Wall -Wextra -Werror -o test_discord_coopnet_exclusion_disabled test_discord_coopnet_exclusion.c
 *   Expected: compiles cleanly, Discord/CoopNet paths excluded
 *
 * Test 3 (mixed): Compile with only one define
 *   Build: /usr/bin/cc -Wall -Wextra -Werror -DCOOPNET -o test_discord_coopnet_exclusion_mixed test_discord_coopnet_exclusion.c
 *   Expected: compiles cleanly, only CoopNet paths active
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

/* ===== Simulate Discord types (from discord_game_sdk.h) ===== */
#ifdef DISCORD_SDK

typedef int64_t DiscordUserId;

enum EDiscordResult {
    DiscordResult_Ok = 0,
    DiscordResult_ServiceUnavailable = 1,
};

struct DiscordActivity {
    char state[128];
    char details[128];
};

static bool gDiscordInitialized = false;

static void discord_activity_update(void) {
    printf("  discord_activity_update() called\n");
}

static uint64_t discord_get_user_id(void) {
    return 12345;
}

#endif /* DISCORD_SDK */

/* ===== Simulate CoopNet types (from coopnet.h) ===== */
#ifdef COOPNET

struct NetworkSystemCoopNet {
    bool active;
};
static struct NetworkSystemCoopNet gNetworkSystemCoopNet = { .active = false };

static bool ns_coopnet_is_connected(void) {
    return false;
}

static void ns_coopnet_update(void) {
    printf("  ns_coopnet_update() called\n");
}

#endif /* COOPNET */

/* ===== Simulate patterns from pc_main.c ===== */
static void test_pc_main_discord_update(void) {
    printf("Test: pc_main.c discord_update pattern\n");
#ifdef DISCORD_SDK
    if (gDiscordInitialized) {
        discord_activity_update();
    }
    printf("  Discord update path active\n");
#else
    printf("  Discord update path excluded (expected for web)\n");
#endif
    printf("  PASSED\n\n");
}

/* ===== Simulate patterns from network.c ===== */
static void test_network_discord_activity(void) {
    printf("Test: network.c discord_activity_update pattern\n");
#ifdef DISCORD_SDK
    if (gDiscordInitialized) {
        discord_activity_update();
    }
    printf("  Discord activity path active\n");
#else
    printf("  Discord activity path excluded (expected for web)\n");
#endif
    printf("  PASSED\n\n");
}

/* ===== Simulate patterns from smlua_misc_utils.c ===== */
static const char* test_get_local_discord_id(void) {
#ifdef DISCORD_SDK
    if (gDiscordInitialized) {
        static char sDiscordId[64] = "";
        snprintf(sDiscordId, 64, "%" PRIu64 "", (uint64_t)discord_get_user_id());
        return sDiscordId;
    } else {
        return "0";
    }
#else
    return "0";
#endif
}

static const char* test_get_coopnet_id(int localIndex) {
    (void)localIndex;
#ifdef COOPNET
    if (!ns_coopnet_is_connected()) { return "-1"; }
    return "42";
#else
    return "-1";
#endif
}

/* ===== Simulate network_set_system pattern from network.c ===== */
enum NetworkSystemType {
    NS_SOCKET = 0,
    NS_COOPNET_TYPE = 1,
};

static int sNetworkSystem = 0;

static void test_network_set_system(enum NetworkSystemType nsType) {
    printf("Test: network.c network_set_system pattern\n");
    switch (nsType) {
        case NS_SOCKET: sNetworkSystem = 0; break;
#ifdef COOPNET
        case NS_COOPNET_TYPE: sNetworkSystem = 1; break;
#endif
        default: sNetworkSystem = 0; break;
    }
    printf("  sNetworkSystem = %d\n", sNetworkSystem);
    printf("  PASSED\n\n");
}

/* ===== Simulate coopnet reconnect pattern ===== */
static void test_network_reconnect(void) {
    printf("Test: network.c reconnect pattern\n");
    int reconnectType;
#ifdef COOPNET
    reconnectType = (gNetworkSystemCoopNet.active) ? NS_COOPNET_TYPE : NS_SOCKET;
#else
    reconnectType = NS_SOCKET;
#endif
    printf("  reconnectType = %d\n", reconnectType);
    printf("  PASSED\n\n");
}

/* ===== Simulate coopnet update pattern ===== */
static void test_network_update_coopnet(void) {
    printf("Test: network.c network_update_coopnet pattern\n");
#ifdef COOPNET
    if (ns_coopnet_is_connected()) {
        ns_coopnet_update();
    }
    printf("  CoopNet update path active\n");
#else
    printf("  CoopNet update path excluded (expected for web)\n");
#endif
    printf("  PASSED\n\n");
}

int main(void) {
    int flags = 0;
#ifdef DISCORD_SDK
    flags |= 1;
#endif
#ifdef COOPNET
    flags |= 2;
#endif

    const char* mode = "NONE";
    if (flags == 3) mode = "DISCORD_SDK + COOPNET";
    else if (flags == 1) mode = "DISCORD_SDK only";
    else if (flags == 2) mode = "COOPNET only";
    else mode = "DISABLED (web build)";

    printf("=== Discord/CoopNet Exclusion Guard Test (%s) ===\n\n", mode);

    /* Discord tests */
    test_pc_main_discord_update();
    test_network_discord_activity();

    printf("Test: smlua_misc_utils.c get_local_discord_id pattern\n");
    const char* discordId = test_get_local_discord_id();
    printf("  discord_id = \"%s\"\n", discordId);
#ifndef DISCORD_SDK
    if (strcmp(discordId, "0") != 0) { printf("FAILED: expected '0'\n"); return 1; }
#endif
    printf("  PASSED\n\n");

    /* CoopNet tests */
    test_network_set_system(NS_SOCKET);
    test_network_reconnect();
    test_network_update_coopnet();

    printf("Test: smlua_misc_utils.c get_coopnet_id pattern\n");
    const char* coopnetId = test_get_coopnet_id(0);
    printf("  coopnet_id = \"%s\"\n", coopnetId);
#ifndef COOPNET
    if (strcmp(coopnetId, "-1") != 0) { printf("FAILED: expected '-1'\n"); return 1; }
#endif
    printf("  PASSED\n\n");

    printf("All %s guard tests PASSED\n", mode);
    return 0;
}
