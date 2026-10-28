# The engine with no drawing (CL_HEADLESS), and the checks and fuzzers that run
# on it, on any machine: macOS, Linux, Windows with clang. CI builds it with
# AddressSanitizer + UndefinedBehaviorSanitizer (.github/workflows/core-hardening.yml):
#
#   cmake -S mac/Tools/headless -B build/headless -G Ninja -DCL_SANITIZE=ON
#   cmake --build build/headless
#   ctest --test-dir build/headless --output-on-failure
#
# docs/CORE-HARDENING.md says what each check and fuzzer is for.
cmake_minimum_required(VERSION 3.16)
project(CedarCoreHeadless LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# RelWithDebInfo, but with the asserts left in (it adds -DNDEBUG): CL_NDEBUG
# below takes them out on purpose.
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O1 -g")

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

option(CL_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(CL_LIBFUZZER "Also build fuzz_load_libfuzzer (clang's -fsanitize=fuzzer)" OFF)
# Release builds compile asserts out; the engine must handle bad state without
# them, so the checks are run both ways.
option(CL_NDEBUG "Compile the engine's asserts out, as a release build does" OFF)

if(CL_SANITIZE)
	# -fno-sanitize=vptr as the wx app's own sanitizer build (see the top-level
	# CMakeLists.txt): vptr disagrees with itself across differently built
	# copies of the same std:: template.
	add_compile_options(-fsanitize=address,undefined -fno-sanitize=vptr -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
	add_link_options(-fsanitize=address,undefined -fno-sanitize=vptr)
endif()

# The engine: the same sources as mac/build.sh's CORE list, minus the drawing.
file(GLOB CL_COMMANDS "${ROOT}/src/gui/command/*.cpp")
list(FILTER CL_COMMANDS EXCLUDE REGEX "cmd(AddTab|DeleteTab)\\.cpp$")
file(GLOB CL_LOGIC "${ROOT}/logic/src/*.cpp")
file(GLOB CL_CEDARCORE "${ROOT}/mac/CedarCore/*.cpp")
list(FILTER CL_CEDARCORE EXCLUDE REGEX "CGScene\\.cpp$")
# Classroom and Sync run their own self-tests; nothing the checks here call
# needs them, and Sync's crypto comes from each platform's app.
list(FILTER CL_CEDARCORE EXCLUDE REGEX "/(Classroom|Sync)[^/]*\\.cpp$")

add_library(CedarCoreHeadless STATIC
	${ROOT}/src/gui/guiGate.cpp ${ROOT}/src/gui/guiWire.cpp ${ROOT}/src/gui/wireSegment.cpp
	${ROOT}/src/gui/klsCollisionChecker.cpp ${ROOT}/src/gui/GateLibrary.cpp ${ROOT}/src/gui/LibraryParse.cpp
	${ROOT}/src/gui/XMLParser.cpp ${ROOT}/src/gui/GUICircuitModel.cpp ${ROOT}/src/gui/RenderMode.cpp
	${ROOT}/src/gui/PaletteDrag.cpp ${ROOT}/src/gui/Settings.cpp ${ROOT}/src/gui/gl_defs.cpp
	${ROOT}/src/gui/CircuitParse.cpp ${ROOT}/src/gui/klsClipboard.cpp ${ROOT}/src/gui/CircuitEdits.cpp
	${ROOT}/src/gui/route/TrunkRouter.cpp ${ROOT}/src/gui/route/GridRouter.cpp ${ROOT}/src/gui/route/Layout.cpp
	${CL_COMMANDS}
	${CL_LOGIC}
	${ROOT}/format/circuit_file_io.cpp ${ROOT}/format/ink.cpp ${ROOT}/format/legacy_cdl.cpp ${ROOT}/format/migrate.cpp
	${ROOT}/format/numeric.cpp ${ROOT}/format/sexpr.cpp
	${CL_CEDARCORE}
)
target_compile_definitions(CedarCoreHeadless PUBLIC CL_NO_WX CL_HEADLESS _PRODUCTION_)
if(CL_NDEBUG)
	target_compile_definitions(CedarCoreHeadless PUBLIC NDEBUG)
endif()
target_include_directories(CedarCoreHeadless PUBLIC
	${ROOT}/include ${ROOT}/include/gui ${ROOT}/include/gui/command ${ROOT}/logic/include ${ROOT}/format
	${ROOT}/mac/CedarCore ${ROOT}/mac/CedarCore/include)
target_compile_options(CedarCoreHeadless PRIVATE -Wno-deprecated-declarations -Wno-self-assign-field)
find_package(Threads REQUIRED)
target_link_libraries(CedarCoreHeadless PUBLIC Threads::Threads)

foreach(tool core_check fuzz_edit fuzz_load)
	add_executable(${tool} ${ROOT}/mac/Tools/${tool}.cpp)
	target_link_libraries(${tool} PRIVATE CedarCoreHeadless)
endforeach()

if(CL_LIBFUZZER)
	add_executable(fuzz_load_libfuzzer ${ROOT}/mac/Tools/fuzz_load.cpp)
	target_compile_definitions(fuzz_load_libfuzzer PRIVATE CL_LIBFUZZER)
	target_compile_options(fuzz_load_libfuzzer PRIVATE -fsanitize=fuzzer)
	target_link_options(fuzz_load_libfuzzer PRIVATE -fsanitize=fuzzer)
	target_link_libraries(fuzz_load_libfuzzer PRIVATE CedarCoreHeadless)
endif()

enable_testing()
set(GATEDEFS ${ROOT}/res/cl_gatedefs.xml)
file(GLOB FIXTURES ${ROOT}/format/tests/fixtures/*.cdl)
add_test(NAME core_check COMMAND core_check ${GATEDEFS} ${ROOT}/format/tests/fixtures)
# Short fuzz runs, so a regression the long runs found once stays found. The
# workflow runs longer ones as a step of its own.
add_test(NAME fuzz_edit_short COMMAND fuzz_edit ${GATEDEFS} 1 6 200)
add_test(NAME fuzz_load_short COMMAND fuzz_load ${GATEDEFS} 1 2 300 ${FIXTURES})
set_tests_properties(fuzz_edit_short PROPERTIES
	ENVIRONMENT "CL_FUZZ_BOTH=1;CL_FUZZ_REACH=1;CL_FUZZ_INVARIANT=1;CL_FUZZ_DUP=1;CL_FUZZ_TREE=1")
