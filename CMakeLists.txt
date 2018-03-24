cmake_minimum_required (VERSION 2.6)

# disable GLFW docs, examples and tests
# see http://www.glfw.org/docs/latest/build_guide.html
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(submodules/glfw)
include_directories(submodules/glfw/include)
include_directories(submodules/imgui)
include_directories(submodules/stb)
include_directories(submodules/dj_opengl)
include_directories(submodules/dj_algebra)

project (hello-window)
include_directories(demo-hello-window)
add_executable(hello-window demo-hello-window/hello-window.cpp demo-hello-window/glad/glad.c)
target_link_libraries(hello-window glfw)

