// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <atomic>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <node_api.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "encos/encos_driver.h"
#include "encos_version.h"
#include "node/native_api.h"
#include "node/native_devices.h"
#include "node_c_api.h"
#include "wasm/wasm_runtime.h"

namespace {
using Json = encos::node::Json;
using Value = std::variant<double, std::string, std::vector<double>>;
struct CallResult {
    int code = 0;
    std::vector<std::vector<double>> outputs;
    std::string message;
    Json value;
    bool structured = false;
};
using Dispatch = CallResult (*)(const std::vector<Value>&);

void RequireCount(const std::vector<Value>& args, std::size_t count) {
    if (args.size() != count) {
        throw std::invalid_argument("incorrect argument count");
    }
}
template <typename T>
const T& GetValue(const Value& value) {
    if (const auto* typed = std::get_if<T>(&value)) {
        return *typed;
    }
    throw std::invalid_argument("native argument has the wrong type");
}
const std::string& String(const Value& value) {
    return GetValue<std::string>(value);
}
template <typename T>
T Number(const Value& value) {
    const double number = GetValue<double>(value);
    if constexpr (std::is_integral_v<T>) {
        if (!std::isfinite(number) || std::trunc(number) != number ||
            number < static_cast<double>(std::numeric_limits<T>::lowest()) ||
            number > static_cast<double>(std::numeric_limits<T>::max())) {
            throw std::invalid_argument("integer argument out of range");
        }
    } else if (!std::isfinite(number)) {
        throw std::invalid_argument("numeric argument must be finite");
    }
    return static_cast<T>(number);
}
template <typename T>
std::vector<T> Numbers(const Value& value) {
    std::vector<T> result;
    for (double number : GetValue<std::vector<double>>(value)) {
        if constexpr (std::is_floating_point_v<T>) {
            if (std::isinf(number)) {
                throw std::invalid_argument("array value must not be infinite");
            }
            result.push_back(static_cast<T>(number));
        } else {
            result.push_back(Number<T>(Value(number)));
        }
    }
    return result;
}

#include "node_dispatch.inc"

struct Subscription;
void CancelSubscription(const std::shared_ptr<Subscription>& subscription);
std::uint32_t SubscriptionHandle(const std::shared_ptr<Subscription>& subscription);
struct Runtime;
struct Work;
std::mutex manager_mutex;
std::unordered_map<std::string, Runtime*> interface_owners;
std::string plugin_path;

struct StoreScope {
    encos::wasm::RuntimeStore* previous;
    explicit StoreScope(encos::wasm::RuntimeStore& store)
        : previous(encos::wasm::SetThreadStore(&store)) {}
    ~StoreScope() {
        encos::wasm::SetThreadStore(previous);
    }
};
struct Runtime {
    encos::wasm::RuntimeStore store;
    std::mutex mutex;
    std::atomic<bool> disposed{false};
    std::mutex deletion_mutex;
    std::unordered_set<std::uint32_t> deletions;
    std::deque<Work*> queue;
    std::unordered_map<std::uint32_t, std::string> interfaces;
    bool cleaned = false;
    std::unordered_map<std::string, std::shared_ptr<Subscription>> subscriptions;
    void CancelFor(std::uint32_t handle) {
        for (auto it = subscriptions.begin(); it != subscriptions.end();) {
            if (store.IsHandleDescendant(SubscriptionHandle(it->second), handle)) {
                CancelSubscription(it->second);
                it = subscriptions.erase(it);
            } else
                ++it;
        }
    }
    void ClearSubscriptions() {
        for (auto& item : subscriptions)
            CancelSubscription(item.second);
        subscriptions.clear();
    }
    void Cleanup() {
        if (cleaned) {
            return;
        }
        std::lock_guard<std::mutex> manager_lock(manager_mutex);
        StoreScope scope(store);
        ClearSubscriptions();
        encos::node::ClearNativeUtilities(store);
        store.DisposeAll();
        for (auto it = interface_owners.begin(); it != interface_owners.end();) {
            if (it->second == this) {
                it = interface_owners.erase(it);
            } else {
                ++it;
            }
        }
        cleaned = true;
    }
    ~Runtime() {
        Cleanup();
    }
    void ReleaseInterface(std::uint32_t handle) {
        const auto entry = interfaces.find(handle);
        if (entry == interfaces.end()) {
            return;
        }
        const auto interface_name = entry->second;
        interfaces.erase(entry);
        for (const auto& remaining : interfaces) {
            if (remaining.second == interface_name) {
                return;
            }
        }
        interface_owners.erase(interface_name);
    }
    void DrainDeletions() {
        std::unordered_set<std::uint32_t> pending;
        {
            std::lock_guard<std::mutex> lock(deletion_mutex);
            pending.swap(deletions);
        }
        if (!pending.empty()) {
            for (auto handle : pending)
                CancelFor(handle);
            std::lock_guard<std::mutex> lock(manager_mutex);
            for (auto handle : pending) {
                if (store.DisposeAdapter(handle) == encos::wasm::ErrorCode::Ok) {
                    ReleaseInterface(handle);
                }
            }
        }
    }
};
struct Work {
    std::shared_ptr<Runtime> runtime;
    std::string name;
    std::vector<Value> args;
    CallResult result;
    napi_async_work work = nullptr;
    napi_deferred deferred = nullptr;
    Json structured_args;
    bool structured = false;
};

CallResult Run(Runtime& runtime, const std::string& name, const std::vector<Value>& args) {
    StoreScope scope(runtime.store);
    runtime.DrainDeletions();
    if (runtime.disposed.load()) {
        return {3, {}, "runtime is disposed"};
    }
    try {
        const auto found = kDispatch.find(name);
        if (found == kDispatch.end()) {
            return {7, {}, "unknown native operation: " + name};
        }
        const bool create = name == "encos_create_adapter" || name == "encos_create_fake_adapter";
        if (create) {
            const auto& interface_name = String(args.at(name == "encos_create_adapter" ? 1 : 0));
            std::lock_guard<std::mutex> lock(manager_mutex);
            const auto owner = interface_owners.find(interface_name);
            if (owner != interface_owners.end() && owner->second != &runtime) {
                return {5, {}, "adapter interface belongs to another native runtime"};
            }
            auto result = found->second(args);
            if (result.code == 0) {
                interface_owners[interface_name] = &runtime;
                runtime.interfaces[static_cast<std::uint32_t>(result.outputs.at(0).at(0))] =
                    interface_name;
            }
            return result;
        }
        if (name == "encos_dispose_adapter") {
            runtime.CancelFor(Number<std::uint32_t>(args.at(0)));
            std::lock_guard<std::mutex> lock(manager_mutex);
            auto result = found->second(args);
            if (result.code == 0) {
                runtime.ReleaseInterface(Number<std::uint32_t>(args.at(0)));
            }
            return result;
        }
        return found->second(args);
    } catch (const std::exception& error) {
        return {1, {}, error.what()};
    } catch (...) {
        return {9, {}, "unknown native exception"};
    }
}

CallResult RunStructured(Runtime& runtime, const std::string& requested, const Json& args) {
    std::string name = requested;
    static const std::unordered_map<std::string, std::string> aliases = {
        {"manager.createAdapter", "runtime.createAdapter"},
        {"manager.destroyAdapter", "adapter.dispose"},
        {"manager.destroyBus", "bus.dispose"},
        {"manager.destroyMotor", "motor.dispose"},
        {"manager.destroyBattery", "battery.dispose"},
        {"manager.destroyImu", "imu.dispose"},
        {"manager.destroyPms", "pms.dispose"},
        {"manager.destroyGlove", "glove.dispose"}};
    if (auto alias = aliases.find(name); alias != aliases.end())
        name = alias->second;
    StoreScope scope(runtime.store);
    CallResult result;
    result.structured = true;
    if (runtime.disposed.load()) {
        result.code = 3;
        result.message = "runtime is disposed";
        return result;
    }
    try {
        runtime.DrainDeletions();
        if (name == "runtime.createAdapter") {
            const auto interface_name = args.at("interfaceName").get<std::string>();
            std::lock_guard<std::mutex> lock(manager_mutex);
            auto owner = interface_owners.find(interface_name);
            if (owner != interface_owners.end() && owner->second != &runtime) {
                result.code = 5;
                result.message = "adapter interface belongs to another native runtime";
                return result;
            }
            result.value = encos::node::InvokeNative(runtime.store, name, args);
            interface_owners[interface_name] = &runtime;
            runtime.interfaces[result.value.get<std::uint32_t>()] = interface_name;
            auto created = runtime.store.ResolveAdapter(result.value.get<std::uint32_t>());
            if (created.Ok()) {
                created.value->fake_adapter =
                    dynamic_cast<encos::FakeAdapter*>(created.value->adapter);
                created.value->fake_tools_enabled = created.value->fake_adapter != nullptr;
            }
        } else if (name == "manager.destroyAdapterByInterfaceName" ||
                   name == "runtime.deleteAdapterByInterfaceName" ||
                   name == "runtime.unloadAdapterByInterfaceName") {
            auto interface_name = args.at("interfaceName").get<std::string>();
            std::vector<std::uint32_t> handles;
            for (const auto& entry : runtime.interfaces)
                if (entry.second == interface_name)
                    handles.push_back(entry.first);
            if (handles.empty()) {
                result.value = false;
                return result;
            }
            for (auto handle : handles)
                runtime.CancelFor(handle);
            std::lock_guard<std::mutex> lock(manager_mutex);
            for (auto handle : handles) {
                auto code = runtime.store.DisposeAdapter(handle);
                if (code != encos::wasm::ErrorCode::Ok)
                    throw encos::wasm::ErrorState{code, "adapter disposal failed"};
                runtime.ReleaseInterface(handle);
            }
            result.value = true;
        } else if (name == "runtime.setPluginPath") {
            std::lock_guard<std::mutex> lock(manager_mutex);
            plugin_path = args.at("path").get<std::string>();
            encos::SetPluginPath(plugin_path);
        } else if (name.size() > 8 && name.substr(name.size() - 8) == ".dispose") {
            runtime.CancelFor(args.at("handle"));
            std::lock_guard<std::mutex> lock(manager_mutex);
            result.value = encos::node::InvokeNative(runtime.store, name, args);
            if (name == "adapter.dispose")
                runtime.ReleaseInterface(args.at("handle"));
        } else {
            result.value = encos::node::InvokeNative(runtime.store, name, args);
            if (name == "bus.resetMotorsId") {
                for (auto it = runtime.subscriptions.begin(); it != runtime.subscriptions.end();) {
                    if (!runtime.store.IsHandleAlive(SubscriptionHandle(it->second))) {
                        CancelSubscription(it->second);
                        it = runtime.subscriptions.erase(it);
                    } else
                        ++it;
                }
            }
        }
    } catch (const encos::wasm::ErrorState& error) {
        result.code = static_cast<int>(error.code);
        result.message = error.message;
    } catch (const std::invalid_argument& error) {
        result.code = 1;
        result.message = error.what();
    } catch (const Json::exception& error) {
        result.code = 1;
        result.message = error.what();
    } catch (const std::exception& error) {
        result.code = 5;
        result.message = error.what();
    }
    return result;
}

void Check(napi_status status) {
    if (status != napi_ok) {
        throw std::runtime_error("Node-API call failed");
    }
}
napi_value Text(napi_env env, const std::string& text) {
    napi_value value;
    Check(napi_create_string_utf8(env, text.c_str(), text.size(), &value));
    return value;
}
std::string ReadString(napi_env env, napi_value value) {
    std::size_t size = 0;
    Check(napi_get_value_string_utf8(env, value, nullptr, 0, &size));
    std::vector<char> buffer(size + 1);
    Check(napi_get_value_string_utf8(env, value, buffer.data(), buffer.size(), &size));
    std::string result(buffer.data(), size);
    if (result.find('\0') != std::string::npos) {
        throw std::invalid_argument("native strings must not contain NUL characters");
    }
    return result;
}
Json ReadJson(napi_env env, napi_value value, int depth = 0) {
    if (depth > 64)
        throw std::invalid_argument("native value nesting exceeds 64");
    napi_valuetype type;
    Check(napi_typeof(env, value, &type));
    if (type == napi_null || type == napi_undefined)
        return nullptr;
    if (type == napi_string)
        return ReadString(env, value);
    if (type == napi_boolean) {
        bool v;
        Check(napi_get_value_bool(env, value, &v));
        return v;
    }
    if (type == napi_number) {
        double v;
        Check(napi_get_value_double(env, value, &v));
        return v;
    }
    if (type == napi_bigint) {
        std::int64_t v;
        bool lossless;
        Check(napi_get_value_bigint_int64(env, value, &v, &lossless));
        if (lossless)
            return v;
        std::uint64_t u;
        Check(napi_get_value_bigint_uint64(env, value, &u, &lossless));
        if (lossless)
            return u;
        throw std::invalid_argument("bigint exceeds 64-bit range");
    }
    if (type != napi_object)
        throw std::invalid_argument("unsupported native value");
    bool array, typed;
    Check(napi_is_array(env, value, &array));
    Check(napi_is_typedarray(env, value, &typed));
    if (array || typed) {
        std::uint32_t length;
        if (array)
            Check(napi_get_array_length(env, value, &length));
        else {
            napi_value v;
            Check(napi_get_named_property(env, value, "length", &v));
            Check(napi_get_value_uint32(env, v, &length));
        }
        if (length > 1048576)
            throw std::invalid_argument("native array exceeds maximum length");
        Json result = Json::array();
        for (std::uint32_t i = 0; i < length; ++i) {
            napi_value v;
            Check(napi_get_element(env, value, i, &v));
            result.push_back(ReadJson(env, v, depth + 1));
        }
        return result;
    }
    napi_value keys;
    Check(napi_get_all_property_names(env, value, napi_key_own_only, napi_key_enumerable,
                                      napi_key_numbers_to_strings, &keys));
    std::uint32_t count;
    Check(napi_get_array_length(env, keys, &count));
    if (count > 65536)
        throw std::invalid_argument("native object exceeds maximum size");
    Json result = Json::object();
    for (std::uint32_t i = 0; i < count; ++i) {
        napi_value key, v;
        Check(napi_get_element(env, keys, i, &key));
        auto name = ReadString(env, key);
        Check(napi_get_property(env, value, key, &v));
        napi_valuetype property_type;
        Check(napi_typeof(env, v, &property_type));
        if (property_type == napi_undefined)
            continue;
        result[name] = ReadJson(env, v, depth + 1);
    }
    return result;
}
napi_value JsonToJs(napi_env env, const Json& value) {
    napi_value result;
    if (value.is_null())
        Check(napi_get_null(env, &result));
    else if (value.is_boolean())
        Check(napi_get_boolean(env, value.get<bool>(), &result));
    else if (value.is_string())
        return Text(env, value.get<std::string>());
    else if (value.is_number_unsigned() && value.get<std::uint64_t>() > 9007199254740991ULL)
        Check(napi_create_bigint_uint64(env, value.get<std::uint64_t>(), &result));
    else if (value.is_number_integer() && !value.is_number_unsigned() &&
             (value.get<std::int64_t>() > 9007199254740991LL ||
              value.get<std::int64_t>() < -9007199254740991LL))
        Check(napi_create_bigint_int64(env, value.get<std::int64_t>(), &result));
    else if (value.is_number())
        Check(napi_create_double(env, value.get<double>(), &result));
    else if (value.is_array()) {
        Check(napi_create_array_with_length(env, value.size(), &result));
        for (std::size_t i = 0; i < value.size(); ++i)
            Check(napi_set_element(env, result, i, JsonToJs(env, value[i])));
    } else {
        Check(napi_create_object(env, &result));
        for (auto it = value.begin(); it != value.end(); ++it) {
            auto key = Text(env, it.key());
            auto v = JsonToJs(env, it.value());
            napi_property_descriptor prop = {
                nullptr, key, nullptr, nullptr, nullptr, v, napi_default_jsproperty, nullptr};
            Check(napi_define_properties(env, result, 1, &prop));
        }
    }
    return result;
}
struct Subscription {
    std::mutex mutex;
    bool active = true;
    std::uint32_t handle = 0;
    napi_threadsafe_function function = nullptr;
    std::function<void()> unbind;
};
std::uint32_t SubscriptionHandle(const std::shared_ptr<Subscription>& subscription) {
    return subscription->handle;
}
void CancelSubscription(const std::shared_ptr<Subscription>& s) {
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (!s->active)
            return;
        s->active = false;
        napi_release_threadsafe_function(s->function, napi_tsfn_abort);
    }
    if (s->unbind) {
        auto unbind = std::move(s->unbind);
        unbind();
    }
}
void CallbackJs(napi_env env, napi_value callback, void* context, void* data) {
    std::unique_ptr<Json> event(static_cast<Json*>(data));
    if (!env || !callback)
        return;
    auto* subscription = static_cast<Subscription*>(context);
    {
        std::lock_guard<std::mutex> lock(subscription->mutex);
        if (!subscription->active)
            return;
    }
    try {
        napi_value receiver, result, arg = JsonToJs(env, *event);
        Check(napi_get_undefined(env, &receiver));
        auto status = napi_call_function(env, receiver, callback, 1, &arg, &result);
        if (status == napi_pending_exception) {
            napi_value error;
            napi_get_and_clear_last_exception(env, &error);
            napi_fatal_exception(env, error);
        } else
            Check(status);
    } catch (const std::exception& error) {
        napi_throw_error(env, nullptr, error.what());
    }
}
void CallbackFinalize(napi_env /*env*/, void* data, void* /*hint*/) {
    auto* owner = static_cast<std::shared_ptr<Subscription>*>(data);
    {
        std::lock_guard<std::mutex> lock((*owner)->mutex);
        (*owner)->active = false;
        (*owner)->function = nullptr;
    }
    delete owner;
}

Value ReadValue(napi_env env, napi_value value) {
    napi_valuetype type;
    Check(napi_typeof(env, value, &type));
    if (type == napi_string) {
        return ReadString(env, value);
    }
    if (type == napi_number) {
        double number;
        Check(napi_get_value_double(env, value, &number));
        return number;
    }
    bool array = false;
    Check(napi_is_array(env, value, &array));
    if (!array) {
        throw std::invalid_argument("native arguments must be numbers, strings or numeric arrays");
    }
    std::uint32_t size;
    Check(napi_get_array_length(env, value, &size));
    if (size > 1024 * 1024) {
        throw std::invalid_argument("native array exceeds maximum length");
    }
    std::vector<double> numbers(size);
    for (std::uint32_t i = 0; i < size; ++i) {
        napi_value item;
        Check(napi_get_element(env, value, i, &item));
        Check(napi_get_value_double(env, item, &numbers[i]));
    }
    return numbers;
}
napi_value ToJs(napi_env env, const CallResult& result) {
    napi_value object, code, outputs;
    Check(napi_create_object(env, &object));
    Check(napi_create_int32(env, result.code, &code));
    Check(napi_set_named_property(env, object, "code", code));
    Check(napi_set_named_property(env, object, "message", Text(env, result.message)));
    if (result.structured) {
        Check(napi_set_named_property(env, object, "value", JsonToJs(env, result.value)));
        return object;
    }
    Check(napi_create_array_with_length(env, result.outputs.size(), &outputs));
    for (std::size_t i = 0; i < result.outputs.size(); ++i) {
        napi_value array;
        Check(napi_create_array_with_length(env, result.outputs[i].size(), &array));
        for (std::size_t j = 0; j < result.outputs[i].size(); ++j) {
            napi_value number;
            Check(napi_create_double(env, result.outputs[i][j], &number));
            Check(napi_set_element(env, array, j, number));
        }
        Check(napi_set_element(env, outputs, i, array));
    }
    Check(napi_set_named_property(env, object, "outputs", outputs));
    return object;
}

void Execute(napi_env /*env*/, void* data) {
    auto& work = *static_cast<Work*>(data);
    std::lock_guard<std::mutex> lock(work.runtime->mutex);
    work.result = work.structured ? RunStructured(*work.runtime, work.name, work.structured_args)
                                  : Run(*work.runtime, work.name, work.args);
    work.runtime->DrainDeletions();
    if (work.runtime->disposed.load()) {
        work.runtime->Cleanup();
    }
}
void Complete(napi_env env, napi_status status, void* data) {
    std::unique_ptr<Work> work(static_cast<Work*>(data));
    auto runtime = work->runtime;
    if (status != napi_ok || runtime->disposed.load()) {
        work->result = {3, {}, "runtime is disposed"};
    }
    try {
        if (work->deferred != nullptr) {
            napi_resolve_deferred(env, work->deferred, ToJs(env, work->result));
        }
    } catch (...) {
        // 环境退出时不再创建 JavaScript 值。
    }
    napi_delete_async_work(env, work->work);
    runtime->queue.pop_front();
    if (!runtime->queue.empty()) {
        if (napi_queue_async_work(env, runtime->queue.front()->work) != napi_ok) {
            runtime->disposed.store(true);
            while (!runtime->queue.empty()) {
                auto* pending = runtime->queue.front();
                runtime->queue.pop_front();
                napi_delete_async_work(env, pending->work);
                delete pending;
            }
        }
    }
}

napi_value Call(napi_env env, napi_callback_info info) {
    try {
        std::size_t argc = 2;
        napi_value argv[2], self;
        void* mode;
        Check(napi_get_cb_info(env, info, &argc, argv, &self, &mode));
        if (argc != 2) {
            throw std::invalid_argument("call requires name and arguments");
        }
        void* wrapped;
        Check(napi_unwrap(env, self, &wrapped));
        auto runtime = *static_cast<std::shared_ptr<Runtime>*>(wrapped);
        auto work = std::make_unique<Work>();
        work->runtime = runtime;
        work->name = ReadString(env, argv[0]);
        work->structured = mode == reinterpret_cast<void*>(2) || mode == reinterpret_cast<void*>(3);
        if (work->structured) {
            work->structured_args = ReadJson(env, argv[1]);
            if (!work->structured_args.is_object())
                throw std::invalid_argument("invoke arguments must be an object");
        }
        std::uint32_t count = 0;
        if (!work->structured)
            Check(napi_get_array_length(env, argv[1], &count));
        if (count > 64) {
            throw std::invalid_argument("too many arguments");
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            napi_value item;
            Check(napi_get_element(env, argv[1], i, &item));
            work->args.push_back(ReadValue(env, item));
        }
        if (mode == nullptr || mode == reinterpret_cast<void*>(2)) {
            if (work->name == "encos_dispose_adapter") {
                RequireCount(work->args, 1);
                const auto handle = Number<std::uint32_t>(work->args[0]);
                {
                    std::lock_guard<std::mutex> lock(runtime->deletion_mutex);
                    runtime->deletions.insert(handle);
                }
                std::unique_lock<std::mutex> lock(runtime->mutex, std::try_to_lock);
                if (lock.owns_lock()) {
                    runtime->DrainDeletions();
                }
                return ToJs(env, {});
            }
            std::unique_lock<std::mutex> lock(runtime->mutex, std::try_to_lock);
            if (!lock.owns_lock() || !runtime->queue.empty()) {
                return ToJs(env,
                            {5, {}, "native runtime has an asynchronous operation in progress"});
            }
            return ToJs(env, work->structured
                                 ? RunStructured(*runtime, work->name, work->structured_args)
                                 : Run(*runtime, work->name, work->args));
        }
        napi_value promise;
        Check(napi_create_promise(env, &work->deferred, &promise));
        Check(napi_create_async_work(env, nullptr, Text(env, "EncosDriver"), Execute, Complete,
                                     work.get(), &work->work));
        runtime->queue.push_back(work.get());
        if (runtime->queue.size() == 1) {
            const auto status = napi_queue_async_work(env, work->work);
            if (status != napi_ok) {
                runtime->queue.pop_back();
                napi_delete_async_work(env, work->work);
                Check(status);
            }
        }
        work.release();
        return promise;
    } catch (const std::exception& error) {
        napi_throw_error(env, nullptr, error.what());
        return nullptr;
    }
}
std::string CallbackKey(Runtime& runtime, const std::string& name, const Json& args) {
    const auto handle = encos::node::CheckedInteger<std::uint32_t>(args.at("handle"));
    std::uintptr_t identity = 0;
    if (auto result = runtime.store.ResolveAdapter(handle); result.Ok())
        identity = reinterpret_cast<std::uintptr_t>(result.value->adapter);
    if (auto result = runtime.store.ResolveMotor(handle); result.Ok())
        identity = reinterpret_cast<std::uintptr_t>(result.value->motor);
    if (auto result = runtime.store.ResolveBattery(handle); result.Ok())
        identity = reinterpret_cast<std::uintptr_t>(result.value->battery);
    if (auto result = runtime.store.ResolveImu(handle); result.Ok())
        identity = reinterpret_cast<std::uintptr_t>(result.value->imu);
    if (auto result = runtime.store.ResolvePms(handle); result.Ok())
        identity = reinterpret_cast<std::uintptr_t>(result.value->pms);
    if (auto result = runtime.store.ResolveGlove(handle); result.Ok())
        identity = reinterpret_cast<std::uintptr_t>(result.value->glove);
    if (identity == 0)
        throw std::invalid_argument("invalid callback handle");
    auto options = args;
    options.erase("handle");
    return name + ":" + std::to_string(identity) + ":" + options.dump();
}

napi_value Subscribe(napi_env env, napi_callback_info info) {
    try {
        std::size_t argc = 3;
        napi_value argv[3], self;
        Check(napi_get_cb_info(env, info, &argc, argv, &self, nullptr));
        if (argc != 3)
            throw std::invalid_argument("subscribe requires operation, arguments and callback");
        void* wrapped;
        Check(napi_unwrap(env, self, &wrapped));
        auto runtime = *static_cast<std::shared_ptr<Runtime>*>(wrapped);
        auto name = ReadString(env, argv[0]);
        auto args = ReadJson(env, argv[1]);

        std::unique_lock<std::mutex> lock(runtime->mutex, std::try_to_lock);
        if (!lock.owns_lock() || !runtime->queue.empty())
            throw std::runtime_error("native runtime is busy");
        if (runtime->disposed.load())
            throw std::runtime_error("runtime is disposed");
        auto key = CallbackKey(*runtime, name, args);
        auto existing = runtime->subscriptions.find(key);
        if (existing != runtime->subscriptions.end()) {
            CancelSubscription(existing->second);
            runtime->subscriptions.erase(existing);
        }
        napi_valuetype type;
        Check(napi_typeof(env, argv[2], &type));
        if (type != napi_null && type != napi_undefined) {
            if (type != napi_function)
                throw std::invalid_argument("callback must be a function or null");
            auto subscription = std::make_shared<Subscription>();
            subscription->handle = encos::node::CheckedInteger<std::uint32_t>(args.at("handle"));
            auto* owner = new std::shared_ptr<Subscription>(subscription);
            auto status = napi_create_threadsafe_function(
                env, argv[2], nullptr, Text(env, "EncosCallback"), 0, 1, owner, CallbackFinalize,
                subscription.get(), CallbackJs, &subscription->function);
            if (status != napi_ok) {
                delete owner;
                Check(status);
            }
            napi_unref_threadsafe_function(env, subscription->function);
            try {
                subscription->unbind = encos::node::BindNativeCallback(
                    runtime->store, name, args, [subscription](const Json& event) {
                        auto data = std::make_unique<Json>(event);
                        std::lock_guard<std::mutex> guard(subscription->mutex);
                        if (!subscription->active)
                            return;
                        auto queued = napi_call_threadsafe_function(
                            subscription->function, data.get(), napi_tsfn_nonblocking);
                        if (queued == napi_ok)
                            data.release();
                    });
                auto native_unbind = std::move(subscription->unbind);
                std::weak_ptr<Runtime> owner_runtime = runtime;
                const auto source_handle = subscription->handle;
                subscription->unbind = [owner_runtime, source_handle,
                                        native_unbind = std::move(native_unbind)] {
                    if (auto owner = owner_runtime.lock();
                        owner && owner->store.IsHandleAlive(source_handle))
                        native_unbind();
                };
            } catch (...) {
                CancelSubscription(subscription);
                throw;
            }
            runtime->subscriptions[key] = subscription;
        }
        napi_value result;
        Check(napi_get_undefined(env, &result));
        return result;
    } catch (const encos::wasm::ErrorState& error) {
        napi_throw_error(env, nullptr, error.message.c_str());
        return nullptr;
    } catch (const std::exception& error) {
        napi_throw_error(env, nullptr, error.what());
        return nullptr;
    }
}

napi_value Dispose(napi_env env, napi_callback_info info) {
    napi_value self, result;
    std::size_t argc = 0;
    void* wrapped = nullptr;
    if (napi_get_cb_info(env, info, &argc, nullptr, &self, nullptr) != napi_ok ||
        napi_unwrap(env, self, &wrapped) != napi_ok) {
        napi_throw_error(env, nullptr, "invalid native runtime");
        return nullptr;
    }
    auto runtime = *static_cast<std::shared_ptr<Runtime>*>(wrapped);
    runtime->disposed.store(true);
    std::unique_lock<std::mutex> lock(runtime->mutex, std::try_to_lock);
    if (lock.owns_lock()) {
        runtime->Cleanup();
    }
    napi_get_undefined(env, &result);
    return result;
}
void Finalize(napi_env /*env*/, void* data, void* /*hint*/) {
    auto* runtime = static_cast<std::shared_ptr<Runtime>*>(data);
    (*runtime)->disposed.store(true);
    delete runtime;
}
napi_value Create(napi_env env, napi_callback_info info) {
    try {
        std::size_t argc = 1;
        napi_value argv[1];
        Check(napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));
        if (argc == 1) {
            auto path = ReadString(env, argv[0]);
            if (!path.empty()) {
                std::lock_guard<std::mutex> lock(manager_mutex);
                if (!plugin_path.empty() && plugin_path != path) {
                    throw std::invalid_argument(
                        "native plugin path is process-wide and already configured");
                }
                encos::SetPluginPath(path);
                plugin_path = std::move(path);
            }
        }
        auto runtime = std::make_unique<std::shared_ptr<Runtime>>(std::make_shared<Runtime>());
        napi_value object;
        Check(napi_create_object(env, &object));
        napi_property_descriptor properties[] = {
            {"call", nullptr, Call, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"callAsync", nullptr, Call, nullptr, nullptr, nullptr, napi_default,
             reinterpret_cast<void*>(1)},
            {"invoke", nullptr, Call, nullptr, nullptr, nullptr, napi_default,
             reinterpret_cast<void*>(2)},
            {"invokeAsync", nullptr, Call, nullptr, nullptr, nullptr, napi_default,
             reinterpret_cast<void*>(3)},
            {"subscribe", nullptr, Subscribe, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"dispose", nullptr, Dispose, nullptr, nullptr, nullptr, napi_default, nullptr},
        };
        Check(napi_define_properties(env, object, 6, properties));
        Check(napi_wrap(env, object, runtime.get(), Finalize, nullptr, nullptr));
        runtime.release();
        return object;
    } catch (const std::exception& error) {
        napi_throw_error(env, nullptr, error.what());
        return nullptr;
    }
}
napi_value Init(napi_env env, napi_value exports) {
    try {
        napi_value create;
        Check(
            napi_create_function(env, "createRuntime", NAPI_AUTO_LENGTH, Create, nullptr, &create));
        Check(napi_set_named_property(env, exports, "createRuntime", create));
        Check(napi_set_named_property(env, exports, "version", Text(env, ENCOS_PROJECT_VERSION)));
        return exports;
    } catch (const std::exception& error) {
        napi_throw_error(env, nullptr, error.what());
        return nullptr;
    }
}
}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
