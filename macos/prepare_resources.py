"""Package a generic firmware and pinned official Espressif tools for maintainers."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
resources = Path(sys.argv[1])
config = (root / 'sdkconfig.standalone').read_text()
for key in ('RONIN_WIFI_SSID', 'RONIN_WIFI_PASSWORD', 'RONIN_BLE_NAME'):
    if f'CONFIG_{key}=""' not in config:
        raise SystemExit(f'Refusing to package a non-generic firmware: {key}')
firmware = resources / 'Firmware'
firmware.mkdir(parents=True)
files = {'bootloader.bin': 'bootloader/bootloader.bin', 'partition-table.bin': 'partition_table/partition-table.bin', 'ronin_c6.bin': 'ronin_c6.bin'}
manifest = {}
for name, path in files.items():
    source = root / 'build-standalone' / path
    shutil.copy2(source, firmware / name)
    manifest[name] = hashlib.sha256(source.read_bytes()).hexdigest()
(firmware / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
archives = {
    'arm64': ('arm64', '08a58829a3f14bb08f2e39f4cad48dcb14b7000384268e7f06689836332d6e25'),
    'x86_64': ('amd64', '23921776a7df75f2e6f3ca10a604d74db4661e1b8a0ebb89bdddcd2d26f37055'),
}
for arch, (asset, expected) in archives.items():
    vendor = root / 'macos' / 'vendor'
    vendor.mkdir(exist_ok=True)
    archive = vendor / f'{asset}.tar.gz'
    if not archive.exists():
        subprocess.run(['curl', '--fail', '--location', '--output', str(archive), f'https://github.com/espressif/esptool/releases/download/v5.6.0/esptool-v5.6.0-macos-{asset}.tar.gz'], check=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise SystemExit(f'Unexpected official esptool archive digest: {arch}')
    subprocess.run(['tar', '-xzf', str(archive), '-C', str(vendor)], check=True)
    destination = resources / 'Tools' / arch
    destination.mkdir(parents=True)
    for name in ('esptool', 'LICENSE', 'README.md'):
        shutil.copy2(vendor / f'esptool-macos-{asset}' / name, destination / name)
shutil.copy2(root / 'macos' / 'THIRD_PARTY.md', resources / 'THIRD_PARTY.md')

source_archive = root / 'macos/vendor/esptool-v5.6.0-source.tar.gz'
if not source_archive.exists():
    subprocess.run(['curl', '--fail', '--location', '--output', str(source_archive), 'https://github.com/espressif/esptool/archive/refs/tags/v5.6.0.tar.gz'], check=True)
if hashlib.sha256(source_archive.read_bytes()).hexdigest() != '33c1907f45513240d04057b93857b1e1b155396dc41d96cf2ae4cb7aabaa782c':
    raise SystemExit('Unexpected esptool source archive digest')
(resources / 'Sources').mkdir()
shutil.copy2(source_archive, resources / 'Sources' / source_archive.name)
