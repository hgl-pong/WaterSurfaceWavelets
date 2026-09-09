#pragma once

#include "DtkCamera.h"

#include <CommonStates.h>
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <vector>

// Static Phong-shaded mesh used for the displaced harbour/terrain plane.
class DtkTerrain {
public:
  struct Vertex {
    DirectX::XMFLOAT4 position;
    DirectX::XMFLOAT4 normal;
  };

  DtkTerrain(ID3D11Device *device, ID3D11DeviceContext *context, int nx,
             int ny);
  ~DtkTerrain() = default;

  DtkTerrain(const DtkTerrain &) = delete;
  DtkTerrain &operator=(const DtkTerrain &) = delete;

  template <class Fun> void setVertices(Fun fun) {
    const int count = static_cast<int>(_vertices.size());
    for (int i = 0; i < count; ++i)
      fun(i, _vertices[static_cast<size_t>(i)]);
    recomputeNormals();
    uploadVertices();
  }

  void draw(const DtkCamera &camera,
            const DirectX::SimpleMath::Vector3 &lightPosition);

  void setDiffuseColor(const DirectX::XMFLOAT4 &color) { _diffuseColor = color; }
  void setAmbientColor(const DirectX::XMFLOAT4 &color) { _ambientColor = color; }
  void setCullNone(bool cullNone) { _cullNone = cullNone; }

private:
  struct Constants {
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4X4 viewProjection;
    DirectX::XMFLOAT4   lightPosition;
    DirectX::XMFLOAT4   ambientColor;
    DirectX::XMFLOAT4   diffuseColor;
    DirectX::XMFLOAT4   cameraPosition;
  };

  void createShaders();
  void createBuffers(int nx, int ny);
  void uploadVertices();
  void recomputeNormals();

  Microsoft::WRL::ComPtr<ID3D11Device>        _device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> _context;

  Microsoft::WRL::ComPtr<ID3D11Buffer>       _vertexBuffer;
  Microsoft::WRL::ComPtr<ID3D11Buffer>       _indexBuffer;
  Microsoft::WRL::ComPtr<ID3D11Buffer>       _constantBuffer;
  Microsoft::WRL::ComPtr<ID3D11InputLayout>  _inputLayout;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> _vertexShader;
  Microsoft::WRL::ComPtr<ID3D11PixelShader>  _pixelShader;

  std::unique_ptr<DirectX::CommonStates> _states;

  std::vector<Vertex>   _vertices;
  std::vector<uint32_t> _indices;

  DirectX::XMFLOAT4 _diffuseColor{0.4f, 0.4f, 0.4f, 1.0f};
  DirectX::XMFLOAT4 _ambientColor{0.25f, 0.2f, 0.23f, 1.0f};
  bool              _cullNone = false;
};
