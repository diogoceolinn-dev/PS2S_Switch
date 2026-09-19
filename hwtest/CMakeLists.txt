cmake_minimum_required(VERSION 3.16)

project(hwtest)

add_executable(${PROJECT_NAME}
	main.c
)

target_link_libraries(${PROJECT_NAME}
	nx
	deko3dd
)

nx_generate_nacp(${PROJECT_NAME}
	NAME "PS2S hwtest"
	AUTHOR "RoxasBR90"
	VERSION "0.1.0"
)

nx_create_nro(${PROJECT_NAME})
