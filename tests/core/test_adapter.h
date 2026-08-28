#pragma once

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <vector>

#include "adapter/base_adapter.h"
#include "platform/sync.h"

namespace fs = std::filesystem;

namespace encos {

class TestAdapter : public BaseAdapter {
public:
    TestAdapter(const std::string& interface_name = "test", const std::string& logger_name = "");

    // Implement pure virtual methods
    virtual void Send(const MotorMessage& message) override;
    void Send(const MotorMessages& messages) override;
    void SendSynchronized(const MotorMessages& messages) override;
    std::unordered_map<int, Bus*> GetBuses() override;
    virtual bool Ok() override {
        return true;
    }

    // Expose OnMessage for testing
    void SimulateOnMessage(const MotorMessages& messages);

    void SetRawMessageCallbackForTests(std::function<void(const MotorMessages&)> callback);

    // Access sent messages
    std::vector<MotorMessage> GetSentMessages() const;
    std::vector<MotorMessages> GetSentBatches() const;
    std::vector<MotorMessages> GetSynchronizedBatches() const;
    bool WaitForSentCount(std::size_t count,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
    void ClearSentMessages();

private:
    mutable platform::Mutex send_mutex_;
    std::condition_variable_any send_condition_;
    std::vector<MotorMessage> send_buffer_;
    std::vector<MotorMessages> sent_batches_;
    std::vector<MotorMessages> synchronized_batches_;
};

}  // namespace encos
