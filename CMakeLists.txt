cmake_minimum_required(VERSION 3.16)

project(reflexwall-supervisor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt'ye veya başka bir kütüphaneye bağımlı değil: yalnızca POSIX. Gözetmen
# sürecin kendisi, gözettiği uygulamadan daha az şeye bağlı olmalı.
add_executable(reflexwall-supervisor
    src/main.cpp
    src/logger.cpp
    src/options.cpp
    src/childprocess.cpp
    src/supervisor.cpp
)

target_compile_definitions(reflexwall-supervisor PRIVATE
    RW_SUPERVISOR_VERSION="${PROJECT_VERSION}"
    _GNU_SOURCE
)

target_compile_options(reflexwall-supervisor PRIVATE
    -Wall -Wextra -Wpedantic
)

include(GNUInstallDirs)
install(TARGETS reflexwall-supervisor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
