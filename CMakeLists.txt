cmake_minimum_required(VERSION 3.16)
project(forest VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(FOREST_ENABLE_CLANG_TIDY "Run clang-tidy during C++ compilation" OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Test)
include(CTest)

set(FOREST_APP_SOURCES
    src/storage/DatabaseManager.cpp
    src/storage/ApplicationInstanceLock.cpp
    src/storage/BackupService.cpp
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
    src/core/FocusSessionCoordinator.cpp
    src/core/FocusResultService.cpp
    src/core/FocusSettlementManager.cpp
    src/core/DashboardSnapshotService.cpp
    src/core/FocusStatisticsQuery.cpp
    src/core/QuoteProvider.cpp
    src/core/CoinManager.cpp
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
    src/ui/SettingsPage.cpp
    src/ui/SproutToggle.cpp
    src/ui/PlantImageUtils.cpp
    src/ui/GachaDigSiteWidget.cpp
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

add_library(forest_app STATIC ${FOREST_APP_SOURCES})
target_include_directories(forest_app PUBLIC ${CMAKE_SOURCE_DIR}/src)
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
copy_qt_plugins(forest)

if(BUILD_TESTING)
    function(add_forest_test target_name test_source needs_qt_test)
        add_executable(${target_name} ${test_source} src/resources.qrc)
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
    add_forest_test(forest_focus_resilience_selftest src/tests/FocusResilienceSelfTest.cpp FALSE)
    add_forest_test(forest_user_security_selftest src/tests/UserSecuritySelfTest.cpp FALSE)
    add_forest_test(forest_plant_catalog_selftest src/tests/PlantCatalogSelfTest.cpp FALSE)
    add_forest_test(forest_statistics_selftest src/tests/StatisticsSelfTest.cpp FALSE)
    add_forest_test(forest_ui_smoketest src/tests/UiSmokeTest.cpp TRUE)
endif()
