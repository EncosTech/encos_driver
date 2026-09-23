# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

file(READ "${INPUT}" bpf_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bpf_bytes "${bpf_hex}")
file(WRITE "${OUTPUT}"
    "// SPDX-FileCopyrightText: 2026 Encos\n"
    "// SPDX-License-Identifier: MIT\n"
    "#pragma once\nnamespace encos::ethernet {\n"
    "inline constexpr unsigned char kEmbeddedXdpFilter[] = {\n${bpf_bytes}\n};\n}\n")
