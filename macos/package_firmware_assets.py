"""Create the firmware files to attach to a GitHub release so installed apps can pull them.

Usage: python3 macos/package_firmware_assets.py [output-dir] [build-dir] [sdkconfig]
Then:  gh release upload vX.Y.Z <output-dir>/* --repo tlacassi/ronin-c6
"""
import hashlib
import json
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parent.parent
output = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'macos' / 'dist' / 'firmware-assets'
build = Path(sys.argv[2]) if len(sys.argv) > 2 else root / 'build-standalone'
config_path = Path(sys.argv[3]) if len(sys.argv) > 3 else root / 'sdkconfig.standalone'
config = config_path.read_text()
for key in ('RONIN_WIFI_SSID', 'RONIN_WIFI_PASSWORD', 'RONIN_BLE_NAME'):
    if f'CONFIG_{key}=""' not in config:
        raise SystemExit(f'Refusing to publish a non-generic firmware: {key}')
files = {'bootloader.bin': 'bootloader/bootloader.bin', 'partition-table.bin': 'partition_table/partition-table.bin', 'ronin_c6.bin': 'ronin_c6.bin'}
limits = {'bootloader.bin': 0x8000, 'partition-table.bin': 0x1000, 'ronin_c6.bin': 0x180000}
output.mkdir(parents=True, exist_ok=True)
manifest = {}
for name, path in files.items():
    data = (build / path).read_bytes()
    if not 0 < len(data) <= limits[name]:
        raise SystemExit(f'{name} has an invalid size: {len(data)}')
    shutil.copyfile(build / path, output / f'firmware-{name}')
    manifest[name] = hashlib.sha256(data).hexdigest()
(output / 'firmware-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'Firmware assets written to {output}')
