cmake_minimum_required(VERSION 3.16)
project(open_cpp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(open_sim STATIC src/move_order.cpp src/character_rules.cpp src/character.cpp)
target_include_directories(open_sim PUBLIC include)
target_compile_options(open_sim PRIVATE -Wall -Wextra -Werror)
set_target_properties(open_sim PROPERTIES POSITION_INDEPENDENT_CODE ON)

enable_testing()
add_executable(open_tests tests/move_order_test.cpp)
target_link_libraries(open_tests PRIVATE open_sim)
target_compile_options(open_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME move_order COMMAND open_tests)

add_executable(character_tests tests/character_test.cpp)
target_link_libraries(character_tests PRIVATE open_sim)
target_compile_options(character_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME character COMMAND character_tests)

add_executable(character_rules_tests tests/character_rules_test.cpp)
target_link_libraries(character_rules_tests PRIVATE open_sim)
target_compile_options(character_rules_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME character_rules COMMAND character_rules_tests)

option(OPEN_BUILD_GDEXTENSION "Build the Godot GDExtension that exposes MoveOrder" ON)
if(OPEN_BUILD_GDEXTENSION)
	include(FetchContent)
	FetchContent_Declare(
		godot_cpp
		GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
		GIT_TAG godot-4.5-stable
	)
	FetchContent_MakeAvailable(godot_cpp)

	add_library(open SHARED
		${CMAKE_CURRENT_SOURCE_DIR}/../extension/move_order_bridge.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../extension/character_bridge.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../extension/register_types.cpp
	)
	target_include_directories(open PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../extension)
	target_link_libraries(open PRIVATE godot-cpp open_sim)
	target_compile_options(open PRIVATE -Wall -Wextra)
	set_target_properties(open PROPERTIES
		PREFIX "lib"
		OUTPUT_NAME "open"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../extension/bin"
	)
endif()
