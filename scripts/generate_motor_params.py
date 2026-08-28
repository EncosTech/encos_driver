#!/usr/bin/env python3
"""Generate MotorModel enum and parameter lookup from CSV."""

import argparse
import csv
import hashlib
import os
import sys


def generated_banner(csv_sha1):
    return f"// Auto-generated from motor_models.csv. CSV SHA1: {csv_sha1}"


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
        "enum class MotorModel {",
    ]
    for row in models:
        lines.append(f"    {row['model']},")
    lines.append("};")
    lines.append("")
    lines.append("struct MotorPVTRanges;")
    lines.append("ENCOS_BASE_API MotorPVTRanges GetMotorModelRanges(MotorModel model);")
    lines.append("")
    lines.append("ENCOS_BASE_API MotorModel StringToMotorModel(const std::string& str);")
    lines.append("ENCOS_BASE_API const char* MotorModelToString(MotorModel model);")
    lines.append("ENCOS_BASE_API std::vector<const char*> GetAllMotorModelStrings();")
    lines.append("")
    lines.append("}  // namespace encos")
    lines.append("")
    return "\n".join(lines)


def generate_source(models, csv_sha1):
    lines = [
        generated_banner(csv_sha1),
        '#include "motor/motor_model_generated.h"',
        "",
        "#include <stdexcept>",
        "",
        '#include "motor/types.h"',
        "",
        "namespace encos {",
        "",
        "MotorPVTRanges GetMotorModelRanges(MotorModel model) {",
        "    MotorPVTRanges ranges{};",
        "    switch (model) {",
    ]

    def fmt(val):
        s = str(float(val))
        if '.' in s:
            s = s.rstrip('0').rstrip('.')
        if '.' not in s:
            s += '.0'
        return s + 'f'

    for row in models:
        lines.append(f"        case MotorModel::{row['model']}:")
        lines.append(
            f"            ranges.kp = {{{fmt(row['kp_min'])}, {fmt(row['kp_max'])}}};"
        )
        lines.append(
            f"            ranges.kd = {{{fmt(0)}, {fmt(row['kd_max'])}}};"
        )
        lines.append(
            f"            ranges.position = {{{fmt(row['pos_min'])}, {fmt(row['pos_max'])}}};"
        )
        lines.append(
            f"            ranges.speed = {{{fmt(row['speed_min'])}, {fmt(row['speed_max'])}}};"
        )
        lines.append(
            f"            ranges.torque = {{{fmt(row['torque_min'])}, {fmt(row['torque_max'])}}};"
        )
        neg_current = fmt(-float(row['current_max']))
        pos_current = fmt(row['current_max'])
        lines.append(
            f"            ranges.current = {{{neg_current}, {pos_current}}};"
        )
        lines.append(
            f"            ranges.kt = {fmt(row['kt'])};"
        )
        lines.append("            break;")

    lines.append("    }")
    lines.append("    return ranges;")
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
