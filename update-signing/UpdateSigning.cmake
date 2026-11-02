# Signed preview updates (UpdateSignature.h), for the native Windows and
# Linux apps: included by windows/CMakeLists.txt and linux/CMakeLists.txt.
#
#   cl_updatesig    the signature check and manifest reader, with Monocypher
#   verify_check    their checks (RFC 8032 vectors and the ways to fail)
#   CL_BUILD_TIME   the commit's committer time (Unix seconds), the app's own
#                   build number: a signed manifest's must be higher

set(CL_UPDATESIG_DIR "${CMAKE_CURRENT_LIST_DIR}")
add_library(cl_updatesig STATIC
	"${CL_UPDATESIG_DIR}/UpdateSignature.cpp"
	"${CL_UPDATESIG_DIR}/monocypher/monocypher.c"
	"${CL_UPDATESIG_DIR}/monocypher/monocypher-ed25519.c")
target_include_directories(cl_updatesig PUBLIC "${CL_UPDATESIG_DIR}")
if(MSVC)
	target_compile_options(cl_updatesig PRIVATE /utf-8 /W3)
endif()

execute_process(COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}/.." log -1 --format=%ct HEAD
                OUTPUT_VARIABLE CL_BUILD_TIME OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE CL_BUILD_TIME_RC ERROR_QUIET)
if(NOT CL_BUILD_TIME_RC EQUAL 0 OR NOT CL_BUILD_TIME MATCHES "^[0-9]+$")
	set(CL_BUILD_TIME 0)
endif()

add_executable(verify_check "${CL_UPDATESIG_DIR}/verify_check.cpp")
target_link_libraries(verify_check PRIVATE cl_updatesig)
