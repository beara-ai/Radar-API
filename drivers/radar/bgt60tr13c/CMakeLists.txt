zephyr_library()

zephyr_library_sources(
	bgt60tr13c.c
	bgt60tr13c_platform.c
)

zephyr_library_sources(vendor/xensiv_bgt60trxx.c)
zephyr_include_directories(.)
zephyr_include_directories(vendor)
