cmake_minimum_required (VERSION 3.1)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)

# disable GLFW docs, examples and tests
# see http://www.glfw.org/docs/latest/build_guide.html
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# set path to dependencies
add_subdirectory(submodules/glfw)
include_directories(submodules/glfw/include)
include_directories(submodules/imgui)
include_directories(submodules/stb)
include_directories(submodules/dj_opengl)
include_directories(submodules/dj_algebra)

# imgui implementation (compiled as a library)
aux_source_directory(submodules/imgui IMGUI_SRC_FILES)
set(IMGUI_INCLUDE_DIR imgui submodules/imgui)
add_library(imgui STATIC imgui/imgui_impl.cpp ${IMGUI_SRC_FILES})


# ------------------------------------------------------------------------------
project (opengl)

# compiler specific options
if(MSVC OR MSVC_IDE)
	add_definitions("/D_CRT_SECURE_NO_WARNINGS")
endif()


# ------------------------------------------------------------------------------
set(SRC_DIR demo-hello-window)
include_directories(${SRC_DIR})
add_executable(hello-window ${SRC_DIR}/hello-window.cpp ${SRC_DIR}/glad/glad.c)
target_link_libraries(hello-window glfw)

# ------------------------------------------------------------------------------
set(SRC_DIR demo-hello-imgui)
include_directories(${SRC_DIR} ${IMGUI_INCLUDE_DIR})
aux_source_directory(${SRC_DIR} SRC_FILES)
add_executable(hello-imgui ${SRC_FILES} ${SRC_DIR}/glad/glad.c)
target_link_libraries(hello-imgui glfw imgui)
unset(SRC_FILES)

# ------------------------------------------------------------------------------
set(SRC_DIR demo-fisheye)
include_directories(${SRC_DIR} ${IMGUI_INCLUDE_DIR})
aux_source_directory(${SRC_DIR} SRC_FILES)
add_executable(fisheye ${SRC_FILES} ${SRC_DIR}/glad/glad.c)
target_link_libraries(fisheye glfw imgui)
target_compile_definitions(fisheye PUBLIC -DPATH_TO_SRC_DIRECTORY="${CMAKE_SOURCE_DIR}/${SRC_DIR}/" -DPATH_TO_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/")
unset(SRC_FILES)

# ------------------------------------------------------------------------------
set(SRC_DIR demo-merl)
include_directories(${SRC_DIR} ${IMGUI_INCLUDE_DIR})
aux_source_directory(${SRC_DIR} SRC_FILES)
add_executable(merl ${SRC_FILES} ${SRC_DIR}/glad/glad.c)
target_link_libraries(merl glfw imgui)
target_compile_definitions(merl PUBLIC -DPATH_TO_SRC_DIRECTORY="${CMAKE_SOURCE_DIR}/${SRC_DIR}/" -DPATH_TO_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/")
unset(SRC_FILES)

# ------------------------------------------------------------------------------
set(SRC_DIR plot-brdf)
include_directories(${SRC_DIR} ${IMGUI_INCLUDE_DIR})
aux_source_directory(${SRC_DIR} SRC_FILES)
add_executable(brdf-plot ${SRC_FILES} ${SRC_DIR}/glad/glad.c)
target_link_libraries(brdf-plot glfw imgui)
target_compile_definitions(brdf-plot PUBLIC -DPATH_TO_SRC_DIRECTORY="${CMAKE_SOURCE_DIR}/${SRC_DIR}/" -DPATH_TO_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/")
unset(SRC_FILES)

# ------------------------------------------------------------------------------
set(SRC_DIR demo-isubd-terrain)
include_directories(${SRC_DIR} ${IMGUI_INCLUDE_DIR})
aux_source_directory(${SRC_DIR} SRC_FILES)
add_executable(isubd-terrain ${SRC_FILES} ${SRC_DIR}/glad/glad.c)
target_link_libraries(isubd-terrain glfw imgui)
target_compile_definitions(isubd-terrain PUBLIC -DPATH_TO_SRC_DIRECTORY="${CMAKE_SOURCE_DIR}/${SRC_DIR}/" -DPATH_TO_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/")
unset(SRC_FILES)

# ------------------------------------------------------------------------------
set(SRC_DIR demo-isubd-bs)
include_directories(${SRC_DIR} ${IMGUI_INCLUDE_DIR})
aux_source_directory(${SRC_DIR} SRC_FILES)
add_executable(isubd-bs ${SRC_FILES} ${SRC_DIR}/glad/glad.c)
target_link_libraries(isubd-bs glfw imgui)
target_compile_definitions(isubd-bs PUBLIC -DPATH_TO_SRC_DIRECTORY="${CMAKE_SOURCE_DIR}/${SRC_DIR}/" -DPATH_TO_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/")
unset(SRC_FILES)

# ------------------------------------------------------------------------------
set(SRC_DIR demo-isubd-cc)
include_directories(${SRC_DIR} ${IMGUI_INCLUDE_DIR})
aux_source_directory(${SRC_DIR} SRC_FILES)
add_executable(isubd-cc ${SRC_FILES} ${SRC_DIR}/glad/glad.c)
target_link_libraries(isubd-cc glfw imgui)
target_compile_definitions(isubd-cc PUBLIC -DPATH_TO_SRC_DIRECTORY="${CMAKE_SOURCE_DIR}/${SRC_DIR}/" -DPATH_TO_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/")
unset(SRC_FILES)

# ------------------------------------------------------------------------------
set(SRC_DIR demo-isubd-ccmesh)
include_directories(${SRC_DIR} ${IMGUI_INCLUDE_DIR})
aux_source_directory(${SRC_DIR} SRC_FILES)
add_executable(isubd-ccmesh ${SRC_FILES} ${SRC_DIR}/glad/glad.c)
target_link_libraries(isubd-ccmesh glfw imgui)
target_compile_definitions(isubd-ccmesh PUBLIC -DPATH_TO_SRC_DIRECTORY="${CMAKE_SOURCE_DIR}/${SRC_DIR}/" -DPATH_TO_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/")
unset(SRC_FILES)

# ------------------------------------------------------------------------------
set(SRC_DIR demo-isubd-loop)
include_directories(${SRC_DIR} ${IMGUI_INCLUDE_DIR})
aux_source_directory(${SRC_DIR} SRC_FILES)
add_executable(isubd-loop ${SRC_FILES} ${SRC_DIR}/glad/glad.c)
target_link_libraries(isubd-loop glfw imgui)
target_compile_definitions(isubd-loop PUBLIC -DPATH_TO_SRC_DIRECTORY="${CMAKE_SOURCE_DIR}/${SRC_DIR}/" -DPATH_TO_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/")
unset(SRC_FILES)

