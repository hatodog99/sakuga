cmake_minimum_required(VERSION 3.10.0)
project(sakuga VERSION 0.1.0 LANGUAGES C CXX)
cmake_policy(SET CMP0072 NEW)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(assimp REQUIRED)

# Dear ImGui (git submodule: git submodule add https://github.com/ocornut/imgui.git includes/imgui)
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/includes/imgui")
if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR "Dear ImGui not found in ${IMGUI_DIR}. Run: git submodule update --init")
endif()

# ImGui has no CMake of its own, so build it as a small static library
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/backend/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backend/imgui_impl_opengl3.cpp"
)
target_include_directories(imgui PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)
target_link_libraries(
    imgui PUBLIC glfw ${CMAKE_DL_LIBS}
)

add_executable(sakuga
    demo/demo.cpp
    includes/glad/glad.c
    includes/sakuga/camera.cpp
    includes/sakuga/gui.cpp
    includes/sakuga/input.cpp
    includes/sakuga/mesh.cpp
    includes/sakuga/model.cpp
    includes/sakuga/renderer.cpp
    includes/sakuga/shader.cpp
    includes/sakuga/shader_config.cpp
    includes/stb/stb_image.cpp
)

target_include_directories(sakuga PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/includes"
)

# Stop GLFW from pulling in the system GL header (conflicts with glad)
target_compile_definitions(sakuga PRIVATE GLFW_INCLUDE_NONE)

target_link_libraries(sakuga PRIVATE
    glfw
    OpenGL::GL
    assimp::assimp
    imgui
    ${CMAKE_DL_LIBS}
)

# Copy resources (shaders, textures, etc.) next to the built executable
add_custom_command(TARGET sakuga POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:sakuga>/resources"
    COMMENT "Copying resources to output directory"
    VERBATIM
)

# ST-07 lifecycle test: reuses the demo's sources, includes and libraries, but has its own main
get_target_property(ST07_SOURCES sakuga SOURCES)
list(FILTER ST07_SOURCES EXCLUDE REGEX "demo/demo\\.cpp$")
add_executable(st07 tests/st07_lifecycle_test.cpp ${ST07_SOURCES})
target_include_directories(st07 PRIVATE $<TARGET_PROPERTY:sakuga,INCLUDE_DIRECTORIES>)
target_link_libraries(st07 PRIVATE $<TARGET_PROPERTY:sakuga,LINK_LIBRARIES>)
set_target_properties(st07 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

add_executable(unit_tests tests/unit_tests.cpp ${ST07_SOURCES})
target_include_directories(unit_tests PRIVATE $<TARGET_PROPERTY:sakuga,INCLUDE_DIRECTORIES>)
target_link_libraries(unit_tests PRIVATE $<TARGET_PROPERTY:sakuga,LINK_LIBRARIES>)
set_target_properties(unit_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)