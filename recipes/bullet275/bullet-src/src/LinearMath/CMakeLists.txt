
INCLUDE_DIRECTORIES(
	${BULLET_PHYSICS_SOURCE_DIR}/src }
)

SET(LinearMath_SRCS
	btMmdMath.cpp
	btAlignedAllocator.cpp
		btConvexHull.cpp
		btGeometryUtil.cpp
	btQuickprof.cpp
)

SET(LinearMath_HDRS
	btMmdMath.h
	btAabbUtil2.h
	btAlignedAllocator.h
		btAlignedObjectArray.h
	btConvexHull.h
		btDefaultMotionState.h
	btGeometryUtil.h
		btHashMap.h
	btIDebugDraw.h
	btList.h
	btMatrix3x3.h
		btMinMax.h
	btMotionState.h
	btPoolAllocator.h
	btQuadWord.h
		btQuaternion.h
	btQuickprof.h
	btRandom.h
	btScalar.h
		btStackAlloc.h
		btTransform.h
		btTransformUtil.h
	btVector3.h
)

ADD_LIBRARY(LinearMath ${LinearMath_SRCS} ${LinearMath_HDRS})
SET_TARGET_PROPERTIES(LinearMath PROPERTIES VERSION ${BULLET_VERSION})
SET_TARGET_PROPERTIES(LinearMath PROPERTIES SOVERSION ${BULLET_VERSION})

#FILES_MATCHING requires CMake 2.6
IF (${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} GREATER 2.5)
	IF (APPLE AND BUILD_SHARED_LIBS AND FRAMEWORK)
		INSTALL(TARGETS LinearMath DESTINATION .)
	ELSE (APPLE AND BUILD_SHARED_LIBS AND FRAMEWORK)
	INSTALL(TARGETS LinearMath DESTINATION lib)
	INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} DESTINATION include FILES_MATCHING PATTERN "*.h")
	ENDIF (APPLE AND BUILD_SHARED_LIBS AND FRAMEWORK)
ENDIF (${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} GREATER 2.5)

IF (APPLE AND BUILD_SHARED_LIBS AND FRAMEWORK)
	SET_TARGET_PROPERTIES(LinearMath PROPERTIES FRAMEWORK true)
	SET_TARGET_PROPERTIES(LinearMath PROPERTIES PUBLIC_HEADER "${LinearMath_HDRS}")
ENDIF (APPLE AND BUILD_SHARED_LIBS AND FRAMEWORK)

# MMD's x86 arithmetic requires x87 intermediates and native math intrinsics.
# Compile this TU consistently in every configuration; the rest of Bullet keeps
# its existing compiler settings. /GL- preserves the noinline rounding boundaries.
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_source_files_properties(btMmdMath.cpp PROPERTIES
        COMPILE_OPTIONS "/arch:IA32;/fp:precise;/O2;/Oi;/GL-;/RTC-")
endif()
