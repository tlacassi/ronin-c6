# Bundled software

The app embeds unmodified official standalone esptool 5.6.0 executables for macOS arm64 and x86_64, published by Espressif. Each executable includes its Python runtime and dependencies; no system Python is used. The accompanying upstream LICENSE and README are included next to each executable under Resources/Tools.

The matching source archive is also included under Resources/Sources/esptool-v5.6.0-source.tar.gz.

Official release and corresponding source:
https://github.com/espressif/esptool/releases/tag/v5.6.0
https://github.com/espressif/esptool/tree/v5.6.0
https://github.com/espressif/esptool/archive/refs/tags/v5.6.0.tar.gz

Embedded Ronin firmware: this repository, including upstream notices in NOTICE and LICENSE.upstream. Firmware images are compiled with empty Wi-Fi credentials; user settings reside on the device.
