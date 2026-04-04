# SPDX-License-Identifier: GPL-2.0
dtbo-y += samsung/a14/a14_eur_open_w00_r03.dtbo
dtbo-y += samsung/a14/a14_eur_open_w00_r04.dtbo
dtbo-y += samsung/a14/a14_eur_open_w00_r00.dtbo
dtbo-y += samsung/a14/a14_eur_open_w00_r02.dtbo
dtbo-y += samsung/a14/a14_eur_open_w00_r01.dtbo
dtbo-y += exynos/board/erd/s5e3830-erd3830.dtbo
dtb-y += exynos/s5e3830.dtb

targets += dtbs
DTB_LIST  := $(dtb-y) $(dtbo-y)
always-y  := $(DTB_LIST)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := *.dtb*
