# 编译前把网页（web/*.html）压缩成 gzip，生成 src/web_assets.h 塞进固件。
# PlatformIO 在 platformio.ini 的 extra_scripts = pre:embed_web.py 里调用；网页没改就不重写，免得每次都重编。
import gzip
from pathlib import Path

try:
    Import("env")  # noqa: F821  PlatformIO 提供
    root = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
except NameError:
    root = Path(__file__).resolve().parent

PAGES = {'MONITOR_HTML': 'monitor.html', 'SETTINGS_HTML': 'settings.html', 'WIFI_HTML': 'wifi.html'}
tools = root / 'web'
out = root / 'src' / 'web_assets.h'

lines = ['// 由 embed_web.py 生成，别手改：改 web/ 里的网页', '#pragma once', '#include <Arduino.h>', '']
for name, fn in PAGES.items():
    data = gzip.compress((tools / fn).read_bytes(), compresslevel=9, mtime=0)
    lines.append(f'// {fn}：{(tools / fn).stat().st_size} 字节，压缩后 {len(data)} 字节')
    lines.append(f'const uint8_t {name}_GZ[] PROGMEM = {{')
    for i in range(0, len(data), 24):
        lines.append('  ' + ','.join(str(b) for b in data[i:i + 24]) + ',')
    lines.append('};')
    lines.append(f'const size_t {name}_GZ_LEN = {len(data)};')
    lines.append('')
text = '\n'.join(lines)
if not out.exists() or out.read_text() != text:
    out.write_text(text)
    print('embed_web.py: 重新生成', out.name)
