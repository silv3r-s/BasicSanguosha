cmake_minimum_required(VERSION 3.21)

project(BasicSanguosha VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

qt_add_executable(BasicSanguosha
    src/main.cpp
    src/core/Player.cpp
    src/core/Deck.cpp
    src/core/GameEngine.cpp
    src/core/RandomGenerator.cpp
    src/core/DistanceCalculator.cpp
    src/core/DamageSystem.cpp
    src/core/EventDispatcher.cpp
    src/cards/Card.cpp
    src/cards/StandardDeckDefinition.cpp
    src/session/GameSession.cpp
    src/client/GameClientController.cpp
    src/network/NetworkProtocol.cpp
    src/network/GameServer.cpp
    src/network/NetworkGameClient.cpp
    src/ai/AIController.cpp
    src/ai/AIThreatEvaluator.cpp
    src/ai/AIResourcePlanner.cpp
    src/ai/AITargetEvaluator.cpp
    src/ai/AIIdentityInference.cpp
    src/ai/AIIdentityStrategy.cpp
    src/ai/AIActionPacing.cpp
    src/ui/GameText.cpp
    src/ui/GameWidgets.cpp
    src/ui/BattleLogWindow.cpp
    src/ui/InteractionPanel.cpp
    src/ui/ConnectionPage.cpp
    src/ui/MainWindow.cpp
)

target_include_directories(BasicSanguosha PRIVATE src)
target_link_libraries(BasicSanguosha PRIVATE Qt6::Widgets Qt6::Network)
if(MSVC)
    target_compile_options(BasicSanguosha PRIVATE /utf-8)
endif()

set_target_properties(BasicSanguosha PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# Resolve the deployment tool from the same Qt installation that supplied Qt6::Widgets.
get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
if(NOT QT_QMAKE_EXECUTABLE)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION_RELEASE)
endif()
if(NOT QT_QMAKE_EXECUTABLE)
    message(FATAL_ERROR "Cannot resolve qmake from the configured Qt 6 installation.")
endif()

get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
set(WINDEPLOYQT_EXECUTABLE "${QT_BIN_DIR}/windeployqt.exe")
if(NOT EXISTS "${WINDEPLOYQT_EXECUTABLE}")
    message(FATAL_ERROR "windeployqt.exe was not found beside Qt6::qmake: ${WINDEPLOYQT_EXECUTABLE}")
endif()

set(DEPLOY_DIRECTORY "${CMAKE_SOURCE_DIR}/dist/BasicSanguosha")
add_custom_target(deploy
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${DEPLOY_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:BasicSanguosha>" "${DEPLOY_DIRECTORY}/BasicSanguosha.exe"
    COMMAND "${WINDEPLOYQT_EXECUTABLE}" --release --no-translations --dir "${DEPLOY_DIRECTORY}" "${DEPLOY_DIRECTORY}/BasicSanguosha.exe"
    DEPENDS BasicSanguosha
    COMMENT "Deploying BasicSanguosha with the configured Qt runtime"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    # Several legacy tests use assert() to execute setup/actions.  Release
    # builds define NDEBUG by default, which would silently remove those
    # operations and make the test binaries invalid rather than merely quiet.
    # Keep assertions active for every test target; production targets are
    # created before this directory option and remain unchanged.
    if(MSVC)
        add_compile_options(/UNDEBUG)
    endif()
    add_executable(BasicSanguoshaCoreTests
        tests/CoreTests.cpp
        tests/TestHelpers.cpp
        tests/HalberdTests.cpp
        src/core/Player.cpp
        src/core/Deck.cpp
        src/core/GameEngine.cpp
        src/core/RandomGenerator.cpp
        src/core/DistanceCalculator.cpp
        src/core/DamageSystem.cpp
        src/core/EventDispatcher.cpp
        src/cards/Card.cpp
        src/cards/StandardDeckDefinition.cpp
        src/session/GameSession.cpp
        src/client/GameClientController.cpp
        src/network/NetworkProtocol.cpp
        src/network/GameServer.cpp
        src/network/NetworkGameClient.cpp
    )
    target_include_directories(BasicSanguoshaCoreTests PRIVATE src)
    target_link_libraries(BasicSanguoshaCoreTests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaCoreTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaCoreTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaCoreTests COMMAND BasicSanguoshaCoreTests)
    add_executable(BasicSanguoshaNetworkTests
        tests/NetworkTests.cpp
        src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp
        src/session/GameSession.cpp src/client/GameClientController.cpp src/network/NetworkProtocol.cpp src/network/GameServer.cpp src/network/NetworkGameClient.cpp
    )
    target_include_directories(BasicSanguoshaNetworkTests PRIVATE src)
    target_link_libraries(BasicSanguoshaNetworkTests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaNetworkTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaNetworkTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaNetworkTests COMMAND BasicSanguoshaNetworkTests)
    add_executable(BasicSanguoshaKirinBowNetworkTests tests/KirinBowNetworkTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp src/client/GameClientController.cpp src/network/NetworkProtocol.cpp src/network/GameServer.cpp src/network/NetworkGameClient.cpp)
    target_include_directories(BasicSanguoshaKirinBowNetworkTests PRIVATE src)
    target_link_libraries(BasicSanguoshaKirinBowNetworkTests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaKirinBowNetworkTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaKirinBowNetworkTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaKirinBowNetworkTests COMMAND BasicSanguoshaKirinBowNetworkTests)

    add_executable(BasicSanguoshaVermilionVineTests
        tests/VermilionVineTests.cpp
        src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp
        src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp
    )
    target_include_directories(BasicSanguoshaVermilionVineTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaVermilionVineTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaVermilionVineTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaVermilionVineTests COMMAND BasicSanguoshaVermilionVineTests)

    add_executable(BasicSanguoshaGudingBladeTests
        tests/GudingBladeTests.cpp
        src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp
        src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp
    )
    target_include_directories(BasicSanguoshaGudingBladeTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaGudingBladeTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaGudingBladeTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaGudingBladeTests COMMAND BasicSanguoshaGudingBladeTests)

    add_executable(BasicSanguoshaComplexUiTests
        tests/ComplexUiStateTests.cpp
        src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp
        src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp
    )
    target_include_directories(BasicSanguoshaComplexUiTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaComplexUiTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaComplexUiTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaComplexUiTests COMMAND BasicSanguoshaComplexUiTests)

    add_executable(BasicSanguoshaZhangbaSpearTests
        tests/ZhangbaSpearTests.cpp
        src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp
        src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp
    )
    target_include_directories(BasicSanguoshaZhangbaSpearTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaZhangbaSpearTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaZhangbaSpearTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaZhangbaSpearTests COMMAND BasicSanguoshaZhangbaSpearTests)

    add_executable(BasicSanguoshaNullificationCoverageTests
        tests/NullificationCoverageTests.cpp
        src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp
        src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp
    )
    target_include_directories(BasicSanguoshaNullificationCoverageTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaNullificationCoverageTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaNullificationCoverageTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaNullificationCoverageTests COMMAND BasicSanguoshaNullificationCoverageTests)

    add_executable(BasicSanguoshaWeaponCoverageTests
        tests/WeaponCoverageTests.cpp
        src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp
        src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp
    )
    target_include_directories(BasicSanguoshaWeaponCoverageTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaWeaponCoverageTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaWeaponCoverageTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaWeaponCoverageTests COMMAND BasicSanguoshaWeaponCoverageTests)

    add_executable(BasicSanguoshaSlashDamageResumeTests
        tests/SlashDamageResumeTests.cpp
        src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp
        src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp
    )
    target_include_directories(BasicSanguoshaSlashDamageResumeTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaSlashDamageResumeTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaSlashDamageResumeTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaSlashDamageResumeTests COMMAND BasicSanguoshaSlashDamageResumeTests)

    add_executable(BasicSanguoshaAxeTests
        tests/AxeTests.cpp
        src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp
        src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp
    )
    target_include_directories(BasicSanguoshaAxeTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaAxeTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaAxeTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaAxeTests COMMAND BasicSanguoshaAxeTests)
    add_executable(BasicSanguoshaKirinBowTests tests/KirinBowTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp)
    target_include_directories(BasicSanguoshaKirinBowTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaKirinBowTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaKirinBowTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaKirinBowTests COMMAND BasicSanguoshaKirinBowTests)
    add_executable(BasicSanguoshaIceSwordTests tests/IceSwordTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp)
    target_include_directories(BasicSanguoshaIceSwordTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaIceSwordTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaIceSwordTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaIceSwordTests COMMAND BasicSanguoshaIceSwordTests)
    add_executable(BasicSanguoshaIceSwordNetworkTests tests/IceSwordNetworkTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp src/client/GameClientController.cpp src/network/NetworkProtocol.cpp src/network/GameServer.cpp src/network/NetworkGameClient.cpp)
    target_include_directories(BasicSanguoshaIceSwordNetworkTests PRIVATE src)
    target_link_libraries(BasicSanguoshaIceSwordNetworkTests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaIceSwordNetworkTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaIceSwordNetworkTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaIceSwordNetworkTests COMMAND BasicSanguoshaIceSwordNetworkTests)
    add_executable(BasicSanguoshaDoubleSwordTests tests/DoubleSwordTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp)
    target_include_directories(BasicSanguoshaDoubleSwordTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaDoubleSwordTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaDoubleSwordTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaDoubleSwordTests COMMAND BasicSanguoshaDoubleSwordTests)
    add_executable(BasicSanguoshaDoubleSwordReturnToLobbyTests tests/DoubleSwordReturnToLobbyTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp src/client/GameClientController.cpp src/network/NetworkProtocol.cpp src/network/GameServer.cpp src/network/NetworkGameClient.cpp)
    target_include_directories(BasicSanguoshaDoubleSwordReturnToLobbyTests PRIVATE src)
    target_link_libraries(BasicSanguoshaDoubleSwordReturnToLobbyTests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaDoubleSwordReturnToLobbyTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaDoubleSwordReturnToLobbyTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaDoubleSwordReturnToLobbyTests COMMAND BasicSanguoshaDoubleSwordReturnToLobbyTests)
    add_executable(BasicSanguoshaDoubleSwordNetworkTests tests/DoubleSwordNetworkTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp src/client/GameClientController.cpp src/network/NetworkProtocol.cpp src/network/GameServer.cpp src/network/NetworkGameClient.cpp)
    target_include_directories(BasicSanguoshaDoubleSwordNetworkTests PRIVATE src)
    target_link_libraries(BasicSanguoshaDoubleSwordNetworkTests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaDoubleSwordNetworkTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaDoubleSwordNetworkTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaDoubleSwordNetworkTests COMMAND BasicSanguoshaDoubleSwordNetworkTests)
    add_executable(BasicSanguoshaLobbySeatAllocationTests tests/LobbySeatAllocationTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp src/client/GameClientController.cpp src/network/NetworkProtocol.cpp src/network/GameServer.cpp src/network/NetworkGameClient.cpp)
    target_include_directories(BasicSanguoshaLobbySeatAllocationTests PRIVATE src)
    target_link_libraries(BasicSanguoshaLobbySeatAllocationTests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaLobbySeatAllocationTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaLobbySeatAllocationTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaLobbySeatAllocationTests COMMAND BasicSanguoshaLobbySeatAllocationTests)
    add_executable(BasicSanguoshaEquipmentSealTests tests/EquipmentSealTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/ui/GameText.cpp)
    target_include_directories(BasicSanguoshaEquipmentSealTests PRIVATE src)
    target_link_libraries(BasicSanguoshaEquipmentSealTests PRIVATE Qt6::Core)
    target_compile_definitions(BasicSanguoshaEquipmentSealTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaEquipmentSealTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaEquipmentSealTests COMMAND BasicSanguoshaEquipmentSealTests)
    add_executable(BasicSanguoshaGameTextTests tests/GameTextTests.cpp src/core/Player.cpp src/cards/Card.cpp src/ui/GameText.cpp)
    target_include_directories(BasicSanguoshaGameTextTests PRIVATE src)
    target_link_libraries(BasicSanguoshaGameTextTests PRIVATE Qt6::Core)
    target_compile_definitions(BasicSanguoshaGameTextTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaGameTextTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaGameTextTests COMMAND BasicSanguoshaGameTextTests)
    add_executable(BasicSanguoshaUXClosureTests tests/UXClosureTests.cpp src/core/Player.cpp src/cards/Card.cpp src/ui/GameText.cpp src/ui/GameWidgets.cpp src/ui/BattleLogWindow.cpp src/ui/InteractionPanel.cpp)
    target_include_directories(BasicSanguoshaUXClosureTests PRIVATE src)
    target_link_libraries(BasicSanguoshaUXClosureTests PRIVATE Qt6::Widgets)
    target_compile_definitions(BasicSanguoshaUXClosureTests PRIVATE SANGUOSHA_TESTING SANGUOSHA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(BasicSanguoshaUXClosureTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaUXClosureTests COMMAND BasicSanguoshaUXClosureTests)
    set_tests_properties(BasicSanguoshaUXClosureTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${QT_BIN_DIR}/../plugins/platforms"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_BIN_DIR}"
    )
    add_executable(BasicSanguoshaEquipmentEffectTests tests/EquipmentEffectTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp)
    target_include_directories(BasicSanguoshaEquipmentEffectTests PRIVATE src)
    target_compile_definitions(BasicSanguoshaEquipmentEffectTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaEquipmentEffectTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaEquipmentEffectTests COMMAND BasicSanguoshaEquipmentEffectTests)
    add_executable(BasicSanguoshaLanNetworkTests tests/LanNetworkTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp src/client/GameClientController.cpp src/network/NetworkProtocol.cpp src/network/GameServer.cpp src/network/NetworkGameClient.cpp)
    target_include_directories(BasicSanguoshaLanNetworkTests PRIVATE src)
    target_link_libraries(BasicSanguoshaLanNetworkTests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaLanNetworkTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaLanNetworkTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaLanNetworkTests COMMAND BasicSanguoshaLanNetworkTests)
    add_executable(BasicSanguoshaReconnectLobbyTests tests/ReconnectLobbyTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp src/client/GameClientController.cpp src/network/NetworkProtocol.cpp src/network/GameServer.cpp src/network/NetworkGameClient.cpp)
    target_include_directories(BasicSanguoshaReconnectLobbyTests PRIVATE src)
    target_link_libraries(BasicSanguoshaReconnectLobbyTests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaReconnectLobbyTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaReconnectLobbyTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaReconnectLobbyTests COMMAND BasicSanguoshaReconnectLobbyTests)
    add_executable(BasicSanguoshaGameReconnectTests tests/GameReconnectTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp src/client/GameClientController.cpp src/network/NetworkProtocol.cpp src/network/GameServer.cpp src/network/NetworkGameClient.cpp)
    target_include_directories(BasicSanguoshaGameReconnectTests PRIVATE src)
    target_link_libraries(BasicSanguoshaGameReconnectTests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaGameReconnectTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaGameReconnectTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaGameReconnectTests COMMAND BasicSanguoshaGameReconnectTests)
    add_executable(BasicSanguoshaDisconnectStabilityTests tests/DisconnectStabilityTests.cpp src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp src/client/GameClientController.cpp src/network/NetworkProtocol.cpp src/network/GameServer.cpp src/network/NetworkGameClient.cpp)
    target_include_directories(BasicSanguoshaDisconnectStabilityTests PRIVATE src)
    target_link_libraries(BasicSanguoshaDisconnectStabilityTests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaDisconnectStabilityTests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaDisconnectStabilityTests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaDisconnectStabilityTests COMMAND BasicSanguoshaDisconnectStabilityTests)

    # AIController is used by GameServer and therefore participates in every
    # network/server test target without introducing a TCP bot transport.
    foreach(ai_network_target
        BasicSanguoshaCoreTests
        BasicSanguoshaNetworkTests
        BasicSanguoshaKirinBowNetworkTests
        BasicSanguoshaIceSwordNetworkTests
        BasicSanguoshaDoubleSwordReturnToLobbyTests
        BasicSanguoshaDoubleSwordNetworkTests
        BasicSanguoshaLobbySeatAllocationTests
        BasicSanguoshaLanNetworkTests
        BasicSanguoshaReconnectLobbyTests
        BasicSanguoshaGameReconnectTests
        BasicSanguoshaDisconnectStabilityTests)
        target_sources(${ai_network_target} PRIVATE src/ai/AIController.cpp src/ai/AIThreatEvaluator.cpp src/ai/AIResourcePlanner.cpp src/ai/AITargetEvaluator.cpp src/ai/AIIdentityInference.cpp src/ai/AIIdentityStrategy.cpp src/ai/AIActionPacing.cpp)
    endforeach()

    add_executable(BasicSanguoshaAITests
        tests/AITests.cpp
        tests/AIIdentityTests.cpp
        src/core/Player.cpp src/core/Deck.cpp src/core/GameEngine.cpp src/core/RandomGenerator.cpp src/core/DistanceCalculator.cpp src/core/DamageSystem.cpp src/core/EventDispatcher.cpp
        src/cards/Card.cpp src/cards/StandardDeckDefinition.cpp src/session/GameSession.cpp src/client/GameClientController.cpp src/network/NetworkProtocol.cpp src/network/GameServer.cpp src/network/NetworkGameClient.cpp src/ai/AIController.cpp src/ai/AIThreatEvaluator.cpp src/ai/AIResourcePlanner.cpp src/ai/AITargetEvaluator.cpp src/ai/AIIdentityInference.cpp src/ai/AIIdentityStrategy.cpp src/ai/AIActionPacing.cpp
    )
    target_include_directories(BasicSanguoshaAITests PRIVATE src)
    target_link_libraries(BasicSanguoshaAITests PRIVATE Qt6::Network)
    target_compile_definitions(BasicSanguoshaAITests PRIVATE SANGUOSHA_TESTING)
    if(MSVC)
        target_compile_options(BasicSanguoshaAITests PRIVATE /utf-8)
    endif()
    add_test(NAME BasicSanguoshaAITests COMMAND BasicSanguoshaAITests)
endif()
