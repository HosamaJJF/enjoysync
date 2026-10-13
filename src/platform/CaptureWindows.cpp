#include "Capture.h"
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl.h>
#include <array>
#include <future>
#include <thread>
namespace enjoysync {
using Microsoft::WRL::ComPtr;
class Activation final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                                           Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler> {
public:
    Activation() : event(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
    ~Activation() { if (event) CloseHandle(event); }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        ComPtr<IUnknown> object; HRESULT activationResult = E_FAIL;
        result = operation->GetActivateResult(&activationResult, &object);
        if (SUCCEEDED(result)) result = activationResult;
        if (SUCCEEDED(result)) result = object.As(&client);
        SetEvent(event); return S_OK;
    }
    HANDLE event;
    HRESULT result = E_FAIL;
    ComPtr<IAudioClient> client;
};
class WindowsCapture final : public SystemCapture {
public:
    WindowsCapture() : stopEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~WindowsCapture() override { stop(); CloseHandle(stopEvent); }
    juce::Result start(AudioEngine& target) override {
        stop(); ResetEvent(stopEvent); { std::lock_guard lock(mutex); lastError.clear(); }
        std::promise<juce::Result> started; auto result = started.get_future();
        worker = std::thread([this, &target, promise = std::move(started)]() mutable { run(target, std::move(promise)); });
        if (result.wait_for(std::chrono::seconds(8)) != std::future_status::ready) { stop(); return juce::Result::fail(u8"系统声音采集启动超时"_es); }
        auto status = result.get(); if (status.failed()) stop(); return status;
    }
    void stop() override { SetEvent(stopEvent); if (worker.joinable()) worker.join(); active = false; }
    bool running() const override { return active.load(); }
    juce::String error() const override { std::lock_guard lock(mutex); return lastError; }
private:
    void run(AudioEngine& engine, std::promise<juce::Result> promise) {
        HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool reported = false;
        HANDLE dataEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ComPtr<IAudioClient> client; ComPtr<IAudioCaptureClient> capture;
        auto finish = [&](HRESULT code, const juce::String& operation) {
            if (client) client->Stop(); engine.setCapturing(false); active = false;
            auto text = operation + " (0x" + juce::String::toHexString((int)code) + ")";
            { std::lock_guard lock(mutex); if (FAILED(code)) lastError = text; }
            if (!reported) { promise.set_value(FAILED(code) ? juce::Result::fail(text) : juce::Result::ok()); reported = true; }
            if (dataEvent) CloseHandle(dataEvent);
            capture.Reset(); client.Reset(); if (SUCCEEDED(apartment)) CoUninitialize();
        };
        if (FAILED(apartment)) { finish(apartment, u8"COM 初始化失败"_es); return; }
        auto handler = Microsoft::WRL::Make<Activation>();
        AUDIOCLIENT_ACTIVATION_PARAMS parameters {};
        parameters.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        parameters.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
        parameters.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT value {}; value.vt = VT_BLOB; value.blob.cbSize = sizeof(parameters);
        value.blob.pBlobData = reinterpret_cast<BYTE*>(&parameters);
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        auto hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                              &value, handler.Get(), &operation);
        if (FAILED(hr)) { finish(hr, u8"请使用支持进程回环采集的 Windows 11 系统"_es); return; }
        HANDLE activationEvents[] {stopEvent, handler->event};
        if (WaitForMultipleObjects(2, activationEvents, FALSE, 6000) != WAIT_OBJECT_0 + 1) { finish(E_ABORT, u8"采集初始化已取消或超时"_es); return; }
        if (FAILED(handler->result)) { finish(handler->result, u8"无法激活系统声音采集"_es); return; }
        client = handler->client;
        WAVEFORMATEX format {}; format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; format.nChannels = 2;
        format.nSamplesPerSec = 48000; format.wBitsPerSample = 32; format.nBlockAlign = 8;
        format.nAvgBytesPerSec = 384000;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                               | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, 200000, 0, &format, nullptr);
        if (FAILED(hr)) { finish(hr, u8"无法设置系统声音采集格式"_es); return; }
        hr = client->SetEventHandle(dataEvent);
        if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&capture));
        if (FAILED(hr)) { finish(hr, u8"无法打开系统声音采集缓冲"_es); return; }
        engine.configureCapture(48000); engine.setCapturing(true);
        hr = client->Start(); if (FAILED(hr)) { finish(hr, u8"无法开始系统声音采集"_es); return; }
        active = true; promise.set_value(juce::Result::ok()); reported = true;
        std::array<float, 8192> left {}, right {};
        HANDLE events[] {stopEvent, dataEvent};
        for (;;) {
            auto wait = WaitForMultipleObjects(2, events, FALSE, 1000);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_FAILED) { hr = E_FAIL; break; }
            UINT32 packet = 0; hr = capture->GetNextPacketSize(&packet);
            if (FAILED(hr)) break;
            while (packet > 0) {
                BYTE* bytes = nullptr; UINT32 frames = 0; DWORD flags = 0;
                hr = capture->GetBuffer(&bytes, &frames, &flags, nullptr, nullptr); if (FAILED(hr)) break;
                auto time = aoo_getCurrentNtpTime(); auto* samples = reinterpret_cast<const float*>(bytes);
                for (UINT32 offset = 0; offset < frames; offset += 8192) {
                    auto count = std::min<UINT32>(8192, frames - offset);
                    for (UINT32 i = 0; i < count; ++i) {
                        left[i] = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.0f : samples[(offset + i) * 2];
                        right[i] = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.0f : samples[(offset + i) * 2 + 1];
                    }
                    engine.pushCapture(left.data(), right.data(), (int)count, time + aoo_ntpTimeFromSeconds(offset / 48000.0));
                }
                capture->ReleaseBuffer(frames); hr = capture->GetNextPacketSize(&packet); if (FAILED(hr)) break;
            }
            if (FAILED(hr)) break;
        }
        finish(hr, u8"系统音频设备已变化，请重新开启共享"_es);
    }
    HANDLE stopEvent;
    std::thread worker;
    std::atomic<bool> active { false };
    mutable std::mutex mutex;
    juce::String lastError;
};
std::unique_ptr<SystemCapture> createSystemCapture() { return std::make_unique<WindowsCapture>(); }
}
