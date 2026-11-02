// SPDX-License-Identifier: GPL-2.0-or-later
// Sound out to the default device (round 365, task 0342): see audio_out.h.
#include "audio_out.h"

#include <atomic>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#elif !defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace bl::audio_out {

struct Stream::Impl {
  Fill fill;
  int rate = 48000;
  std::atomic<bool> stop{false};
  std::atomic<bool> lost{false};  // (the device went away or the writes failed: not open any more - review 365)
  std::thread thread;
  bool open = false;
#if defined(_WIN32)
  /* COM is per thread: the device is opened, run and released on the stream's own thread; open() waits for the
   * outcome of the opening. */
  void run(std::promise<std::string> opened);
#elif !defined(__APPLE__)
  void *pulse = nullptr;  // (the library) and its stream
  void *simple = nullptr;
  int (*write)(void *, const void *, size_t, int *) = nullptr;
  void (*drain_free)(void *) = nullptr;
  bool open_pulse(std::string *why);
  void run();
#endif
};

#if defined(_WIN32)

namespace {
template <class T> void release(T *&p) {
  if (p) p->Release();
  p = nullptr;
}
}  // namespace

void Stream::Impl::run(std::promise<std::string> opened) {
  const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IMMDeviceEnumerator *en = nullptr;
  IMMDevice *dev = nullptr;
  IAudioClient *client = nullptr;
  IAudioRenderClient *render = nullptr;
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  WAVEFORMATEX wf = {};
  wf.wFormatTag = 3;  // (WAVE_FORMAT_IEEE_FLOAT: the mixer's floats as they are; the device converts in shared mode)
  wf.nChannels = 2;
  wf.nSamplesPerSec = (DWORD)rate;
  wf.wBitsPerSample = 32;
  wf.nBlockAlign = 8;
  wf.nAvgBytesPerSec = (DWORD)rate * 8;
  const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
  std::string why;
  UINT32 total = 0;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&en))) why = "no audio devices (WASAPI)";
  if (why.empty() && FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) why = "no default audio output";
  if (why.empty() && FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client))) why = "the audio output can't be opened";
  if (why.empty() && FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 400000, 0, &wf, nullptr))) why = "the audio output refused stereo float sound";
  if (why.empty() && (FAILED(client->SetEventHandle(event)) || FAILED(client->GetBufferSize(&total)))) why = "the audio output's buffer";
  if (why.empty() && FAILED(client->GetService(__uuidof(IAudioRenderClient), (void **)&render))) why = "the audio output's renderer";
  if (why.empty() && FAILED(client->Start())) why = "the audio output didn't start";
  opened.set_value(why);
  while (why.empty() && !stop.load()) {
    WaitForSingleObject(event, 100);
    UINT32 pad = 0;
    const HRESULT hr = client->GetCurrentPadding(&pad);
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_SERVICE_NOT_RUNNING) break;  // (unplugged, another default device)
    if (FAILED(hr) || pad >= total) continue;
    BYTE *data = nullptr;
    const UINT32 n = total - pad;
    if (FAILED(render->GetBuffer(n, &data))) continue;
    fill((float *)data, n);
    render->ReleaseBuffer(n, 0);
  }
  lost = why.empty() && !stop.load();
  if (client) client->Stop();
  release(render), release(client), release(dev), release(en);
  CloseHandle(event);
  if (SUCCEEDED(co)) CoUninitialize();
}

#elif !defined(__APPLE__)

namespace {
struct PaSampleSpec {
  int format;  // (PA_SAMPLE_FLOAT32LE = 5)
  uint32_t rate;
  uint8_t channels;
};
struct PaBufferAttr {
  uint32_t maxlength, tlength, prebuf, minreq, fragsize;
};
using PaSimpleNew = void *(*)(const char *, const char *, int, const char *, const char *, const PaSampleSpec *, const void *,
                              const PaBufferAttr *, int *);
}  // namespace

bool Stream::Impl::open_pulse(std::string *why) {
  pulse = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
  if (!pulse) return *why = "no PulseAudio (libpulse-simple.so.0) on this machine", false;
  auto make = (PaSimpleNew)dlsym(pulse, "pa_simple_new");
  write = (int (*)(void *, const void *, size_t, int *))dlsym(pulse, "pa_simple_write");
  drain_free = (void (*)(void *))dlsym(pulse, "pa_simple_free");
  if (!make || !write || !drain_free) return *why = "PulseAudio's simple API is incomplete", false;
  const PaSampleSpec ss = {5, (uint32_t)rate, 2};
  const PaBufferAttr attr = {(uint32_t)-1, (uint32_t)(rate * 8 / 25), (uint32_t)-1, (uint32_t)-1, (uint32_t)-1};  // (40 ms queued)
  int err = 0;
  simple = make(nullptr, "Blendity", 1 /* PA_STREAM_PLAYBACK */, nullptr, "Sequencer", &ss, nullptr, &attr, &err);
  if (!simple) return *why = "PulseAudio didn't open a stream", false;
  return true;
}

void Stream::Impl::run() {
  std::vector<float> chunk((size_t)rate / 100 * 2);  // (10 ms at a time; the write blocks while the queue is full)
  int err = 0;
  while (!stop.load()) {
    fill(chunk.data(), chunk.size() / 2);
    if (write(simple, chunk.data(), chunk.size() * sizeof(float), &err) < 0) return (void)(lost = true);
  }
}

#endif

Stream::Stream() : impl_(std::make_unique<Impl>()) {}
Stream::~Stream() { close(); }
bool Stream::is_open() const { return impl_->open && !impl_->lost.load(); }

bool Stream::open(int rate, Fill fill, std::string *why) {
  close();
  std::string none;
  if (!why) why = &none;
  impl_->rate = rate > 0 ? rate : 48000;
  impl_->fill = std::move(fill);
  impl_->stop = false;
  impl_->lost = false;
#if defined(_WIN32)
  std::promise<std::string> opened;
  std::future<std::string> outcome = opened.get_future();
  impl_->thread = std::thread([this, p = std::move(opened)]() mutable { impl_->run(std::move(p)); });
  *why = outcome.get();
  if (!why->empty()) impl_->thread.join();
  impl_->open = why->empty();
#elif defined(__APPLE__)
  *why = "sound out isn't on macOS yet";
#else
  if (!impl_->open_pulse(why)) {
    if (impl_->pulse) dlclose(impl_->pulse);
    impl_->pulse = nullptr;
    return false;
  }
  impl_->thread = std::thread([this] { impl_->run(); });
  impl_->open = true;
#endif
  return impl_->open;
}

void Stream::close() {
  if (!impl_) return;
  impl_->stop = true;
  if (impl_->thread.joinable()) impl_->thread.join();
#if !defined(_WIN32) && !defined(__APPLE__)
  if (impl_->simple) impl_->drain_free(impl_->simple);
  if (impl_->pulse) dlclose(impl_->pulse);
  impl_->simple = impl_->pulse = nullptr;
#endif
  impl_->open = false;
}

}  // namespace bl::audio_out
