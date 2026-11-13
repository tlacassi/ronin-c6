#include "ws_arbiter.h"

static ws_slot_t *find(ws_arbiter_t *arbiter, int fd)
{
    for(unsigned i = 0u; i < WS_MAX_CLIENTS; i++)
    { if(arbiter->slots[i].used && (arbiter->slots[i].fd == fd)) { return &arbiter->slots[i]; } }
    return NULL;
}

void ws_arbiter_init(ws_arbiter_t *arbiter)
{
    for(unsigned i = 0u; i < WS_MAX_CLIENTS; i++) { arbiter->slots[i] = (ws_slot_t){.fd = -1}; }
}

bool ws_arbiter_add(ws_arbiter_t *arbiter, int fd, int64_t now_us)
{
    if((fd < 0) || (find(arbiter, fd) != NULL)) { return false; }
    for(unsigned i = 0u; i < WS_MAX_CLIENTS; i++)
    {
        if(!arbiter->slots[i].used)
        {
            arbiter->slots[i] = (ws_slot_t){.fd = fd, .used = true, .last_rx_us = now_us};
            return true;
        }
    }
    return false;
}

bool ws_arbiter_remove(ws_arbiter_t *arbiter, int fd, uint32_t *session)
{
    ws_slot_t *slot = find(arbiter, fd);
    if(slot == NULL) { return false; }
    bool was_controller = slot->controller;
    if(was_controller && (session != NULL)) { *session = slot->session; }
    *slot = (ws_slot_t){.fd = -1};
    return was_controller;
}

void ws_arbiter_touch(ws_arbiter_t *arbiter, int fd, int64_t now_us)
{
    ws_slot_t *slot = find(arbiter, fd);
    if(slot != NULL) { slot->last_rx_us = now_us; slot->degraded = false; }
}

bool ws_arbiter_controller(const ws_arbiter_t *arbiter, int fd, uint32_t *session)
{
    for(unsigned i = 0u; i < WS_MAX_CLIENTS; i++)
    {
        const ws_slot_t *slot = &arbiter->slots[i];
        if(slot->used && (slot->fd == fd) && slot->controller && !slot->closing)
        {
            if(session != NULL) { *session = slot->session; }
            return true;
        }
    }
    return false;
}

ws_role_t ws_arbiter_hello(ws_arbiter_t *arbiter, int fd, uint32_t session, int64_t now_us, int *evicted)
{
    if(evicted != NULL) { *evicted = -1; }
    ws_slot_t *self = find(arbiter, fd);
    if((self == NULL) || self->closing) { return WS_ROLE_UNKNOWN; }
    for(unsigned i = 0u; i < WS_MAX_CLIENTS; i++)
    {
        ws_slot_t *other = &arbiter->slots[i];
        if(other->used && other->controller && (other != self))
        {
            if(!other->degraded) { return WS_ROLE_OBSERVER; }
            other->controller = false;
            other->closing = true;
            if(evicted != NULL) { *evicted = other->fd; }
        }
    }
    self->controller = true;
    self->session = session;
    self->last_rx_us = now_us;
    self->degraded = false;
    return WS_ROLE_CONTROLLER;
}

unsigned ws_arbiter_poll(ws_arbiter_t *arbiter, int64_t now_us, ws_action_t *actions, unsigned capacity)
{
    unsigned count = 0u;
    for(unsigned i = 0u; (i < WS_MAX_CLIENTS) && (count < capacity); i++)
    {
        ws_slot_t *slot = &arbiter->slots[i];
        if(!slot->used || slot->closing) { continue; }
        int64_t age = now_us - slot->last_rx_us;
        if(age > WS_DEAD_US)
        {
            slot->closing = true;
            actions[count++] = (ws_action_t){WS_ACTION_CLOSE, slot->fd, slot->controller ? slot->session : 0u};
        }
        else if(slot->controller && !slot->degraded && (age > WS_DEGRADED_US))
        {
            slot->degraded = true;
            actions[count++] = (ws_action_t){WS_ACTION_DEGRADE, slot->fd, slot->session};
        }
    }
    return count;
}

unsigned ws_arbiter_fds(const ws_arbiter_t *arbiter, int fds[WS_MAX_CLIENTS])
{
    unsigned count = 0u;
    for(unsigned i = 0u; i < WS_MAX_CLIENTS; i++)
    { if(arbiter->slots[i].used && !arbiter->slots[i].closing) { fds[count++] = arbiter->slots[i].fd; } }
    return count;
}
