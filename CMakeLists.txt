# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# 将 parts 目录注册为额外的组件目录
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/app_parts)

project(P4_Master)
 