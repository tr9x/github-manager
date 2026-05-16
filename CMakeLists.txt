cmake_minimum_required(VERSION 3.21)
project(github-manager
    VERSION 1.0.1
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

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent LinguistTools)
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
    ${GHM_SRC_DIR}/core/Translator.h
    ${GHM_SRC_DIR}/core/Translator.cpp

    ${GHM_SRC_DIR}/github/Repository.h
    ${GHM_SRC_DIR}/github/GitHubClient.h
    ${GHM_SRC_DIR}/github/SshUrlConverter.h
    ${GHM_SRC_DIR}/github/OAuthResponseParser.h
    ${GHM_SRC_DIR}/github/GitHubClient.cpp

    ${GHM_SRC_DIR}/git/GitHandler.h
    ${GHM_SRC_DIR}/git/SshKeyInfo.h
    ${GHM_SRC_DIR}/git/GitHandler.cpp
    ${GHM_SRC_DIR}/git/GitWorker.h
    ${GHM_SRC_DIR}/git/GitWorker.cpp
    ${GHM_SRC_DIR}/git/CommitSigner.h
    ${GHM_SRC_DIR}/git/CommitSigner.cpp
    ${GHM_SRC_DIR}/git/SignatureVerifier.h
    ${GHM_SRC_DIR}/git/SignatureVerifier.cpp

    ${GHM_SRC_DIR}/session/SessionController.h
    ${GHM_SRC_DIR}/session/SessionController.cpp
    ${GHM_SRC_DIR}/session/OAuthFlowController.h
    ${GHM_SRC_DIR}/session/OAuthFlowController.cpp

    ${GHM_SRC_DIR}/workspace/LocalWorkspaceController.h
    ${GHM_SRC_DIR}/workspace/LocalWorkspaceController.cpp
    ${GHM_SRC_DIR}/workspace/PublishController.h
    ${GHM_SRC_DIR}/workspace/PublishController.cpp
    ${GHM_SRC_DIR}/workspace/ConflictController.h
    ${GHM_SRC_DIR}/workspace/ConflictController.cpp
    ${GHM_SRC_DIR}/workspace/GitHubCloneController.h
    ${GHM_SRC_DIR}/workspace/GitHubCloneController.cpp

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
    ${GHM_SRC_DIR}/ui/PublishToGitHubDialog.h
    ${GHM_SRC_DIR}/ui/PublishToGitHubDialog.cpp
    ${GHM_SRC_DIR}/ui/CreateBranchDialog.h
    ${GHM_SRC_DIR}/ui/CreateBranchDialog.cpp
    ${GHM_SRC_DIR}/ui/StashSaveDialog.h
    ${GHM_SRC_DIR}/ui/StashSaveDialog.cpp
    ${GHM_SRC_DIR}/ui/StashListDialog.h
    ${GHM_SRC_DIR}/ui/StashListDialog.cpp
    ${GHM_SRC_DIR}/ui/TagsDialog.h
    ${GHM_SRC_DIR}/ui/TagsDialog.cpp
    ${GHM_SRC_DIR}/ui/ReflogDialog.h
    ${GHM_SRC_DIR}/ui/ReflogDialog.cpp
    ${GHM_SRC_DIR}/ui/ConflictResolutionDialog.h
    ${GHM_SRC_DIR}/ui/ConflictResolutionDialog.cpp
    ${GHM_SRC_DIR}/ui/SshKeyDialog.h
    ${GHM_SRC_DIR}/ui/SshKeyDialog.cpp
    ${GHM_SRC_DIR}/ui/HostKeyApprovalDialog.h
    ${GHM_SRC_DIR}/ui/HostKeyApprovalDialog.cpp
    ${GHM_SRC_DIR}/ui/HostKeyApprover.h
    ${GHM_SRC_DIR}/ui/HostKeyApprover.cpp
    ${GHM_SRC_DIR}/ui/TlsCertApprover.h
    ${GHM_SRC_DIR}/ui/TlsCertApprover.cpp
    ${GHM_SRC_DIR}/ui/TlsCertApprovalDialog.h
    ${GHM_SRC_DIR}/ui/TlsCertApprovalDialog.cpp
    ${GHM_SRC_DIR}/ui/TrustedServersDialog.h
    ${GHM_SRC_DIR}/ui/TrustedServersDialog.cpp
    ${GHM_SRC_DIR}/ui/RememberedKeysDialog.h
    ${GHM_SRC_DIR}/ui/RememberedKeysDialog.cpp
    ${GHM_SRC_DIR}/ui/ChangelogDialog.h
    ${GHM_SRC_DIR}/ui/ChangelogDialog.cpp
    ${GHM_SRC_DIR}/ui/AddSubmoduleDialog.h
    ${GHM_SRC_DIR}/ui/AddSubmoduleDialog.cpp
    ${GHM_SRC_DIR}/ui/SigningSettingsDialog.h
    ${GHM_SRC_DIR}/ui/SigningSettingsDialog.cpp
    ${GHM_SRC_DIR}/ui/OAuthLoginDialog.h
    ${GHM_SRC_DIR}/ui/OAuthLoginDialog.cpp
    ${GHM_SRC_DIR}/ui/SupportDialog.h
    ${GHM_SRC_DIR}/ui/SupportDialog.cpp
    ${GHM_SRC_DIR}/ui/DiffViewWidget.h
    ${GHM_SRC_DIR}/ui/DiffViewWidget.cpp
    ${GHM_SRC_DIR}/ui/SearchBar.h
    ${GHM_SRC_DIR}/ui/SearchBar.cpp
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
        CHANGELOG.md
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

# GitHub OAuth App client ID. To enable the "Sign in with GitHub"
# button in the login dialog, register an OAuth App at
# https://github.com/settings/developers and pass the client ID via:
#
#   cmake -DGHM_OAUTH_CLIENT_ID=Ov23liXXXXXXXXXXXXXX -S . -B build
#
# When unset, the OAuth button is disabled and the user can still
# sign in with a Personal Access Token (PAT) as before.
#
# Why this isn't hardcoded: a published OAuth App client_id pinned
# in source code commits us to a specific upstream app forever. By
# making it a build option, anyone forking or self-hosting can use
# their own app, and we don't have to embed a credential in the
# public repo.
set(GHM_OAUTH_CLIENT_ID "" CACHE STRING
    "GitHub OAuth App client_id for device flow login. Leave empty to disable.")
if(GHM_OAUTH_CLIENT_ID)
    target_compile_definitions(github-manager PRIVATE
        GHM_OAUTH_CLIENT_ID="${GHM_OAUTH_CLIENT_ID}")
    message(STATUS "OAuth device flow enabled with client_id: ${GHM_OAUTH_CLIENT_ID}")
else()
    message(STATUS "OAuth device flow DISABLED (GHM_OAUTH_CLIENT_ID not set). "
                   "PAT login still works.")
endif()

# --- Translations ----------------------------------------------------------
#
# Pipeline: translations/*.ts → lrelease → .qm → embedded into the
# binary as Qt resources at /i18n/. Translator loads them at runtime
# via QTranslator::load("github-manager_<lang>", ":/i18n").
#
# qt_add_translations does the whole thing in one call:
#   * Runs lrelease at build time, producing .qm in the build tree
#   * Bakes those .qm into a generated resource, linked into the target
#   * Creates a `<target>_lupdate` custom target for refreshing .ts
#     from source (run manually, not part of default build — see
#     CMakeLists comment below)
#
# We DO NOT auto-run lupdate at build time. Running lupdate modifies
# translations/*.ts, which would be a source-tree write during build
# — surprising, and incompatible with read-only build trees /
# packaging systems. Run it explicitly when you've added or changed
# tr() strings in the code:
#
#   cmake --build build --target github-manager_lupdate

set(GHM_TS_FILES
    translations/github-manager_pl.ts
    translations/github-manager_de.ts
    translations/github-manager_es.ts
    translations/github-manager_fr.ts
)

qt_add_translations(github-manager
    TS_FILES ${GHM_TS_FILES}
    RESOURCE_PREFIX "/i18n"
    LUPDATE_TARGET github-manager_lupdate
)

# --- Install ---------------------------------------------------------------

include(GNUInstallDirs)
install(TARGETS github-manager
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES resources/github-manager.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    OPTIONAL)

# --- Tests -----------------------------------------------------------------
#
# Opt-in: pass -DGHM_BUILD_TESTS=ON to cmake to enable. The default OFF
# keeps install.py's standard build flow lean — most users don't care
# about test artefacts.
#
# Tests use QtTest (no extra deps; we already pull in Qt6::Test via the
# component below). Each test is its own small executable so a single
# failing case doesn't block the rest from running.

option(GHM_BUILD_TESTS "Build unit tests" OFF)

if(GHM_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
