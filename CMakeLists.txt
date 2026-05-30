cmake_minimum_required(VERSION 3.16)
project(Forest VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/Qt/6.5.3/mingw_64")
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(Forest WIN32
    src/main.cpp
    src/resources.qrc
    src/storage/DatabaseManager.cpp
    src/storage/PresetManager.cpp
    src/storage/UserManager.cpp
    src/plant/AbstractPlant.cpp
    src/plant/Tree.cpp
    src/plant/Flower.cpp
    src/plant/OakTree.cpp
    src/plant/PineTree.cpp
    src/plant/Rose.cpp
    src/core/FocusController.cpp
    src/core/QuoteProvider.cpp
    src/core/CoinManager.cpp
    src/core/AchievementEngine.cpp
    src/system/RuleEngine.cpp
    src/system/SystemMonitor.cpp
    src/ui/MainWindow.cpp
    src/ui/TimerRing.cpp
    src/ui/GardenCanvas.cpp
    src/ui/SettingsDialog.cpp
    src/ui/StoreDialog.cpp
    src/ui/StatisticsDialog.cpp
    src/ui/LoginDialog.cpp
    src/ui/TimelineItemWidget.cpp
    src/ui/HistoryWidget.cpp
    src/utils/Logger.cpp
)

target_include_directories(Forest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(Forest PRIVATE Qt6::Widgets)

if(MINGW)
    target_link_options(Forest PRIVATE "-static-libgcc" "-static-libstdc++")
endif()
