# CedarLogic for Linux (native): a GTK 3 front end on the same C++ engine the
# wx app and the native Mac app use. A project of its own, apart from the main
# CMake build (which builds the wx app):
#
#   cmake -S linux -B linux/build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build linux/build
#
# Drawing is Cairo on the processor, so nothing here needs OpenGL.
cmake_minimum_required(VERSION 3.16)
project(CedarLogicNative VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo cairo-ft fontconfig freetype2)

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
	CedarCore/CairoScene.cpp
)
target_compile_definitions(CedarCore PUBLIC CL_NO_WX _PRODUCTION_)
target_include_directories(CedarCore PUBLIC
	${ROOT}/include ${ROOT}/include/gui ${ROOT}/include/gui/command ${ROOT}/logic/include ${ROOT}/format
	${ROOT}/mac/CedarCore ${ROOT}/mac/CedarCore/include ${CMAKE_CURRENT_SOURCE_DIR}/CedarCore)
target_compile_options(CedarCore PRIVATE -Wno-deprecated-declarations)
target_link_libraries(CedarCore PUBLIC PkgConfig::CAIRO)

# ---- The app -------------------------------------------------------------------
option(CL_BUILD_APP "Build the GTK app" ON)
if(CL_BUILD_APP)
	pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
	file(GLOB CL_APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/App/*.cpp")
	add_executable(CedarLogic ${CL_APP_SOURCES})
	target_link_libraries(CedarLogic PRIVATE CedarCore PkgConfig::GTK3)
	execute_process(COMMAND git -C "${ROOT}" rev-parse HEAD
	                OUTPUT_VARIABLE CL_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
	                RESULT_VARIABLE CL_GIT_RC ERROR_QUIET)
	if(NOT CL_GIT_RC EQUAL 0 OR CL_GIT_COMMIT STREQUAL "")
		set(CL_GIT_COMMIT "unknown")
	endif()
	target_compile_definitions(CedarLogic PRIVATE CL_VERSION="${PROJECT_VERSION}" CL_GIT_COMMIT="${CL_GIT_COMMIT}")
	set(CL_FEEDBACK_URL "https://cedarlogic.netlify.app" CACHE STRING "Where Send Feedback sends to")
	set(CL_FEEDBACK_KEY "96611596304230a4c3c21e22b1b3e523" CACHE STRING "Sent with feedback so the site knows it's from the app")
	target_compile_definitions(CedarLogic PRIVATE CL_FEEDBACK_URL="${CL_FEEDBACK_URL}" CL_FEEDBACK_KEY="${CL_FEEDBACK_KEY}")
	find_package(Threads REQUIRED)
	target_link_libraries(CedarLogic PRIVATE Threads::Threads)
	target_compile_options(CedarLogic PRIVATE -Wall -Wno-deprecated-declarations -Wno-missing-field-initializers)
	set_target_properties(CedarLogic PROPERTIES OUTPUT_NAME cedarlogic)

	include(GNUInstallDirs)
	install(TARGETS CedarLogic RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(FILES ${ROOT}/res/cl_gatedefs.xml res/cedarlogic.png res/FirstLaunch.wav DESTINATION ${CMAKE_INSTALL_DATADIR}/CedarLogic)
	install(DIRECTORY ${ROOT}/res/help ${ROOT}/res/samples DESTINATION ${CMAKE_INSTALL_DATADIR}/CedarLogic)
	install(FILES res/cedarlogic.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
	install(FILES res/cedarlogic.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
	install(FILES res/cedarlogic-mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
endif()

# ---- The checks (no window needed) -------------------------------------------
option(CL_BUILD_TOOLS "Build the headless check tools" ON)
if(CL_BUILD_TOOLS)
	foreach(t render_png sim_check edit_check save_check tt_check cl_check)
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tools/${t}.cpp")
			add_executable(${t} Tools/${t}.cpp)
		else()
			add_executable(${t} ${ROOT}/mac/Tools/${t}.cpp)
		endif()
		target_link_libraries(${t} PRIVATE CedarCore)
	endforeach()
endif()
