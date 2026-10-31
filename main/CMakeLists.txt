idf_component_register(
    SRCS "main.cpp" "board.cpp" "display.cpp" "grokbot_player.cpp" "face_scaler.cpp"
    INCLUDE_DIRS "."
    EMBED_FILES "grokbot.atlas"
    REQUIRES M5GFX m5pm1 m5ioe1 i2c_bus
    PRIV_REQUIRES driver esp_driver_i2c esp_driver_gpio esp_timer
)
