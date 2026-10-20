// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

// 实板场景测试：新版本「运行期按总线动态更新 CAN 波特率/采样点」的多消费者用法。
//
// 场景：同一个适配器（按 interface_name 缓存）被多次「分别建立」，每次携带不同的
// `buses` 参数，命中缓存的调用会经 BaseAdapter::UpdateBusExtend 增量下发到网关：
//   - 消费者 A：buses{0:0 = 1M/5M}         —— CAN0 电机持续跑 PVT
//   - 消费者 B：buses{0:7 = 500k}          —— CAN7 IMU0（测试板 500k 经典 CAN）
//   - 消费者 C：buses{0:3 = 250k}          —— CAN3 电池0（测试板 250k 经典 CAN）
// 三个消费者必须同时成立：更新 B/C 时 CAN0 的 PVT 不中断，且 A/B/C 各自的总线时序互不影响。
//
// 若传入第二组 <AdapterType2> <Interface2>，还会建立一个真正独立的第二个适配器
// （不同 interface_name），验证「多个适配器分别建立、携带不同参数」互不干扰。
//
// 编译（用已安装的 encos_driver）：
//   g++ -std=c++17 -O2 -o build/ecat_multi_consumer_timing \
//       tests/manual/ecat_multi_consumer_timing.cc \
//       -I include -I plugins \
//       -L <prefix>/lib -lencosdriver -lencosdriver_base -Wl,-rpath,"<prefix>/lib"
//
// 运行前把网关切到 ECAT 模式，网线接 ECAT 口：
//   ./ecat_multi_consumer_timing Ethercat enx6c1ff7bd2e87 [PVT秒数] [读取秒数]
//   ./ecat_multi_consumer_timing Ethercat enx6c1ff7bd2e87 4 6 Ethercat <第二块板网卡>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <encos/device/battery/battery.h>
#include <encos/device/imu/imu.h>
#include <encos/encos_driver.h>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

long long MsSince(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

/// 构造只点名一条总线的 extend 信封（`buses` 增量更新）。
nlohmann::json BusTiming(int channel, int nominal_bps, int data_bps, double sp, double dsp) {
    const std::string key = "0:" + std::to_string(channel);
    return nlohmann::json{{"buses",
                           {{key,
                             {{"bitrate", nominal_bps},
                              {"dbitrate", data_bps},
                              {"sample_point", sp},
                              {"dsample_point", dsp}}}}}};
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    const std::string adapter_type = argc > 1 ? argv[1] : "Ethercat";
    const std::string interface_name = argc > 2 ? argv[2] : "enx6c1ff7bd2e87";
    const int pvt_seconds = argc > 3 ? std::atoi(argv[3]) : 4;
    const int read_seconds = argc > 4 ? std::atoi(argv[4]) : 6;
    const std::string type2 = argc > 5 ? argv[5] : "";
    const std::string interface2 = argc > 6 ? argv[6] : "";

    if (const char* plugin_dir = std::getenv("ECAT_TEST_PLUGIN_DIR")) {
        encos::SetPluginPath(plugin_dir);
        std::printf("插件目录: %s\n", plugin_dir);
    }

    std::printf("=== 消费者 A：%s/%s，buses{0:0 = 1M/5M} ===\n", adapter_type.c_str(),
                interface_name.c_str());
    encos::BaseAdapter* adapter = nullptr;
    try {
        adapter = encos::MakeAdapter(adapter_type, interface_name, "", encos::LogLevel::Info,
                                     BusTiming(0, 1000000, 5000000, 0.75, 0.875));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "消费者 A 创建适配器失败: %s\n", error.what());
        return 1;
    }
    if (adapter == nullptr) {
        std::fprintf(stderr, "消费者 A 创建适配器返回空\n");
        return 1;
    }

    encos::Bus* can0 = adapter->GetBus(0, 0);
    const auto motors = can0->ScanMotors();
    std::printf("CAN0 扫描到 %zu 个电机\n", motors.size());
    if (motors.empty()) {
        std::fprintf(stderr, "CAN0 无电机，检查接线与供电\n");
        encos::DeleteAdapter(adapter);
        return 1;
    }

    std::vector<encos::Motor*> motor_list;
    motor_list.reserve(motors.size());
    for (const auto& entry : motors) {
        motor_list.push_back(entry.second);
    }
    std::atomic<bool> running{true};
    std::atomic<long long> pvt_calls{0};
    std::atomic<long long> max_gap_ms{0};
    std::thread pvt_thread([&] {
        const auto origin = Clock::now();
        auto next = origin;
        while (running.load()) {
            next += std::chrono::milliseconds(1);
            const double phase = static_cast<double>(MsSince(origin)) / 1000.0;
            const float pos = 0.1F * static_cast<float>(std::sin(phase * 2.0));
            for (encos::Motor* motor : motor_list) {
                motor->PVTControl<0>(1.0F, 0.0F, pos, 0.0F, 0.0F);
            }
            pvt_calls.fetch_add(1);
            const auto now = Clock::now();
            const long long gap =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - next).count();
            if (gap > max_gap_ms.load()) {
                max_gap_ms.store(gap);
            }
            if (now < next) {
                std::this_thread::sleep_until(next);
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(pvt_seconds));
    const long long calls_before = pvt_calls.load();
    std::printf("PVT 已运行 %lld 次（%d 秒）\n", calls_before, pvt_seconds);

    std::printf("\n=== 消费者 B：同一 interface 再建立，buses{0:7 = 500k} ===\n");
    const auto b_start = Clock::now();
    encos::BaseAdapter* second = nullptr;
    try {
        second = encos::MakeAdapter(adapter_type, interface_name, "", encos::LogLevel::Info,
                                    BusTiming(7, 500000, 500000, 0.75, 0.75));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "消费者 B 抛异常: %s\n", error.what());
    }
    std::printf("消费者 B 返回，耗时 %lld ms，指针%s\n", MsSince(b_start),
                second == adapter ? "命中缓存（增量更新）" : "不同实例");

    std::printf("\n=== 消费者 C：同一 interface 再建立，buses{0:3 = 250k} ===\n");
    const auto c_start = Clock::now();
    encos::BaseAdapter* third = nullptr;
    try {
        third = encos::MakeAdapter(adapter_type, interface_name, "", encos::LogLevel::Info,
                                   BusTiming(3, 250000, 250000, 0.75, 0.75));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "消费者 C 抛异常: %s\n", error.what());
    }
    std::printf("消费者 C 返回，耗时 %lld ms，指针%s\n", MsSince(c_start),
                third == adapter ? "命中缓存（增量更新）" : "不同实例");

    encos::BaseAdapter* live = third != nullptr ? third : (second != nullptr ? second : adapter);
    const long long calls_after_update = pvt_calls.load();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::printf("更新后 1 秒内 PVT 调用 %lld 次；全过程最大调度间隔 %lld ms\n",
                pvt_calls.load() - calls_after_update, max_gap_ms.load());

    std::printf("\n=== 同时读取 CAN7 IMU0（500k）与 CAN3 电池0（250k）===\n");
    encos::Imu* imu = live->GetBus(0, 7)->GetImu(0);
    encos::Battery* battery = live->GetBus(0, 3)->GetBattery(0);
    int imu_frames = 0;
    int battery_ok = 0;
    bool battery_first = false;
    const auto deadline = Clock::now() + std::chrono::seconds(read_seconds);
    while (Clock::now() < deadline) {
        const encos::ImuStatus status = imu->GetStatus();
        if (status.acceleration || status.angular_velocity || status.euler_angle) {
            ++imu_frames;
            if (imu_frames == 1) {
                std::printf(
                    "IMU0 首个有效状态：accel=%s euler=%s\n",
                    status.acceleration ? std::to_string(status.acceleration->x).c_str() : "无",
                    status.euler_angle ? std::to_string(status.euler_angle->pitch).c_str() : "无");
            }
        }
        const encos::BatteryStatus bs = battery->GetStatus();
        if (bs.state && !bs.error.AnyError()) {
            ++battery_ok;
            if (!battery_first) {
                battery_first = true;
                std::printf("电池0 首个有效状态：soc=%.3f voltage=%.2f\n", bs.state->soc,
                            bs.state->voltage);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    bool second_adapter_ok = true;
    if (!type2.empty() && !interface2.empty()) {
        std::printf("\n=== 独立适配器 #2：%s/%s，buses{0:0 = 1M/5M} ===\n", type2.c_str(),
                    interface2.c_str());
        const auto d_start = Clock::now();
        encos::BaseAdapter* other = nullptr;
        try {
            other = encos::MakeAdapter(type2, interface2, "", encos::LogLevel::Info,
                                       BusTiming(0, 1000000, 5000000, 0.75, 0.875));
        } catch (const std::exception& error) {
            std::fprintf(stderr, "适配器 #2 抛异常: %s\n", error.what());
            second_adapter_ok = false;
        }
        if (other != nullptr) {
            std::printf("适配器 #2 建立耗时 %lld ms，与 #1 %s\n", MsSince(d_start),
                        other == adapter ? "命中同一缓存（同名！）" : "为独立实例");
            second_adapter_ok = other != adapter;
        }
    }

    running.store(false);
    pvt_thread.join();

    const long long total_calls = pvt_calls.load();
    std::printf("\n=== 结果 ===\n");
    std::printf("PVT 总调用 %lld 次，最大调度间隔 %lld ms\n", total_calls, max_gap_ms.load());
    std::printf("IMU0 有效状态 %d 次；电池0 有效状态 %d 次\n", imu_frames, battery_ok);
    const bool pass =
        imu_frames > 0 && battery_ok > 0 && max_gap_ms.load() < 100 && second_adapter_ok;
    std::printf("多消费者/多适配器动态波特率更新：%s\n", pass ? "PASS" : "FAIL");

    encos::DeleteAdapter(adapter);
    return pass ? 0 : 2;
}
