#pragma once

#include <DirectXMath.h>
#include <SimpleMath.h>

#include <algorithm>
#include <cmath>
#include <tuple>

// Orbit camera for the DTK demo: Z-up world, longitude/latitude orbit around a
// target and a ray cast helper used to build the screen-space water grid.
class DtkCamera {
public:
  using Vector2  = DirectX::SimpleMath::Vector2;
  using Vector3  = DirectX::SimpleMath::Vector3;
  using Matrix   = DirectX::SimpleMath::Matrix;
  using Vector2i = DirectX::XMINT2;

  DtkCamera() {
    setViewport(1280, 720);
    setProjection(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
  }

  void setViewport(int width, int height) {
    _viewportWidth  = std::max(1, width);
    _viewportHeight = std::max(1, height);
    _aspect = static_cast<float>(_viewportWidth) / _viewportHeight;
  }

  void setProjection(float fovDegrees, float aspect, float nearZ, float farZ) {
    _fovDegrees = fovDegrees;
    _aspect     = aspect;
    _nearZ      = nearZ;
    _farZ       = farZ;
  }

  void centerToOrigin() { _target = Vector3::Zero; }

  void rotate(Vector2i screenPosNew, Vector2i screenPosOld,
              Vector2i screenSize) {
    const Vector2 delta{
        static_cast<float>(screenPosNew.x - screenPosOld.x) / screenSize.x,
        static_cast<float>(screenPosNew.y - screenPosOld.y) / screenSize.y};
    rotate(-3.0f * delta.x, 3.0f * delta.y);
  }

  void rotate(float deltaLongitude, float deltaLatitude) {
    _longitude += deltaLongitude;
    constexpr float limit = DirectX::XM_PIDIV2 - 0.001f;
    _latitude = std::clamp(_latitude + deltaLatitude, -limit, limit);
  }

  void zoom(Vector2i screenPosNew, Vector2i screenPosOld, Vector2i screenSize) {
    const float mult =
        1.0f + 2.0f * static_cast<float>(screenPosOld.y - screenPosNew.y) /
                   (0.5f * screenSize.y);
    zoom(mult);
  }

  void zoom(float mult) {
    _targetDistance = std::max(0.05f, _targetDistance * mult);
  }

  void pan(Vector2i screenPosNew, Vector2i screenPosOld, Vector2i screenSize) {
    auto pointFromCamera = [this, screenSize](Vector2i screenPos) {
      auto[dir, camPos] = cameraRayCast(screenPos, screenSize);
      return camPos + _targetDistance * dir;
    };

    pan(pointFromCamera(screenPosOld) - pointFromCamera(screenPosNew));
  }

  void pan(Vector3 targetShift) { _target += targetShift; }

  std::tuple<Vector3, Vector3> cameraRayCast(Vector2 ndc) const {
    const Matrix inverse = viewProjection().Invert();
    const Vector3 nearPoint =
        Vector3::Transform(Vector3(ndc.x, ndc.y, 0.0f), inverse);
    const Vector3 farPoint =
        Vector3::Transform(Vector3(ndc.x, ndc.y, 1.0f), inverse);

    Vector3 dir = farPoint - nearPoint;
    dir.Normalize();
    return {dir, eye()};
  }

  std::tuple<Vector3, Vector3> cameraRayCast(Vector2i pixel,
                                             Vector2i screenSize) const {
    const Vector2 ndc{
        2.0f * static_cast<float>(pixel.x) / screenSize.x - 1.0f,
        1.0f - 2.0f * static_cast<float>(pixel.y) / screenSize.y};
    return cameraRayCast(ndc);
  }

  Matrix view() const {
    return Matrix::CreateLookAt(eye(), _target, Vector3::UnitZ);
  }

  Matrix projection() const {
    return Matrix::CreatePerspectiveFieldOfView(
        DirectX::XMConvertToRadians(_fovDegrees), _aspect, _nearZ, _farZ);
  }

  Matrix viewProjection() const { return view() * projection(); }

  Vector3 eye() const {
    const float cosLat = std::cos(_latitude);
    const float sinLat = std::sin(_latitude);
    const float cosLon = std::cos(_longitude);
    const float sinLon = std::sin(_longitude);
    return _target + _targetDistance *
                         Vector3(cosLat * sinLon, -cosLat * cosLon, sinLat);
  }

  const Vector3 &target() const { return _target; }
  float distance() const { return _targetDistance; }

private:
  Vector3 _target         = Vector3::Zero;
  float   _longitude      = DirectX::XM_PIDIV4;
  float   _latitude       = DirectX::XM_PIDIV4;
  float   _targetDistance = 10.0f;

  int   _viewportWidth  = 1280;
  int   _viewportHeight = 720;
  float _fovDegrees     = 60.0f;
  float _aspect         = 16.0f / 9.0f;
  float _nearZ          = 0.1f;
  float _farZ           = 1000.0f;
};
