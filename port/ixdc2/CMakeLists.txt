if(NOT EXISTS "${IXION_DC2_SOURCE_DIR}/ps2/src/actionchara.cpp")
    message(FATAL_ERROR "IXION_DC2_SOURCE_DIR needs a populated ChronicleTwo checkout")
endif()
add_library(ixdc2_ridepod STATIC ridepod.cpp combat.cpp)
target_include_directories(ixdc2_ridepod PRIVATE ${CMAKE_SOURCE_DIR}/port/include)
target_compile_options(ixdc2_ridepod PRIVATE -ffp-contract=off -ffunction-sections -fdata-sections -Wall)
set_property(SOURCE ridepod.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/helpers.json;${CMAKE_CURRENT_SOURCE_DIR}/check_symbols.py;${IXION_DC2_SOURCE_DIR}/ps2/src/actionchara.cpp;${IXION_DC2_SOURCE_DIR}/ps2/src/dng_effect.cpp")
add_custom_command(TARGET ixdc2_ridepod POST_BUILD
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/check_symbols.py
        --nm ${CMAKE_NM} --archive $<TARGET_FILE:ixdc2_ridepod>
        --donor ${IXION_DC2_SOURCE_DIR} --helpers ${CMAKE_CURRENT_SOURCE_DIR}/helpers.json
    VERBATIM)
target_compile_definitions(dc_options INTERFACE IXION_HAS_DC2_RIDEPOD=1)
target_link_libraries(darkcloud PRIVATE ixdc2_ridepod)
if(TARGET ixion_c1)
    target_link_libraries(ixion_c1 PRIVATE ixdc2_ridepod)
endif()

function(ixdc2_check_binary binary)
    if(WIN32)
        set(module_arg "")
        if(binary STREQUAL "ixion_c1")
            set(module_arg --module)
        endif()
        # Include the executable/module-specific objects as well as the shared
        # engine objects. The donor archive itself is intentionally excluded.
        set(entry_objects "${CMAKE_BINARY_DIR}/port/CMakeFiles/${binary}.dir/src/main.cpp.obj\n")
        if(binary STREQUAL "ixion_c1")
            string(APPEND entry_objects "${CMAKE_BINARY_DIR}/port/CMakeFiles/${binary}.dir/src/ixion_plugin.cpp.obj\n")
        else()
            string(APPEND entry_objects "${CMAKE_BINARY_DIR}/port/CMakeFiles/${binary}.dir/icon/chronicle.rc.obj\n")
        endif()
        file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/ixdc2-host-${binary}.txt
            CONTENT "$<JOIN:$<TARGET_OBJECTS:dc_port>,\n>\n$<JOIN:$<TARGET_OBJECTS:dc_host>,\n>\n$<JOIN:$<TARGET_OBJECTS:dc_ps2>,\n>\n${entry_objects}")
        add_custom_command(TARGET ${binary} POST_BUILD
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/check_symbols.py
                --nm ${CMAKE_NM} --archive $<TARGET_FILE:ixdc2_ridepod>
                --binary $<TARGET_FILE:${binary}> ${module_arg}
                --host-objects ${CMAKE_BINARY_DIR}/ixdc2-host-${binary}.txt
            VERBATIM)
    endif()
endfunction()

if(DC_BUILD_TESTS)
    add_executable(ixdc2_monster_tests ../src/tests/main.cpp ../src/tests/ixion_dc2_monster_test.cpp)
    dc_link_game(ixdc2_monster_tests)
    target_link_libraries(ixdc2_monster_tests PRIVATE ixdc2_ridepod GTest::gtest nlohmann_json::nlohmann_json)
    set_target_properties(ixdc2_monster_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    add_executable(ixdc2_ridepod_tests ridepod_test.cpp)
    target_include_directories(ixdc2_ridepod_tests PRIVATE ${CMAKE_SOURCE_DIR}/port/include)
    target_link_libraries(ixdc2_ridepod_tests PRIVATE ixdc2_ridepod GTest::gtest_main)
    set_target_properties(ixdc2_ridepod_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    gtest_discover_tests(ixdc2_ridepod_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 5)
    # Compile just the collision test file against the actual DC1 objects. Run
    # only the added Ridepod case, not the existing first-person cases/suite.
    add_executable(ixdc2_dc1_collision_tests ../src/tests/main.cpp ../src/tests/first_person_walk_test.cpp)
    dc_link_game(ixdc2_dc1_collision_tests)
    target_link_libraries(ixdc2_dc1_collision_tests PRIVATE ixdc2_ridepod GTest::gtest nlohmann_json::nlohmann_json)
    set_target_properties(ixdc2_dc1_collision_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    gtest_discover_tests(ixdc2_dc1_collision_tests DISCOVERY_MODE PRE_TEST
        TEST_FILTER FirstPersonWalk.Ridepod* PROPERTIES TIMEOUT 5)
    target_link_libraries(darkcloud_tests PRIVATE ixdc2_ridepod)
    # The random-floor tool shares dc_port objects, including the donor bridge.
    # Keep it linkable when both integration lanes are enabled.
    if(TARGET randfloor_dump)
        target_link_libraries(randfloor_dump PRIVATE ixdc2_ridepod)
    endif()
endif()
