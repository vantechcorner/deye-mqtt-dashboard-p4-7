# Convert Linux rpi_7inch_init to esp_lcd_ili9881c init cmds
from pathlib import Path

raw = r'''
PAGE 3
01 00
02 00
03 73
04 00
05 00
06 0a
07 00
08 00
09 61
0a 00
0b 00
0c 01
0d 00
0e 00
0f 61
10 61
11 00
12 00
13 00
14 00
15 00
16 00
17 00
18 00
19 00
1a 00
1b 00
1c 00
1d 00
1e 40
1f 80
20 06
21 01
22 00
23 00
24 00
25 00
26 00
27 00
28 33
29 03
2a 00
2b 00
2c 00
2d 00
2e 00
2f 00
30 00
31 00
32 00
33 00
34 04
35 00
36 00
37 00
38 3c
39 00
3a 00
3b 00
3c 00
3d 00
3e 00
3f 00
40 00
41 00
42 00
43 00
44 00
50 10
51 32
52 54
53 76
54 98
55 ba
56 10
57 32
58 54
59 76
5a 98
5b ba
5c dc
5d fe
5e 00
5f 0e
60 0f
61 0c
62 0d
63 06
64 07
65 02
66 02
67 02
68 02
69 01
6a 00
6b 02
6c 15
6d 14
6e 02
6f 02
70 02
71 02
72 02
73 02
74 02
75 0e
76 0f
77 0c
78 0d
79 06
7a 07
7b 02
7c 02
7d 02
7e 02
7f 01
80 00
81 02
82 14
83 15
84 02
85 02
86 02
87 02
88 02
89 02
8A 02
PAGE 4
6C 15
6E 2A
6F 33
3B 98
3a 94
8D 14
87 BA
26 76
B2 D1
B5 06
38 01
39 00
PAGE 1
22 0A
31 00
53 7d
55 8f
40 33
50 96
51 96
60 23
A0 08
A1 1d
A2 2a
A3 10
A4 15
A5 28
A6 1c
A7 1d
A8 7e
A9 1d
AA 29
AB 6b
AC 1a
AD 18
AE 4b
AF 20
B0 27
B1 50
B2 64
B3 39
C0 08
C1 1d
C2 2a
C3 10
C4 15
C5 28
C6 1c
C7 1d
C8 7e
C9 1d
CA 29
CB 6b
CC 1a
CD 18
CE 4b
CF 20
D0 27
D1 50
D2 64
D3 39
'''

out = []
out.append('/* Auto-generated from Linux rpi_7inch_init (panel-ilitek-ili9881c.c) */')
out.append('#pragma once')
out.append('#include "esp_lcd_ili9881c.h"')
out.append('')
out.append('static const ili9881c_lcd_init_cmd_t rpi_7inch_init_cmds[] = {')
for line in raw.strip().splitlines():
    line = line.strip()
    if not line:
        continue
    if line.startswith('PAGE'):
        p = int(line.split()[1])
        out.append(f'    {{0xFF, (uint8_t[]){{0x98, 0x81, 0x0{p}}}, 3, 0}},')
        continue
    a, b = line.split()
    out.append(f'    {{0x{a}, (uint8_t[]){{0x{b}}}, 1, 0}},')
out.append('    {0xFF, (uint8_t[]){0x98, 0x81, 0x00}, 3, 0},')
out.append('    {0x35, (uint8_t[]){0x00}, 1, 0}, /* TEAR ON VBLANK */')
out.append('    {0x29, (uint8_t[]){0x00}, 0, 20}, /* DISPLAY ON */')
out.append('};')
out.append('')
out.append('#define RPI_7INCH_INIT_CMDS_SIZE (sizeof(rpi_7inch_init_cmds) / sizeof(rpi_7inch_init_cmds[0]))')
out.append('')

path = Path(r'D:\Github\deye-mqtt-dashboard-p4-7\examples\rpi_td2_colorbar\main\rpi_7inch_init_cmds.h')
path.write_text('\n'.join(out) + '\n', encoding='utf-8')
print('wrote', path, 'entries ~', sum(1 for l in out if l.strip().startswith('{')))
