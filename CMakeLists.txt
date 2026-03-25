cmake_minimum_required(VERSION 3.30)

project(AutoEqualizer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(AUTOEQUALIZER_BUILD_TESTS "Build AutoEqualizer tests" ON)

find_package(SndFile REQUIRED)

set(AUTOEQUALIZER_SOURCES
    audio/AudioBuffer.cpp
    audio/AudioFileIO.cpp
    analysis/LoudnessMeter.cpp
    analysis/AnalysisEngine.cpp
    dsp/Processors.cpp
    pipeline/DecisionEngine.cpp
    pipeline/MasteringStage.cpp
    pipeline/AutoEqualizerPipeline.cpp
    report/Report.cpp
    cli/CommandLine.cpp)

add_library(autoequalizer_lib ${AUTOEQUALIZER_SOURCES})
target_include_directories(autoequalizer_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(autoequalizer_lib PUBLIC SndFile::sndfile)

if(MSVC)
  target_compile_options(autoequalizer_lib PRIVATE /W4 /permissive-)
else()
  target_compile_options(autoequalizer_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(autoequalizer app/main.cpp)
target_link_libraries(autoequalizer PRIVATE autoequalizer_lib)

if(AUTOEQUALIZER_BUILD_TESTS)
  enable_testing()

  add_executable(
      autoequalizer_tests
      tests/TestMain.cpp
      tests/LoudnessMeterTests.cpp
      tests/AnalysisEngineTests.cpp
      tests/DecisionEngineTests.cpp
      tests/DspTests.cpp
      tests/MasteringStageTests.cpp
      tests/CliTests.cpp
      tests/PipelineSmokeTests.cpp)
  target_link_libraries(autoequalizer_tests PRIVATE autoequalizer_lib)

  add_test(NAME autoequalizer_tests COMMAND autoequalizer_tests)
endif()
