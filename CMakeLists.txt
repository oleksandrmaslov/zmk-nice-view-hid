if(CONFIG_NICE_VIEW_HID)

  zephyr_library_sources(src/hid.c)
  zephyr_library_sources(src/custom_status_screen.c)
  zephyr_library_sources(src/widgets/bolt.c)
  zephyr_library_sources(src/widgets/util.c)
  zephyr_library_sources(src/widgets/status.c)

  zephyr_include_directories(include)
  zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)

endif()
