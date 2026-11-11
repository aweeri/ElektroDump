# ElektroDump — plain GNU Make build (no CMake).
#
# Native (WSL Debian / Raspberry Pi):
#   make                     # release -> build/elektrodump
#   make debug               # -O0 -g -fsanitize=address,undefined
#   make test                # build + run the self-tests
#   make clean
#   make config              # print the effective flags + detected deps
#
# Cross-compile for a 64-bit Pi:
#   make CROSS=aarch64-linux-gnu- PI=1
#
# Deploy (M10):
#   make deploy-pi PI_HOST=pi@elektro.local
#
# Optional dependencies (librtlsdr, libairspy, FFTW3f, libmicrohttpd) are
# detected with pkg-config. Missing dev libraries simply disable that backend
# (the mock/file SDR backends and the self-contained FFT keep the build
# working), so the project always builds.

APP        := elektrodump
BUILD_DIR  ?= build
GEN_DIR    := $(BUILD_DIR)/gen
SRC_DIR    := src
TEST_DIR   := tests
SHARE_DIR  := /usr/local/share/$(APP)

# --- toolchain -------------------------------------------------------------
CROSS      ?=
CXX        := $(CROSS)g++
CC         := $(CROSS)gcc
PKG_CONFIG ?= pkg-config
NODE       ?= node

# --- optional dependencies -------------------------------------------------
RTLSDR_CFLAGS := $(shell $(PKG_CONFIG) --cflags librtlsdr 2>/dev/null)
RTLSDR_LIBS   := $(shell $(PKG_CONFIG) --libs librtlsdr 2>/dev/null)
AIRSPY_CFLAGS := $(shell $(PKG_CONFIG) --cflags libairspy 2>/dev/null)
AIRSPY_LIBS   := $(shell $(PKG_CONFIG) --libs libairspy 2>/dev/null)
FFTW_CFLAGS   := $(shell $(PKG_CONFIG) --cflags fftw3f 2>/dev/null)
FFTW_LIBS     := $(shell $(PKG_CONFIG) --libs fftw3f 2>/dev/null)
MHD_CFLAGS    := $(shell $(PKG_CONFIG) --cflags libmicrohttpd 2>/dev/null)
MHD_LIBS      := $(shell $(PKG_CONFIG) --libs libmicrohttpd 2>/dev/null)
PNG_CFLAGS    := $(shell $(PKG_CONFIG) --cflags libpng 2>/dev/null)
PNG_LIBS      := $(shell $(PKG_CONFIG) --libs libpng 2>/dev/null)
WEBP_CFLAGS   := $(shell $(PKG_CONFIG) --cflags libwebp 2>/dev/null)
WEBP_LIBS     := $(shell $(PKG_CONFIG) --libs libwebp 2>/dev/null)
CURL_CFLAGS   := $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null)
CURL_LIBS     := $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null)

HAVE_RTLSDR := $(shell $(PKG_CONFIG) --exists librtlsdr 2>/dev/null && echo 1)
HAVE_AIRSPY := $(shell $(PKG_CONFIG) --exists libairspy 2>/dev/null && echo 1)
HAVE_FFTW   := $(shell $(PKG_CONFIG) --exists fftw3f 2>/dev/null && echo 1)
HAVE_MHD    := $(shell $(PKG_CONFIG) --exists libmicrohttpd 2>/dev/null && echo 1)
HAVE_PNG    := $(shell $(PKG_CONFIG) --exists libpng 2>/dev/null && echo 1)
HAVE_WEBP   := $(shell $(PKG_CONFIG) --exists libwebp 2>/dev/null && echo 1)
HAVE_CURL   := $(shell $(PKG_CONFIG) --exists libcurl 2>/dev/null && echo 1)

ifeq ($(HAVE_RTLSDR),1)
  DEFS      += -DED_HAVE_RTLSDR
  EXTRA_CPP += $(RTLSDR_CFLAGS)
  LDLIBS    += $(RTLSDR_LIBS)
endif
ifeq ($(HAVE_AIRSPY),1)
  DEFS      += -DED_HAVE_AIRSPY
  EXTRA_CPP += $(AIRSPY_CFLAGS)
  LDLIBS    += $(AIRSPY_LIBS)
endif
ifeq ($(HAVE_FFTW),1)
  DEFS      += -DED_HAVE_FFTW3
  EXTRA_CPP += $(FFTW_CFLAGS)
  LDLIBS    += $(FFTW_LIBS)
endif
ifeq ($(HAVE_MHD),1)
  DEFS      += -DED_HAVE_MHD
  EXTRA_CPP += $(MHD_CFLAGS)
  LDLIBS    += $(MHD_LIBS)
endif
ifeq ($(HAVE_PNG),1)
  DEFS      += -DED_HAVE_PNG
  EXTRA_CPP += $(PNG_CFLAGS)
  LDLIBS    += $(PNG_LIBS)
endif
ifeq ($(HAVE_WEBP),1)
  DEFS      += -DED_HAVE_WEBP
  EXTRA_CPP += $(WEBP_CFLAGS)
  LDLIBS    += $(WEBP_LIBS)
endif
ifeq ($(HAVE_CURL),1)
  DEFS      += -DED_HAVE_CURL
  EXTRA_CPP += $(CURL_CFLAGS)
  LDLIBS    += $(CURL_LIBS)
endif

# --- build mode ------------------------------------------------------------
MODE       ?= release
ifeq ($(MODE),debug)
  OPT      := -O0 -g3
  DEFS     += -DED_DEBUG -DED_BUILD_MODE=\"debug\"
  SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
  BIN      := $(BUILD_DIR)/$(APP)-debug
else ifeq ($(MODE),release)
  OPT      := -O2 -g
  DEFS     += -DED_RELEASE -DED_BUILD_MODE=\"release\"
  SAN      :=
  BIN      := $(BUILD_DIR)/$(APP)
else
  $(error MODE must be 'debug' or 'release')
endif

# Objects are kept per-mode so switching modes never reuses stale objects.
OBJ_DIR    := $(BUILD_DIR)/obj/$(MODE)

# --- architecture ----------------------------------------------------------
# Never use -march=native: it breaks cross-compilation and Pi deployment.
PI         ?= 0
ifeq ($(PI),1)
  ARCH     := -mcpu=cortex-a53
else
  ARCH     :=
endif

# --- flags -----------------------------------------------------------------
CXXSTD     := -std=c++17
WARN       := -Wall -Wextra -Wpedantic -Werror
CPPFLAGS   += -I$(SRC_DIR) -I$(GEN_DIR) $(DEFS) $(EXTRA_CPP)
CXXFLAGS   += $(CXXSTD) $(WARN) $(OPT) $(ARCH) $(SAN) -MMD -MP
LDFLAGS    += $(ARCH) $(SAN)
LDLIBS     += -pthread -lm

# --- sources ---------------------------------------------------------------
SRCS       := $(shell find $(SRC_DIR) -name '*.cpp' | sort)
OBJS       := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
DEPS       := $(OBJS:.o=.d)

LIB_SRCS   := $(filter-out $(SRC_DIR)/main.cpp,$(SRCS))
LIB_OBJS   := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(LIB_SRCS))

TEST_SRCS  := $(wildcard $(TEST_DIR)/*.cpp)
# One binary per test file (each has its own main()).
TEST_BINS  := $(patsubst $(TEST_DIR)/%.cpp,$(BUILD_DIR)/$(APP)_test_%-$(MODE),$(TEST_SRCS))

# --- generated embedded defaults ------------------------------------------
DEFAULT_JSON := resources/default_config.json
DEFAULT_INC  := $(GEN_DIR)/default_config.inc

# --- generated embedded web assets (M9) -----------------------------------
WEB_DIR      := web
WEB_INC      := $(GEN_DIR)/web_assets.inc
WEB_GEN      := tools/gen_web_assets.py
WEB_FILES    := $(shell find $(WEB_DIR) -type f 2>/dev/null)

# Web assets are embedded in the binary by default. Set EMBED_WEB=0 to also
# install web/ to the share dir (development / non-embedded builds).
EMBED_WEB    ?= 1

# --- targets ---------------------------------------------------------------
.PHONY: all release debug test smoke clean config install deploy-pi deploy-pi-cross check-web

all: $(BIN)

release:
	$(MAKE) MODE=release all

debug:
	$(MAKE) MODE=debug all

$(BIN): $(OBJS) | $(BUILD_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# The embedded default config is generated from the JSON resource.
$(OBJ_DIR)/core/default_config.o: $(DEFAULT_INC)

$(DEFAULT_INC): $(DEFAULT_JSON)
	@mkdir -p $(dir $@)
	@{ printf 'static const char kDefaultConfigJson[] = R"EDCFG('; cat $<; printf ')EDCFG";\n'; } > $@

# The embedded web assets are generated from the web/ tree (M9).
$(OBJ_DIR)/web/static_files.o: $(WEB_INC)

$(WEB_INC): $(WEB_FILES) $(WEB_GEN)
	@mkdir -p $(dir $@)
	python3 $(WEB_GEN) $(WEB_DIR) $@

$(BUILD_DIR):
	@mkdir -p $@

test: check-web $(TEST_BINS)
	@echo "Running self-tests..."
	@for t in $(TEST_BINS); do echo "--- $$t"; $$t || exit 1; done

# The web UI is embedded into the binary at build time, so a broken script is
# only visible in a browser. A SyntaxError in one shared file (web/js/app.js)
# leaves every page blank while the daemon looks healthy. Parse-check the JS
# when node is available.
WEB_JS     := $(shell ls $(WEB_DIR)/js/*.js 2>/dev/null)
check-web:
	@if command -v $(NODE) >/dev/null 2>&1; then \
		for f in $(WEB_JS); do \
			echo "--- syntax $$f"; $(NODE) --check $$f || exit 1; \
		done; \
	else \
		echo "--- skipping JS syntax check (no $(NODE))"; \
	fi

smoke: $(BIN)
	@for s in $(TEST_DIR)/m2_smoke.sh $(TEST_DIR)/m3_smoke.sh $(TEST_DIR)/m4_smoke.sh $(TEST_DIR)/m5_smoke.sh $(TEST_DIR)/m6_smoke.sh $(TEST_DIR)/m7_smoke.sh $(TEST_DIR)/m8_smoke.sh $(TEST_DIR)/m9_smoke.sh $(TEST_DIR)/m10_smoke.sh; do \
		echo "--- $$s"; \
		bash $$s || exit 1; \
	done

$(BUILD_DIR)/$(APP)_test_%-$(MODE): $(TEST_DIR)/%.cpp $(LIB_OBJS) $(DEFAULT_INC) $(WEB_INC) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LIB_OBJS) $(LDFLAGS) -o $@ $(LDLIBS)

config:
	@echo "MODE      = $(MODE)"
	@echo "CROSS     = $(CROSS)"
	@echo "PI        = $(PI)"
	@echo "CXX       = $(CXX)"
	@echo "CPPFLAGS  = $(CPPFLAGS)"
	@echo "CXXFLAGS  = $(CXXFLAGS)"
	@echo "LDFLAGS   = $(LDFLAGS)"
	@echo "LDLIBS    = $(LDLIBS)"
	@echo "SRCS      = $(words $(SRCS)) file(s)"
	@echo "deps      = rtlsdr=$(HAVE_RTLSDR) airspy=$(HAVE_AIRSPY) fftw3f=$(HAVE_FFTW) microhttpd=$(HAVE_MHD) png=$(HAVE_PNG) webp=$(HAVE_WEBP) curl=$(HAVE_CURL)"

clean:
	rm -rf $(BUILD_DIR)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)/usr/local/bin/$(APP)
	install -d $(DESTDIR)$(SHARE_DIR)/resources
	cp -R resources/. $(DESTDIR)$(SHARE_DIR)/resources/
	@if [ "$(EMBED_WEB)" = "0" ]; then \
		echo "installing web/ (not embedded)"; \
		install -d $(DESTDIR)$(SHARE_DIR)/web; \
		cp -R web/. $(DESTDIR)$(SHARE_DIR)/web/; \
	fi

deploy-pi: $(BIN)
	@test -n "$(PI_HOST)" || (echo "PI_HOST is required, e.g. make deploy-pi PI_HOST=pi@elektro.local" && exit 1)
	ssh $(PI_HOST) 'rm -rf /tmp/elektrodump-deploy && mkdir -p /tmp/elektrodump-deploy/build /tmp/elektrodump-deploy/config'
	scp $(BIN) $(PI_HOST):/tmp/elektrodump-deploy/build/$(APP)
	scp -r resources $(PI_HOST):/tmp/elektrodump-deploy/
	scp -r deploy $(PI_HOST):/tmp/elektrodump-deploy/
	scp config/config.example.json $(PI_HOST):/tmp/elektrodump-deploy/config/
	ssh $(PI_HOST) 'sudo bash /tmp/elektrodump-deploy/deploy/install.sh'

deploy-pi-cross:
	$(MAKE) CROSS=aarch64-linux-gnu- PI=1
	$(MAKE) deploy-pi

-include $(DEPS)
