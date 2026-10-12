# ============================================================
#  02-GasDetector —— 便携式气体检测仪（LVGL 8.2 + STM32F103RC）
#  由 01-LGVL_sourceCode 的 Makefile 派生，编译链与链接规则保持一致
#  用法：build.bat           增量构建
#        build.bat clean     清理 build\
#        build.bat DEBUG=1   带调试信息
#  注意：不要给 make 加 -j（armcc 嵌入式汇编用固定临时文件名，会互相覆盖）
# ============================================================

# ---------------- 工具链（Keil MDK ARMCC v5） ----------------
KEIL    := D:/apps/keil5MDK
ARMBIN  := $(KEIL)/ARM/ARMCC/bin
CC      := $(ARMBIN)/armcc.exe
AS      := $(ARMBIN)/armasm.exe
LD      := $(ARMBIN)/armlink.exe
ELF     := $(ARMBIN)/fromelf.exe

# ---------------- 工程路径 ----------------
ROOT    := D:/Workspace/LVGL/02-GasDetector
BUILD   := $(ROOT)/build
OBJDIR  := $(BUILD)/obj
TARGET  := $(BUILD)/GasDetector

# ---------------- 编译选项 ----------------
DEFS    := -DSTM32F10X_HD -DUSE_STDPERIPH_DRIVER

INCS    := -I$(ROOT)/USER \
           -I$(ROOT)/CORE \
           -I$(ROOT)/SYSTEM/delay \
           -I$(ROOT)/SYSTEM/sys \
           -I$(ROOT)/SYSTEM/usart \
           -I$(ROOT)/HARDWARE/LED \
           -I$(ROOT)/HARDWARE/LCD \
           -I$(ROOT)/HARDWARE/KEY \
           -I$(ROOT)/HARDWARE/IIC \
           -I$(ROOT)/HARDWARE/24CXX \
           -I$(ROOT)/HARDWARE/TOUCH \
           -I$(ROOT)/HARDWARE/TIMER \
           -I$(ROOT)/HARDWARE/GAS \
           -I$(ROOT)/HARDWARE/DHT11 \
           -I$(ROOT)/HARDWARE/BATT \
           -I$(ROOT)/HARDWARE/EEPROM \
           -I$(ROOT)/HARDWARE/APP \
           -I$(ROOT)/STM32F10x_FWLib/inc \
           -I$(ROOT)/LVGL/GUI \
           -I$(ROOT)/LVGL/GUI/lvgl \
           -I$(ROOT)/LVGL/GUI/lvgl/src \
           -I$(ROOT)/LVGL/GUI/lvgl/examples/porting

# DEBUG=1 时加入调试信息(-g)：断点可对应源码行、可查看变量，
# 但会阻止链接器消除未引用代码，镜像约从 190KB 涨到 237KB。
# 用法:  build.bat DEBUG=1   （或 make DEBUG=1）
ifeq ($(DEBUG),1)
DBGFLAG := -g
endif

CFLAGS  := --cpu Cortex-M3 --c99 -c --split_sections $(DBGFLAG)
AFLAGS  := --cpu Cortex-M3 $(DBGFLAG)
# 分散加载文件：Keil 工程里 ScatterFile 为空时由 uVision 自动生成，
# 内容与 OBJ/GasDetector.sct 完全相同（RESET 段 +First、InRoot$$Sections、RW/ZI 放 0x20000000）
LFLAGS  := --cpu Cortex-M3 --scatter $(ROOT)/OBJ/GasDetector.sct \
           --strict --info sizes --info totals

# ---------------- 源文件（来自 Keil 工程清单） ----------------
SRCS_C := \
  $(ROOT)/USER/main.c \
  $(ROOT)/USER/stm32f10x_it.c \
  $(ROOT)/USER/system_stm32f10x.c \
  $(ROOT)/HARDWARE/LED/led.c \
  $(ROOT)/HARDWARE/LCD/lcd.c \
  $(ROOT)/HARDWARE/KEY/key.c \
  $(ROOT)/HARDWARE/IIC/myiic.c \
  $(ROOT)/HARDWARE/24CXX/24cxx.c \
  $(ROOT)/HARDWARE/TOUCH/ctiic.c \
  $(ROOT)/HARDWARE/TOUCH/ott2001a.c \
  $(ROOT)/HARDWARE/TOUCH/touch.c \
  $(ROOT)/HARDWARE/TOUCH/gt9147.c \
  $(ROOT)/HARDWARE/TOUCH/ft5206.c \
  $(ROOT)/HARDWARE/TIMER/timer.c \
  $(ROOT)/HARDWARE/GAS/adc1.c \
  $(ROOT)/HARDWARE/GAS/gas_mq7.c \
  $(ROOT)/HARDWARE/DHT11/dht11.c \
  $(ROOT)/HARDWARE/BATT/batt.c \
  $(ROOT)/HARDWARE/EEPROM/eeprom.c \
  $(ROOT)/HARDWARE/APP/gas_app.c \
  $(ROOT)/HARDWARE/APP/ui_gas.c \
  $(ROOT)/SYSTEM/delay/delay.c \
  $(ROOT)/SYSTEM/sys/sys.c \
  $(ROOT)/SYSTEM/usart/usart.c \
  $(ROOT)/CORE/core_cm3.c \
  $(ROOT)/STM32F10x_FWLib/src/stm32f10x_gpio.c \
  $(ROOT)/STM32F10x_FWLib/src/stm32f10x_rcc.c \
  $(ROOT)/STM32F10x_FWLib/src/stm32f10x_usart.c \
  $(ROOT)/STM32F10x_FWLib/src/misc.c \
  $(ROOT)/STM32F10x_FWLib/src/stm32f10x_tim.c \
  $(ROOT)/STM32F10x_FWLib/src/stm32f10x_adc.c \
  $(ROOT)/LVGL/GUI/lvgl/examples/porting/lv_port_disp_template.c \
  $(ROOT)/LVGL/GUI/lvgl/examples/porting/lv_port_indev_template.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_disp.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_event.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_group.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_indev.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_indev_scroll.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_obj.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_obj_class.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_obj_draw.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_obj_pos.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_obj_scroll.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_obj_style.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_obj_style_gen.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_obj_tree.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_refr.c \
  $(ROOT)/LVGL/GUI/lvgl/src/core/lv_theme.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_arc.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_blend.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_dither.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_gradient.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_img.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_letter.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_line.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_polygon.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sw/lv_draw_sw_rect.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/lv_draw.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/lv_draw_arc.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/lv_draw_img.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/lv_draw_label.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/lv_draw_line.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/lv_draw_mask.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/lv_draw_rect.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/lv_draw_triangle.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/lv_img_buf.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/lv_img_cache.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/lv_img_decoder.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/layouts/flex/lv_flex.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/layouts/grid/lv_grid.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/bmp/lv_bmp.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/ffmpeg/lv_ffmpeg.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/freetype/lv_freetype.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/fsdrv/lv_fs_fatfs.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/fsdrv/lv_fs_posix.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/fsdrv/lv_fs_stdio.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/fsdrv/lv_fs_win32.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/gif/gifdec.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/gif/lv_gif.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/png/lodepng.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/png/lv_png.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/qrcode/lv_qrcode.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/qrcode/qrcodegen.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/rlottie/lv_rlottie.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/sjpg/lv_sjpg.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/libs/sjpg/tjpgd.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/others/gridnav/lv_gridnav.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/others/monkey/lv_monkey.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/others/snapshot/lv_snapshot.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/themes/basic/lv_theme_basic.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/themes/default/lv_theme_default.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/themes/mono/lv_theme_mono.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/animimg/lv_animimg.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/calendar/lv_calendar.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/calendar/lv_calendar_header_arrow.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/calendar/lv_calendar_header_dropdown.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/chart/lv_chart.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/colorwheel/lv_colorwheel.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/imgbtn/lv_imgbtn.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/keyboard/lv_keyboard.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/led/lv_led.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/list/lv_list.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/menu/lv_menu.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/meter/lv_meter.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/span/lv_span.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/spinbox/lv_spinbox.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/spinner/lv_spinner.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/tabview/lv_tabview.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/tileview/lv_tileview.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/win/lv_win.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/lv_extra.c \
  $(ROOT)/LVGL/GUI/lvgl/src/extra/widgets/msgbox/lv_msgbox.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_dejavu_16_persian_hebrew.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_fmt_txt.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_loader.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_8.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_10.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_12.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_12_subpx.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_14.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_16.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_18.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_20.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_22.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_24.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_26.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_28.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_28_compressed.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_30.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_32.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_34.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_36.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_38.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_40.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_42.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_44.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_46.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_montserrat_48.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_simsun_16_cjk.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_unscii_8.c \
  $(ROOT)/LVGL/GUI/lvgl/src/font/lv_font_unscii_16.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/stm32_dma2d/lv_gpu_stm32_dma2d.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_arc.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_bg.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_composite.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_img.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_label.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_line.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_mask.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_polygon.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_rect.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_stack_blur.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_texture_cache.c \
  $(ROOT)/LVGL/GUI/lvgl/src/draw/sdl/lv_draw_sdl_utils.c \
  $(ROOT)/LVGL/GUI/lvgl/src/hal/lv_hal_indev.c \
  $(ROOT)/LVGL/GUI/lvgl/src/hal/lv_hal_tick.c \
  $(ROOT)/LVGL/GUI/lvgl/src/hal/lv_hal_disp.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_anim.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_anim_timeline.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_area.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_async.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_bidi.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_color.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_fs.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_gc.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_ll.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_log.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_lru.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_math.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_mem.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_printf.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_style.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_style_gen.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_templ.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_timer.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_tlsf.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_txt.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_txt_ap.c \
  $(ROOT)/LVGL/GUI/lvgl/src/misc/lv_utils.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_arc.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_bar.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_btn.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_btnmatrix.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_canvas.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_checkbox.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_dropdown.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_img.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_label.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_line.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_objx_templ.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_roller.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_slider.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_switch.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_table.c \
  $(ROOT)/LVGL/GUI/lvgl/src/widgets/lv_textarea.c

SRCS_S := $(ROOT)/CORE/startup_stm32f10x_hd.s

# ---------------- 目标文件 ----------------
# 链接顺序与 Keil 一致：startup 必须放第一个（承载 RESET 向量表）
OBJS := $(patsubst $(ROOT)/%.s,$(OBJDIR)/%.o,$(SRCS_S)) \
        $(patsubst $(ROOT)/%.c,$(OBJDIR)/%.o,$(SRCS_C))

OBJDIRS := $(sort $(dir $(OBJS)))

# ---------------- 目录准备（cmd.exe 只认反斜杠，故做替换） ----------------
OBJDIRS_WIN := $(subst /,\,$(OBJDIRS))
SENTINEL    := $(BUILD)/.dirs

# 注意：armcc 的嵌入式汇编用固定临时文件名，并行(-j)会互相覆盖导致
# "#564: cannot open embedded assembler output file" 错误，因此不要加 -j。
.PHONY: all clean size

all: $(TARGET).hex

$(SENTINEL):
	@if not exist "$(subst /,\,$(BUILD))" mkdir "$(subst /,\,$(BUILD))"
	@$(foreach d,$(OBJDIRS_WIN),if not exist "$(d)" mkdir "$(d)" &)
	@echo done > $(SENTINEL)

$(TARGET).hex: $(TARGET).axf
	$(ELF) --i32combined --output $@ $<

$(TARGET).axf: $(OBJS) | $(SENTINEL)
	@echo "Linking $@ ..."
	$(LD) $(LFLAGS) -o $@ $(OBJS)

$(OBJDIR)/%.o: $(ROOT)/%.c | $(SENTINEL)
	$(CC) $(CFLAGS) $(DEFS) $(INCS) -o $@ $<

$(OBJDIR)/%.o: $(ROOT)/%.s | $(SENTINEL)
	$(AS) $(AFLAGS) $(INCS) -o $@ $<

size: $(TARGET).axf
	$(ELF) --text -c $<

clean:
	@if exist "$(subst /,\,$(BUILD))" rmdir /S /Q "$(subst /,\,$(BUILD))"
	@echo "cleaned $(BUILD)"
