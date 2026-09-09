add_library(WaveGrid WaveGrid.cpp Enviroment.cpp Grid.cpp ProfileBuffer.cpp Spectrum.cpp ${Environment_RCS})
target_link_libraries(WaveGrid PUBLIC Eigen3::Eigen)
if(MSVC)
  target_compile_options(WaveGrid PRIVATE /bigobj)
endif()

add_subdirectory(tests)
add_subdirectory(visualization)

