cmake_minimum_required(VERSION 3.16)
project(forest VERSION 1.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(FOREST_ENABLE_CLANG_TIDY "Run clang-tidy during C++ compilation" OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

set(FOREST_APP_SOURCES
    src/storage/DatabaseManager.cpp
    src/storage/ApplicationInstanceLock.cpp
    src/storage/StartupDataService.cpp
    src/storage/BackupService.cpp
    src/storage/BackupSnapshot.cpp
    src/storage/RestoreTransaction.cpp
    src/storage/StorageEnvelope.cpp
    src/storage/UserManager.cpp
    src/config/PlantCatalog.cpp
    src/config/UserPreferences.cpp
    src/plant/AbstractPlant.cpp
    src/plant/PlantFactory.cpp
    src/plant/Tree.cpp
    src/plant/Flower.cpp
    src/plant/OakTree.cpp
    src/plant/PineTree.cpp
    src/plant/Rose.cpp
    src/plant/GinkgoTree.cpp
    src/plant/Sunflower.cpp
    src/plant/Cactus.cpp
    src/plant/VariantPlants.cpp
    src/core/FocusController.cpp
    src/core/FocusClock.cpp
    src/core/FocusSessionCoordinator.cpp
    src/core/FocusResultService.cpp
    src/core/FocusSettlementManager.cpp
    src/core/DashboardSnapshotService.cpp
    src/core/FocusStatisticsQuery.cpp
    src/core/QuoteProvider.cpp
    src/core/CoinManager.cpp
    src/core/ChallengeRewardService.cpp
    src/core/AchievementEngine.cpp
    src/core/GachaManager.cpp
    src/core/ForestLayoutManager.cpp
    src/core/FriendManager.cpp
    src/core/ChallengeManager.cpp
    src/core/TagManager.cpp
    src/core/GuardianManager.cpp
    src/core/UserFeatureServices.cpp
    src/system/RuleEngine.cpp
    src/system/SystemMonitor.cpp
    src/ui/MainWindow.cpp
    src/ui/CommercePages.cpp
    src/ui/DashboardPageHost.cpp
    src/ui/SocialPages.cpp
    src/ui/SidebarNavigation.cpp
    src/ui/AppStyle.cpp
    src/ui/PlantImageUtils.cpp
    src/ui/PlantTimerWidget.cpp
    src/ui/GardenCanvas.cpp
    src/ui/ForestDashboardWidget.cpp
    src/ui/ChallengeDashboardWidget.cpp
    src/ui/GuardianDashboardWidget.cpp
    src/ui/SettingsDialog.cpp
    src/ui/StoreDialog.cpp
    src/ui/LoginDialog.cpp
    src/ui/GridSelectDialog.cpp
    src/ui/AchievementDialog.cpp
    src/ui/MyForestDialog.cpp
    src/ui/FriendDialog.cpp
    src/ui/ChallengeDialog.cpp
    src/ui/GuardianDialog.cpp
    src/utils/Logger.cpp
)

include("${CMAKE_SOURCE_DIR}/cmake/CheckModuleBoundaries.cmake")

# Include sources, resources, tests, the UI smoke runner, and CMake inputs.
set(FOREST_PROVENANCE_INPUTS "${CMAKE_SOURCE_DIR}/CMakeLists.txt")
file(GLOB_RECURSE FOREST_PRODUCTION_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${CMAKE_SOURCE_DIR}/src/*"
)
file(GLOB FOREST_ROOT_CMAKE_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${CMAKE_SOURCE_DIR}/*.cmake"
)
file(GLOB_RECURSE FOREST_CMAKE_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${CMAKE_SOURCE_DIR}/cmake/*.cmake"
    "${CMAKE_SOURCE_DIR}/cmake/*.in"
)
list(APPEND FOREST_PROVENANCE_INPUTS
    ${FOREST_PRODUCTION_FILES}
    ${FOREST_ROOT_CMAKE_FILES}
    ${FOREST_CMAKE_FILES}
    "${CMAKE_SOURCE_DIR}/scripts/run_ui_smoke.ps1"
    "${CMAKE_SOURCE_DIR}/scripts/package_portable.ps1"
    "${CMAKE_SOURCE_DIR}/scripts/PackageSafety.ps1"
    "${CMAKE_SOURCE_DIR}/scripts/RuntimeSmoke.ps1"
    "${CMAKE_SOURCE_DIR}/scripts/tests/PackageSafetySelfTest.ps1"
)
list(SORT FOREST_PROVENANCE_INPUTS)

set(FOREST_FINGERPRINT_CONTENT "")
foreach(provenance_file IN LISTS FOREST_PROVENANCE_INPUTS)
    file(RELATIVE_PATH provenance_path "${CMAKE_SOURCE_DIR}" "${provenance_file}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${provenance_file}")
    file(SHA256 "${provenance_file}" provenance_hash)
    string(APPEND FOREST_FINGERPRINT_CONTENT "${provenance_path}:${provenance_hash}\n")
endforeach()
string(SHA256 FOREST_SOURCE_FINGERPRINT "${FOREST_FINGERPRINT_CONTENT}")

set(FOREST_SOURCE_DIRECTORY "${CMAKE_SOURCE_DIR}")
set(FOREST_APP_VERSION "${PROJECT_VERSION}")
set(FOREST_QT_VERSION "${Qt6Core_VERSION}")
set(FOREST_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
string(TIMESTAMP FOREST_CONFIGURED_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)

set(FOREST_REVISION "unknown")
execute_process(
    COMMAND git rev-parse --verify HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE FOREST_GIT_RESULT
    OUTPUT_VARIABLE FOREST_GIT_REVISION
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if("${FOREST_GIT_RESULT}" STREQUAL "0" AND FOREST_GIT_REVISION MATCHES "^[0-9a-fA-F]+$")
    set(FOREST_REVISION "${FOREST_GIT_REVISION}")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/BuildProvenance.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildProvenance.h"
    @ONLY
)

add_library(forest_app STATIC ${FOREST_APP_SOURCES})
target_include_directories(forest_app PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_definitions(forest_app PUBLIC "FOREST_BUILD_CONFIGURATION=\"$<CONFIG>\"")
target_link_libraries(forest_app PUBLIC Qt6::Widgets Qt6::Sql)

if(MINGW)
    target_compile_options(forest_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(FOREST_ENABLE_CLANG_TIDY)
    find_program(FOREST_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
    set_target_properties(forest_app PROPERTIES CXX_CLANG_TIDY "${FOREST_CLANG_TIDY_EXECUTABLE}")
endif()

add_executable(forest WIN32 src/main.cpp src/resources.qrc)
target_link_libraries(forest PRIVATE forest_app)

if(MINGW)
    target_link_options(forest PRIVATE "-static-libgcc" "-static-libstdc++")
endif()

function(copy_mingw_runtime target_name)
    if(MINGW)
        get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(runtime_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
            set(runtime_path "${MINGW_BIN_DIR}/${runtime_dll}")
            if(EXISTS "${runtime_path}")
                add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${runtime_path}"
                        "$<TARGET_FILE_DIR:${target_name}>/${runtime_dll}")
            endif()
        endforeach()
    endif()
endfunction()

function(copy_qt_plugins target_name)
    if(WIN32)
        get_filename_component(QT6_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
        foreach(plugin_spec "platforms/qwindows.dll" "sqldrivers/qsqlite.dll")
            set(source "${QT6_PREFIX}/plugins/${plugin_spec}")
            if(EXISTS "${source}")
                get_filename_component(plugin_dir "${plugin_spec}" DIRECTORY)
                add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>/${plugin_dir}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${source}"
                        "$<TARGET_FILE_DIR:${target_name}>/${plugin_spec}")
            endif()
        endforeach()
    endif()
endfunction()

function(copy_qt_runtime target_name include_test)
    if(WIN32)
        set(qt_runtime Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql)
        if(${include_test})
            list(APPEND qt_runtime Qt6::Test)
        endif()
        foreach(runtime IN LISTS qt_runtime)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${runtime}>"
                    "$<TARGET_FILE_DIR:${target_name}>/$<TARGET_FILE_NAME:${runtime}>")
        endforeach()
    endif()
endfunction()

copy_mingw_runtime(forest)
copy_qt_runtime(forest FALSE)
copy_qt_plugins(forest)

if(BUILD_TESTING)
    function(add_forest_test target_name test_source needs_qt_test)
        add_executable(${target_name} ${test_source})
        target_link_libraries(${target_name} PRIVATE forest_app)
        if(${needs_qt_test})
            target_link_libraries(${target_name} PRIVATE Qt6::Test)
        endif()
        copy_mingw_runtime(${target_name})
        copy_qt_runtime(${target_name} ${needs_qt_test})
        copy_qt_plugins(${target_name})
        if(WIN32 AND target_name STREQUAL "forest_ui_smoketest")
            add_test(NAME ${target_name}
                COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_SOURCE_DIR}/scripts/run_ui_smoke.ps1"
                    -Executable "$<TARGET_FILE:${target_name}>")
        else()
            add_test(NAME ${target_name} COMMAND ${target_name})
        endif()
    endfunction()

    add_forest_test(forest_storage_selftest src/tests/StorageSelfTest.cpp FALSE)
    add_forest_test(forest_wallet_operations_selftest src/tests/WalletOperationsSelfTest.cpp FALSE)
    add_forest_test(forest_challenge_claims_selftest src/tests/ChallengeClaimsSelfTest.cpp FALSE)
    add_forest_test(forest_account_preferences_selftest src/tests/AccountPreferencesSelfTest.cpp FALSE)
    add_forest_test(forest_preferences_validation_selftest src/tests/PreferencesValidationSelfTest.cpp FALSE)
    add_forest_test(forest_logger_selftest src/tests/LoggerSelfTest.cpp FALSE)
    add_forest_test(forest_gacha_consistency_selftest src/tests/GachaConsistencySelfTest.cpp FALSE)
    add_forest_test(forest_focus_resilience_selftest src/tests/FocusResilienceSelfTest.cpp FALSE)
    add_forest_test(forest_focus_clock_selftest src/tests/FocusClockSelfTest.cpp FALSE)
    add_forest_test(forest_focus_terminal_selftest src/tests/FocusTerminalSelfTest.cpp FALSE)
    add_forest_test(forest_user_security_selftest src/tests/UserSecuritySelfTest.cpp FALSE)
    add_forest_test(forest_plant_catalog_selftest src/tests/PlantCatalogSelfTest.cpp FALSE)
    add_forest_test(forest_statistics_selftest src/tests/StatisticsSelfTest.cpp FALSE)
    add_forest_test(forest_business_semantics_selftest src/tests/BusinessSemanticsSelfTest.cpp FALSE)
    add_forest_test(forest_local_friend_semantics_selftest src/tests/LocalFriendSemanticsSelfTest.cpp FALSE)
    add_forest_test(forest_guardian_semantics_selftest src/tests/GuardianSemanticsSelfTest.cpp FALSE)
    add_forest_test(forest_backup_path_safety_selftest src/tests/BackupPathSafetySelfTest.cpp FALSE)
    add_forest_test(forest_backup_snapshot_selftest src/tests/BackupSnapshotSelfTest.cpp FALSE)
    add_forest_test(forest_restore_transaction_selftest src/tests/RestoreTransactionSelfTest.cpp FALSE)
    add_forest_test(forest_startup_data_safety_selftest src/tests/StartupDataSafetySelfTest.cpp FALSE)
    add_forest_test(forest_migration_safety_selftest src/tests/MigrationSafetySelfTest.cpp FALSE)
    add_forest_test(forest_ui_smoketest src/tests/UiSmokeTest.cpp TRUE)
    # Only image-dependent tests need the embedded plant resources.
    target_sources(forest_plant_catalog_selftest PRIVATE src/resources.qrc)
    target_sources(forest_ui_smoketest PRIVATE src/resources.qrc)
    if(WIN32)
        add_test(NAME forest_package_safety_selftest
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/tests/PackageSafetySelfTest.ps1")
    endif()
endif()
