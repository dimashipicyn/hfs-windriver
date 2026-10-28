add_executable(test_volume test_volume.c)
target_link_libraries(test_volume PRIVATE hfsplus_core)

add_executable(test_catalog test_catalog.c)
target_link_libraries(test_catalog PRIVATE hfsplus_core)

add_test(NAME test_volume COMMAND test_volume)
add_test(NAME test_catalog COMMAND test_catalog)
