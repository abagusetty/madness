# src/madness/external

add_subdirectory(gtest)
add_subdirectory(muParser)
add_subdirectory(tinyxml)

if(ENABLE_ELEMENTAL)

  # Note: Here we are changing the build configuration of for Elemental since it
  # requires [Hybrid|Pure][Debug|Release] as the build configuratino. This
  # should not be required if we upgrade Elemental to v0.85.
  
  # Store the build configuratin
  set(PUSH_CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE})
  
  # Set the build type used by Elemental. We use Pure (MPI only/no OpenMP) since
  # MADNESS threads do not play nice with Elemental threads.
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_BUILD_TYPE "PureDebug" 
        CACHE STRING "Specifies the build type on single-configuration generators" FORCE)
  else()
    set(CMAKE_BUILD_TYPE "PureRelease" 
        CACHE STRING "Specifies the build type on single-configuration generators" FORCE)
  endif()
  
  # Set the configuration variables used by elemental
  set(MATH_LIBS "${LAPACK_LIBRARIES}")
  if((ENABLE_SPINLOCKS OR NOT ENABLE_NEVER_SPIN) AND NOT CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(HAVE_SPINLOCKS ON CACHE BOOL "Enable if pthread lib supports spinlocks")
  else()
    set(HAVE_SPINLOCKS OFF CACHE BOOL "Enable if pthread lib supports spinlocks")
  endif()

  message(STATUS "Begin Elemental Configure")
  add_subdirectory(elemental)
  message(STATUS "End Elemental Configure")
  set(ELEMENTAL_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/elemental/include
      ${CMAKE_CURRENT_BINARY_DIR}/elemental/include PARENT_SCOPE)
  set(ELEMENTAL_LIBRARIES elemental PARENT_SCOPE)
  
  # Restore the build configuration.
  set(CMAKE_BUILD_TYPE ${PUSH_CMAKE_BUILD_TYPE}
      CACHE STRING "Specifies the build type on single-configuration generators" FORCE) 

  set(MADNESS_HAS_ELEMENTAL 1)
  set(ELEMENTAL_FOUND TRUE)
  
  # Add a component install target for elemental 
  add_custom_target(install-elemental
      COMMAND ${CMAKE_COMMAND} -DCOMPONENT=elemental -P ${CMAKE_BINARY_DIR}/cmake_install.cmake
      COMMENT "Installing elemental library components")
  add_dependencies(install-elemental elemental)
  
  list(APPEND MADNESS_EXPORT_TARGETS elemental pmrrr)
endif()