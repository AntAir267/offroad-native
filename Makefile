# Off-Road native: build the runtime and the C generated from the user's own OffRoad.exe.
#   make GAME=<folder with OffRoad.exe>      translate + build build/offroad
# Python with Capstone (and Unicorn, for the function check): the repository's .venv if there is one
PY ?= $(firstword $(wildcard .venv/bin/python) python3)
GAME ?= $(HOME)/.local/share/offroad-native/game
BUILD_CFG ?= builds/orr_zoom.py
# Parallel compiles: each takes ~300 MB; keep it modest so the machine stays usable.
JOBS ?= 8
MAKEFLAGS += -j$(JOBS)
CC = gcc
SDL3 ?= third_party/sdl3
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libswscale libswresample libavutil 2>/dev/null)
FFMPEG_LIBS := $(shell pkg-config --libs libavformat libavcodec libswscale libswresample libavutil 2>/dev/null)
CFLAGS_COMMON = -std=gnu11 -g -fno-strict-aliasing -frounding-math -fno-omit-frame-pointer -Iruntime -Igen -I$(SDL3)/include
CFLAGS_GEN = $(CFLAGS_COMMON) -O3 -fno-semantic-interposition -w -DCPU_FAST_MEM
CFLAGS_RT = $(CFLAGS_COMMON) -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation $(FFMPEG_CFLAGS)
LDFLAGS = -rdynamic $(SDL3)/lib/libSDL3.a $(FFMPEG_LIBS) -pthread -lm -ldl -lz -lvulkan

RT_SRC = $(wildcard runtime/*.c)
RT_OBJ = $(patsubst runtime/%.c,build/obj/rt_%.o,$(RT_SRC))
# The code files recomp.py wrote, listed in gen/files.mk once it has run (a fresh tree has none
# yet: make writes the list after translating, then reads it again).
-include gen/files.mk
GEN_SRC = $(GEN_CODE) gen/table.c gen/stubs.c
GEN_OBJ = $(patsubst gen/%.c,build/obj/gen_%.o,$(GEN_SRC))

all: build/offroad

gen/funcs.h: $(filter-out tools/profile.py tools/fsr2_shaders.py,$(wildcard tools/*.py)) $(BUILD_CFG)
	$(PY) tools/recomp.py $(GAME)/OffRoad.exe $(BUILD_CFG) gen
gen/table.c gen/stubs.c: gen/funcs.h ;
gen/files.mk: gen/funcs.h
	@echo 'GEN_CODE = $(sort $(wildcard gen/code_*.c))' > $@

build/obj/gen_%.o: gen/%.c gen/funcs.h runtime/cpu.h | build/obj
	$(CC) $(CFLAGS_GEN) -c $< -o $@

build/obj/rt_%.o: runtime/%.c runtime/*.h gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -c $< -o $@

# Shaders: SPIR-V headers are generated with glslang and kept in the repository, so building
# needs glslang only after a shader changed.
SHADERS = $(wildcard runtime/shaders/*.vert runtime/shaders/*.frag)
SPV_H = $(addsuffix .spv.h,$(SHADERS))
GLSLANG ?= glslangValidator
ifneq ($(shell command -v $(GLSLANG) 2>/dev/null),)
runtime/shaders/%.spv.h: runtime/shaders/%
	$(GLSLANG) --target-env vulkan1.3 --vn spv_$(subst .,_,$(notdir $<)) -o $@ $<
endif
# draw.frag with rays for the car lights' shadows, on the devices that have them
runtime/shaders/draw.frag.rays.spv.h: runtime/shaders/draw.frag
	$(GLSLANG) -DLIGHT_RAYS --target-env vulkan1.3 --vn spv_draw_frag_rays -S frag -o $@ $<
SPV_H += runtime/shaders/draw.frag.rays.spv.h
build/obj/rt_vk.o: $(SPV_H)
shaders: $(SPV_H)

# FSR 2 (third_party/FidelityFX-FSR2, fetched locally, MIT licence; vk.c's temporal anti-aliasing):
# its API and Vulkan backend (C++, as AMD ships them, with runtime/fsr2_compat.h for Linux), its
# shaders compiled into the headers the backend includes (tools/fsr2_shaders.py; AMD's Windows
# compiler is never run), and runtime/fsr2.cpp around them. Without the source, Anti-Aliasing
# has no FSR 2.
FSR2 ?= third_party/FidelityFX-FSR2/src/ffx-fsr2-api
ifneq ($(wildcard $(FSR2)/ffx_fsr2.h),)
FSR2_PERM = build/fsr2/ffx_fsr2_accumulate_pass_permutations.h
FSR2_OBJ = build/obj/fsr2_api.o build/obj/fsr2_assert.o build/obj/fsr2_vk.o build/obj/fsr2_shaders_vk.o build/obj/rt_fsr2.o
CXXFLAGS_FSR2 = -std=c++17 -O2 -g -DFFX_GCC -include runtime/fsr2_compat.h -I$(FSR2) -Ibuild/fsr2 -Iruntime -w
CFLAGS_RT += -DHAVE_FSR2
LDFLAGS += -lstdc++
EXTRA_OBJ += $(FSR2_OBJ)
$(FSR2_PERM): tools/fsr2_shaders.py | build/obj
	$(PY) tools/fsr2_shaders.py $(FSR2) build/fsr2 $(GLSLANG)
build/obj/fsr2_api.o: $(FSR2)/ffx_fsr2.cpp runtime/fsr2_compat.h | build/obj
	$(CXX) $(CXXFLAGS_FSR2) -c $< -o $@
build/obj/fsr2_assert.o: $(FSR2)/ffx_assert.cpp runtime/fsr2_compat.h | build/obj
	$(CXX) $(CXXFLAGS_FSR2) -c $< -o $@
# (its memory types through fsr2.cpp: it would take AMD's device-coherent ones, which need a feature)
build/obj/fsr2_vk.o: $(FSR2)/vk/ffx_fsr2_vk.cpp runtime/fsr2_compat.h | build/obj
	$(CXX) $(CXXFLAGS_FSR2) -DvkGetPhysicalDeviceMemoryProperties=fsr2_memory_properties -c $< -o $@
build/obj/fsr2_shaders_vk.o: $(FSR2)/vk/shaders/ffx_fsr2_shaders_vk.cpp $(FSR2_PERM) runtime/fsr2_compat.h | build/obj
	$(CXX) $(CXXFLAGS_FSR2) -c $< -o $@
build/obj/rt_fsr2.o: runtime/fsr2.cpp runtime/fsr2.h runtime/fsr2_compat.h | build/obj
	$(CXX) $(CXXFLAGS_FSR2) -c $< -o $@
endif

build/offroad: $(GEN_OBJ) $(RT_OBJ) $(EXTRA_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

build/obj:
	mkdir -p $@

# make install: the game in ~/.local/opt/offroad-native with an applications-menu entry and the
# game's own icon (taken from the user's OffRoad.exe)
PREFIX ?= $(HOME)/.local/opt/offroad-native
APPS ?= $(HOME)/.local/share/applications
install: all
	mkdir -p $(PREFIX) $(APPS)
	cp build/offroad $(PREFIX)/offroad.new && mv -f $(PREFIX)/offroad.new $(PREFIX)/offroad
	-$(PY) tools/icon.py $(GAME)/OffRoad.exe $(PREFIX)/offroad.png
	printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=Off-Road: Redneck Racing' \
		'Comment=Off-Road recompiled for Linux, on Vulkan, with controller support' \
		'Exec=$(PREFIX)/offroad' 'Icon=$(PREFIX)/offroad.png' 'Categories=Game;' \
		'StartupWMClass=offroad-native' 'Terminal=false' > $(APPS)/offroad-native.desktop
	@echo "installed in $(PREFIX); the applications menu has Off-Road"

# Function-level check against the original code in Unicorn (tests/fncheck.c)
UNICORN ?= $(firstword $(wildcard .venv/lib/python3*/site-packages/unicorn))
build/obj/rt_main_nomain.o: runtime/main.c runtime/*.h gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -DRUNTIME_NO_MAIN -c $< -o $@

build/fncheck: tests/fncheck.c $(filter-out build/obj/rt_main.o,$(RT_OBJ)) build/obj/rt_main_nomain.o $(GEN_OBJ) build/obj/gen_x87_special.o
	$(CC) $(CFLAGS_RT) -I$(UNICORN)/include -o $@ $^ $(UNICORN)/lib/libunicorn.a $(LDFLAGS) -lrt

# Reference build: the original code in Unicorn on the same runtime (ref/ref_cpu.c)
build/obj/ref_cpu.o: ref/ref_cpu.c runtime/*.h gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -I$(UNICORN)/include -c $< -o $@

build/offroad_ref: $(RT_OBJ) $(GEN_OBJ) build/obj/gen_x87_special.o build/obj/ref_cpu.o
	$(CC) -o $@ $^ $(UNICORN)/lib/libunicorn.a $(LDFLAGS) -lrt

check: build/fncheck
	build/fncheck --game $(GAME)

clean:
	rm -rf build gen

.PHONY: all clean shaders check install
