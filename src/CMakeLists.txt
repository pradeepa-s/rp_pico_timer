# Add the library
add_library(Application debug_messages.c display_framework.c)

# Optionally, set library properties
target_include_directories(Application PUBLIC inc)

target_link_libraries(Application PUBLIC lvgl)