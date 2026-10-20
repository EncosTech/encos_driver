#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

"""Generate MotorModel enum and parameter lookup from CSV."""

import argparse
import csv
import hashlib
import os
import sys


def generated_banner(csv_sha1):
    return (
        f"// Auto-generated from motor_models.csv. CSV SHA1: {csv_sha1}\n"
        "// SPDX-FileCopyrightText: 2026 Encos\n"
        "// SPDX-License-Identifier: MIT\n"
    )


def generate_header(models, csv_sha1):
    lines = [
        generated_banner(csv_sha1),
        "#pragma once",
        "",
        "#include <string>",
        "#include <vector>",
        "",
        '#include "encos/export.h"',
        "",
        "namespace encos {",
        "",
        "/** @brief 受支持的电机型号 */",
        "enum class MotorModel {",
    ]
    for row in models:
        lines.append(f"    {row['model']},")
    lines.append("};")
    lines.append("")
    lines.append("struct MotorProperties;")
    lines.append("/**")
    lines.append(" * @brief 获取指定电机型号的控制属性")
    lines.append(" * @param model 电机型号")
    lines.append(" * @return 该型号的控制属性、范围与转矩常数")
    lines.append(" */")
    lines.append("ENCOS_BASE_API MotorProperties GetMotorModelRanges(MotorModel model);")
    lines.append("")
    lines.append("/** @brief 将型号名称转换为枚举，未知名称会抛出 std::invalid_argument */")
    lines.append("ENCOS_BASE_API MotorModel StringToMotorModel(const std::string& str);")
    lines.append("/** @brief 将电机型号枚举转换为静态字符串 */")
    lines.append("ENCOS_BASE_API const char* MotorModelToString(MotorModel model);")
    lines.append("/** @brief 获取全部受支持的电机型号名称 */")
    lines.append("ENCOS_BASE_API std::vector<const char*> GetAllMotorModelStrings();")
    lines.append("")
    lines.append("}  // namespace encos")
    lines.append("")
    return "\n".join(lines)


def generate_source(models, csv_sha1):
    lines = [
        generated_banner(csv_sha1),
        '#include "device/motor/motor_model_generated.h"',
        "",
        "#include <stdexcept>",
        "",
        '#include "device/motor/types.h"',
        "",
        "namespace encos {",
        "",
        "MotorProperties GetMotorModelRanges(MotorModel model) {",
        "    MotorProperties properties{};",
        "    switch (model) {",
    ]

    def fmt(val):
        s = str(float(val))
        if '.' in s:
            s = s.rstrip('0').rstrip('.')
        if '.' not in s:
            s += '.0'
        return s + 'f'

    def fmt_bool(val):
        return 'true' if str(val).strip().lower() in ('true', '1', 'yes') else 'false'

    for row in models:
        lines.append(f"        case MotorModel::{row['model']}:")
        lines.append(
            f"            properties.kp = {{{fmt(row['kp_min'])}, {fmt(row['kp_max'])}}};"
        )
        lines.append(
            f"            properties.kd = {{{fmt(0)}, {fmt(row['kd_max'])}}};"
        )
        lines.append(
            f"            properties.position = {{{fmt(row['pos_min'])}, {fmt(row['pos_max'])}}};"
        )
        lines.append(
            f"            properties.speed = {{{fmt(row['speed_min'])}, {fmt(row['speed_max'])}}};"
        )
        lines.append(
            f"            properties.torque = {{{fmt(row['torque_min'])}, {fmt(row['torque_max'])}}};"
        )
        neg_current = fmt(-float(row['current_max']))
        pos_current = fmt(row['current_max'])
        lines.append(
            f"            properties.current = {{{neg_current}, {pos_current}}};"
        )
        lines.append(
            f"            properties.kt = {fmt(row['kt'])};"
        )
        lines.append(
            f"            properties.brake = {fmt_bool(row.get('brake', ''))};"
        )
        lines.append("            break;")

    lines.append("    }")
    lines.append("    return properties;")
    lines.append("}")
    lines.append("")
    lines.append("MotorModel StringToMotorModel(const std::string& str) {")
    for row in models:
        lines.append(f"    if (str == \"{row['model']}\") {{")
        lines.append(f"        return MotorModel::{row['model']};")
        lines.append("    }")
    lines.append('    throw std::invalid_argument("Unknown motor model: " + str);')
    lines.append("}")
    lines.append("")
    lines.append("const char* MotorModelToString(MotorModel model) {")
    lines.append("    switch (model) {")
    for row in models:
        lines.append(f"        case MotorModel::{row['model']}:")
        lines.append(f"            return \"{row['model']}\";")
    lines.append("    }")
    lines.append('    throw std::invalid_argument("Unknown motor model enum");')
    lines.append("}")
    lines.append("")
    lines.append("std::vector<const char*> GetAllMotorModelStrings() {")
    lines.append("    // clang-format off")
    lines.append("    return {")
    for i, row in enumerate(models):
        if i < len(models) - 1:
            lines.append(f"        \"{row['model']}\",")
        else:
            lines.append(f"        \"{row['model']}\",")
    lines.append("    };")
    lines.append("    // clang-format on")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace encos")
    lines.append("")
    return "\n".join(lines)


def write_if_changed(path, content):
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            if f.read() == content:
                return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)


def main():
    parser = argparse.ArgumentParser(
        description="Generate motor model enum and parameter lookup from CSV."
    )
    parser.add_argument("--csv", required=True, help="Path to motor_models.csv")
    parser.add_argument("--out-header", required=True, help="Output header file path")
    parser.add_argument("--out-source", required=True, help="Output source file path")
    args = parser.parse_args()

    with open(args.csv, "rb") as f:
        csv_sha1 = hashlib.sha1(f.read()).hexdigest()

    with open(args.csv, "r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        models = list(reader)

    header_content = generate_header(models, csv_sha1)
    source_content = generate_source(models, csv_sha1)

    write_if_changed(args.out_header, header_content)
    write_if_changed(args.out_source, source_content)

    print(f"Generated {args.out_header} and {args.out_source} from {args.csv}")


if __name__ == "__main__":
    main()
