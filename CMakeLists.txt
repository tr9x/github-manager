cmake_minimum_required(VERSION 3.21)
project(github-manager
    VERSION 0.1.0
    DESCRIPTION "Linux desktop GitHub repository manager"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
endif()

# --- Dependencies ----------------------------------------------------------

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)
qt_standard_project_setup()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)
pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)

# --- Sources ---------------------------------------------------------------
# Paths are anchored to CMAKE_CURRENT_SOURCE_DIR explicitly so qt_add_executable
# can never mis-resolve a relative path against the build tree.

set(GHM_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(GHM_SOURCES
    ${GHM_SRC_DIR}/main.cpp

    ${GHM_SRC_DIR}/core/Settings.h
    ${GHM_SRC_DIR}/core/Settings.cpp
    ${GHM_SRC_DIR}/core/SecureStorage.h
    ${GHM_SRC_DIR}/core/SecureStorage.cpp

    ${GHM_SRC_DIR}/github/Repository.h
    ${GHM_SRC_DIR}/github/GitHubClient.h
    ${GHM_SRC_DIR}/github/GitHubClient.cpp

    ${GHM_SRC_DIR}/git/GitHandler.h
    ${GHM_SRC_DIR}/git/GitHandler.cpp
    ${GHM_SRC_DIR}/git/GitWorker.h
    ${GHM_SRC_DIR}/git/GitWorker.cpp

    ${GHM_SRC_DIR}/ui/MainWindow.h
    ${GHM_SRC_DIR}/ui/MainWindow.cpp
    ${GHM_SRC_DIR}/ui/LoginDialog.h
    ${GHM_SRC_DIR}/ui/LoginDialog.cpp
    ${GHM_SRC_DIR}/ui/CloneDialog.h
    ${GHM_SRC_DIR}/ui/CloneDialog.cpp
    ${GHM_SRC_DIR}/ui/IdentityDialog.h
    ${GHM_SRC_DIR}/ui/IdentityDialog.cpp
    ${GHM_SRC_DIR}/ui/AddRemoteDialog.h
    ${GHM_SRC_DIR}/ui/AddRemoteDialog.cpp
    ${GHM_SRC_DIR}/ui/RepositoryListWidget.h
    ${GHM_SRC_DIR}/ui/RepositoryListWidget.cpp
    ${GHM_SRC_DIR}/ui/RepositoryDetailWidget.h
    ${GHM_SRC_DIR}/ui/RepositoryDetailWidget.cpp
    ${GHM_SRC_DIR}/ui/LocalRepositoryWidget.h
    ${GHM_SRC_DIR}/ui/LocalRepositoryWidget.cpp
)

# Sanity check: fail early with a clear message if the tree was downloaded
# flat (a frequent issue when files are downloaded one by one rather than as
# an archive) instead of letting Qt macros emit a confusing error.
foreach(_src IN LISTS GHM_SOURCES)
    if(NOT EXISTS "${_src}")
        message(FATAL_ERROR
            "Source file missing: ${_src}\n"
            "It looks like the project tree is incomplete. Make sure you "
            "extracted the full archive and that the 'src/' directory exists "
            "next to CMakeLists.txt with all subdirectories (core, git, "
            "github, ui).")
    endif()
endforeach()

qt_add_executable(github-manager ${GHM_SOURCES})

qt_add_resources(github-manager "app_resources"
    PREFIX "/"
    FILES
        resources/styles/dark.qss
)

target_include_directories(github-manager PRIVATE ${GHM_SRC_DIR})

target_link_libraries(github-manager PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    PkgConfig::LIBGIT2
    PkgConfig::LIBSECRET
)

target_compile_definitions(github-manager PRIVATE
    GHM_VERSION="${PROJECT_VERSION}"
    QT_NO_KEYWORDS  # avoid clash with libsecret's `signals` etc.
)

# --- Install ---------------------------------------------------------------

include(GNUInstallDirs)
install(TARGETS github-manager
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES resources/github-manager.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    OPTIONAL)
