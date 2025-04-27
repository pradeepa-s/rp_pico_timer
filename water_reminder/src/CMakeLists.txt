# Add the library
add_library(Application debug_messages.c)

# Optionally, set library properties
target_include_directories(Application PUBLIC inc)