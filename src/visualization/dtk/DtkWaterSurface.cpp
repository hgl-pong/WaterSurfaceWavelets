#include "DtkWaterSurface.h"

#include <d3dcompiler.h>

#include <cstddef>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

const char kWaterShaderSource[] = R"HLSL(
#define NUM (DIR_NUM / 4)
#define NUM_INTEGRATION_NODES (8 * DIR_NUM)

cbuffer WaterConstants : register(b0)
{
    float4x4 world;
    float4x4 viewProjection;
    float4x4 view;
    float4   lightPosition;
    float4   ambientColor;
    float4   diffuseColor;
    float4   cameraPosition;
    float4   params; // x = profile period, y = time
};

Texture1D<float4> profileTexture : register(t0);
SamplerState      profileSampler : register(s0);

struct VSInput
{
    float4 position : POSITION;
    float4 amp0     : TEXCOORD0;
    float4 amp1     : TEXCOORD1;
    float4 amp2     : TEXCOORD2;
    float4 amp3     : TEXCOORD3;
};

struct VSOutput
{
    float4 position  : SV_Position;
    float3 worldPos  : TEXCOORD0;
    float3 basePos   : TEXCOORD1;
    float3 lightDir  : TEXCOORD2;
    float3 cameraDir : TEXCOORD3;
    float4 amp0      : TEXCOORD4;
    float4 amp1      : TEXCOORD5;
    float4 amp2      : TEXCOORD6;
    float4 amp3      : TEXCOORD7;
};

float4x4 BuildAmplitudes(float4 a0, float4 a1, float4 a2, float4 a3)
{
    return float4x4(a0, a1, a2, a3);
}

float Ampl(float4x4 amplitude, int i)
{
    i = ((i % DIR_NUM) + DIR_NUM) % DIR_NUM;
    float4 rows[4] = { amplitude[0], amplitude[1],
                       amplitude[2], amplitude[3] };
    float4 v = rows[i / 4];
    int c = i % 4;
    float r = v.x;
    if (c == 1)
        r = v.y;
    else if (c == 2)
        r = v.z;
    else if (c == 3)
        r = v.w;
    return r;
}

static const float TAU  = 6.28318530718;
static const float SEED = 40234324.0;

float iAmpl(float4x4 amplitude, float angle)
{
    float a  = DIR_NUM * angle / TAU + DIR_NUM - 0.5;
    int   ia = (int)floor(a);
    float w  = a - ia;
    return (1.0 - w) * Ampl(amplitude, ia) + w * Ampl(amplitude, ia + 1);
}

float3 wavePosition(float4x4 amplitude, float3 p)
{
    float3 result = float3(0.0, 0.0, 0.0);

    const int N  = NUM_INTEGRATION_NODES;
    float     da = 1.0 / N;
    float     dx = DIR_NUM * TAU / N;

    [unroll] for (int i = 0; i < N; ++i)
    {
        float  a     = i * da;
        float  angle = a * TAU;
        float2 kdir  = float2(cos(angle), sin(angle));
        float  kdirX = dot(p.xy, kdir) + TAU * sin(SEED * a);
        float  w     = kdirX / params.x;

        float4 tt = dx * iAmpl(amplitude, angle) *
                    profileTexture.SampleLevel(profileSampler, w, 0);

        result.xy += kdir * tt.x;
        result.z  += tt.y;
    }

    return result;
}

float3 waveNormal(float4x4 amplitude, float3 p)
{
    float3 tx = float3(1.0, 0.0, 0.0);
    float3 ty = float3(0.0, 1.0, 0.0);

    const int N  = NUM_INTEGRATION_NODES;
    float     da = 1.0 / N;
    float     dx = DIR_NUM * TAU / N;

    [unroll] for (int i = 0; i < N; ++i)
    {
        float  a     = i * da;
        float  angle = a * TAU;
        float2 kdir  = float2(cos(angle), sin(angle));
        float  kdirX = dot(p.xy, kdir) + TAU * sin(SEED * a);
        float  w     = kdirX / params.x;

        float4 tt = dx * iAmpl(amplitude, angle) *
                    profileTexture.SampleLevel(profileSampler, w, 0);

        tx.xz += kdir.x * tt.zw;
        ty.yz += kdir.y * tt.zw;
    }

    return normalize(cross(tx, ty));
}

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4x4 amplitude = BuildAmplitudes(input.amp0, input.amp1,
                                         input.amp2, input.amp3);

    float3 p = input.position.xyz;
    p += wavePosition(amplitude, p);

    float4 worldPosition = mul(world, float4(p, 1.0));
    float3 worldPos      = worldPosition.xyz / worldPosition.w;

    output.position  = mul(viewProjection, worldPosition);
    output.worldPos  = worldPos;
    output.basePos   = input.position.xyz;
    output.lightDir  = lightPosition.xyz - worldPos;
    output.cameraDir = cameraPosition.xyz - worldPos;
    output.amp0      = input.amp0;
    output.amp1      = input.amp1;
    output.amp2      = input.amp2;
    output.amp3      = input.amp3;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    float4x4 amplitude = BuildAmplitudes(input.amp0, input.amp1,
                                         input.amp2, input.amp3);

    float3 normal = waveNormal(amplitude, input.basePos);

    float4 finalAmbientColor  = ambientColor;
    float4 finalDiffuseColor  = diffuseColor;
    float4 finalSpecularColor = float4(1.0, 1.0, 1.0, 1.0);
    float4 lightColor         = float4(1.0, 1.0, 1.0, 1.0);

    if (input.basePos.x < -50.0 || input.basePos.x > 50.0 ||
        input.basePos.y < -50.0 || input.basePos.y > 50.0)
        finalDiffuseColor.rgb = float3(0.6, 0.6, 0.6);

    float4 fragmentColor = finalAmbientColor;

    float3 n = normalize(normal);
    float3 l = normalize(input.lightDir);

    float intensity = max(0.0, dot(n, l));
    fragmentColor += finalDiffuseColor * lightColor * intensity;

    if (intensity > 0.001)
    {
        float3 cameraDir = normalize(input.cameraDir);
        float3 ref       = reflect(cameraDir, n);

        // The original GLSL evaluates the sparkle term in camera space.
        float3 normalCamera = mul(view, float4(n, 0.0)).xyz;
        float3 cameraDirCamera =
            normalize(-mul(view, float4(input.worldPos, 1.0)).xyz);
        float3 refCamera = reflect(cameraDirCamera, normalize(normalCamera));

        float sky = max(0.0,
                        (1.0 - abs(dot(cameraDirCamera,
                                       normalize(normalCamera)))) *
                            sin(20.0 * refCamera.x) *
                            sin(20.0 * refCamera.y) *
                            sin(20.0 * refCamera.z));

        float3 reflection = reflect(-l, n);
        float  shininess  = 80.0;
        float  specularity =
            pow(max(0.0, dot(cameraDir, reflection)), shininess);

        fragmentColor += finalSpecularColor * specularity +
                         float4(sky, sky, sky, sky);
    }

    return fragmentColor;
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

#define DTK_STRINGIFY_IMPL(x) #x
#define DTK_STRINGIFY(x) DTK_STRINGIFY_IMPL(x)
  const D3D_SHADER_MACRO macros[] = {{"DIR_NUM", DTK_STRINGIFY(DIR_NUM)},
                                     {nullptr, nullptr}};
#undef DTK_STRINGIFY
#undef DTK_STRINGIFY_IMPL

  const HRESULT hr =
      D3DCompile(kWaterShaderSource, sizeof(kWaterShaderSource) - 1,
                 "DtkWaterSurface.hlsl", macros, nullptr, entryPoint, target,
                 flags, 0, &blob, &errors);

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

DtkWaterSurface::DtkWaterSurface(ID3D11Device *device,
                                 ID3D11DeviceContext *context,
                                 int gridResolution)
    : _device(device), _context(context), _gridResolution(gridResolution) {
  _states = std::make_unique<DirectX::CommonStates>(device);

  createShaders();
  createBuffers();
}

void DtkWaterSurface::createShaders() {
  auto vertexBlob   = compileShader("VSMain", "vs_5_0");
  auto pixelBlob    = compileShader("PSMain", "ps_5_0");

  if (FAILED(_device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                         vertexBlob->GetBufferSize(), nullptr,
                                         &_vertexShader)))
    throw std::runtime_error("Failed to create water vertex shader");

  if (FAILED(_device->CreatePixelShader(pixelBlob->GetBufferPointer(),
                                        pixelBlob->GetBufferSize(), nullptr,
                                        &_pixelShader)))
    throw std::runtime_error("Failed to create water pixel shader");

  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
       offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
       offsetof(Vertex, amplitude) + 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
       offsetof(Vertex, amplitude) + 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
       offsetof(Vertex, amplitude) + 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
       offsetof(Vertex, amplitude) + 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  if (FAILED(_device->CreateInputLayout(
          layout, static_cast<UINT>(std::size(layout)),
          vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(),
          &_inputLayout)))
    throw std::runtime_error("Failed to create water input layout");
}

void DtkWaterSurface::createBuffers() {
  const int nx = _gridResolution;
  const int ny = _gridResolution;
  const float dx = 2.0f / nx;
  const float dy = 2.0f / ny;

  _vertices.clear();
  _vertices.reserve(static_cast<size_t>(nx + 1) * (ny + 1));
  for (int i = 0; i <= nx; ++i) {
    for (int j = 0; j <= ny; ++j) {
      Vertex vertex = {};
      vertex.position =
          DirectX::XMFLOAT4(-1.0f + i * dx, -1.0f + j * dy, 0.0f, 1.0f);
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
    throw std::runtime_error("Failed to create water vertex buffer");

  D3D11_BUFFER_DESC indexDesc = {};
  indexDesc.ByteWidth = static_cast<UINT>(_indices.size() * sizeof(uint32_t));
  indexDesc.Usage     = D3D11_USAGE_IMMUTABLE;
  indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  D3D11_SUBRESOURCE_DATA indexData = {};
  indexData.pSysMem = _indices.data();
  if (FAILED(_device->CreateBuffer(&indexDesc, &indexData, &_indexBuffer)))
    throw std::runtime_error("Failed to create water index buffer");

  D3D11_BUFFER_DESC constantDesc = {};
  constantDesc.ByteWidth      = sizeof(Constants);
  constantDesc.Usage          = D3D11_USAGE_DYNAMIC;
  constantDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
  constantDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(_device->CreateBuffer(&constantDesc, nullptr, &_constantBuffer)))
    throw std::runtime_error("Failed to create water constant buffer");

  uploadVertices();
}

void DtkWaterSurface::uploadVertices() {
  if (!_vertexBuffer)
    return;

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(_context->Map(_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                           &mapped)))
    return;

  std::memcpy(mapped.pData, _vertices.data(),
              _vertices.size() * sizeof(Vertex));
  _context->Unmap(_vertexBuffer.Get(), 0);
}

void DtkWaterSurface::createProfileTexture(size_t count) {
  _profileTexture.Reset();
  _profileView.Reset();

  D3D11_TEXTURE1D_DESC desc = {};
  desc.Width          = static_cast<UINT>(count);
  desc.MipLevels      = 1;
  desc.ArraySize      = 1;
  desc.Format         = DXGI_FORMAT_R32G32B32A32_FLOAT;
  desc.Usage          = D3D11_USAGE_DYNAMIC;
  desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  desc.MiscFlags      = 0;

  if (FAILED(_device->CreateTexture1D(&desc, nullptr, &_profileTexture)))
    throw std::runtime_error("Failed to create profile texture");
  if (FAILED(_device->CreateShaderResourceView(_profileTexture.Get(), nullptr,
                                               &_profileView)))
    throw std::runtime_error("Failed to create profile texture view");
}

void DtkWaterSurface::uploadProfile() {
  if (!_profileTexture || _profileData.empty())
    return;

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(_context->Map(_profileTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                           0, &mapped)))
    return;

  std::memcpy(mapped.pData, _profileData.data(),
              _profileData.size() * sizeof(float));
  _context->Unmap(_profileTexture.Get(), 0);
}

void DtkWaterSurface::loadProfile(const float *data, size_t count,
                                  float period) {
  _profilePeriod = period;

  const size_t floatCount = count * 4;
  if (!_profileTexture || _profileData.size() != floatCount)
    createProfileTexture(count);

  _profileData.assign(data, data + floatCount);
  uploadProfile();
}

void DtkWaterSurface::draw(const DtkCamera &camera,
                           const DirectX::SimpleMath::Vector3 &lightPosition) {
  if (!_vertexBuffer || !_profileView)
    return;

  const DirectX::SimpleMath::Vector3 camPos = camera.eye();
  const DirectX::SimpleMath::Matrix world =
      DirectX::SimpleMath::Matrix::Identity;
  const DirectX::SimpleMath::Matrix viewProjection = camera.viewProjection();

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(_context->Map(_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                           0, &mapped)))
    return;

  auto *constants = static_cast<Constants *>(mapped.pData);
  // DirectXMath stores row-major matrices for row-vector math; HLSL's default
  // column-major constant buffer packing combined with mul(matrix, vector)
  // expects them exactly as they are.
  XMStoreFloat4x4(&constants->world, world);
  XMStoreFloat4x4(&constants->viewProjection, viewProjection);
  XMStoreFloat4x4(&constants->view, camera.view());
  constants->lightPosition = {lightPosition.x, lightPosition.y,
                              lightPosition.z, 1.0f};
  constants->ambientColor  = _ambientColor;
  constants->diffuseColor  = _diffuseColor;
  constants->cameraPosition = {camPos.x, camPos.y, camPos.z, 1.0f};
  constants->params = {_profilePeriod, 0.0f, 0.0f, 0.0f};
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

  ID3D11SamplerState *sampler = _states->LinearWrap();
  _context->VSSetShaderResources(0, 1, _profileView.GetAddressOf());
  _context->VSSetSamplers(0, 1, &sampler);
  _context->PSSetShaderResources(0, 1, _profileView.GetAddressOf());
  _context->PSSetSamplers(0, 1, &sampler);

  _context->RSSetState(_showTriangulation
                           ? _states->Wireframe()
                           : (_cullNone ? _states->CullNone()
                                        : _states->CullClockwise()));
  _context->OMSetDepthStencilState(_states->DepthDefault(), 0);
  _context->OMSetBlendState(_states->Opaque(), nullptr, 0xffffffff);

  _context->DrawIndexed(static_cast<UINT>(_indices.size()), 0, 0);
}
