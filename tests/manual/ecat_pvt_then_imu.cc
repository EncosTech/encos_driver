// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

// 实板场景测试：先在 ECAT 上跑 CAN0 的 8 电机 PVT 循环，再用第二个消费者
// （同一 interface_name，命中适配器缓存）把 CAN7 改到 500 kbps 并读取 IMU0。
//
// 这一步验证的是 BaseAdapter::UpdateBusExtend 的在线更新路径：第二个 MakeAdapter
// 不再丢弃 extend，而是通过管理器转发给已存在的实例。
//
// 编译（无需改 CMake）：
//   g++ -std=c++17 -O2 -o build/ecat_pvt_then_imu tests/manual/ecat_pvt_then_imu.cc \
//       -I include -I plugins \
//       -L build-verify -lencosdriver -lencosdriver_base -Wl,-rpath,"$PWD/build-verify"
//
// 运行前确保网关处于 ECAT 模式、网线接在 ECAT 口上：
//   ./build/ecat_pvt_then_imu Ethercat enx6c1ff7bd2e87 [PVT秒数] [IMU秒数]   # ECAT 口
//   ./build/ecat_pvt_then_imu Ethernet enx6c1ff7bd2e87 [PVT秒数] [IMU秒数]   # ENET 口

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <encos/encos_driver.h>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

long long MillisSince(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

nlohmann::json BusTiming(unsigned channel, int nominal_bps, int data_bps, double sp, double dsp) {
    const std::string key = "0:" + std::to_string(channel);
    return nlohmann::json{{"buses",
                           {{key,
                             {{"bitrate", nominal_bps},
                              {"dbitrate", data_bps},
                              {"sample_point", sp},
                              {"dsample_point", dsp}}}}}};
}

void PrintImuStatus(const encos::ImuStatus& status, const char* tag) {
    const auto& a = status.acceleration;
    const auto& g = status.angular_velocity;
    const auto& e = status.euler_angle;
    std::printf("  [%s] accel=%s gyro=%s euler=%s\n", tag, a ? std::to_string(a->x).c_str() : "无",
                g ? std::to_string(g->x).c_str() : "无",
                e ? std::to_string(e->pitch).c_str() : "无");
}

}  // namespace

int main(int argc, char** argv) {
    // 第一个参数是适配器类型：ECAT 口用 Ethercat，ENET 口用 Ethernet。
    const std::string adapter_type = argc > 1 ? argv[1] : "Ethercat";
    const std::string interface_name = argc > 2 ? argv[2] : "enx6c1ff7bd2e87";
    const int pvt_seconds = argc > 3 ? std::atoi(argv[3]) : 3;
    const int imu_seconds = argc > 4 ? std::atoi(argv[4]) : 5;

    const char* plugin_dir = std::getenv("ECAT_TEST_PLUGIN_DIR");
    if (plugin_dir != nullptr) {
        encos::SetPluginPath(plugin_dir);
        std::printf("插件目录: %s\n", plugin_dir);
    }

    std::printf("=== 消费者 A：%s/%s 上创建适配器并把 CAN0 配成 1M/5M ===\n", adapter_type.c_str(),
                interface_name.c_str());
    encos::BaseAdapter* adapter = nullptr;
    try {
        adapter = encos::MakeAdapter(adapter_type, interface_name, "", encos::LogLevel::Info,
                                     BusTiming(0, 1000000, 5000000, 0.75, 0.875));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "创建适配器失败: %s\n", error.what());
        return 1;
    }
    if (adapter == nullptr) {
        std::fprintf(stderr, "创建适配器返回空\n");
        return 1;
    }

    encos::Bus* can0 = adapter->GetBus(0, 0);
    const auto motors = can0->ScanMotors();
    std::printf("CAN0 扫描到 %zu 个电机\n", motors.size());
    if (motors.empty()) {
        std::fprintf(stderr, "CAN0 上没有电机，后续 PVT 循环无意义；请检查接线\n");
        encos::DeleteAdapter(adapter);
        return 1;
    }

    // PVT 循环跑在独立线程：适配器自己在线程上做 PDO 交换，控制调用只是入队，
    // 因此这里不会阻塞，也能观察到更新期间的停顿。
    std::atomic<bool> running{true};
    std::atomic<long long> pvt_calls{0};
    std::atomic<long long> max_gap_ms{0};
    std::vector<encos::Motor*> motor_list;
    for (const auto& entry : motors) {
        motor_list.push_back(entry.second);
    }

    std::thread pvt_thread([&] {
        auto last = Clock::now();
        auto next = last;
        while (running.load()) {
            next += std::chrono::milliseconds(1);
            const double phase = static_cast<double>(MillisSince(last)) / 1000.0;
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

    const auto start = Clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(pvt_seconds));
    const long long before_calls = pvt_calls.load();
    std::printf("PVT 已运行 %lld 次（约 %d 秒）\n", before_calls, pvt_seconds);

    std::printf("\n=== 消费者 B：同一 interface_name 再建一次，要求 CAN7 = 500k ===\n");
    const auto reconfig_start = Clock::now();
    encos::BaseAdapter* second = nullptr;
    try {
        second = encos::MakeAdapter(adapter_type, interface_name, "", encos::LogLevel::Info,
                                    BusTiming(7, 500000, 500000, 0.75, 0.75));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "第二个 MakeAdapter 抛异常: %s\n", error.what());
        running.store(false);
        pvt_thread.join();
        encos::DeleteAdapter(adapter);
        return 1;
    }
    const long long reconfig_ms = MillisSince(reconfig_start);
    std::printf("第二个 MakeAdapter 返回，耗时 %lld ms；指针%s\n", reconfig_ms,
                second == adapter ? "命中缓存（与 A 相同）" : "不同（未命中缓存！）");

    std::this_thread::sleep_for(std::chrono::seconds(1));
    const long long after_calls = pvt_calls.load();
    std::printf("更新后 1 秒内 PVT 调用 %lld 次；全过程 PVT 最大调度间隔 %lld ms\n",
                after_calls - before_calls, max_gap_ms.load());

    std::printf("\n=== 从 CAN7 读 IMU0（500 kbps，测试板 CAN7@500k）===\n");
    encos::Bus* can7 = second->GetBus(0, 7);
    encos::Imu* imu = can7->GetImu(0);
    int frames = 0;
    const auto imu_deadline = Clock::now() + std::chrono::seconds(imu_seconds);
    while (Clock::now() < imu_deadline) {
        const encos::ImuStatus status = imu->GetStatus();
        if (status.acceleration || status.angular_velocity || status.euler_angle) {
            ++frames;
            if (frames == 1 || frames % 100 == 0) {
                std::printf("第 %d 次有效状态（整体已运行 %lld ms）\n", frames, MillisSince(start));
                PrintImuStatus(status, "IMU0");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::printf("\n=== 结果 ===\n");
    const long long total_calls = pvt_calls.load();
    std::printf("PVT 总调用 %lld 次，全程最大调度间隔 %lld ms\n", total_calls, max_gap_ms.load());
    std::printf("IMU 有效状态采样 %d 次（%d 秒内）-> %s\n", frames, imu_seconds,
                frames > 0 ? "CAN7@500k 正常工作" : "未收到任何 IMU 数据");

    running.store(false);
    pvt_thread.join();
    encos::DeleteAdapter(adapter);
    return frames > 0 ? 0 : 2;
}
