"""Stack high-water mark of a running build (the startup code paints the free RAM): python test/stackcheck.py [elf]"""
import re, subprocess, sys
from pyocd.core.helpers import ConnectHelper
elf = sys.argv[1] if len(sys.argv) > 1 else "build/calib.elf"
nm = subprocess.run([r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-nm.exe", elf], capture_output=True, text=True).stdout
end = int(re.search(r"([0-9a-f]{8}) \w _bss_end", nm).group(1), 16)
top = 0x20002000
with ConnectHelper.session_with_chosen_probe(target_override="py32f003x8", options={"connect_mode": "attach", "resume_on_disconnect": True}) as s:
    words = s.target.read_memory_block32(end, (top - end) // 4)
untouched = next((k for k, w in enumerate(words) if w != 0x57AC57AC), len(words))
print(f"free RAM above .bss: {top - end} B; deepest stack use: {top - end - untouched * 4} B; never touched: {untouched * 4} B")
