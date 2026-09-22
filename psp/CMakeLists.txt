# ARGUS — сборка EBOOT.PBP тулчейном pspdev. Запускать через `make psp` (передаёт тулчейн-файл и ассеты).
cmake_minimum_required(VERSION 3.16)
project(argus C)

if(NOT PSP)
    message(FATAL_ERROR "Нужен тулчейн pspdev: cmake -DCMAKE_TOOLCHAIN_FILE=$ENV{PSPDEV}/psp/share/pspdev.cmake (или просто `make psp`)")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(ARGUS_ASSETS "${CMAKE_SOURCE_DIR}/build/assets" CACHE PATH "каталог собранных ассетов (иконки XMB)")

file(GLOB CORE_SRC CONFIGURE_DEPENDS core/*.c)
# Модули, которые ещё не подключены к игре (в работе): исключаем из сборки,
# чтобы EBOOT собирался. Список сокращается по мере интеграции.
set(ARGUS_PENDING "core/(entity|puzzle_beam|puzzle_mech)\\.c$" CACHE STRING "не собираемые пока модули")
if(ARGUS_PENDING)
    list(FILTER CORE_SRC EXCLUDE REGEX "${ARGUS_PENDING}")
endif()
file(GLOB PSP_SRC CONFIGURE_DEPENDS platform/psp/*.c)

add_executable(argus ${CORE_SRC} ${PSP_SRC})
target_include_directories(argus PRIVATE core platform/psp "${ARGUS_ASSETS}")
# В Debug — -O0 и символы: трассы PPSSPP тогда указывают точную строку.
target_compile_options(argus PRIVATE -Wall -Wextra -G0
    $<$<CONFIG:Debug>:-O0>
    $<$<NOT:$<CONFIG:Debug>>:-O2>)
target_compile_definitions(argus PRIVATE $<$<CONFIG:Debug>:ARGUS_DEBUG=1>)
# ВАЖНО: перечисляем только библиотеки приложения. Явные -lc/-lm/-lpspuser/-lpspkernel
# ломают порядок импорт-стабов (psp-fixup-imports: «stubs out of order»), и вызовы sce*
# уходят в мусор. Остальное добавляют specs psp-gcc в правильном порядке.
target_link_libraries(argus PRIVATE pspgum pspgu psputility pspdebug pspdisplay pspge pspctrl)

create_pbp_file(
    TARGET argus
    TITLE "ARGUS"
    ICON_PATH "${ARGUS_ASSETS}/xmb/ICON0.PNG"
    BACKGROUND_PATH "${ARGUS_ASSETS}/xmb/PIC1.PNG"
    VERSION 0.0.1
    MEMSIZE 1
    OUTPUT_DIR "${CMAKE_BINARY_DIR}"
)
