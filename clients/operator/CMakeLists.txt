cmake_minimum_required(VERSION 3.16)
project(iot_operator_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencias externas via FetchContent ---
include(FetchContent)

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.4-docking
)
FetchContent_MakeAvailable(imgui)

# GLFW (para la ventana y contexto OpenGL)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.10
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# --- Biblioteca de Dear ImGui (compilada manualmente) ---
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_lib PUBLIC glfw)

# --- OpenGL ---
find_package(OpenGL REQUIRED)

# --- Ejecutable del operador ---
add_executable(iot_operator
    src/main.cpp
    src/gui.cpp
    src/network_thread.cpp
)

target_include_directories(iot_operator PRIVATE src)

target_link_libraries(iot_operator PRIVATE
    imgui_lib
    OpenGL::GL
    glfw
)

# Dependencias de plataforma
if(WIN32)
    target_link_libraries(iot_operator PRIVATE ws2_32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(iot_operator PRIVATE pthread)
endif()
