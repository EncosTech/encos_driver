// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import { execFile } from "node:child_process";
import { realpath } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

export interface PermissionStatus {
  required: boolean;
  authorized: boolean;
  missing: { target: string; capabilities: string[] }[];
}

const helpers = [
  ["CanFdBrokerExecutable", ["cap_net_raw", "cap_net_admin"]],
  ["EthercatFdBrokerExecutable", ["cap_net_raw", "cap_net_admin"]],
  ["EthernetXdpBrokerExecutable", ["cap_net_raw", "cap_net_admin", "cap_bpf"]],
  ["ThreadPriorityHelper", ["cap_sys_nice"]],
] as const;

interface PermissionDependencies {
  platform: string;
  packageRoot: string;
  realpath: (path: string) => Promise<string>;
  execute: (file: string, args: string[]) => Promise<{ stdout: string }>;
}

/** @internal 用于隔离操作系统依赖的测试入口，不从包入口导出。 */
export function createPermissionApi(dependencies: PermissionDependencies) {
  async function checkPermissions(): Promise<PermissionStatus> {
    if (dependencies.platform !== "linux") {
      return { required: false, authorized: true, missing: [] };
    }
    const root = await dependencies.realpath(dependencies.packageRoot);
    const missing: PermissionStatus["missing"] = [];
    for (const [name, required] of helpers) {
      const target = join(root, "runtime", "lib", "encosPlugins", name);
      let resolved: string;
      try {
        resolved = await dependencies.realpath(target);
      } catch {
        missing.push({ target, capabilities: [...required] });
        continue;
      }
      let stdout: string;
      try {
        ({ stdout } = await dependencies.execute("getcap", [resolved]));
      } catch (cause) {
        throw new Error("Cannot inspect helper capabilities: getcap failed or is unavailable; verify the libcap tools and helper access.", { cause });
      }
      // getcap 输出以路径开头；只解析该路径之后的能力表达式。
      const expression = stdout.trim().startsWith(resolved + " ")
        ? stdout.trim().slice(resolved.length + 1) : "";
      const granted = new Set<string>();
      for (const group of expression.split(/\s+/)) {
        const match = /^([a-z_,]+)[=+]([eip]+)$/.exec(group);
        if (match && match[2].includes("e") && match[2].includes("p")) {
          for (const capability of match[1].split(",")) granted.add(capability);
        }
      }
      const capabilities = required.filter(capability => !granted.has(capability));
      if (capabilities.length) missing.push({ target, capabilities });
    }
    return { required: true, authorized: missing.length === 0, missing };
  }

  async function requestPermissions(): Promise<PermissionStatus> {
    const before = await checkPermissions();
    if (before.authorized) return before;
    const root = await dependencies.realpath(dependencies.packageRoot);
    let script: string;
    try {
      script = await dependencies.realpath(join(root, "scripts", "authorize.sh"));
    } catch (cause) {
      throw new Error("Missing bundled Encos authorization script; reinstall the native package.", { cause });
    }
    try {
      await dependencies.execute("pkexec", ["/bin/bash", script]);
    } catch (cause) {
      const code = (cause as { code?: string | number }).code;
      const detail = code === "ENOENT" ? "pkexec is not installed" :
        code === 126 || code === 127 ? "authorization was cancelled or denied" : "authorization command failed";
      throw new Error(`Cannot authorize Encos helpers: ${detail}.`, { cause });
    }
    const after = await checkPermissions();
    if (!after.authorized) throw new Error("Authorization completed but required helper capabilities are still missing.");
    return after;
  }
  return { checkPermissions, requestPermissions };
}

const api = createPermissionApi({
  platform: process.platform,
  packageRoot: dirname(dirname(fileURLToPath(import.meta.url))),
  realpath,
  execute: promisify(execFile),
});
export const checkPermissions = api.checkPermissions;
export const requestPermissions = api.requestPermissions;
