#include "DtkApplication.h"

#include "DtkCamera.h"

#include <ScreenGrab.h>
#include <wincodec.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <windowsx.h>

#include <cstdio>

// The Win32 backend intentionally hides this declaration behind `#if 0` to
// avoid pulling in <windows.h>; applications are expected to forward declare
// it themselves.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace {

constexpr wchar_t kWindowClassName[] = L"DtkWaterSurfaceWindow";

bool failed(HRESULT hr) { return FAILED(hr); }

} // namespace

DtkApplication::DtkApplication(const wchar_t *title, int width, int height) {
  _title  = title ? title : L"DTK Application";
  _width  = width;
  _height = height;
}

DtkApplication::~DtkApplication() {
  cleanup();
}

int DtkApplication::run() {
  try {
    ImGui_ImplWin32_EnableDpiAwareness();

    if (!createWindow(_title.c_str(), _width, _height))
      return 1;
    if (!createDeviceAndSwapChain())
      return 1;
    if (!createRenderTargets())
      return 1;
    if (!initImGui())
      return 1;
    if (!onInit())
      return 1;
    _initialized = true;

    QueryPerformanceFrequency(&_perfFrequency);
    QueryPerformanceCounter(&_perfLast);

    FILE *timingLog = nullptr;
    {
      wchar_t logPath[MAX_PATH] = {};
      const DWORD logLength =
          GetEnvironmentVariableW(L"DTK_LOG_PATH", logPath, MAX_PATH);
      if (logLength > 0 && logLength < MAX_PATH)
        _wfopen_s(&timingLog, logPath, L"w");
    }

    while (_running) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          _running = false;
          break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      if (!_running)
        break;

      LARGE_INTEGER now;
      QueryPerformanceCounter(&now);
      float dt = static_cast<float>(now.QuadPart - _perfLast.QuadPart) /
                 static_cast<float>(_perfFrequency.QuadPart);
      _perfLast = now;
      if (dt > 0.1f)
        dt = 0.1f;

      LARGE_INTEGER t0, t1, t2;
      QueryPerformanceCounter(&t0);
      onUpdate(dt);
      QueryPerformanceCounter(&t1);

      ImGui_ImplDX11_NewFrame();
      ImGui_ImplWin32_NewFrame();
      ImGui::NewFrame();
      onGui();
      ImGui::Render();

      bindBackBuffer();
      clear(0.09f, 0.11f, 0.15f);
      onDraw(dt);
      ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
      resolveBackBuffer();
      QueryPerformanceCounter(&t2);

      ++_frameIndex;
      if (!_capturePath.empty() && _captureFrame >= 0 &&
          _frameIndex >= _captureFrame) {
        captureBackBuffer(_capturePath.c_str());
        _capturePath.clear();
        if (_captureExit)
          _running = false;
      }

      present();

      if (timingLog && _frameIndex <= 60) {
        const double freq = static_cast<double>(_perfFrequency.QuadPart);
        std::fprintf(timingLog,
                     "frame %lld update=%.2fms draw=%.2fms present=%.2fms\n",
                     _frameIndex,
                     (t1.QuadPart - t0.QuadPart) * 1000.0 / freq,
                     (t2.QuadPart - t1.QuadPart) * 1000.0 / freq, 0.0);
        std::fflush(timingLog);
      }
    }

    if (timingLog)
      std::fclose(timingLog);

    cleanup();
    return 0;
  } catch (...) {
    cleanup();
    throw;
  }
}

void DtkApplication::cleanup() {
  if (_initialized) {
    onShutdown();
    _initialized = false;
  }

  shutdownImGui();
  destroyRenderTargets();

  if (_hwnd) {
    DestroyWindow(_hwnd);
    _hwnd = nullptr;
  }
  if (_classRegistered) {
    UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
    _classRegistered = false;
  }

  _swapChain.Reset();
  _context.Reset();
  _device.Reset();
}

bool DtkApplication::createWindow(const wchar_t *title, int width, int height) {
  HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSEXW wc = {};
  wc.cbSize        = sizeof(WNDCLASSEXW);
  wc.style         = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc   = &DtkApplication::windowProcStatic;
  wc.hInstance     = instance;
  wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kWindowClassName;

  if (!RegisterClassExW(&wc)) {
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return false;
  }
  _classRegistered = true;

  RECT rect = {0, 0, width, height};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

  int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
  int screenHeight = GetSystemMetrics(SM_CYSCREEN);
  int windowWidth  = rect.right - rect.left;
  int windowHeight = rect.bottom - rect.top;
  int posX         = (screenWidth - windowWidth) / 2;
  int posY         = (screenHeight - windowHeight) / 2;

  _hwnd = CreateWindowExW(0, kWindowClassName, title, WS_OVERLAPPEDWINDOW,
                          posX, posY, windowWidth, windowHeight, nullptr,
                          nullptr, instance, this);
  if (!_hwnd)
    return false;

  RECT client = {};
  GetClientRect(_hwnd, &client);
  _width  = client.right - client.left;
  _height = client.bottom - client.top;

  ShowWindow(_hwnd, SW_SHOW);
  UpdateWindow(_hwnd);
  return true;
}

bool DtkApplication::createDeviceAndSwapChain() {
  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferCount                        = 2;
  sd.BufferDesc.Width                   = static_cast<UINT>(_width);
  sd.BufferDesc.Height                  = static_cast<UINT>(_height);
  sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator   = 0;
  sd.BufferDesc.RefreshRate.Denominator = 0;
  sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow                       = _hwnd;
  sd.SampleDesc.Count                   = 1;
  sd.SampleDesc.Quality                 = 0;
  sd.Windowed                           = TRUE;
  sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;
  sd.Flags                              = 0;

  UINT flags = 0;
#if defined(_DEBUG)
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1,
                                D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

  HRESULT hr = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
      static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &sd,
      &_swapChain, &_device, &obtained, &_context);

#if defined(_DEBUG)
  // The D3D11 debug layer is an optional Windows feature; fall back to a
  // normal device when it is not installed.
  if (FAILED(hr)) {
    flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
    hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &sd,
        &_swapChain, &_device, &obtained, &_context);
  }
#endif

  // D3D_FEATURE_LEVEL_11_1 is rejected on machines without the 11.1 runtime.
  if (hr == E_INVALIDARG) {
    hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &levels[1], 1,
        D3D11_SDK_VERSION, &sd, &_swapChain, &_device, &obtained, &_context);
  }

  return SUCCEEDED(hr) && _device && _context && _swapChain;
}

bool DtkApplication::createRenderTargets() {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
  if (failed(_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
    return false;
  if (failed(_device->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                             &_backBufferRTV)))
    return false;

  _msaaSamples = 1;
  UINT quality = 0;
  if (SUCCEEDED(_device->CheckMultisampleQualityLevels(
          DXGI_FORMAT_R8G8B8A8_UNORM, 4, &quality)) &&
      quality > 0) {
    _msaaSamples = 4;
  }

  if (_msaaSamples > 1) {
    D3D11_TEXTURE2D_DESC colorDesc = {};
    colorDesc.Width              = static_cast<UINT>(_width);
    colorDesc.Height             = static_cast<UINT>(_height);
    colorDesc.MipLevels          = 1;
    colorDesc.ArraySize          = 1;
    colorDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count   = _msaaSamples;
    colorDesc.SampleDesc.Quality = 0;
    colorDesc.Usage              = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags          = D3D11_BIND_RENDER_TARGET;

    if (failed(_device->CreateTexture2D(&colorDesc, nullptr, &_msaaColor)))
      return false;
    if (failed(_device->CreateRenderTargetView(_msaaColor.Get(), nullptr,
                                               &_msaaRTV)))
      return false;
  }

  D3D11_TEXTURE2D_DESC depthDesc = {};
  depthDesc.Width              = static_cast<UINT>(_width);
  depthDesc.Height             = static_cast<UINT>(_height);
  depthDesc.MipLevels          = 1;
  depthDesc.ArraySize          = 1;
  depthDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depthDesc.SampleDesc.Count   = _msaaSamples;
  depthDesc.SampleDesc.Quality = 0;
  depthDesc.Usage              = D3D11_USAGE_DEFAULT;
  depthDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;

  if (failed(_device->CreateTexture2D(&depthDesc, nullptr, &_depthTexture)))
    return false;
  if (failed(_device->CreateDepthStencilView(_depthTexture.Get(), nullptr,
                                             &_depthDSV)))
    return false;

  _activeRTV = _msaaSamples > 1 ? _msaaRTV.Get() : _backBufferRTV.Get();
  return true;
}

void DtkApplication::destroyRenderTargets() {
  if (_context) {
    _context->OMSetRenderTargets(0, nullptr, nullptr);
    _context->Flush();
  }

  _activeRTV = nullptr;
  _depthDSV.Reset();
  _depthTexture.Reset();
  _msaaRTV.Reset();
  _msaaColor.Reset();
  _backBufferRTV.Reset();
}

void DtkApplication::bindBackBuffer() {
  if (!_activeRTV || !_depthDSV)
    return;

  _context->OMSetRenderTargets(1, &_activeRTV, _depthDSV.Get());

  D3D11_VIEWPORT viewport = {};
  viewport.Width    = static_cast<float>(_width);
  viewport.Height   = static_cast<float>(_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  _context->RSSetViewports(1, &viewport);
}

void DtkApplication::clear(float r, float g, float b, float a) {
  const float color[4] = {r, g, b, a};
  if (_activeRTV)
    _context->ClearRenderTargetView(_activeRTV, color);
  if (_depthDSV)
    _context->ClearDepthStencilView(
        _depthDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void DtkApplication::present() {
  const HRESULT hr = _swapChain->Present(1, 0);
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    _running = false;
}

void DtkApplication::resolveBackBuffer() {
  if (_msaaSamples > 1 && _msaaColor && _swapChain) {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (SUCCEEDED(_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
      _context->ResolveSubresource(backBuffer.Get(), 0, _msaaColor.Get(), 0,
                                   DXGI_FORMAT_R8G8B8A8_UNORM);
    }
  }
}

void DtkApplication::requestCapture(const std::wstring &path, int frame,
                                    bool exitAfter) {
  _capturePath  = path;
  _captureFrame = frame;
  _captureExit  = exitAfter;
}

void DtkApplication::captureBackBuffer(const wchar_t *path) {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
  if (FAILED(_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
    return;

  DirectX::SaveWICTextureToFile(_context.Get(), backBuffer.Get(),
                                GUID_ContainerFormatPng, path);
}

bool DtkApplication::initImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();

  if (!ImGui_ImplWin32_Init(_hwnd)) {
    ImGui::DestroyContext();
    return false;
  }
  if (!ImGui_ImplDX11_Init(_device.Get(), _context.Get())) {
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return false;
  }

  _imguiInitialized = true;
  return true;
}

void DtkApplication::shutdownImGui() {
  if (!_imguiInitialized)
    return;
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  _imguiInitialized = false;
}

void DtkApplication::handleResize(int width, int height) {
  if (!_device || width <= 0 || height <= 0)
    return;
  if (width == _width && height == _height)
    return;

  _width  = width;
  _height = height;

  destroyRenderTargets();
  if (failed(_swapChain->ResizeBuffers(0, static_cast<UINT>(_width),
                                       static_cast<UINT>(_height),
                                       DXGI_FORMAT_UNKNOWN, 0))) {
    _running = false;
    return;
  }
  if (!createRenderTargets()) {
    _running = false;
    return;
  }
  onResize(_width, _height);
}

void DtkApplication::handleCameraInput(int x, int y, bool altDown) {
  if (!_navigationCamera)
    return;

  if (!_lastMouseValid) {
    _lastMouse      = {x, y};
    _lastMouseValid = true;
    return;
  }

  const DirectX::XMINT2 newPos{x, y};
  const DirectX::XMINT2 oldPos{_lastMouse.x, _lastMouse.y};
  const DirectX::XMINT2 screen{_width, _height};

  if (altDown && (GetKeyState(VK_LBUTTON) & 0x8000))
    _navigationCamera->rotate(newPos, oldPos, screen);
  if (altDown && (GetKeyState(VK_RBUTTON) & 0x8000))
    _navigationCamera->zoom(newPos, oldPos, screen);
  if (altDown && (GetKeyState(VK_MBUTTON) & 0x8000))
    _navigationCamera->pan(newPos, oldPos, screen);

  _lastMouse = {x, y};
}

LRESULT CALLBACK DtkApplication::windowProcStatic(HWND hwnd, UINT msg,
                                                  WPARAM wParam,
                                                  LPARAM lParam) {
  DtkApplication *self = nullptr;

  if (msg == WM_NCCREATE) {
    auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self         = static_cast<DtkApplication *>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<DtkApplication *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self)
    return self->windowProc(hwnd, msg, wParam, lParam);
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT DtkApplication::windowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam) {
  if (_imguiInitialized &&
      ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
    return 1;

  switch (msg) {
  case WM_SIZE:
    handleResize(LOWORD(lParam), HIWORD(lParam));
    return 0;

  case WM_ERASEBKGND:
    return 1;

  case WM_CLOSE:
    _running = false;
    DestroyWindow(hwnd);
    return 0;

  case WM_DESTROY:
    _running = false;
    PostQuitMessage(0);
    return 0;

  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    if (_imguiInitialized && ImGui::GetIO().WantCaptureKeyboard)
      break;
    if (wParam == VK_ESCAPE) {
      _running = false;
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    if (wParam == 'F' && _navigationCamera)
      _navigationCamera->centerToOrigin();
    onKeyDown(wParam);
    return 0;

  case WM_KEYUP:
  case WM_SYSKEYUP:
    if (_imguiInitialized && ImGui::GetIO().WantCaptureKeyboard)
      break;
    onKeyUp(wParam);
    return 0;

  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_MBUTTONDOWN: {
    int button = msg == WM_LBUTTONDOWN ? 0 : (msg == WM_RBUTTONDOWN ? 1 : 2);
    onMouseButton(button, true, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    return 0;
  }

  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MBUTTONUP: {
    int button = msg == WM_LBUTTONUP ? 0 : (msg == WM_RBUTTONUP ? 1 : 2);
    onMouseButton(button, false, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    return 0;
  }

  case WM_MOUSEMOVE: {
    const int x = GET_X_LPARAM(lParam);
    const int y = GET_Y_LPARAM(lParam);
    if (!_imguiInitialized || !ImGui::GetIO().WantCaptureMouse) {
      const bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
      handleCameraInput(x, y, altDown);
      onMouseMove(x, y);
    }
    return 0;
  }

  case WM_MOUSEWHEEL:
    if (!_imguiInitialized || !ImGui::GetIO().WantCaptureMouse)
      onMouseWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
                   static_cast<float>(WHEEL_DELTA));
    return 0;

  default:
    break;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}
