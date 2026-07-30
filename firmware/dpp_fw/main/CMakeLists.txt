set(requires espressif__esp_tinyusb fatfs esp_netif bt esp_hid driver nvs_flash app_update bootloader_support json)
set(dpp_sources "mypwm.c" "dsb_cache.c" "ui_task.c" "esp_hid_gap.c" "dsb_cache.c" "hid_task.c" "keyboard.c" "profiles.c" "rotary_encoder.c" "keypress_task.c" "ssd1306_fonts.c" "ssd1306.c" "sd_task.c" "main.c" "input_task.c" "neopixel_task.c" "profiles.c" "shared.c" "keyboard.c" "ds_vm.c" "hid_task.c" "bluetooth_task.c" "esp_hid_gap.c")

if(CONFIG_DPP_CODEX_MICRO_COMPAT)
    list(APPEND dpp_sources
        "codex_wire.c"
        "codex_hid.c"
        "codex_rpc.c"
        "codex_lighting.c"
        "codex_controls.c"
        "codex_settings.c"
        "codex_ui.c"
        "codex_mode.c")
endif()

idf_component_register(
    SRCS "${dpp_sources}"
    INCLUDE_DIRS "."
    REQUIRES ${requires}
    )
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=maybe-uninitialized")
