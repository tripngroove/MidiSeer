FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.9.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(engine_tests
    LengthMapTests.cpp
    SeerTests.cpp)

target_link_libraries(engine_tests PRIVATE midiseer_engine Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(engine_tests)

# Runs the real plugin processor like a host would.
if(TARGET MidiSeer)
    add_executable(plugin_smoke_test PluginSmokeTest.cpp)
    target_link_libraries(plugin_smoke_test PRIVATE MidiSeer midiseer_engine Catch2::Catch2WithMain ${CMAKE_DL_LIBS})
    target_include_directories(plugin_smoke_test PRIVATE ${PROJECT_SOURCE_DIR}/plugin)
    # Reuse the plugin's JUCE headers and settings without compiling JUCE a second time.
    target_include_directories(plugin_smoke_test PRIVATE $<TARGET_PROPERTY:MidiSeer,INCLUDE_DIRECTORIES>)
    target_compile_definitions(plugin_smoke_test PRIVATE $<TARGET_PROPERTY:MidiSeer,COMPILE_DEFINITIONS>)
    catch_discover_tests(plugin_smoke_test)
endif()
