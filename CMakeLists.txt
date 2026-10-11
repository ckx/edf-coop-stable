cmake_minimum_required(VERSION 3.20)
# EDF6Coop.dll: the transport part (src/) and the game part (game/) as one plugin.
project(EDF6Coop VERSION 3.0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Every file sees <windows.h> the same way.
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
# The version EDFModLoader is told (PluginInfo) and the plugin's exported marker carry.
add_compile_definitions(EDF6COOP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} EDF6COOP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
                        EDF6COOP_VERSION_PATCH=${PROJECT_VERSION_PATCH})
# Where the plugin and the race-fixed loader land (package.ps1 takes them from there).
set(OUT_DIR "${CMAKE_SOURCE_DIR}/out")
# PluginAPI.h from EDFModLoader (BlueAmulet, MIT).
set(MODLOADER_INCLUDE "${CMAKE_SOURCE_DIR}/third_party/EDFModLoader")

# The installed game the tests read EDF.dll from (only read). Pass -DEDF6_GAME_DIR=... or set the environment variable.
set(EDF6_GAME_DIR "$ENV{EDF6_GAME_DIR}" CACHE PATH "EARTH DEFENSE FORCE 6 install folder (holds EDF.dll)")
# Release builds pass ON: a build that cannot run the tests against the game must not pass for one that did.
option(EDF6_REQUIRE_GAME "Fail the configure when EDF6_GAME_DIR has no EDF.dll" OFF)

if(MSVC)
    set(WARNINGS /W4 /WX /utf-8 /permissive- /EHsc)
else()
    # MinGW-w64 (cmake/mingw-w64.cmake): the plugin and its tests link the C and C++ runtimes statically, so the
    # DLL needs nothing next to it but what Windows (or Wine) has.
    set(WARNINGS -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-unknown-pragmas)
    add_link_options(-static -static-libgcc -static-libstdc++)
    string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -lwinpthread")
    set(CMAKE_SHARED_LIBRARY_PREFIX "")
endif()
add_compile_options(${WARNINGS})

# The transport part: EOS hooks, held disconnects, the direct link.
set(TRANSPORT_DIR "${CMAKE_SOURCE_DIR}/src")
set(TRANSPORT_CORE log auth wire reliable direct_net netif config iat hold upnp fragment)
list(TRANSFORM TRANSPORT_CORE PREPEND "${TRANSPORT_DIR}/")
list(TRANSFORM TRANSPORT_CORE APPEND ".cpp")
set(TRANSPORT_DEFINES _WINSOCK_DEPRECATED_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
add_library(TransportCore STATIC ${TRANSPORT_CORE})
target_compile_definitions(TransportCore PUBLIC ${TRANSPORT_DEFINES})
target_link_libraries(TransportCore PUBLIC ws2_32 iphlpapi bcrypt ole32 oleaut32)
add_library(TransportPart STATIC "${TRANSPORT_DIR}/lobby_marker.cpp" "${TRANSPORT_DIR}/eos_hooks.cpp"
    "${TRANSPORT_DIR}/dn_part.cpp")
target_link_libraries(TransportPart PUBLIC TransportCore)

# The game part: patches to EDF.dll, the netcode version gate, player sync, hit authority, the log.
set(GAME_CORE log code midhook patches identity crashlog coop peertimeout weaponguard lobbytexts netfeature hitauth
    hitauthgame netplayer netplayer_game)
list(TRANSFORM GAME_CORE PREPEND "game/")
list(TRANSFORM GAME_CORE APPEND ".cpp")
add_library(GameCore STATIC ${GAME_CORE})
# "src/config.h" and the like name the transport part's headers (both parts have a log.h).
target_include_directories(GameCore PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(GameCore PUBLIC TransportPart)

add_library(EDF6Coop SHARED game/plugin.cpp)
option(EDF6_SHIELD_TRACE "Build temporary Luminous Shield visibility diagnostics" ON)
if(EDF6_SHIELD_TRACE)
    target_sources(EDF6Coop PRIVATE game/shieldtrace.cpp)
    target_compile_definitions(EDF6Coop PRIVATE EDF6_SHIELD_TRACE)
endif()
target_include_directories(EDF6Coop PRIVATE ${MODLOADER_INCLUDE})
target_link_libraries(EDF6Coop PRIVATE GameCore)
if(MSVC)
    target_link_options(EDF6Coop PRIVATE /DYNAMICBASE /NXCOMPAT /INCREMENTAL:NO /PDBALTPATH:EDF6Coop.pdb)
endif()
set_target_properties(EDF6Coop PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OUT_DIR}")

# The race-fixed EDFModLoader winmm.dll the package ships (tools/fix_winmm_proxy.py).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT "${OUT_DIR}/winmm.dll" "${OUT_DIR}/loader-fix.json"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/fix_winmm_proxy.py"
        "${CMAKE_SOURCE_DIR}/third_party/EDFModLoader/winmm.dll" "${OUT_DIR}/winmm.dll"
        --manifest "${OUT_DIR}/loader-fix.json"
    DEPENDS tools/fix_winmm_proxy.py tools/pe.py third_party/EDFModLoader/winmm.dll
    VERBATIM)
add_custom_target(LoaderProxyFix ALL DEPENDS "${OUT_DIR}/winmm.dll" "${OUT_DIR}/loader-fix.json")

# --- Tests ---
enable_testing()
set(EDF_DLL "${EDF6_GAME_DIR}/EDF.dll")
# The tests against the game's code are registered either way, labelled `game`. Without EDF.dll they report
# themselves skipped (exit code 77), so ctest names them in its summary instead of a configure warning nobody reads.
if(NOT EXISTS "${EDF_DLL}")
    if(EDF6_REQUIRE_GAME)
        message(FATAL_ERROR "No EDF.dll in EDF6_GAME_DIR (${EDF6_GAME_DIR}), and EDF6_REQUIRE_GAME is ON")
    endif()
    message(WARNING "No EDF.dll in EDF6_GAME_DIR (${EDF6_GAME_DIR}): the tests labelled `game` will be reported as skipped")
endif()
function(game_test name)
    add_test(NAME ${name} COMMAND ${ARGN})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 LABELS game)
endfunction()
# A test program `target` from `source`, linked against `libraries`, run as `name` with the remaining arguments.
function(unit_test name target source libraries)
    add_executable(${target} ${source})
    if(libraries)
        target_link_libraries(${target} PRIVATE ${libraries})
    endif()
    add_test(NAME ${name} COMMAND ${target} ${ARGN})
endfunction()

unit_test(DirectNet DirectNetTests tests/test_main.cpp TransportCore)
unit_test(Fragments FragmentTests tests/fragment_test.cpp TransportCore)
unit_test(GuardedFaults GuardTests tests/game/guard_test.cpp "")
unit_test(MidFunctionHooks MidHookTests tests/game/midhook_test.cpp GameCore)
unit_test(LogFileCap LogTests tests/game/log_test.cpp GameCore "${CMAKE_BINARY_DIR}/logtest")
unit_test(CrashDump CrashDumpTests tests/game/crashdump_test.cpp GameCore "${CMAKE_BINARY_DIR}/crashdumptest")
add_test(NAME CrashDumpInPlugin COMMAND CrashDumpTests "${CMAKE_BINARY_DIR}/crashdumptest" plugin)
unit_test(PeerTimeoutKeepsRoom PeerTimeoutTests tests/game/peertimeout_test.cpp GameCore "${CMAKE_BINARY_DIR}/peer-timeout-test.log")
unit_test(WeaponGuard WeaponGuardTests tests/game/weaponguard_test.cpp GameCore)
unit_test(NetcodeVersionGate NetFeatureTests tests/game/netfeature_test.cpp GameCore)
unit_test(NetPlayerSync NetPlayerTests tests/game/netplayer_test.cpp GameCore)
unit_test(HitAuthority HitAuthTests tests/game/hitauth_test.cpp GameCore)
game_test(HitAuthorityGameCode HitAuthTests "${EDF_DLL}")

unit_test(LoaderProxyRace LoaderProxyTests tests/game/loader_proxy_test.cpp ""
    "${CMAKE_SOURCE_DIR}/third_party/EDFModLoader/winmm.dll" "${OUT_DIR}/winmm.dll")
add_dependencies(LoaderProxyTests LoaderProxyFix)

# A plugin that refuses (Enabled=0, or no supported EDF.dll) is unloaded by EDFModLoader at once: nothing of it
# may keep running. Needs no game: without EDF.dll in the process the plugin refuses as it does in an unsupported one.
unit_test(PluginRefusalUnloads RefusalTests tests/game/refusal_test.cpp "" "$<TARGET_FILE:EDF6Coop>"
    "${CMAKE_BINARY_DIR}/refusaltest")
target_include_directories(RefusalTests PRIVATE ${MODLOADER_INCLUDE})
add_dependencies(RefusalTests EDF6Coop)

add_executable(PluginLoadTests tests/game/load_test.cpp)
target_include_directories(PluginLoadTests PRIVATE ${MODLOADER_INCLUDE})
target_link_libraries(PluginLoadTests PRIVATE GameCore)
add_dependencies(PluginLoadTests EDF6Coop)
foreach(mode on off fresh keeproomoff replaced)
    game_test(PluginLoad_${mode} PluginLoadTests "${EDF_DLL}" "$<TARGET_FILE:EDF6Coop>" "${CMAKE_BINARY_DIR}/loadtest" ${mode})
endforeach()

# Rooms played by several processes, each running the real EDF.dll and the real EDF6Coop.dll with a fake EOS
# between them (tests/gamenet). The fake is named as Epic's SDK and sits next to GameMachine.exe, where the loader
# looks first. Skipped (77) without the game.
set(GAMENET_DIR "${CMAKE_BINARY_DIR}/gamenet")
add_library(GameNetEos SHARED tests/gamenet/fake_eos_net.cpp)
set_target_properties(GameNetEos PROPERTIES OUTPUT_NAME EOSSDK-Win64-Shipping RUNTIME_OUTPUT_DIRECTORY "${GAMENET_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${GAMENET_DIR}")
add_executable(GameMachine tests/gamenet/machine.cpp tests/gamenet/roles.cpp tests/gamenet/game.cpp game/netplayer.cpp)
set_target_properties(GameMachine PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GAMENET_DIR}")
target_include_directories(GameMachine PRIVATE ${MODLOADER_INCLUDE})
add_dependencies(GameMachine GameNetEos)
add_executable(GameNetTests tests/gamenet/gamenet_test.cpp)
target_link_libraries(GameNetTests PRIVATE ws2_32)
add_dependencies(GameNetTests GameMachine EDF6Coop)
foreach(scenario room link playersync versiongate netstats netstatsunreliable direct directoutage)
    game_test(GameNet_${scenario} GameNetTests "${EDF6_GAME_DIR}" "$<TARGET_FILE:EDF6Coop>" "$<TARGET_FILE:GameMachine>"
              "${CMAKE_BINARY_DIR}/gamenettest" ${scenario})
endforeach()

# Test programs with wmain: MinGW-w64 links them against its wide-character startup.
if(NOT MSVC)
    foreach(target LoaderProxyTests PeerTimeoutTests LogTests RefusalTests PluginLoadTests GameMachine GameNetTests
                   DirectNetTests)
        target_link_options(${target} PRIVATE -municode)
    endforeach()
endif()
