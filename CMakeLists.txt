cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

target_sources(app PRIVATE src/http.c src/net.c src/shell.c src/sensor.c
                           src/storage.c src/timer.c)
