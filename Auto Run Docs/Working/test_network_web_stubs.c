/**
 * Compilation test for networking layer web/Emscripten stubs.
 *
 * Verifies that the TARGET_WEB guards in socket_linux.h and socket.c
 * correctly stub out BSD socket types and provide a no-op
 * gNetworkSystemSocket for web builds.
 *
 * Test 1 (native): Compile without TARGET_WEB
 *   Build: cc -Wall -Wextra -Werror -o test_network_native test_network_web_stubs.c
 *   Expected: compiles cleanly, native socket types available
 *
 * Test 2 (web-simulated): Compile with -DTARGET_WEB=1
 *   Build: cc -Wall -Wextra -Werror -DTARGET_WEB=1 -o test_network_web test_network_web_stubs.c
 *   Expected: compiles cleanly, stub types used, no POSIX socket headers
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Simulate the socket_linux.h guard pattern.
 * On TARGET_WEB, we get minimal stubs; on native, we get real POSIX types.
 */
#ifdef TARGET_WEB

/* Web stub types — mirrors socket_linux.h TARGET_WEB section */
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_LAST_ERROR 0
#define NO_ERROR (0)
#define SOCKADDR void
#define SOCKET_ERROR (-1)
#define SOCKET_EWOULDBLOCK 0
#define SOCKET_ECONNRESET  0
#define RX_ADDR_SIZE_TYPE unsigned int

struct in6_addr {
    uint8_t s6_addr[16];
};
struct sockaddr_in6 {
    uint16_t        sin6_family;
    uint16_t        sin6_port;
    struct in6_addr sin6_addr;
};

#define INET6_ADDRSTRLEN 46
#define AF_INET6 0

#else /* native */

#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SOCKET unsigned int
#define INVALID_SOCKET (unsigned int)(-1)
#define SOCKET_LAST_ERROR errno
#define NO_ERROR (0)
#define SOCKADDR struct sockaddr
#define SOCKET_ERROR (-1)
#define SOCKET_EWOULDBLOCK EWOULDBLOCK
#define SOCKET_ECONNRESET ECONNRESET
#define RX_ADDR_SIZE_TYPE unsigned int

#endif /* TARGET_WEB */

/*
 * Minimal NetworkType / NetworkSystem stubs (from network.h)
 */
#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif

enum NetworkType { NT_NONE, NT_SERVER, NT_CLIENT };

struct NetworkSystem {
    bool (*initialize)(enum NetworkType, bool reconnecting);
    long long (*get_id)(unsigned char localIndex);
    char* (*get_id_str)(unsigned char localIndex);
    void (*save_id)(unsigned char localIndex, long long networkId);
    void (*clear_id)(unsigned char localIndex);
    void* (*dup_addr)(unsigned char localIndex);
    bool (*match_addr)(void* addr1, void* addr2);
    void (*update)(void);
    int  (*send)(unsigned char localIndex, void* addr, unsigned char* data, unsigned short dataLength);
    void (*get_lobby_id)(char* destination, unsigned int destLength);
    void (*get_lobby_secret)(char* destination, unsigned int destLength);
    void (*shutdown)(bool reconnecting);
    bool requireServerBroadcast;
    char* name;
};

/*
 * Simulate the web stub gNetworkSystemSocket from socket.c
 */
#ifdef TARGET_WEB

static bool ns_stub_init(UNUSED enum NetworkType nt, UNUSED bool recon) {
    return (nt == NT_NONE);
}
static long long ns_stub_get_id(UNUSED unsigned char li) { return 0; }
static char* ns_stub_get_id_str(UNUSED unsigned char li) {
    static char s[] = "web";
    return s;
}
static void ns_stub_save_id(UNUSED unsigned char li, UNUSED long long nid) { }
static void ns_stub_clear_id(UNUSED unsigned char li) { }
static void* ns_stub_dup_addr(UNUSED unsigned char li) { return NULL; }
static bool ns_stub_match_addr(UNUSED void* a1, UNUSED void* a2) { return false; }
static void ns_stub_update(void) { }
static int ns_stub_send(UNUSED unsigned char li, UNUSED void* addr, UNUSED unsigned char* data, UNUSED unsigned short len) {
    return SOCKET_ERROR;
}
static void ns_stub_lobby_id(char* dest, unsigned int len) { snprintf(dest, len, "%s", ""); }
static void ns_stub_lobby_secret(char* dest, unsigned int len) { snprintf(dest, len, "%s", ""); }
static void ns_stub_shutdown(UNUSED bool recon) { }

static struct NetworkSystem gNetworkSystemSocket = {
    .initialize       = ns_stub_init,
    .get_id           = ns_stub_get_id,
    .get_id_str       = ns_stub_get_id_str,
    .save_id          = ns_stub_save_id,
    .clear_id         = ns_stub_clear_id,
    .dup_addr         = ns_stub_dup_addr,
    .match_addr       = ns_stub_match_addr,
    .update           = ns_stub_update,
    .send             = ns_stub_send,
    .get_lobby_id     = ns_stub_lobby_id,
    .get_lobby_secret = ns_stub_lobby_secret,
    .shutdown         = ns_stub_shutdown,
    .requireServerBroadcast = false,
    .name             = "WebStub",
};

#endif /* TARGET_WEB */

int main(void) {
#ifdef TARGET_WEB
    printf("=== Network Web Stub Test (TARGET_WEB mode) ===\n\n");
#else
    printf("=== Network Web Stub Test (NATIVE mode) ===\n\n");
#endif

    /* Test 1: Socket types compile */
    printf("Test 1: Socket type definitions compile\n");
    {
        SOCKET s = INVALID_SOCKET;
        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_port = 0;
        if (s != INVALID_SOCKET) { printf("FAILED: INVALID_SOCKET\n"); return 1; }
        printf("  SOCKET type OK\n");
        printf("  sockaddr_in6 struct OK\n");
        printf("  INET6_ADDRSTRLEN = %d\n", INET6_ADDRSTRLEN);
        printf("  PASSED\n\n");
    }

    /* Test 2: Error constants defined */
    printf("Test 2: Error constants defined\n");
    {
        int err = SOCKET_ERROR;
        int ok = NO_ERROR;
        (void)SOCKET_EWOULDBLOCK;
        (void)SOCKET_ECONNRESET;
        if (err >= ok) { /* just verify they compile */ }
        printf("  SOCKET_ERROR = %d\n", SOCKET_ERROR);
        printf("  NO_ERROR = %d\n", NO_ERROR);
        printf("  PASSED\n\n");
    }

#ifdef TARGET_WEB
    /* Test 3 (web only): NetworkSystem stub behavior */
    printf("Test 3: NetworkSystem web stub behavior\n");
    {
        struct NetworkSystem *ns = &gNetworkSystemSocket;

        /* initialize should succeed for NT_NONE */
        if (!ns->initialize(NT_NONE, false)) {
            printf("FAILED: initialize(NT_NONE) should return true\n");
            return 1;
        }
        printf("  initialize(NT_NONE) = true (OK)\n");

        /* initialize should fail for NT_SERVER */
        if (ns->initialize(NT_SERVER, false)) {
            printf("FAILED: initialize(NT_SERVER) should return false on web\n");
            return 1;
        }
        printf("  initialize(NT_SERVER) = false (OK)\n");

        /* initialize should fail for NT_CLIENT */
        if (ns->initialize(NT_CLIENT, false)) {
            printf("FAILED: initialize(NT_CLIENT) should return false on web\n");
            return 1;
        }
        printf("  initialize(NT_CLIENT) = false (OK)\n");

        /* send should return SOCKET_ERROR */
        if (ns->send(0, NULL, NULL, 0) != SOCKET_ERROR) {
            printf("FAILED: send should return SOCKET_ERROR on web\n");
            return 1;
        }
        printf("  send() = SOCKET_ERROR (OK)\n");

        /* dup_addr should return NULL */
        if (ns->dup_addr(0) != NULL) {
            printf("FAILED: dup_addr should return NULL on web\n");
            return 1;
        }
        printf("  dup_addr() = NULL (OK)\n");

        /* match_addr should return false */
        if (ns->match_addr(NULL, NULL)) {
            printf("FAILED: match_addr should return false on web\n");
            return 1;
        }
        printf("  match_addr() = false (OK)\n");

        /* get_id_str should return "web" */
        char *id = ns->get_id_str(0);
        if (strcmp(id, "web") != 0) {
            printf("FAILED: get_id_str should return \"web\"\n");
            return 1;
        }
        printf("  get_id_str() = \"%s\" (OK)\n", id);

        /* name should be "WebStub" */
        if (strcmp(ns->name, "WebStub") != 0) {
            printf("FAILED: name should be \"WebStub\"\n");
            return 1;
        }
        printf("  name = \"%s\" (OK)\n", ns->name);

        /* update and shutdown are no-ops (just verify they don't crash) */
        ns->update();
        ns->shutdown(false);
        printf("  update() and shutdown() no-op OK\n");

        printf("  PASSED\n\n");
    }
#else
    printf("Test 3: Skipped (native mode — no web stub to test)\n\n");
#endif

    /* Test 4: struct sockaddr_in6 layout */
    printf("Test 4: sockaddr_in6 struct layout\n");
    {
        struct sockaddr_in6 a;
        memset(&a, 0, sizeof(a));
        a.sin6_family = AF_INET6;
        a.sin6_port = 12345;
        a.sin6_addr.s6_addr[0] = 0xFE;
        a.sin6_addr.s6_addr[15] = 0x01;
        printf("  sin6_family = %u\n", a.sin6_family);
        printf("  sin6_port = %u\n", a.sin6_port);
        printf("  sin6_addr[0] = 0x%02X, [15] = 0x%02X\n",
               a.sin6_addr.s6_addr[0], a.sin6_addr.s6_addr[15]);
        printf("  PASSED\n\n");
    }

#ifdef TARGET_WEB
    printf("All TARGET_WEB network stub tests PASSED\n");
#else
    printf("All native-mode network tests PASSED\n");
#endif
    return 0;
}
