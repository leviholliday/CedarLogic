# CedarLogic for Windows (native): plain Win32 and Direct2D on the same C++
# engine the Mac and Linux apps use. A project of its own, apart from the main
# CMake build (which builds the wx app):
#
#   cmake -S windows -B windows/build -A x64
#   cmake --build windows/build --config Release
#
# Everything it needs comes with Windows (Direct2D, DirectWrite, WIC, the
# common controls): no wxWidgets, no Skia, nothing to download.
cmake_minimum_required(VERSION 3.16)
project(CedarLogicWindows VERSION 0.1.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()
# One self-contained exe: the C++ runtime inside it, so there's no Visual C++
# Redistributable to install first.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# ---- The engine --------------------------------------------------------------
# The model, drawing, routing and loading/saving, shared with the wx app; the
# editing commands (minus the two that manage wx tabs); the logic engine; the
# file format library; and CedarCore, the plain C interface the apps call.
file(GLOB CL_COMMANDS "${ROOT}/src/gui/command/*.cpp")
list(FILTER CL_COMMANDS EXCLUDE REGEX "cmd(AddTab|DeleteTab)\\.cpp$")
file(GLOB CL_LOGIC "${ROOT}/logic/src/*.cpp")
file(GLOB CL_CEDARCORE "${ROOT}/mac/CedarCore/*.cpp")
list(FILTER CL_CEDARCORE EXCLUDE REGEX "CGScene\\.cpp$")

add_library(CedarCore STATIC
	${ROOT}/src/gui/guiGate.cpp ${ROOT}/src/gui/guiWire.cpp ${ROOT}/src/gui/wireSegment.cpp
	${ROOT}/src/gui/klsCollisionChecker.cpp ${ROOT}/src/gui/GateLibrary.cpp ${ROOT}/src/gui/LibraryParse.cpp
	${ROOT}/src/gui/XMLParser.cpp ${ROOT}/src/gui/GUICircuitModel.cpp ${ROOT}/src/gui/RenderMode.cpp
	${ROOT}/src/gui/PaletteDrag.cpp ${ROOT}/src/gui/Settings.cpp ${ROOT}/src/gui/gl_defs.cpp
	${ROOT}/src/gui/CircuitParse.cpp ${ROOT}/src/gui/klsClipboard.cpp ${ROOT}/src/gui/CircuitEdits.cpp
	${ROOT}/src/gui/route/TrunkRouter.cpp ${ROOT}/src/gui/route/GridRouter.cpp ${ROOT}/src/gui/route/Layout.cpp
	${CL_COMMANDS}
	${CL_LOGIC}
	${ROOT}/format/circuit_file_io.cpp ${ROOT}/format/legacy_cdl.cpp ${ROOT}/format/migrate.cpp
	${ROOT}/format/numeric.cpp ${ROOT}/format/sexpr.cpp
	${CL_CEDARCORE}
	CedarCore/D2DScene.cpp
)
target_compile_definitions(CedarCore PUBLIC CL_NO_WX _PRODUCTION_ UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
	_WIN32_WINNT=0x0A00 WINVER=0x0A00 _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
target_include_directories(CedarCore PUBLIC
	${ROOT}/include ${ROOT}/include/gui ${ROOT}/include/gui/command ${ROOT}/logic/include ${ROOT}/format
	${ROOT}/mac/CedarCore ${ROOT}/mac/CedarCore/include ${CMAKE_CURRENT_SOURCE_DIR}/CedarCore)
target_link_libraries(CedarCore PUBLIC d2d1 dwrite)
if(MSVC)
	# Sources are UTF-8; /bigobj for the long command files; big stacks are
	# normal in the router.
	target_compile_options(CedarCore PUBLIC /utf-8 /bigobj /EHsc /W3 /wd4244 /wd4267 /wd4305 /wd4996 /wd4018 /wd4101)
else()
	target_compile_options(CedarCore PRIVATE -Wno-deprecated-declarations)
endif()

# ---- The app -------------------------------------------------------------------
option(CL_BUILD_APP "Build the app" ON)
if(CL_BUILD_APP)
	file(GLOB CL_APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/App/*.cpp")
	set(CL_ICON "${ROOT}/res/icon.ico")
	set(CL_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/res/CedarLogic.manifest")
	configure_file(res/CedarLogic.rc.in "${CMAKE_CURRENT_BINARY_DIR}/CedarLogic.rc" @ONLY)
	add_executable(CedarLogic WIN32 ${CL_APP_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/CedarLogic.rc")
	target_include_directories(CedarLogic PRIVATE App)
	target_link_libraries(CedarLogic PRIVATE CedarCore windowscodecs ole32 comctl32 comdlg32 shell32 dwmapi uuid gdi32 user32)
	execute_process(COMMAND git -C "${ROOT}" rev-parse HEAD
	                OUTPUT_VARIABLE CL_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
	                RESULT_VARIABLE CL_GIT_RC ERROR_QUIET)
	if(NOT CL_GIT_RC EQUAL 0 OR CL_GIT_COMMIT STREQUAL "")
		set(CL_GIT_COMMIT "unknown")
	endif()
	target_compile_definitions(CedarLogic PRIVATE CL_VERSION="${PROJECT_VERSION}" CL_GIT_COMMIT="${CL_GIT_COMMIT}")
	if(MSVC)
		# The manifest is in the resources (res/CedarLogic.manifest).
		target_link_options(CedarLogic PRIVATE /MANIFEST:NO)
	else()
		target_link_options(CedarLogic PRIVATE -municode -static)
	endif()
endif()

# ---- The engine's checks -----------------------------------------------------------
# The Mac app's headless checks (mac/Tools), drawing with Direct2D here.
option(CL_BUILD_TOOLS "Build the engine's checks" ON)
if(CL_BUILD_TOOLS)
	foreach(tool render_png sim_check edit_check save_check tt_check cl_check part_check)
		add_executable(${tool} ${ROOT}/mac/Tools/${tool}.cpp)
		target_include_directories(${tool} PRIVATE ${ROOT}/mac/Tools)
		target_link_libraries(${tool} PRIVATE CedarCore windowscodecs ole32)
		if(NOT MSVC)
			target_link_options(${tool} PRIVATE -static)
		endif()
	endforeach()
endif()
