#include <assert.h>
#include "ronin_ble.h"

int main(void)
{
    assert(ronin_model_supports_zoom(RONIN_MODEL_RS3));
    assert(ronin_model_supports_zoom(RONIN_MODEL_RS4_PRO));
    assert(ronin_model_zoom_address(RONIN_MODEL_RS3) == 0xdfu);
    assert(ronin_model_zoom_address(RONIN_MODEL_RS4_PRO) == 0xbfu);
    assert(!ronin_model_supports_zoom(RONIN_MODEL_RS4));
    assert(!ronin_model_supports_zoom(RONIN_MODEL_RS5));
    assert(!ronin_model_supports_zoom(RONIN_MODEL_UNKNOWN));
    assert(ronin_model_zoom_address(RONIN_MODEL_RS4) == 0u);
    assert(ronin_model_zoom_address(RONIN_MODEL_RS5) == 0u);
    return 0;
}
