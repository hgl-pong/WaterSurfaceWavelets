#pragma once

#include "DtkCamera.h"
#include "DirectionNumber.h"

#include <CommonStates.h>
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <vector>

// Water surface renderer for the DTK demo.
//
// The CPU keeps a screen-space grid whose vertices carry the 16 directional
// amplitudes sampled from the WaveGrid. The vertex shader reconstructs the
// Gerstner displacement by integrating the precomputed profile texture, the
// pixel shader reconstructs the normal the same way - a Direct3D port of the
// original WaterSurfaceShader (GLSL).
class DtkWaterSurface {
public:
  struct Vertex {
    DirectX::XMFLOAT4 position;
    float             amplitude[DIR_NUM];
  };

  DtkWaterSurface(ID3D11Device *device, ID3D11DeviceContext *context,
                  int gridResolution);
  ~DtkWaterSurface() = default;

  DtkWaterSurface(const DtkWaterSurface &) = delete;
  DtkWaterSurface &operator=(const DtkWaterSurface &) = delete;

  template <class Fun> void setVertices(Fun fun) {
    const int count = static_cast<int>(_vertices.size());
#pragma omp parallel for
    for (int i = 0; i < count; ++i)
      fun(i, _vertices[static_cast<size_t>(i)]);
    uploadVertices();
  }

  void loadProfile(const float *data, size_t count, float period);

  void draw(const DtkCamera &camera,
            const DirectX::SimpleMath::Vector3 &lightPosition);

  void setShowTriangulation(bool show) { _showTriangulation = show; }
  void setCullNone(bool cullNone) { _cullNone = cullNone; }
  void setDiffuseColor(const DirectX::XMFLOAT4 &color) { _diffuseColor = color; }
  void setAmbientColor(const DirectX::XMFLOAT4 &color) { _ambientColor = color; }

private:
  struct Constants {
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4X4 viewProjection;
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4   lightPosition;
    DirectX::XMFLOAT4   ambientColor;
    DirectX::XMFLOAT4   diffuseColor;
    DirectX::XMFLOAT4   cameraPosition;
    DirectX::XMFLOAT4   params;
  };

  void createShaders();
  void createBuffers();
  void createProfileTexture(size_t count);
  void uploadVertices();
  void uploadProfile();

  Microsoft::WRL::ComPtr<ID3D11Device>        _device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> _context;

  Microsoft::WRL::ComPtr<ID3D11Buffer>       _vertexBuffer;
  Microsoft::WRL::ComPtr<ID3D11Buffer>       _indexBuffer;
  Microsoft::WRL::ComPtr<ID3D11Buffer>       _constantBuffer;
  Microsoft::WRL::ComPtr<ID3D11InputLayout>  _inputLayout;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> _vertexShader;
  Microsoft::WRL::ComPtr<ID3D11PixelShader>  _pixelShader;

  Microsoft::WRL::ComPtr<ID3D11Texture1D>           _profileTexture;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  _profileView;

  std::unique_ptr<DirectX::CommonStates> _states;

  std::vector<Vertex>   _vertices;
  std::vector<uint32_t> _indices;
  std::vector<float>    _profileData;
  float                 _profilePeriod = 1.0f;
  int                   _gridResolution;
  bool                  _showTriangulation = false;
  bool                  _cullNone          = false;

  DirectX::XMFLOAT4 _diffuseColor{0.4f, 0.4f, 0.8f, 1.0f};
  DirectX::XMFLOAT4 _ambientColor{0.25f, 0.2f, 0.23f, 1.0f};
};
