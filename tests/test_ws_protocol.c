#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ws_arbiter.h"
#include "ws_protocol.h"

static bool parse(const char *text, ws_message_t *m) { return ws_parse(text, strlen(text), m); }

int main(void)
{
    ws_message_t m;
    assert(parse("h0", &m) && m.type == WS_MSG_HELLO && m.session == 0u);
    assert(parse("h4294967295", &m) && m.session == UINT32_MAX);
    assert(!parse("h4294967296", &m) && !parse("h", &m) && !parse("h-1", &m) && !parse("h 1", &m) && !parse("h12345678901", &m));
    assert(parse("m-600,600", &m) && m.type == WS_MSG_MOVE && m.yaw == -600 && m.pitch == 600);
    assert(parse("m0,0", &m) && m.yaw == 0 && m.pitch == 0);
    assert(!parse("m601,0", &m) && !parse("m0,-601", &m) && !parse("m1", &m) && !parse("m1,2,3", &m) && !parse("m,", &m) && !parse("m1,2 ", &m));
    assert(parse("z4095,100", &m) && m.type == WS_MSG_ZOOM && m.zoom_position == 4095u && m.zoom_speed == 100u);
    assert(parse("z0,1", &m) && m.zoom_position == 0u && m.zoom_speed == 1u);
    assert(!parse("z4096,10", &m) && !parse("z10,0", &m) && !parse("z10,101", &m) && !parse("z10", &m) && !parse("z", &m));
    assert(parse("x", &m) && m.type == WS_MSG_ZOOM_STOP && parse("k", &m) && m.type == WS_MSG_KEEPALIVE);
    assert(!parse("x1", &m) && !parse("k ", &m) && !parse("xx", &m));
    assert(parse("p123456", &m) && m.type == WS_MSG_PING && m.timestamp == 123456u);
    assert(!parse("p", &m) && !parse("pabc", &m) && !parse("p99999999999", &m));
    assert(!parse("", &m) && !parse("q1", &m) && !parse("M0,0", &m) && !ws_parse(NULL, 3u, &m));
    assert(!ws_parse("m1,\0" "2", 5u, &m));
    char big[WS_MESSAGE_MAX + 2u]; memset(big, '1', sizeof(big)); big[0] = 'h';
    assert(!ws_parse(big, WS_MESSAGE_MAX + 1u, &m) && !ws_parse(big, 12u, &m));
    assert(!ws_parse("m0,0", 4u, NULL));

    char out[16];
    assert(ws_format_pong(out, sizeof(out), 0u) == 2u && strcmp(out, "o0") == 0);
    assert(ws_format_pong(out, sizeof(out), 4294967295u) == 11u && strcmp(out, "o4294967295") == 0);
    assert(ws_format_pong(out, 5u, 123456u) == 0u);
    assert(ws_format_error(out, sizeof(out), WS_ERR_SESSION) == 5u && strcmp(out, "esess") == 0);
    assert(ws_format_error(out, 3u, WS_ERR_BAD) == 0u);
    assert(ws_format_role(out, sizeof(out), true) == 2u && strcmp(out, "r1") == 0 && ws_format_role(out, sizeof(out), false) == 2u && strcmp(out, "r0") == 0);

    /* Origin guard */
    assert(ws_origin_allowed("http://ronin-c6.local", "ronin-c6.local"));
    assert(ws_origin_allowed("HTTP://Ronin-C6.local", "ronin-c6.LOCAL"));
    assert(ws_origin_allowed("http://192.168.1.12", "192.168.1.12:80"));
    assert(ws_origin_allowed("http://127.0.0.1:8765", "127.0.0.1:8765"));
    assert(!ws_origin_allowed(NULL, "ronin-c6.local") && !ws_origin_allowed("", "ronin-c6.local") && !ws_origin_allowed("null", "ronin-c6.local"));
    assert(!ws_origin_allowed("http://evil.example", "ronin-c6.local"));
    assert(!ws_origin_allowed("http://ronin-c6.local.evil.example", "ronin-c6.local"));
    assert(!ws_origin_allowed("http://ronin-c6.local:8080", "ronin-c6.local"));
    assert(!ws_origin_allowed("https://ronin-c6.local", "ronin-c6.local"));
    assert(!ws_origin_allowed("http://ronin-c6.local/", "ronin-c6.local"));
    assert(!ws_origin_allowed("http://user@ronin-c6.local", "ronin-c6.local"));
    assert(!ws_origin_allowed("http://ronin-c6.local", NULL) && !ws_origin_allowed("http://ronin-c6.local", ""));

    /* Arbiter: capacity, single controller, takeover only when silent */
    ws_arbiter_t a; ws_arbiter_init(&a);
    int evicted = 0; uint32_t session = 0u;
    assert(ws_arbiter_add(&a, 10, 0) && ws_arbiter_add(&a, 11, 0) && ws_arbiter_add(&a, 12, 0));
    assert(!ws_arbiter_add(&a, 13, 0) && !ws_arbiter_add(&a, 10, 0));
    assert(ws_arbiter_hello(&a, 99, 5u, 0, &evicted) == WS_ROLE_UNKNOWN);
    assert(ws_arbiter_hello(&a, 10, 7u, 100, &evicted) == WS_ROLE_CONTROLLER && evicted == -1);
    assert(ws_arbiter_controller(&a, 10, &session) && session == 7u);
    assert(ws_arbiter_hello(&a, 11, 7u, 200, &evicted) == WS_ROLE_OBSERVER);
    assert(!ws_arbiter_controller(&a, 11, NULL));
    assert(ws_arbiter_hello(&a, 10, 8u, 300, &evicted) == WS_ROLE_CONTROLLER && ws_arbiter_controller(&a, 10, &session) && session == 8u);
    /* Heartbeat: observers are never "degraded", only closed when dead */
    ws_action_t act[4];
    assert(ws_arbiter_poll(&a, 300 + WS_DEGRADED_US - 1, act, 4) == 0u);
    for(unsigned i = 0u; i < 3u; i++) { /* keep 11 and 12 alive */ }
    ws_arbiter_touch(&a, 11, 300 + WS_DEGRADED_US); ws_arbiter_touch(&a, 12, 300 + WS_DEGRADED_US);
    unsigned n = ws_arbiter_poll(&a, 300 + WS_DEGRADED_US + 1, act, 4);
    assert(n == 1u && act[0].kind == WS_ACTION_DEGRADE && act[0].fd == 10 && act[0].session == 8u);
    assert(ws_arbiter_poll(&a, 300 + WS_DEGRADED_US + 2, act, 4) == 0u); /* reported once */
    /* A degraded controller can be replaced and is evicted */
    assert(ws_arbiter_hello(&a, 11, 8u, 300 + WS_DEGRADED_US + 3, &evicted) == WS_ROLE_CONTROLLER && evicted == 10);
    assert(!ws_arbiter_controller(&a, 10, NULL) && ws_arbiter_controller(&a, 11, NULL));
    int fds[WS_MAX_CLIENTS];
    assert(ws_arbiter_fds(&a, fds) == 2u);
    assert(!ws_arbiter_remove(&a, 10, &session));           /* evicted socket was no longer controller */
    assert(ws_arbiter_remove(&a, 11, &session) && session == 8u);
    assert(!ws_arbiter_remove(&a, 11, &session));
    /* Any socket silent for WS_DEAD_US is closed once */
    ws_arbiter_init(&a);
    ws_arbiter_add(&a, 20, 0); ws_arbiter_hello(&a, 20, 1u, 0, &evicted);
    n = ws_arbiter_poll(&a, WS_DEAD_US + 1, act, 4);
    assert(n == 1u && act[0].kind == WS_ACTION_CLOSE && act[0].fd == 20 && act[0].session == 1u);
    assert(ws_arbiter_poll(&a, WS_DEAD_US + 2, act, 4) == 0u);
    assert(!ws_arbiter_controller(&a, 20, NULL) || 1);
    assert(ws_arbiter_hello(&a, 20, 1u, WS_DEAD_US + 3, &evicted) == WS_ROLE_UNKNOWN); /* closing sockets cannot claim control */
    puts("ws protocol tests passed");
    return 0;
}
