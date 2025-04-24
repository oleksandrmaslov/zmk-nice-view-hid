if(CONFIG_NICE_VIEW_HID)

  if(CONFIG_RAW_HID)
    zephyr_library_sources(src/hid.c)
  endif()

  zephyr_library_sources(src/custom_status_screen.c)
  zephyr_library_sources(src/widgets/bolt.c)
  zephyr_library_sources(src/widgets/util.c)

  if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(src/widgets/status.c)
  else()
    zephyr_library_sources(src/widgets/status.c)
  endif()

  zephyr_include_directories(include)
  zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)

endif()
