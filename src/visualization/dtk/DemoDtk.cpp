#include "DtkApplication.h"
#include "DtkCamera.h"
#include "DtkTerrain.h"
#include "DtkWaterSurface.h"

#include "DirectionNumber.h"
#include "../../WaveGrid.h"

#include <imgui.h>
#include <objbase.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>

using namespace WaterWavelets;
using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

// Simulation settings.
auto settings = []() {
  auto s = WaveGrid::Settings{};

  s.size     = 50;
  s.min_zeta = log2(0.03);
  s.max_zeta = log2(10);

  s.n_x     = 100;
  s.n_theta = DIR_NUM;
  s.n_zeta  = 1;

  s.initial_time = 100;

  return s;
}();

// Visualization settings.
int   visGridResolution  = 100;
float amplitudeMult      = 4.0f;
bool  update_screen_grid = true;
bool  continuousSource   = false;
float logdt              = -0.9f;
int   directionToShow    = -1;

const Vec2 demoSourcePosition{0.0f, 0.0f};

// Debug switches driven by DTK_DEBUG:
//   1 = water only, 2 = terrain only, 3 = flat water, 4 = disable face culling
int debugMode = 0;

class Scene3D : public DtkApplication {
public:
  Scene3D()
      : DtkApplication(L"Water Surface Wavelets - DirectXTK", 1200, 800),
        _waveGrid(settings) {}

protected:
  bool onInit() override {
    setNavigationCamera(&_camera);

    _waterSurface = std::make_unique<DtkWaterSurface>(device(), context(),
                                                      visGridResolution);
    _terrain = std::make_unique<DtkTerrain>(device(), context(), 100, 100);

    _terrain->setVertices([this](int, DtkTerrain::Vertex &v) {
      v.position.x *= 0.99f * settings.size;
      v.position.y *= 0.99f * settings.size;
      v.position.z =
          -10.0f *
          std::tanh(0.1f * _waveGrid.m_enviroment.levelset(
                               {v.position.x, v.position.y}));
    });
    _terrain->setDiffuseColor({0.4f, 0.4f, 0.4f, 1.0f});

    initializeDemoWaves();

    if (debugMode == 3)
      amplitudeMult = 0.0f;
    if (debugMode == 4) {
      _waterSurface->setCullNone(true);
      _terrain->setCullNone(true);
    }

    float cameraDistance = 60.0f;
    wchar_t distanceBuffer[32] = {};
    if (GetEnvironmentVariableW(L"DTK_CAMERA_DISTANCE", distanceBuffer,
                                static_cast<DWORD>(_countof(distanceBuffer))) >
        0)
      cameraDistance = static_cast<float>(_wtof(distanceBuffer));
    _camera.zoom(cameraDistance / 10.0f);

    _camera.setViewport(width(), height());
    _camera.setProjection(60.0f, aspect(), 0.1f, 1000.0f);
    return true;
  }

  void onResize(int width, int height) override {
    _camera.setViewport(width, height);
    _camera.setProjection(60.0f, aspect(), 0.1f, 1000.0f);
  }

  void onUpdate(float dt) override {
    (void)dt;

    // Advance the simulation before uploading the profile texture so the
    // first rendered frame also has valid wave data.
    _waveGrid.timeStep(_waveGrid.cflTimeStep() * std::pow(10.0f, logdt),
                       update_screen_grid);

    if (continuousSource)
      _waveGrid.addPointDisturbance(demoSourcePosition, 0.001f);

    // Sample the simulation into the screen-space water grid.
    if (update_screen_grid) {
      _waterSurface->setVertices([this](int i, DtkWaterSurface::Vertex &v) {
        const int ix = i / (visGridResolution + 1);
        const int iy = i % (visGridResolution + 1);

        const Vector2 screenPos{
            (2.0f * ix) / visGridResolution - 1.0f,
            (2.0f * iy) / visGridResolution - 1.0f};

        auto[dir, camPos] = _camera.cameraRayCast(screenPos);

        float t = 1000.0f;
        if (std::abs(dir.z) > 1e-6f) {
          t = -camPos.z / dir.z;
          if (!std::isfinite(t) || t < 0.0f)
            t = 1000.0f;
        }

        Vector3 position = camPos + t * dir;
        position.z       = 0.0f;

        v.position = {position.x, position.y, position.z, 1.0f};

        for (int itheta = 0; itheta < DIR_NUM; ++itheta) {
          const float theta = _waveGrid.idxToPos(itheta, WaveGrid::Theta);
          const Vec4  pos4{position.x, position.y, theta,
                           _waveGrid.idxToPos(0, WaveGrid::Zeta)};

          if (directionToShow == -1 || directionToShow == itheta)
            v.amplitude[itheta] = amplitudeMult * _waveGrid.amplitude(pos4);
          else
            v.amplitude[itheta] = 0.0f;
        }
      });
    }

    const ProfileBuffer &profile = _waveGrid.m_profileBuffers[0];
    _waterSurface->loadProfile(
        reinterpret_cast<const float *>(profile.m_data.data()),
        profile.m_data.size(), profile.m_period);
  }

  void onDraw(float) override {
    // World-fixed light at the point (15, 15, 30).
    const Vector3 lightPosition{15.0f, 15.0f, 30.0f};

    if (debugMode != 1)
      _terrain->draw(_camera, lightPosition);
    if (debugMode != 2)
      _waterSurface->draw(_camera, lightPosition);
  }

  void onGui() override {
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Water Surface Wavelets", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Water Surface Wavelets Demo (DirectXTK / D3D11)");

      ImGui::SliderInt("direction", &directionToShow, -1, DIR_NUM - 1);
      ImGui::SliderFloat("amplitude", &amplitudeMult, 0.0f, 20.0f);
      ImGui::SliderFloat("log(dt)", &logdt, -2.0f, 2.0f);
      ImGui::DragFloat("time", &_waveGrid.m_time);
      ImGui::Checkbox("update screen grid", &update_screen_grid);
      ImGui::Checkbox("continuous source", &continuousSource);

      if (ImGui::Button("Reset")) {
        _waveGrid = WaveGrid(settings);
        initializeDemoWaves();
      }

      static bool triangulation = false;
      if (ImGui::Checkbox("triangulation", &triangulation))
        _waterSurface->setShowTriangulation(triangulation);

      if (ImGui::CollapsingHeader("Navigation Help")) {
        ImGui::Text("Rotate:   Alt + LMB");
        ImGui::Text("Zoom:     Alt + RMB");
        ImGui::Text("Pan:      Alt + MMB");
        ImGui::Text("Recenter: f");
      }
    }
    ImGui::End();
  }

private:
  void initializeDemoWaves() {
    for (int ix = 0; ix < _waveGrid.m_amplitude.dimension(WaveGrid::X); ++ix) {
      for (int iy = 0; iy < _waveGrid.m_amplitude.dimension(WaveGrid::Y);
           ++iy) {
        const Vec2 pos{_waveGrid.idxToPos(ix, WaveGrid::X),
                       _waveGrid.idxToPos(iy, WaveGrid::Y)};
        if (!_waveGrid.m_enviroment.inDomain(pos))
          continue;

        for (int itheta = 0;
             itheta < _waveGrid.m_amplitude.dimension(WaveGrid::Theta);
             ++itheta)
          _waveGrid.m_amplitude(ix, iy, itheta, 0) = 0.02f;
      }
    }
  }

  WaveGrid _waveGrid;
  DtkCamera _camera;

  std::unique_ptr<DtkTerrain>     _terrain;
  std::unique_ptr<DtkWaterSurface> _waterSurface;
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  int result = 1;
  try {
    Scene3D scene;

    wchar_t capturePath[MAX_PATH] = {};
    wchar_t debugBuffer[16]       = {};
    if (GetEnvironmentVariableW(L"DTK_DEBUG", debugBuffer,
                                static_cast<DWORD>(_countof(debugBuffer))) > 0)
      debugMode = _wtoi(debugBuffer);

    const DWORD captureLength =
        GetEnvironmentVariableW(L"DTK_CAPTURE_PATH", capturePath, MAX_PATH);
    if (captureLength > 0 && captureLength < MAX_PATH) {
      int captureFrame = 180;
      wchar_t frameBuffer[32] = {};
      if (GetEnvironmentVariableW(L"DTK_CAPTURE_FRAME", frameBuffer,
                                  static_cast<DWORD>(_countof(frameBuffer))) >
          0)
        captureFrame = _wtoi(frameBuffer);

      scene.requestCapture(capturePath, captureFrame, true);
    }

    result = scene.run();
  } catch (const std::exception &e) {
    FILE *log = nullptr;
    if (_wfopen_s(&log, L"dtk-error.log", L"w") == 0 && log) {
      std::fprintf(log, "%s\n", e.what());
      std::fclose(log);
    }
    MessageBoxA(nullptr, e.what(), "Water Surface Wavelets (DTK)",
                MB_OK | MB_ICONERROR);
    result = 1;
  }

  if (SUCCEEDED(comResult))
    CoUninitialize();
  return result;
}
