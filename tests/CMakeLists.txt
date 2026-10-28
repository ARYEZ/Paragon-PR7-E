FetchContent_Declare(catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(catch2)

add_executable(ParagonX1Tests
    TestMain.cpp
    ProcessorTests.cpp
    EffectTests.cpp
    RigTests.cpp
    UiTests.cpp
    TunerTests.cpp
    EqTests.cpp
    LimiterTests.cpp
    MasterLimiterTests.cpp
    LimiterPresetTests.cpp
    CompressorTests.cpp
    DominionTests.cpp
    OracleTests.cpp
    MultibandTests.cpp
    PerformanceTests.cpp
    EditorSnapshot.cpp)

# Link the plugin's shared code (processor, editor, JUCE modules) so the
# tests exercise exactly the code that ships in the VST3. JUCE modules are
# linked privately into that target, so borrow its include paths and defines
# (as SYSTEM, so warnings inside JUCE headers don't flood the test build).
target_link_libraries(ParagonX1Tests PRIVATE ParagonX1 Catch2::Catch2)
target_include_directories(ParagonX1Tests SYSTEM PRIVATE $<TARGET_PROPERTY:ParagonX1,INCLUDE_DIRECTORIES>)
target_compile_definitions(ParagonX1Tests PRIVATE
    $<TARGET_PROPERTY:ParagonX1,COMPILE_DEFINITIONS>)

include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(ParagonX1Tests)
