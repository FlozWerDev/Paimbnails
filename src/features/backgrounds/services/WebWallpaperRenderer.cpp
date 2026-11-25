#include "WebWallpaperRenderer.hpp"
#include "WebFrameConvert.hpp"
#include "WebGpuShared.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <wrl.h>
#include <dxgi1_2.h>
#include <d3d11_4.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#endif

namespace paimon::wallpaper {

struct WebWallpaperRenderer::Impl {
    mutable std::mutex mutex;
    // published cpu frame; the worker only rewrites buffers it reclaimed
    // (use_count == 1), so readers never race a rewrite.
    std::shared_ptr<WallpaperFrame> frame;
    uint64_t sequence = 0;
    std::atomic<uint64_t> publishedSequence{0};
    // latest frame on the gpu (guarded by mutex) and its lock-free sequence.
    std::shared_ptr<WebGpuShared> gpu;
    std::atomic<uint64_t> gpuSequence{0};
    std::string failure;
    std::atomic<bool> failureFlag{false};
    std::deque<WallpaperPointer> input;
    std::array<float, 128> audio{};
    bool audioDirty = false;
    // live nodes displaying this renderer, by the path they consume. Once a
    // node registered, zero consumers suspends the page. A renderer nobody
    // ever registered with keeps producing frames (offline tools).
    std::atomic<int> cpuConsumers{0};
    std::atomic<int> gpuConsumers{0};
    std::atomic<bool> consumerRegistered{false};

    void fail(std::string message) {
        {
            std::lock_guard lock(mutex);
            if (!failure.empty()) return;
            failure = std::move(message);
        }
        failureFlag.store(true, std::memory_order_release);
    }

#ifdef _WIN32
    HANDLE wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::stop_source stopSource;
    std::thread worker;
    std::mutex exitMutex;
    std::condition_variable exitCv;
    std::atomic<bool> exited{true};

    ~Impl() {
        if (worker.joinable()) {
            stopSource.request_stop();
            wake();
            worker.join();
        }
        if (wakeEvent) CloseHandle(wakeEvent);
    }

    void wake() const {
        if (wakeEvent) SetEvent(wakeEvent);
    }

    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Browser {
        HWND window = nullptr;
        ComPtr<ICoreWebView2Controller> controller;
        ComPtr<ICoreWebView2> view;
        ComPtr<ICoreWebView2_3> view3;
        bool ready = false;
        ~Browser() {
            if (controller) controller->Close();
            if (window) DestroyWindow(window);
        }
    };

    static std::wstring urlPath(std::string const& entry) {
        constexpr char digits[] = "0123456789ABCDEF";
        std::string encoded;
        for (auto c : entry) {
            auto const byte = static_cast<unsigned char>(c);
            if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                (byte >= '0' && byte <= '9') || c == '/' || c == '.' || c == '-' || c == '_') {
                encoded += c;
            } else if (c == '\\') {
                encoded += '/';
            } else {
                encoded += '%';
                encoded += digits[byte >> 4];
                encoded += digits[byte & 15];
            }
        }
        return L"https://paimon-wallpaper.local/" + std::wstring(encoded.begin(), encoded.end());
    }

    void initializeBrowser(WebWallpaperOptions const& options, std::shared_ptr<Browser> const& browser) {
        static std::once_flag registered;
        std::call_once(registered, [] {
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = DefWindowProcW;
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.lpszClassName = L"Paimbnails.EmbeddedWallpaper";
            if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                winrt::throw_last_error();
            }
        });
        browser->window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"Paimbnails.EmbeddedWallpaper", L"Paimbnails Web Wallpaper", WS_POPUP,
            -20000, -20000, options.width, options.height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!browser->window) winrt::throw_last_error();
        ShowWindow(browser->window, SW_SHOWNOACTIVATE);

        auto const weakBrowser = std::weak_ptr<Browser>(browser);
        auto environmentDone = Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, weakBrowser, options](HRESULT status, ICoreWebView2Environment* environment) -> HRESULT {
                auto browser = weakBrowser.lock();
                if (!browser) return S_OK;
                if (FAILED(status) || !environment) {
                    fail("No se pudo iniciar WebView2. Instala Microsoft Edge WebView2 Runtime.");
                    return S_OK;
                }
                ComPtr<ICoreWebView2Environment> envPtr = environment;
                auto controllerDone = Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, weakBrowser, options, envPtr](HRESULT status, ICoreWebView2Controller* controller) -> HRESULT {
                        auto browser = weakBrowser.lock();
                        if (!browser) return S_OK;
                        if (FAILED(status) || !controller) {
                            fail("No se pudo crear el navegador del fondo.");
                            return S_OK;
                        }
                        browser->controller = controller;
                        controller->get_CoreWebView2(&browser->view);
                        if (!browser->view) { fail("WebView2 no creo una pagina."); return S_OK; }
                        controller->put_Bounds({0, 0, options.width, options.height});
                        ComPtr<ICoreWebView2Controller3> pixels;
                        if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(pixels.GetAddressOf())))) {
                            pixels->put_ShouldDetectMonitorScaleChanges(FALSE);
                            pixels->put_RasterizationScale(1.0);
                            pixels->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
                        }
                        ComPtr<ICoreWebView2Controller2> background;
                        if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(background.GetAddressOf())))) {
                            background->put_DefaultBackgroundColor({255, 0, 0, 0});
                        }
                        controller->put_IsVisible(TRUE);
                        ComPtr<ICoreWebView2Settings> settings;
                        browser->view->get_Settings(&settings);
                        if (settings) {
                            settings->put_AreDefaultContextMenusEnabled(FALSE);
                            settings->put_IsStatusBarEnabled(FALSE);
                            settings->put_IsZoomControlEnabled(FALSE);
                            settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                            settings->put_AreHostObjectsAllowed(FALSE);
                        }
                        if (FAILED(browser->view.As(&browser->view3))) {
                            fail("Actualiza Microsoft Edge WebView2 Runtime.");
                            return S_OK;
                        }
                        auto const root = options.root.wstring();
                        if (FAILED(browser->view3->SetVirtualHostNameToFolderMapping(L"paimon-wallpaper.local", root.c_str(),
                            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) {
                            fail("No se pudo abrir la carpeta del fondo Web."); return S_OK;
                        }
                        EventRegistrationToken token{};
                        browser->view->add_NewWindowRequested(Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* arguments) -> HRESULT {
                                arguments->put_Handled(TRUE); return S_OK;
                            }).Get(), &token);
                        browser->view->add_PermissionRequested(Microsoft::WRL::Callback<ICoreWebView2PermissionRequestedEventHandler>(
                            [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* arguments) -> HRESULT {
                                arguments->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK;
                            }).Get(), &token);
                        // clicks reach the page while the player uses the menu:
                        // a link must not replace the wallpaper with a website.
                        browser->view->add_NavigationStarting(Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* arguments) -> HRESULT {
                                LPWSTR uri = nullptr;
                                if (FAILED(arguments->get_Uri(&uri)) || !uri) return S_OK;
                                std::wstring_view const target(uri);
                                bool const local = target.starts_with(L"https://paimon-wallpaper.local/") ||
                                    target.starts_with(L"about:") || target.starts_with(L"data:") ||
                                    target.starts_with(L"blob:");
                                CoTaskMemFree(uri);
                                if (!local) arguments->put_Cancel(TRUE);
                                return S_OK;
                            }).Get(), &token);
                        // Block outbound subresource network requests (fetch/XHR/images/scripts) from local HTML wallpapers.
                        browser->view->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                        browser->view->add_WebResourceRequested(Microsoft::WRL::Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                            [envPtr](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* arguments) -> HRESULT {
                                ComPtr<ICoreWebView2WebResourceRequest> req;
                                if (FAILED(arguments->get_Request(&req)) || !req) return S_OK;
                                LPWSTR uri = nullptr;
                                if (FAILED(req->get_Uri(&uri)) || !uri) return S_OK;
                                std::wstring_view const target(uri);
                                bool const local = target.starts_with(L"https://paimon-wallpaper.local/") ||
                                    target.starts_with(L"about:") || target.starts_with(L"data:") ||
                                    target.starts_with(L"blob:");
                                CoTaskMemFree(uri);
                                if (!local && envPtr) {
                                    ComPtr<ICoreWebView2WebResourceResponse> blocked;
                                    if (SUCCEEDED(envPtr->CreateWebResourceResponse(nullptr, 403, L"Blocked", L"", &blocked))) {
                                        arguments->put_Response(blocked.Get());
                                    }
                                }
                                return S_OK;
                            }).Get(), &token);
                        browser->view->add_NavigationCompleted(Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                            [this, weakBrowser](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* arguments) -> HRESULT {
                                auto browser = weakBrowser.lock();
                                if (!browser) return S_OK;
                                BOOL succeeded = FALSE;
                                arguments->get_IsSuccess(&succeeded);
                                // later navigations (reloads, cancelled links) do not
                                // break a page that already loaded once.
                                if (browser->ready) return S_OK;
                                if (!succeeded) {
                                    // superseded by another navigation: wait for that one.
                                    COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                    arguments->get_WebErrorStatus(&status);
                                    if (status == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) return S_OK;
                                    fail("No se pudo cargar el archivo HTML del fondo.");
                                } else {
                                    browser->ready = true;
                                }
                                wake();
                                return S_OK;
                            }).Get(), &token);
                        auto const script = winrt::to_hstring(options.bridgeScript);
                        auto const url = urlPath(options.entryFile);
                        browser->view->AddScriptToExecuteOnDocumentCreated(script.c_str(),
                            Microsoft::WRL::Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                                [this, weakBrowser, url](HRESULT status, LPCWSTR) -> HRESULT {
                                    auto browser = weakBrowser.lock();
                                    if (!browser) return S_OK;
                                    if (FAILED(status)) { fail("No se pudo conectar el audio al fondo Web."); return S_OK; }
                                    if (FAILED(browser->view->Navigate(url.c_str()))) fail("No se pudo navegar al fondo Web.");
                                    return S_OK;
                                }).Get());
                        return S_OK;
                    });
                if (FAILED(environment->CreateCoreWebView2Controller(browser->window, controllerDone.Get()))) {
                    fail("No se pudo crear el controlador WebView2.");
                }
                return S_OK;
            });
        auto const storage = options.browserStorage.wstring();
        auto environmentOptions = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        environmentOptions->put_AdditionalBrowserArguments(
            L"--disable-background-timer-throttling --disable-renderer-backgrounding --disable-backgrounding-occluded-windows --disable-features=CalculateNativeWinOcclusion");
        if (FAILED(CreateCoreWebView2EnvironmentWithOptions(nullptr, storage.c_str(), environmentOptions.Get(), environmentDone.Get()))) {
            fail("No se encontro Microsoft Edge WebView2 Runtime.");
        }
    }

    static void sendMouse(ICoreWebView2* view, WallpaperPointer const& pointer,
                          WallpaperPointer const& previous, int width, int height) {
        int const buttons = (pointer.left ? 1 : 0) | (pointer.right ? 2 : 0) | (pointer.middle ? 4 : 0);
        auto send = [&](wchar_t const* type, wchar_t const* button) {
            float const x = pointer.inside ? pointer.x * (width - 1) : -1.f;
            float const y = pointer.inside ? pointer.y * (height - 1) : -1.f;
            wchar_t parameters[192];
            std::swprintf(parameters, std::size(parameters),
                L"{\"type\":\"%ls\",\"x\":%.1f,\"y\":%.1f,\"button\":\"%ls\",\"buttons\":%d,\"clickCount\":1}",
                type, x, y, button, buttons);
            view->CallDevToolsProtocolMethod(L"Input.dispatchMouseEvent", parameters, nullptr);
        };
        send(L"mouseMoved", L"none");
        if (pointer.left != previous.left) send(pointer.left ? L"mousePressed" : L"mouseReleased", L"left");
        if (pointer.right != previous.right) send(pointer.right ? L"mousePressed" : L"mouseReleased", L"right");
        if (pointer.middle != previous.middle) send(pointer.middle ? L"mousePressed" : L"mouseReleased", L"middle");
    }

    static uint32_t defaultAdapterVendor(uint32_t preferred, winrt::com_ptr<IDXGIAdapter1>& chosen) {
        if (!preferred) return 0;
        winrt::com_ptr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void()))) return 0;
        for (UINT i = 0;; ++i) {
            winrt::com_ptr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(i, adapter.put()) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
            if (desc.VendorId == preferred) {
                chosen = adapter;
                return desc.VendorId;
            }
        }
        return 0;
    }

    void run(std::stop_token stop, WebWallpaperOptions const& options) {
        using namespace winrt::Windows::Graphics;
        using namespace winrt::Windows::Graphics::Capture;
        using namespace winrt::Windows::Graphics::DirectX;
        using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
        using Clock = std::chrono::steady_clock;
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        // the capture worker must never outrank the game thread.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        std::stop_callback wakeOnStop(stop, [this] { wake(); });

        auto support = winrt::try_get_activation_factory<GraphicsCaptureSession, IGraphicsCaptureSessionStatics>();
        if (!support || !support.IsSupported()) { fail("El motor Web requiere Windows 10 1903 o posterior."); return; }
        auto browser = std::make_shared<Browser>();
        initializeBrowser(options, browser);

        auto pumpMessages = [] {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        };
        // one wait per iteration: wakes for webview messages, a new frame, an
        // input/consumer change or the timeout.
        HANDLE frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        struct EventGuard { HANDLE h; ~EventGuard() { if (h) CloseHandle(h); } } frameGuard{frameEvent};
        auto waitFor = [&](DWORD timeoutMs) {
            HANDLE handles[2] = {frameEvent, wakeEvent};
            MsgWaitForMultipleObjectsEx(2, handles, std::max<DWORD>(1, timeoutMs), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        };

        auto const deadline = Clock::now() + std::chrono::seconds(30);
        while (!stop.stop_requested() && !browser->ready) {
            pumpMessages();
            if (failureFlag.load(std::memory_order_acquire)) return;
            auto const now = Clock::now();
            if (now >= deadline) { fail("El fondo Web no termino de cargar."); return; }
            waitFor(static_cast<DWORD>(std::min<long long>(250,
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count())));
        }
        if (stop.stop_requested()) return;

        // capture device: same adapter as the game when it can be matched.
        winrt::com_ptr<ID3D11Device> device;
        winrt::com_ptr<ID3D11DeviceContext> context;
        UINT const deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        winrt::com_ptr<IDXGIAdapter1> adapter;
        if (defaultAdapterVendor(options.preferredVendorId, adapter) && adapter) {
            D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, deviceFlags, nullptr, 0,
                D3D11_SDK_VERSION, device.put(), nullptr, context.put());
        }
        if (!device) {
            winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags,
                nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
        }
        // the GL thread copies frames on this same immediate context.
        if (auto multithread = context.try_as<ID3D11Multithread>()) multithread->SetMultithreadProtected(TRUE);

        winrt::com_ptr<IInspectable> inspectable;
        auto dxgi = device.as<IDXGIDevice>();
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
        auto captureDevice = inspectable.as<IDirect3DDevice>();
        GraphicsCaptureItem item{nullptr};
        auto interop = winrt::try_get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        if (!interop) winrt::throw_hresult(E_NOINTERFACE);
        winrt::check_hresult(interop->CreateForWindow(browser->window, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
        auto size = item.Size();
        auto factory = winrt::try_get_activation_factory<Direct3D11CaptureFramePool, IDirect3D11CaptureFramePoolStatics2>();
        if (!factory) winrt::throw_hresult(E_NOINTERFACE);
        auto pool = factory.CreateFreeThreaded(captureDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        auto frameArrived = pool.FrameArrived(winrt::auto_revoke, [frameEvent](auto const&, auto const&) {
            SetEvent(frameEvent);
        });
        auto session = pool.CreateCaptureSession(item);
        if (auto version = session.try_as<IGraphicsCaptureSession2>()) version.IsCursorCaptureEnabled(false);
        if (auto version = session.try_as<IGraphicsCaptureSession3>()) {
            try { version.IsBorderRequired(false); } catch (...) {}
        }
        // Windows 11 24H2+ paces captures to a default minimum interval that
        // halved the page's frame rate; frames are already event driven here.
        if (auto version = session.try_as<IGraphicsCaptureSession5>()) {
            try { version.MinUpdateInterval(std::chrono::milliseconds(1)); } catch (...) {}
        }
        session.StartCapture();

        uint64_t generation = 0;
        auto makeShared = [&](int width, int height) -> std::shared_ptr<WebGpuShared> {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(width);
            desc.Height = static_cast<UINT>(height);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            auto shared = std::make_shared<WebGpuShared>();
            shared->device = device.get();
            shared->context = context.get();
            if (FAILED(device->CreateTexture2D(&desc, nullptr, shared->texture.GetAddressOf()))) return nullptr;
            shared->width = width;
            shared->height = height;
            shared->generation = ++generation;
            return shared;
        };
        std::shared_ptr<WebGpuShared> latest;
        winrt::com_ptr<ID3D11Texture2D> staging;
        int stagingW = 0, stagingH = 0;
        bool readbackPending = false;
        bool cpuDirty = false;
        uint64_t readbackSource = 0;   // gpu sequence copied into staging
        uint64_t readbackPublished = 0; // gpu sequence last published to cpu readers
        auto lastReadback = Clock::now() - std::chrono::seconds(1);
        auto activeSince = Clock::now();
        bool parked = false;
        bool idle = false;
        auto idleSince = Clock::now();
        auto warmupUntil = Clock::now();
        WallpaperPointer previous;
        std::vector<std::shared_ptr<WallpaperFrame>> framePool;
        constexpr size_t kMaxPooledFrames = 4;
        constexpr auto kReadbackInterval = std::chrono::microseconds(16'600);

        auto publishStaging = [&]() -> bool {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            HRESULT const mappedResult = context->Map(staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (mappedResult == DXGI_ERROR_WAS_STILL_DRAWING) return false;
            readbackPending = false;
            if (FAILED(mappedResult)) return true;
            std::shared_ptr<WallpaperFrame> next;
            for (auto it = framePool.begin(); it != framePool.end(); ++it) {
                if (it->use_count() == 1) {
                    next = std::move(*it);
                    framePool.erase(it);
                    break;
                }
            }
            if (!next) next = std::make_shared<WallpaperFrame>();
            next->width = stagingW;
            next->height = stagingH;
            next->rgba.resize(static_cast<size_t>(stagingW) * static_cast<size_t>(stagingH) * 4);
            for (int y = 0; y < stagingH; ++y) {
                auto const* row = static_cast<uint8_t const*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
                convertBgraToRgba(next->rgba.data() + static_cast<size_t>(y) * stagingW * 4, row, static_cast<size_t>(stagingW));
            }
            context->Unmap(staging.get(), 0);
            {
                std::lock_guard lock(mutex);
                next->sequence = ++sequence;
                std::swap(frame, next);
                publishedSequence.store(sequence, std::memory_order_release);
            }
            readbackPublished = readbackSource;
            if (next) {
                framePool.push_back(std::move(next));
                while (framePool.size() > kMaxPooledFrames) {
                    auto reclaim = std::find_if(framePool.begin(), framePool.end(),
                        [](auto const& entry) { return entry.use_count() == 1; });
                    if (reclaim == framePool.end()) break;
                    framePool.erase(reclaim);
                }
            }
            return true;
        };

        while (!stop.stop_requested()) {
            auto const now = Clock::now();
            int const cpuCount = cpuConsumers.load(std::memory_order_relaxed);
            int const gpuCount = gpuConsumers.load(std::memory_order_relaxed);
            bool const registered = consumerRegistered.load(std::memory_order_relaxed);
            bool const active = !registered || cpuCount + gpuCount > 0;
            // offline readers never register: they always get cpu frames.
            bool const cpuWanted = cpuCount > 0 || !registered;

            DWORD timeout = active ? 100 : 250;
            if (active && cpuWanted && (readbackPending || cpuDirty)) {
                auto const sinceReadback = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReadback).count();
                timeout = readbackPending ? 2 : static_cast<DWORD>(std::max<long long>(1, 17 - sinceReadback));
            }
            if (active && now < warmupUntil) {
                timeout = std::min<DWORD>(timeout, static_cast<DWORD>(std::max<long long>(1,
                    std::chrono::duration_cast<std::chrono::milliseconds>(warmupUntil - now).count())));
            }
            waitFor(timeout);
            pumpMessages();
            if (stop.stop_requested()) break;
            auto const tick = Clock::now();

            if (!active) {
                // nothing draws this page: after a short grace period hide and
                // suspend it so it stops running script and rendering frames.
                if (!idle) { idle = true; idleSince = tick; }
                if (!parked && tick - idleSince > std::chrono::milliseconds(800)) {
                    browser->controller->put_IsVisible(FALSE);
                    if (browser->view3) {
                        browser->view3->TrySuspend(Microsoft::WRL::Callback<ICoreWebView2TrySuspendCompletedHandler>(
                            [](HRESULT, BOOL) -> HRESULT { return S_OK; }).Get());
                    }
                    parked = true;
                }
                while (auto discarded = pool.TryGetNextFrame()) discarded.Close();
                continue;
            }
            if (idle) {
                idle = false;
                activeSince = tick;
            }
            if (parked) {
                if (browser->view3) browser->view3->Resume();
                browser->controller->put_IsVisible(TRUE);
                parked = false;
                // the first compositions after unhiding can be blank.
                warmupUntil = tick + std::chrono::milliseconds(150);
            }

            std::deque<WallpaperPointer> pending;
            std::array<float, 128> spectrum{};
            bool sendAudio = false;
            {
                std::lock_guard lock(mutex);
                pending.swap(input);
                if (audioDirty) {
                    spectrum = audio;
                    audioDirty = false;
                    sendAudio = true;
                }
            }
            // coalesce: button transitions keep their position, plain moves
            // collapse into the last one.
            std::optional<WallpaperPointer> move;
            for (auto const& pointer : pending) {
                bool const buttons = pointer.left != previous.left || pointer.right != previous.right ||
                    pointer.middle != previous.middle;
                if (buttons) {
                    sendMouse(browser->view.Get(), pointer, previous, options.width, options.height);
                    previous = pointer;
                    move.reset();
                } else {
                    move = pointer;
                }
            }
            if (move) {
                sendMouse(browser->view.Get(), *move, previous, options.width, options.height);
                previous = *move;
            }
            if (sendAudio) {
                // PostWebMessageAsJson skips the script compile ExecuteScript paid.
                std::wstring message;
                message.reserve(spectrum.size() * 6 + 2);
                message += L'[';
                wchar_t number[16];
                for (size_t i = 0; i < spectrum.size(); ++i) {
                    if (i) message += L',';
                    std::swprintf(number, std::size(number), L"%.3g", spectrum[i]);
                    message += number;
                }
                message += L']';
                browser->view->PostWebMessageAsJson(message.c_str());
            }

            // newest composition only; older queued frames are stale.
            Direct3D11CaptureFrame captured{nullptr};
            while (auto next = pool.TryGetNextFrame()) {
                if (captured) captured.Close();
                captured = next;
            }
            if (captured) {
                auto const content = captured.ContentSize();
                bool const warming = tick < warmupUntil;
                if (!warming && content.Width > 0 && content.Height > 0 && content.Width <= 4096 && content.Height <= 4096) {
                    auto access = captured.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                    winrt::com_ptr<ID3D11Texture2D> source;
                    winrt::check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), source.put_void()));
                    if (!latest || latest->width != content.Width || latest->height != content.Height) {
                        auto replacement = makeShared(content.Width, content.Height);
                        if (replacement) {
                            latest = replacement;
                            std::lock_guard lock(mutex);
                            gpu = replacement;
                        }
                    }
                    if (latest) {
                        D3D11_BOX box{0, 0, 0, static_cast<UINT>(content.Width), static_cast<UINT>(content.Height), 1};
                        context->CopySubresourceRegion(latest->texture.Get(), 0, 0, 0, 0, source.get(), 0, &box);
                        context->Flush();
                        gpuSequence.fetch_add(1, std::memory_order_acq_rel);
                        cpuDirty = true;
                    }
                }
                captured.Close();
                if (content.Width > 0 && content.Height > 0 &&
                    (content.Width != size.Width || content.Height != size.Height)) {
                    size = content;
                    pool.Recreate(captureDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
                }
            } else if (gpuSequence.load(std::memory_order_relaxed) == 0 && tick - activeSince > std::chrono::seconds(25)) {
                fail("WebView2 no entrega imagenes del fondo.");
                break;
            }

            // cpu fallback: rate-limited readback that never blocks on the gpu.
            if (cpuWanted && latest) {
                uint64_t const current = gpuSequence.load(std::memory_order_relaxed);
                if (readbackPublished < current && !readbackPending) cpuDirty = true;
                if (readbackPending) publishStaging();
                if (cpuDirty && !readbackPending && tick - lastReadback >= kReadbackInterval) {
                    if (!staging || stagingW != latest->width || stagingH != latest->height) {
                        D3D11_TEXTURE2D_DESC desc{};
                        latest->texture->GetDesc(&desc);
                        desc.Usage = D3D11_USAGE_STAGING;
                        desc.BindFlags = 0;
                        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                        desc.MiscFlags = 0;
                        staging = nullptr;
                        if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, staging.put()))) {
                            stagingW = latest->width;
                            stagingH = latest->height;
                        }
                    }
                    if (staging) {
                        context->CopyResource(staging.get(), latest->texture.Get());
                        context->Flush();
                        readbackPending = true;
                        readbackSource = current;
                        cpuDirty = false;
                        lastReadback = tick;
                    }
                }
            }
        }
        frameArrived.revoke();
        session.Close();
        pool.Close();
        if (browser->controller) browser->controller->Close();
        browser->controller.Reset();
        browser->view3.Reset();
        browser->view.Reset();
    }
#else
    void wake() const {}
#endif
};

WebWallpaperRenderer::WebWallpaperRenderer(WebWallpaperOptions options) : m_impl(std::make_shared<Impl>()) {
#ifdef _WIN32
    options.width = std::clamp(options.width, 64, 2560);
    options.height = std::clamp(options.height, 64, 1440);
    m_impl->exited.store(false, std::memory_order_release);
    auto impl = m_impl;
    auto stop = m_impl->stopSource.get_token();
    m_impl->worker = std::thread([impl, stop, options = std::move(options)] {
        struct ExitMark {
            std::shared_ptr<Impl> impl;
            ~ExitMark() {
                if (impl) {
                    {
                        std::lock_guard lock(impl->exitMutex);
                        impl->exited.store(true, std::memory_order_release);
                    }
                    impl->exitCv.notify_all();
                }
            }
        } exitMark{impl};
        try { impl->run(stop, options); }
        catch (winrt::hresult_error const& error) { impl->fail("Error del motor Web: " + winrt::to_string(error.message())); }
        catch (std::exception const& error) { impl->fail("Error del fondo Web: " + std::string(error.what())); }
    });
#else
    (void)options;
    m_impl->fail("El motor Web integrado aun requiere Windows.");
#endif
}

WebWallpaperRenderer::~WebWallpaperRenderer() {
    stop();
}

void WebWallpaperRenderer::requestStop() {
#ifdef _WIN32
    if (!m_impl) return;
    m_impl->stopSource.request_stop();
    m_impl->wake();
#endif
}

void WebWallpaperRenderer::waitStop(std::chrono::steady_clock::time_point deadline) {
#ifdef _WIN32
    if (!m_impl || !m_impl->worker.joinable()) return;
    requestStop();
    {
        std::unique_lock lock(m_impl->exitMutex);
        m_impl->exitCv.wait_until(lock, deadline, [impl = m_impl.get()] {
            return impl->exited.load(std::memory_order_acquire);
        });
    }
    if (m_impl->exited.load(std::memory_order_acquire)) {
        m_impl->worker.join();
    } else {
        // Worker owns its own shared_ptr<Impl>; detaching avoids freezing exit on stuck WebView2/WGC teardown.
        m_impl->worker.detach();
    }
#else
    (void)deadline;
#endif
}

void WebWallpaperRenderer::stop() {
    waitStop(std::chrono::steady_clock::now() + std::chrono::milliseconds(1500));
}

void WebWallpaperRenderer::setPointer(WallpaperPointer pointer) {
    pointer.x = std::isfinite(pointer.x) ? std::clamp(pointer.x, 0.f, 1.f) : 0.5f;
    pointer.y = std::isfinite(pointer.y) ? std::clamp(pointer.y, 0.f, 1.f) : 0.5f;
    if (!pointer.inside) pointer.left = pointer.right = pointer.middle = false;
    {
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->input.size() >= 128) m_impl->input.pop_front();
        m_impl->input.push_back(pointer);
    }
    m_impl->wake();
}

void WebWallpaperRenderer::setAudio(std::array<float, 128> const& spectrum) {
    {
        std::lock_guard lock(m_impl->mutex);
        std::transform(spectrum.begin(), spectrum.end(), m_impl->audio.begin(), [](float sample) {
            return std::isfinite(sample) ? std::clamp(sample, 0.f, 1.f) : 0.f;
        });
        m_impl->audioDirty = true;
    }
    m_impl->wake();
}

bool WebWallpaperRenderer::takeLatestFrame(uint64_t afterSequence,
    std::shared_ptr<WallpaperFrame const>& out) const {
    if (m_impl->publishedSequence.load(std::memory_order_acquire) <= afterSequence) return false;
    std::lock_guard lock(m_impl->mutex);
    if (!m_impl->frame || m_impl->frame->sequence <= afterSequence) return false;
    out = m_impl->frame;
    return true;
}

bool WebWallpaperRenderer::copyLatestFrame(uint64_t afterSequence, WallpaperFrame& destination) const {
    if (m_impl->publishedSequence.load(std::memory_order_acquire) <= afterSequence) return false;
    std::lock_guard lock(m_impl->mutex);
    if (!m_impl->frame || m_impl->frame->sequence <= afterSequence) return false;
    destination = *m_impl->frame;
    return true;
}

uint64_t WebWallpaperRenderer::gpuSequence() const {
    return m_impl->gpuSequence.load(std::memory_order_acquire);
}

std::shared_ptr<WebGpuShared const> WebWallpaperRenderer::gpuShared() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->gpu;
}

void WebWallpaperRenderer::addConsumer(WebConsumer kind) {
    m_impl->consumerRegistered.store(true, std::memory_order_relaxed);
    (kind == WebConsumer::Gpu ? m_impl->gpuConsumers : m_impl->cpuConsumers).fetch_add(1, std::memory_order_relaxed);
    m_impl->wake();
}

void WebWallpaperRenderer::removeConsumer(WebConsumer kind) {
    auto& counter = kind == WebConsumer::Gpu ? m_impl->gpuConsumers : m_impl->cpuConsumers;
    int current = counter.load(std::memory_order_relaxed);
    while (current > 0 && !counter.compare_exchange_weak(current, current - 1,
        std::memory_order_relaxed, std::memory_order_relaxed)) {}
    m_impl->wake();
}

bool WebWallpaperRenderer::hasError() const {
    return m_impl->failureFlag.load(std::memory_order_acquire);
}

std::string WebWallpaperRenderer::error() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->failure;
}

bool WebWallpaperRenderer::isAvailable() {
#ifdef _WIN32
    LPWSTR version = nullptr;
    HRESULT const result = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    CoTaskMemFree(version);
    return SUCCEEDED(result);
#else
    return false;
#endif
}

} // namespace paimon::wallpaper
