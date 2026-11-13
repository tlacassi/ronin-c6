#include "ws_protocol.h"
#include <string.h>
#include "motion_parse.h"

static bool parse_u32(const char *data, size_t length, uint32_t *value)
{
    if((length == 0u) || (length > 10u)) { return false; }
    uint64_t result = 0u;
    for(size_t i = 0u; i < length; i++)
    {
        if((data[i] < '0') || (data[i] > '9')) { return false; }
        result = result * 10u + (uint64_t)(data[i] - '0');
        if(result > UINT32_MAX) { return false; }
    }
    *value = (uint32_t)result;
    return true;
}

bool ws_parse(const char *data, size_t length, ws_message_t *out)
{
    if((data == NULL) || (out == NULL) || (length == 0u) || (length > WS_MESSAGE_MAX)) { return false; }
    if(memchr(data, '\0', length) != NULL) { return false; }
    ws_message_t message = {0};
    const char *payload = data + 1;
    size_t size = length - 1u;
    switch(data[0])
    {
    case 'h': message.type = WS_MSG_HELLO; if(!parse_u32(payload, size, &message.session)) { return false; } break;
    case 'm': message.type = WS_MSG_MOVE; if(!motion_parse(payload, size, &message.yaw, &message.pitch)) { return false; } break;
    case 'z': message.type = WS_MSG_ZOOM; if(!zoom_parse(payload, size, &message.zoom_position, &message.zoom_speed)) { return false; } break;
    case 'x': message.type = WS_MSG_ZOOM_STOP; if(size != 0u) { return false; } break;
    case 'k': message.type = WS_MSG_KEEPALIVE; if(size != 0u) { return false; } break;
    case 'p': message.type = WS_MSG_PING; if(!parse_u32(payload, size, &message.timestamp)) { return false; } break;
    default: return false;
    }
    *out = message;
    return true;
}

static size_t put(char *out, size_t capacity, const char *text)
{
    size_t length = strlen(text);
    if((out == NULL) || (length + 1u > capacity)) { return 0u; }
    memcpy(out, text, length + 1u);
    return length;
}

size_t ws_format_pong(char *out, size_t capacity, uint32_t timestamp)
{
    char digits[11];
    size_t count = 0u;
    do { digits[count++] = (char)('0' + (timestamp % 10u)); timestamp /= 10u; } while(timestamp != 0u);
    if((out == NULL) || (count + 2u > capacity)) { return 0u; }
    out[0] = 'o';
    for(size_t i = 0u; i < count; i++) { out[1u + i] = digits[count - 1u - i]; }
    out[1u + count] = '\0';
    return count + 1u;
}

size_t ws_format_error(char *out, size_t capacity, ws_error_t code)
{
    static const char *const names[] = {"ebad", "erole", "esess", "elink", "ezoom", "ebusy"};
    if((unsigned)code >= sizeof(names) / sizeof(names[0])) { code = WS_ERR_BAD; }
    return put(out, capacity, names[code]);
}

size_t ws_format_role(char *out, size_t capacity, bool controller)
{
    return put(out, capacity, controller ? "r1" : "r0");
}

static char lower(char c) { return ((c >= 'A') && (c <= 'Z')) ? (char)(c + 32) : c; }

/* Authority (host[:port]) without the default HTTP port, or false if it is not a plain authority. */
static bool normalize_authority(const char *in, char *out, size_t capacity)
{
    size_t length = strlen(in);
    if((length == 0u) || (length >= capacity) || (length > 64u)) { return false; }
    for(size_t i = 0u; i < length; i++)
    {
        char c = in[i];
        if((c <= ' ') || (c >= 0x7f) || (c == '/') || (c == '?') || (c == '#') || (c == '@') || (c == '\\')) { return false; }
        out[i] = lower(c);
    }
    out[length] = '\0';
    if((length > 3u) && (strcmp(out + length - 3u, ":80") == 0)) { out[length - 3u] = '\0'; }
    return out[0] != '\0';
}

bool ws_origin_allowed(const char *origin, const char *host)
{
    if((origin == NULL) || (host == NULL) || (strlen(origin) < 8u)) { return false; }
    if((lower(origin[0]) != 'h') || (lower(origin[1]) != 't') || (lower(origin[2]) != 't') || (lower(origin[3]) != 'p') ||
       (origin[4] != ':') || (origin[5] != '/') || (origin[6] != '/')) { return false; }
    char a[68], b[68];
    if(!normalize_authority(origin + 7, a, sizeof(a)) || !normalize_authority(host, b, sizeof(b))) { return false; }
    return strcmp(a, b) == 0;
}
