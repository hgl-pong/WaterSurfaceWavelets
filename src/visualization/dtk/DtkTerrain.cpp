#include "DtkTerrain.h"

#include <d3dcompiler.h>

#include <cstddef>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

const char kTerrainShaderSource[] = R"HLSL(
cbuffer TerrainConstants : register(b0)
{
    float4x4 world;
    float4x4 viewProjection;
    float4   lightPosition;
    float4   ambientColor;
    float4   diffuseColor;
    float4   cameraPosition;
};

struct VSInput
{
    float4 position : POSITION;
    float4 normal   : NORMAL;
};

struct VSOutput
{
    float4 position  : SV_Position;
    float3 worldPos  : TEXCOORD0;
    float3 normal    : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4 worldPosition = mul(world, input.position);
    output.position      = mul(viewProjection, worldPosition);
    output.worldPos      = worldPosition.xyz / worldPosition.w;
    output.normal        = mul(world, input.normal).xyz;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    float3 n = normalize(input.normal);
    float3 l = normalize(lightPosition.xyz - input.worldPos);
    float3 v = normalize(cameraPosition.xyz - input.worldPos);

    float intensity = max(0.0, dot(n, l));

    float3 fragmentColor = ambientColor.rgb;
    fragmentColor += diffuseColor.rgb * intensity;

    float3 reflection = reflect(-l, n);
    float  specularity =
        pow(max(0.0, dot(v, reflection)), 80.0);
    fragmentColor += specularity;

    return float4(fragmentColor, 1.0);
}
)HLSL";

Microsoft::WRL::ComPtr<ID3DBlob> compileShader(const char *entryPoint,
                                               const char *target) {
  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
  flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

  Microsoft::WRL::ComPtr<ID3DBlob> blob;
  Microsoft::WRL::ComPtr<ID3DBlob> errors;
  const HRESULT hr =
      D3DCompile(kTerrainShaderSource, sizeof(kTerrainShaderSource) - 1,
                 "DtkTerrain.hlsl", nullptr, nullptr, entryPoint, target, flags,
                 0, &blob, &errors);

  if (FAILED(hr)) {
    std::string message = "D3DCompile failed for ";
    message += entryPoint;
    message += ":\n";
    if (errors)
      message.append(static_cast<const char *>(errors->GetBufferPointer()),
                     errors->GetBufferSize());
    throw std::runtime_error(message);
  }
  return blob;
}

} // namespace

DtkTerrain::DtkTerrain(ID3D11Device *device, ID3D11DeviceContext *context,
                       int nx, int ny)
    : _device(device), _context(context) {
  _states = std::make_unique<DirectX::CommonStates>(device);

  createShaders();
  createBuffers(nx, ny);
}

void DtkTerrain::createShaders() {
  auto vertexBlob = compileShader("VSMain", "vs_5_0");
  auto pixelBlob  = compileShader("PSMain", "ps_5_0");

  if (FAILED(_device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                         vertexBlob->GetBufferSize(), nullptr,
                                         &_vertexShader)))
    throw std::runtime_error("Failed to create terrain vertex shader");

  if (FAILED(_device->CreatePixelShader(pixelBlob->GetBufferPointer(),
                                        pixelBlob->GetBufferSize(), nullptr,
                                        &_pixelShader)))
    throw std::runtime_error("Failed to create terrain pixel shader");

  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
       offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, normal),
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  if (FAILED(_device->CreateInputLayout(
          layout, static_cast<UINT>(std::size(layout)),
          vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(),
          &_inputLayout)))
    throw std::runtime_error("Failed to create terrain input layout");
}

void DtkTerrain::createBuffers(int nx, int ny) {
  const float dx = 2.0f / nx;
  const float dy = 2.0f / ny;

  _vertices.clear();
  _vertices.reserve(static_cast<size_t>(nx + 1) * (ny + 1));
  for (int i = 0; i <= nx; ++i) {
    for (int j = 0; j <= ny; ++j) {
      Vertex vertex = {};
      vertex.position =
          DirectX::XMFLOAT4(-1.0f + i * dx, -1.0f + j * dy, 0.0f, 1.0f);
      vertex.normal   = DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 0.0f);
      _vertices.push_back(vertex);
    }
  }

  _indices.clear();
  _indices.reserve(static_cast<size_t>(nx) * ny * 6);
  for (int i = 0; i < nx; ++i) {
    for (int j = 0; j < ny; ++j) {
      const uint32_t index = static_cast<uint32_t>(j + i * (ny + 1));
      const uint32_t J     = 1;
      const uint32_t I     = static_cast<uint32_t>(ny + 1);

      _indices.push_back(index);
      _indices.push_back(index + I);
      _indices.push_back(index + J);

      _indices.push_back(index + I);
      _indices.push_back(index + I + J);
      _indices.push_back(index + J);
    }
  }

  D3D11_BUFFER_DESC vertexDesc = {};
  vertexDesc.ByteWidth      = static_cast<UINT>(_vertices.size() *
                                                sizeof(Vertex));
  vertexDesc.Usage          = D3D11_USAGE_DYNAMIC;
  vertexDesc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
  vertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(_device->CreateBuffer(&vertexDesc, nullptr, &_vertexBuffer)))
    throw std::runtime_error("Failed to create terrain vertex buffer");

  D3D11_BUFFER_DESC indexDesc = {};
  indexDesc.ByteWidth = static_cast<UINT>(_indices.size() * sizeof(uint32_t));
  indexDesc.Usage     = D3D11_USAGE_IMMUTABLE;
  indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  D3D11_SUBRESOURCE_DATA indexData = {};
  indexData.pSysMem = _indices.data();
  if (FAILED(_device->CreateBuffer(&indexDesc, &indexData, &_indexBuffer)))
    throw std::runtime_error("Failed to create terrain index buffer");

  D3D11_BUFFER_DESC constantDesc = {};
  constantDesc.ByteWidth      = sizeof(Constants);
  constantDesc.Usage          = D3D11_USAGE_DYNAMIC;
  constantDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
  constantDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(_device->CreateBuffer(&constantDesc, nullptr, &_constantBuffer)))
    throw std::runtime_error("Failed to create terrain constant buffer");

  uploadVertices();
}

void DtkTerrain::uploadVertices() {
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(_context->Map(_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                           &mapped)))
    return;

  std::memcpy(mapped.pData, _vertices.data(),
              _vertices.size() * sizeof(Vertex));
  _context->Unmap(_vertexBuffer.Get(), 0);
}

void DtkTerrain::recomputeNormals() {
  for (auto &vertex : _vertices)
    vertex.normal = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

  for (size_t i = 0; i + 2 < _indices.size(); i += 3) {
    const uint32_t i0 = _indices[i + 0];
    const uint32_t i1 = _indices[i + 1];
    const uint32_t i2 = _indices[i + 2];

    const DirectX::SimpleMath::Vector3 v0(
        _vertices[i0].position.x, _vertices[i0].position.y,
        _vertices[i0].position.z);
    const DirectX::SimpleMath::Vector3 v1(
        _vertices[i1].position.x, _vertices[i1].position.y,
        _vertices[i1].position.z);
    const DirectX::SimpleMath::Vector3 v2(
        _vertices[i2].position.x, _vertices[i2].position.y,
        _vertices[i2].position.z);

    const DirectX::SimpleMath::Vector3 normal = (v1 - v0).Cross(v2 - v0);

    auto addNormal = [&normal](Vertex &vertex) {
      vertex.normal.x += normal.x;
      vertex.normal.y += normal.y;
      vertex.normal.z += normal.z;
    };
    addNormal(_vertices[i0]);
    addNormal(_vertices[i1]);
    addNormal(_vertices[i2]);
  }

  for (auto &vertex : _vertices) {
    DirectX::SimpleMath::Vector3 normal(vertex.normal.x, vertex.normal.y,
                                        vertex.normal.z);
    normal.Normalize();
    vertex.normal = {normal.x, normal.y, normal.z, 0.0f};
  }
}

void DtkTerrain::draw(const DtkCamera &camera,
                      const DirectX::SimpleMath::Vector3 &lightPosition) {
  const DirectX::SimpleMath::Vector3 camPos = camera.eye();
  const DirectX::SimpleMath::Matrix world =
      DirectX::SimpleMath::Matrix::Identity;
  const DirectX::SimpleMath::Matrix viewProjection = camera.viewProjection();

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(_context->Map(_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                           0, &mapped)))
    return;

  auto *constants = static_cast<Constants *>(mapped.pData);
  XMStoreFloat4x4(&constants->world, world);
  XMStoreFloat4x4(&constants->viewProjection, viewProjection);
  constants->lightPosition  = {lightPosition.x, lightPosition.y,
                               lightPosition.z, 1.0f};
  constants->ambientColor   = _ambientColor;
  constants->diffuseColor   = _diffuseColor;
  constants->cameraPosition = {camPos.x, camPos.y, camPos.z, 1.0f};
  _context->Unmap(_constantBuffer.Get(), 0);

  const UINT stride = sizeof(Vertex);
  const UINT offset = 0;

  _context->IASetInputLayout(_inputLayout.Get());
  _context->IASetVertexBuffers(0, 1, _vertexBuffer.GetAddressOf(), &stride,
                               &offset);
  _context->IASetIndexBuffer(_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
  _context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  _context->VSSetShader(_vertexShader.Get(), nullptr, 0);
  _context->VSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
  _context->PSSetShader(_pixelShader.Get(), nullptr, 0);
  _context->PSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());

  _context->RSSetState(_cullNone ? _states->CullNone()
                                 : _states->CullClockwise());
  _context->OMSetDepthStencilState(_states->DepthDefault(), 0);
  _context->OMSetBlendState(_states->Opaque(), nullptr, 0xffffffff);

  _context->DrawIndexed(static_cast<UINT>(_indices.size()), 0, 0);
}
