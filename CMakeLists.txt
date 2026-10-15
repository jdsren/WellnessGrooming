cmake_minimum_required(VERSION 3.21)

project(WellnessGrooming VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # static runtime: no redistributable needed

include(FetchContent)

# CommonLibSSE-NG (SE + AE support in one DLL), pinned to the commit this port was checked against.
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    CommonLibSSE
    GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG.git
    GIT_TAG        b93280e832f263dbef44e44cbe2936622a02f91a
)
FetchContent_MakeAvailable(CommonLibSSE)

file(GLOB WG_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(WellnessGrooming SHARED ${WG_SOURCES})
target_include_directories(WellnessGrooming PRIVATE src)
target_compile_definitions(WellnessGrooming PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(WellnessGrooming PRIVATE CommonLibSSE::CommonLibSSE)
target_precompile_headers(WellnessGrooming PRIVATE src/PCH.h)

if(MSVC)
    target_compile_options(WellnessGrooming PRIVATE /MP /bigobj /permissive- /Zc:preprocessor /utf-8 /wd4100 /wd4102 /wd4189 /wd4244 /wd4267 /wd4305 /wd4996)
endif()

set_target_properties(WellnessGrooming PROPERTIES OUTPUT_NAME "WellnessGrooming")
