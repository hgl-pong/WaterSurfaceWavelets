# DirectXTK (Direct3D 11) + Dear ImGui renderer for the water surface demo.

add_executable(Demo WIN32
  DemoDtk.cpp
  DtkApplication.cpp
  DtkTerrain.cpp
  DtkWaterSurface.cpp)

target_compile_definitions(Demo PRIVATE
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN)

target_include_directories(Demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Demo PRIVATE
  Microsoft::DirectXTK
  imgui::imgui
  d3d11
  dxgi
  d3dcompiler
  dwmapi
  windowscodecs
  WaveGrid)

set_target_properties(Demo PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
