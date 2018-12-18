# CONFIG_FOUND
# CONFIG_LIBRARIES
# CONFIG_LIBRARY_DIRS
# CONFIG_LDFLAGS
# CONFIG_INCLUDE_DIRS
# CONFIG_CFLAGS

if (NOT KS_PLAT_WIN)
	include(FindPkgConfig)

	if (NOT PKG_CONFIG_FOUND)
		message("Failed to locate pkg-config" FATAL)
	endif()

	pkg_check_modules(CONFIG libconfig REQUIRED)

	link_directories(${CONFIG_LIBRARY_DIRS})
else()
	if (NOT TARGET libconfig)
		message(FATAL_ERROR "Failed to locate libconfig target")
	endif()
	set(CONFIG_LIBRARIES "libconfig")

	get_target_property(CONFIG_INCLUDE_DIRS libconfig SOURCE_DIR)
	set(CONFIG_INCLUDE_DIRS ${CONFIG_INCLUDE_DIRS})

	message("CONFIG_INCLUDE_DIRS ${CONFIG_INCLUDE_DIRS}")
	set(CONFIG_CFLAGS -DLIBCONFIG_STATIC=1)
endif()
