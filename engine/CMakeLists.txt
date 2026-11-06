add_library(midiseer_engine STATIC
    src/LengthMap.cpp
    src/Seer.cpp)

target_include_directories(midiseer_engine PUBLIC include)
target_compile_features(midiseer_engine PUBLIC cxx_std_20)

# Plugins are loaded into a host process, so the engine must be position independent.
set_target_properties(midiseer_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
