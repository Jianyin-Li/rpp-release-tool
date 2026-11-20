cmake_minimum_required(VERSION 3.16)
project(rpp-release-tool VERSION 0.0.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# --- FTXUI: prefer the system package (Debian trixie ships libftxui-dev) ----
find_package(ftxui CONFIG QUIET)
if(NOT ftxui_FOUND)
  message(STATUS "ftxui not found locally, fetching v6.1.9")
  FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(ftxui)
endif()

# --- toml++ -----------------------------------------------------------------
find_package(tomlplusplus CONFIG QUIET)
if(NOT tomlplusplus_FOUND)
  message(STATUS "tomlplusplus not found locally, fetching v3.4.0")
  FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(tomlplusplus)
endif()

add_library(rpp_core STATIC
  src/i18n.cpp
  src/config.cpp
  src/reprepro_conf.cpp
  src/scan.cpp
  src/stage.cpp
  src/version.cpp
  src/checks.cpp
  src/repo_state.cpp
  src/diff.cpp
  src/exec.cpp
  src/publish.cpp
)
target_include_directories(rpp_core PUBLIC src)
target_link_libraries(rpp_core PUBLIC tomlplusplus::tomlplusplus)

add_executable(rpp-release-tool
  src/main.cpp
  src/ui/app.cpp
)
target_link_libraries(rpp-release-tool PRIVATE
  rpp_core
  ftxui::component
  ftxui::dom
  ftxui::screen
)

enable_testing()
add_executable(rpp_tests tests/unit_tests.cpp)
target_link_libraries(rpp_tests PRIVATE rpp_core)
add_test(NAME unit COMMAND rpp_tests)
add_test(NAME integration COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.sh $<TARGET_FILE:rpp-release-tool>)

# --- install / packaging ----------------------------------------------------
include(GNUInstallDirs)
install(TARGETS rpp-release-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES examples/config.toml
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)

set(CPACK_PACKAGE_NAME "rpp-release-tool")
set(CPACK_PACKAGE_VENDOR "Jianyin Li")
set(CPACK_PACKAGE_CONTACT "Jianyin Li <lixiaotumail@163.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "reprepro release gate TUI")
set(CPACK_PACKAGE_DESCRIPTION "Scan and validate .deb/.dsc artifacts, preview the reprepro diff, publish through reprepro, verify the result, and maintain published packages.")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Jianyin-Li/rpp-release-tool")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "rpp-release-tool")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_STRIP_FILES ON)

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_NAME "rpp-release-tool")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Jianyin Li <lixiaotumail@163.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "reprepro, dpkg, gnupg")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
find_program(DPKG_SHLIBDEPS_EXECUTABLE dpkg-shlibdeps)
find_program(FILE_EXECUTABLE file)
if(DPKG_SHLIBDEPS_EXECUTABLE AND FILE_EXECUTABLE)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)
