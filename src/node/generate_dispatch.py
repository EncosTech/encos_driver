# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

"""从共享 C API 生成原生值参数桥接，输出缓冲区由原生层分配。"""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text()
header = '// SPDX-FileCopyrightText: 2026 Encos\n// SPDX-License-Identifier: MIT\n\n'
declarations = []
wrappers = []
entries = []
sizes = {
    'encos_battery_get_status': [27, 3],
    'encos_imu_get_status': [13, 4],
    'encos_motor_get_status': [5, 4],
    'encos_fake_get_motor_snapshot': [21, 6],
}
for name, params in re.findall(r'EMSCRIPTEN_KEEPALIVE\s+int (encos_\w+)\((.*?)\)\s*\{', source, re.S):
    if name == 'encos_get_last_error_code':
        continue
    params = ' '.join(params.split())
    declarations.append(f'int {name}({params});')
    parsed = [re.match(r'(.*?)\s+(\w+)$', p.strip()).groups() for p in params.split(',')] if params else []
    lines, call, outputs = [], [], []
    arg_index = 0
    index = 0
    while index < len(parsed):
        typ, param = parsed[index]
        variable = f'value_{index}'
        if '*' in typ and typ != 'const char*':
            element = typ.replace('const ', '').replace('*', '').strip()
            has_length = index + 1 < len(parsed) and parsed[index + 1][1].endswith('len')
            if typ.startswith('const '):
                lines.append(f'auto {variable} = Numbers<{element}>(args.at({arg_index}));')
                arg_index += 1
            else:
                lengths = sizes.get(name, [5, 5] if name.endswith('_control') else [1])
                size = lengths[len(outputs)]
                lines.append(f'std::vector<{element}> {variable}({size});')
                outputs.append(variable)
            call.append(f'{variable}.data()')
            if has_length:
                call.append(f'static_cast<int>({variable}.size())')
                index += 1
        else:
            if typ == 'const char*':
                lines.append(f'const auto& {variable} = String(args.at({arg_index}));')
                call.append(f'{variable}.c_str()')
            else:
                lines.append(f'auto {variable} = Number<{typ}>(args.at({arg_index}));')
                call.append(variable)
            arg_index += 1
        index += 1
    wrappers.append(f'CallResult Call_{name}(const std::vector<Value>& args) {{\n'
                    f'    RequireCount(args, {arg_index});\n    ' + '\n    '.join(lines) +
                    f'\n    CallResult result;\n    result.code = {name}(' + ', '.join(call) + ');\n' +
                    ''.join(f'    result.outputs.emplace_back({v}.begin(), {v}.end());\n' for v in outputs) +
                    '    result.message = encos::wasm::Store().LastError().message;\n    return result;\n}')
    entries.append(f'{{"{name}", &Call_{name}}}')
output = pathlib.Path(sys.argv[2])
output.mkdir(parents=True, exist_ok=True)
(output / 'node_c_api.h').write_text(header + '#pragma once\n#include <cstdint>\nextern "C" {\n' + '\n'.join(declarations) + '\n}\n')
(output / 'node_dispatch.inc').write_text(header + '\n\n'.join(wrappers) + '\n\nconst std::unordered_map<std::string, Dispatch> kDispatch = {\n' + ',\n'.join(entries) + '\n};\n')
