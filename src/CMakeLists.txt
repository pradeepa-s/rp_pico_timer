# Add the library
add_library(Application debug_messages.c display_framework.c custom_isr.c touch_screen.c battery_monitor.c)

# Optionally, set library properties
target_include_directories(Application PUBLIC inc)

target_link_libraries(Application PUBLIC lvgl hardware_spi hardware_gpio hardware_adc pico_cyw43_arch_none)