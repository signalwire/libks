# CONFIGCPP_FOUND
# CONFIGCPP_LIBRARIES
# CONFIGCPP_LIBRARY_DIRS
# CONFIGCPP_LDFLAGS
# CONFIGCPP_INCLUDE_DIRS
# CONFIGCPP_CMAKE_DIRS
# CONFIGCPP_CFLAGS

if (NOT KS_PLAT_WIN)
	include(FindPkgConfig)

	if (NOT PKG_CONFIG_FOUND)
		message("Failed to locate pkg-config" FATAL)
	endif()

	pkg_check_modules(CONFIGCPP libconfig++ REQUIRED)

	link_directories(${CONFIGCPP_LIBRARY_DIRS})
else()
#	set(NOT TARGET libconfig++)
#		message(FATAL_ERROR "Failed to locate libconfig++ target")
#	endif()
	set(CONFIGCPP_LIBRARIES "libconfig++")

	get_target_property(CONFIGCPP_INCLUDE_DIRS libconfig++ SOURCE_DIR)
	set(CONFIGCPP_INCLUDE_DIRS ${CONFIGCPP_INCLUDE_DIRS})
	set(CONFIGCPP_CFLAGS -DLIBCONFIGXX_STATIC=1)
endif()
