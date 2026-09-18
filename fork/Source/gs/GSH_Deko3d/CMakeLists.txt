cmake_minimum_required(VERSION 3.5)

set(CMAKE_MODULE_PATH
	${CMAKE_CURRENT_SOURCE_DIR}/../../../deps/Dependencies/cmake-modules
	${CMAKE_CURRENT_SOURCE_DIR}/../../../deps/Framework/build_cmake
	${CMAKE_MODULE_PATH}
)

include(Header)
include(Framework_Common)

project(GSH_Deko3d)

add_subdirectory(shader)

# Fase 2 (RoxasBR90): GSH_Deko3d_Texture.cpp implementa o cache de texturas
# (o stub comentado no upstream virou implementação real).
# GsCachedArea.cpp vai junto aqui (e não só no PlayCore) porque na ordem de
# link do ui_switch o gsh_deko3d vem depois do PlayCore e o ld de uma
# passada só não resolveria esses símbolos (link order, sem --start-group).
add_library(gsh_deko3d STATIC
	GSH_Deko3d.cpp
	GSH_Deko3d.h
	GSH_Deko3d_Texture.cpp
	GSH_Deko3d_Texture.h
	../GsCachedArea.cpp
	../GsCachedArea.h
)

target_include_directories(gsh_deko3d PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/
	# FIXME
	../../../deps/Framework/include/
	/opt/devkitpro/portlibs/switch/include
)

target_link_libraries(gsh_deko3d
	${gsh_deko3d_PROJECT_LIBS}
	gsh_deko3d_shaders
)
