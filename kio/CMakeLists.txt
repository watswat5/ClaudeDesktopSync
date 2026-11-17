cmake_minimum_required(VERSION 3.22)
project(kio_claude_sessions LANGUAGES CXX)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(KF6 6.0 REQUIRED COMPONENTS KIO CoreAddons)

kcoreaddons_add_plugin(kio_claude_sessions SOURCES claudesessions.cpp INSTALL_NAMESPACE "kf6/kio")
set_target_properties(kio_claude_sessions PROPERTIES OUTPUT_NAME "claude-sessions")
target_link_libraries(kio_claude_sessions KF6::KIOCore Qt6::Core)
