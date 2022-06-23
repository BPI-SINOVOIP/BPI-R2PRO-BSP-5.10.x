# Copyright (C) 2019, Fuzhou Rockchip Electronics Co., Ltd
# Released under the MIT license (see COPYING.MIT for the terms)

PACKAGECONFIG[no-egl] = ""

do_install:append:rockchip() {
	if ${@bb.utils.contains('PACKAGECONFIG', 'no-egl', 'true', 'false', d)}; then
		rm -rf ${D}/${includedir}/wayland-egl*.h
		rm -rf ${D}/${libdir}/libwayland-egl*
		rm -rf ${D}/${libdir}/pkgconfig/wayland-egl*
	fi
}
