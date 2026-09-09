#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <string>

class DtkCamera;

// Minimal Win32 + Direct3D 11 application shell used by the DTK demo.
//
// It owns the window, swap chain, depth buffer, Dear ImGui (Win32/DX11
// backends) and the render loop. Derived classes implement onInit/onUpdate/
// onDraw/onGui and receive input events that were not consumed by ImGui.
class DtkApplication {
public:
  DtkApplication(const wchar_t *title, int width, int height);
  virtual ~DtkApplication();

  DtkApplication(const DtkApplication &) = delete;
  DtkApplication &operator=(const DtkApplication &) = delete;

  int run();

  HWND hwnd() const { return _hwnd; }
  ID3D11Device *device() const { return _device.Get(); }
  ID3D11DeviceContext *context() const { return _context.Get(); }
  int width() const { return _width; }
  int height() const { return _height; }
  float aspect() const {
    return _height > 0 ? static_cast<float>(_width) / _height : 1.0f;
  }

  void requestExit() { _running = false; }

  // Camera that receives the built-in Alt+LMB/RMB/MMB navigation.
  void setNavigationCamera(DtkCamera *camera) { _navigationCamera = camera; }

  void clear(float r, float g, float b, float a = 1.0f);
  void present();

  // Saves the current back buffer to a PNG after `frame` rendered frames and
  // optionally exits afterwards. Used for automated visual verification.
  void requestCapture(const std::wstring &path, int frame, bool exitAfter);

protected:
  virtual bool onInit() { return true; }
  virtual void onShutdown() {}
  virtual void onUpdate(float dt) { (void)dt; }
  virtual void onDraw(float dt) { (void)dt; }
  virtual void onGui() {}
  virtual void onResize(int width, int height) {
    (void)width;
    (void)height;
  }

  virtual void onKeyDown(WPARAM key) { (void)key; }
  virtual void onKeyUp(WPARAM key) { (void)key; }
  virtual void onMouseButton(int button, bool down, int x, int y) {
    (void)button;
    (void)down;
    (void)x;
    (void)y;
  }
  virtual void onMouseMove(int x, int y) {
    (void)x;
    (void)y;
  }
  virtual void onMouseWheel(float delta) { (void)delta; }

private:
  void cleanup();
  void resolveBackBuffer();
  bool createWindow(const wchar_t *title, int width, int height);
  bool createDeviceAndSwapChain();
  bool createRenderTargets();
  void destroyRenderTargets();
  void bindBackBuffer();
  bool initImGui();
  void shutdownImGui();
  void handleResize(int width, int height);
  void handleCameraInput(int x, int y, bool altDown);
  void captureBackBuffer(const wchar_t *path);

  static LRESULT CALLBACK windowProcStatic(HWND hwnd, UINT msg, WPARAM wParam,
                                           LPARAM lParam);
  LRESULT windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  HWND _hwnd = nullptr;
  std::wstring _title;
  int _width = 0;
  int _height = 0;
  bool _running = true;
  bool _imguiInitialized = false;
  bool _initialized = false;
  bool _classRegistered = false;
  UINT _msaaSamples = 1;

  Microsoft::WRL::ComPtr<ID3D11Device> _device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> _context;
  Microsoft::WRL::ComPtr<IDXGISwapChain> _swapChain;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _backBufferRTV;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> _msaaColor;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _msaaRTV;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> _depthTexture;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> _depthDSV;
  ID3D11RenderTargetView *_activeRTV = nullptr;

  DtkCamera *_navigationCamera = nullptr;
  POINT _lastMouse{};
  bool _lastMouseValid = false;

  LARGE_INTEGER _perfFrequency{};
  LARGE_INTEGER _perfLast{};

  std::wstring _capturePath;
  int _captureFrame = -1;
  bool _captureExit = false;
  long long _frameIndex = 0;
};
