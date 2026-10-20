// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "external/json.hpp"

namespace encos {

/**
 * @brief 标准化的适配器扩展配置信封
 *
 * 顶层键为该适配器**全部总线**的公共参数，保留键 `buses` 为按总线的覆盖表：
 *
 * @code{.json}
 * {
 *   "bitrate": 1000000,
 *   "buses": { "0": {"bitrate": 500000}, "2:5": {"sample_point": 0.875} }
 * }
 * @endcode
 *
 * 生效参数按「插件默认值 ← 顶层公共参数 ← `buses` 中该总线的覆盖」三层**字段级**合并，
 * 因此空串、空对象、只给公共参数与只给某总线的部分字段都走同一条路径。
 *
 * 总线标识接受两种等价拼写：`<slave>:<bus>`，以及表示运行时索引 `(slave << 16) | bus`
 * 的裸十进制整数（`slave <= 0x7FFF`、`bus <= 0xFF`，与 `GetBus(slave_idx, bus_idx)` 的
 * 掩码一致）。两种拼写解析到同一 `(slave, bus)` 时判为**重复并报错**。
 *
 * 本类只负责信封的结构、总线标识与合并规则；参数名与取值由各插件自行校验（插件对不认识
 * 的字段、以及认识但无法生效的字段报错）。信封解析 MUST 由本头单一实现，插件不得各自重复。
 */
class AdapterExtend {
public:
    /** @brief 空信封：全部参数取插件默认值 */
    AdapterExtend() = default;

    /**
     * @brief 解析并校验信封
     * @param extend_json 宿主规范化后的 JSON 文本；空串视为空信封
     * @return 解析后的信封
     * @throws std::runtime_error 文本非法、顶层或 `buses` 结构非法、总线标识格式非法或重复
     */
    static AdapterExtend Parse(const std::string& extend_json) {
        AdapterExtend result;
        if (extend_json.empty()) {
            return result;
        }
        const auto parsed = nlohmann::json::parse(extend_json, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            throw std::runtime_error("adapter extend configuration MUST be a JSON object");
        }
        for (const auto& [key, value] : parsed.items()) {
            if (key != kBusesKey) {
                result.common_[key] = value;
                continue;
            }
            if (!value.is_object()) {
                throw std::runtime_error("adapter extend 'buses' MUST be a JSON object");
            }
            for (const auto& [bus_key, overrides] : value.items()) {
                if (!overrides.is_object()) {
                    throw std::runtime_error("adapter extend 'buses." + bus_key +
                                             "' MUST be a JSON object");
                }
                const auto bus = ParseBusKey(bus_key);
                const int index = PackBus(bus.first, bus.second);
                if (result.bus_overrides_.count(index) != 0) {
                    throw std::runtime_error("adapter extend declares bus '" + bus_key +
                                             "' more than once");
                }
                result.bus_overrides_.emplace(index, overrides);
                result.declared_buses_.push_back(bus);
            }
        }
        return result;
    }

    /**
     * @brief 取指定总线的生效参数
     * @param slave 从站号
     * @param bus 该从站上的一路总线或通道号
     * @return 公共参数与该总线覆盖合并后的 JSON 对象；该总线无覆盖时即公共参数
     */
    nlohmann::json Scope(int slave, int bus) const {
        const auto overrides = bus_overrides_.find(PackBus(slave, bus));
        if (overrides == bus_overrides_.end()) {
            return common_;
        }
        nlohmann::json merged = common_;
        for (const auto& [key, value] : overrides->second.items()) {
            merged[key] = value;
        }
        return merged;
    }

    /**
     * @brief 信封中显式提到的总线
     * @return `(slave, bus)` 列表，供插件校验"配置了不存在的总线"
     */
    const std::vector<std::pair<int, int>>& DeclaredBuses() const {
        return declared_buses_;
    }

    /** @brief 是否未给出任何参数 */
    bool Empty() const {
        return common_.empty() && bus_overrides_.empty();
    }

    /**
     * @brief 取信封中某条总线**显式给出**的覆盖条目
     * @param slave 从站号
     * @param bus 该从站上的一路总线或通道号
     * @return 该总线在 `buses` 中的覆盖对象指针；该总线未被点名时返回 nullptr
     *
     * 与 `Scope(slave, bus)` 的区别：本函数 MUST NOT 合并顶层公共参数。它服务于
     * 「只按 `buses` 增量更新已初始化实例」的路径，返回的条目只含调用方显式给出的字段。
     */
    const nlohmann::json* BusOverride(int slave, int bus) const {
        const auto overrides = bus_overrides_.find(PackBus(slave, bus));
        return overrides == bus_overrides_.end() ? nullptr : &overrides->second;
    }

    /**
     * @brief 取顶层公共参数对象
     * @return 去掉保留键 `buses` 之后的顶层对象
     *
     * 增量更新路径不用它取值，只用它判定并诊断“被忽略的顶层字段”。
     */
    const nlohmann::json& Common() const {
        return common_;
    }

private:
    /** @brief 保留键：按总线的覆盖表 */
    static constexpr const char* kBusesKey = "buses";
    /** @brief 运行时索引中从站号的编码上界，使 `(slave << 16)` 保持在正整数范围 */
    static constexpr int kMaxSlave = 0x7FFF;
    /** @brief 从站内总线号的编码上界，与 `GetBus(slave_idx, bus_idx)` 的掩码一致 */
    static constexpr int kMaxBus = 0xFF;

    /** @brief 把 `(slave, bus)` 打包为运行时索引 */
    static int PackBus(int slave, int bus) {
        return (slave << 16) | (bus & 0xFFFF);
    }

    /**
     * @brief 解析一个非负十进制字段
     * @param text 待解析文本
     * @param value 解析成功时写入
     * @return 是否解析成功；拒绝空串、前导零、非数字与溢出
     */
    static bool ParseUnsigned(const std::string& text, int* value) {
        if (text.empty() || (text.size() > 1 && text.front() == '0')) {
            return false;
        }
        long long accumulated = 0;
        for (const char digit : text) {
            if (digit < '0' || digit > '9') {
                return false;
            }
            accumulated = accumulated * 10 + (digit - '0');
            if (accumulated > std::numeric_limits<int>::max()) {
                return false;
            }
        }
        *value = static_cast<int>(accumulated);
        return true;
    }

    /**
     * @brief 解析总线标识
     * @param key 形如 `2:5` 或 `131077` 的键
     * @return 解析出的 `(slave, bus)`
     * @throws std::runtime_error 格式非法或超出编码上界
     */
    static std::pair<int, int> ParseBusKey(const std::string& key) {
        const auto colon = key.find(':');
        int slave = 0;
        int bus = 0;
        if (colon == std::string::npos) {
            int index = 0;
            if (!ParseUnsigned(key, &index)) {
                throw std::runtime_error("adapter extend bus key '" + key +
                                         "' MUST be a non-negative decimal integer");
            }
            slave = index >> 16;
            bus = index & 0xFFFF;
        } else if (key.find(':', colon + 1) != std::string::npos ||
                   !ParseUnsigned(key.substr(0, colon), &slave) ||
                   !ParseUnsigned(key.substr(colon + 1), &bus)) {
            throw std::runtime_error("adapter extend bus key '" + key +
                                     "' MUST be '<slave>:<bus>' or a bare runtime index");
        }
        if (slave > kMaxSlave || bus > kMaxBus) {
            throw std::runtime_error("adapter extend bus key '" + key +
                                     "' exceeds the encodable range");
        }
        return {slave, bus};
    }

    /** @brief 公共作用域：顶层去掉保留键之后的对象 */
    nlohmann::json common_ = nlohmann::json::object();
    /** @brief 运行时索引 -> 该总线的覆盖对象 */
    std::map<int, nlohmann::json> bus_overrides_;
    /** @brief 信封显式提到的总线 */
    std::vector<std::pair<int, int>> declared_buses_;
};

}  // namespace encos
