# Third-party dependencies for WaterSurfaceWavelets.
#
# No vcpkg required: each dependency is looked up with find_package() first so
# that system installations still work, and downloaded with FetchContent
# otherwise.  For an offline build, point CMake at local source trees:
#
#   -DFETCHCONTENT_SOURCE_DIR_EIGEN3=<eigen-src>
#   -DFETCHCONTENT_SOURCE_DIR_DIRECTXTK=<directxtk-src>
#   -DFETCHCONTENT_SOURCE_DIR_IMGUI=<imgui-src>

include(FetchContent)

# Extraction timestamps of fetched archives (CMake 3.24+ policy).
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# --- Eigen (header-only linear algebra) -------------------------------------
find_package(Eigen3 3.3 QUIET NO_MODULE)
if(NOT Eigen3_FOUND)
  message(STATUS "Eigen3 not found - fetching Eigen 3.4.0")
  FetchContent_Declare(Eigen3
    URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
    URL_HASH SHA256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72
    # Eigen is header-only, so skip its CMakeLists.txt (which would configure
    # the Eigen test suite, BLAS and LAPACK) and expose the headers directly.
    SOURCE_SUBDIR cmake/wsw-header-only)
  FetchContent_MakeAvailable(Eigen3)

  add_library(wsw_eigen INTERFACE)
  target_include_directories(wsw_eigen SYSTEM INTERFACE "${eigen3_SOURCE_DIR}")
  add_library(Eigen3::Eigen ALIAS wsw_eigen)
endif()

# --- DirectXTK (Direct3D 11 helper library) ---------------------------------
find_package(DirectXTK CONFIG QUIET)
if(NOT TARGET Microsoft::DirectXTK)
  message(STATUS "DirectXTK not found - fetching DirectXTK oct2025")
  set(BUILD_TOOLS OFF)
  set(BUILD_TESTING OFF)
  FetchContent_Declare(DirectXTK
    URL https://github.com/microsoft/DirectXTK/archive/refs/tags/oct2025.tar.gz
    URL_HASH SHA256=317ceac23974bb7347cc75851d195e2c54636433bc48d52e4b5f3275c56cf4b1)
  FetchContent_MakeAvailable(DirectXTK)
  unset(BUILD_TOOLS)
  unset(BUILD_TESTING)

  add_library(Microsoft::DirectXTK ALIAS DirectXTK)
endif()

# --- Dear ImGui (core + Direct3D 11 / Win32 backends) -----------------------
find_package(imgui CONFIG QUIET)
if(NOT TARGET imgui::imgui)
  message(STATUS "Dear ImGui not found - fetching Dear ImGui v1.92.9b")
  FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99)
  FetchContent_MakeAvailable(imgui)

  add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp")
  target_include_directories(imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends")
  target_compile_definitions(imgui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)

  add_library(imgui::imgui ALIAS imgui)
endif()
