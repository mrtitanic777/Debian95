# Debian95 desktop for Linux - compiles the SHARED win32 subsystem + drivers +
# codecs against POSIX (SDL2 + libc) into the Win95 desktop that runs as the X
# session on the Debian image (and as a portable app for dev).
#
#   make debian95-desktop-sdl   build the desktop (needs libsdl2-dev, libx11-dev)

REPO   ?= ../..
CC     ?= cc
CFLAGS ?= -O2
# `override` so our include paths survive even when Buildroot passes CFLAGS on
# the make command line (a command-line assignment otherwise wins over `+=`).
override CFLAGS += -I$(REPO)/sdk/include -Wall -std=gnu11 -fno-strict-aliasing

# Security hardening (GrapheneOS-style, per-binary): stack protector, FORTIFY,
# position-independent + full RELRO + immediate binding + non-exec stack, and
# stack-clash protection. On the Buildroot target the toolchain adds its own
# hardening too (see the defconfig); these also harden the dev/SDL build. Both
# x86-64 and aarch64 support all of these flags.
HARDEN_CFLAGS := -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -fstack-clash-protection
HARDEN_LDFLAGS := -pie -Wl,-z,relro,-z,now,-z,noexecstack
override CFLAGS  += $(HARDEN_CFLAGS)
override LDFLAGS += $(HARDEN_LDFLAGS)

# Shared win32 subsystem (portable subset - the shell/console stays on the
# from-scratch side; the Media Player + MPEG-1/GIF codecs build here too).
# calc + minesweeper are intentionally ABSENT: they ship as hot-swappable app
# The apps listed in PLUGINS below are intentionally ABSENT: they ship as
# hot-swappable app modules (see the `plugins` target), proving the shell core
# builds and runs without them compiled in.
WIN32 := wm comctl icons apps filemgr viewer \
         control applets shelldlg \
         recycle shellutil mediaplayer appreg win32api comdlg dlgeng finddlg openwith clippy \
         print theme systools cpl_passwords cpl_network cpl_access cpl_appwiz cpl_system cpl_mouse cpl_keyboard cpl_misc cpl_sounds cpl_multimedia cpl_bluetooth cpl_regional cpl_datetime cpl_display cpl_fonts vpn deskband fontprev
DRIVERS := fb img inflate png qoi gif jpeg mpegvid
THIRD   := picojpeg
RTL     := font font8x8

SRC := $(addprefix $(REPO)/subsystems/win32/,$(addsuffix .c,$(WIN32))) \
       $(addprefix $(REPO)/drivers/,$(addsuffix .c,$(DRIVERS))) \
       $(REPO)/third_party/picojpeg/picojpeg.c \
       $(addprefix $(REPO)/lib/rtl/,$(addsuffix .c,$(RTL))) \
       shims_linux.c terminal_linux.c platform_linux.c rcparse.c module_loader.c

override CFLAGS += -I$(REPO)/third_party/picojpeg -I$(REPO)/third_party/plmpeg -I$(REPO)/third_party/stb_truetype

# libc-agnostic link libs: glibc keeps crypt()/forkpty() in separate libcrypt/
# libutil; musl folds both into libc (and ships no libcrypt/libutil), so only
# add them for glibc. Detected via the __GLIBC__ macro from <features.h>.
DETECT_GLIBC := $(shell printf '#include <features.h>\n' | $(CC) $(CFLAGS) -E -dM - 2>/dev/null | grep -c __GLIBC__)
LIBS := -lm -ldl
ifneq ($(DETECT_GLIBC),0)
LIBS += -lutil -lcrypt
endif

all: debian95-desktop-sdl plugins

# the Win95 desktop as an SDL2 + X11 window app (the X session on the Debian image,
# and a portable app for dev). debian95-wm frames the downloaded apps around it.
# -rdynamic exports the host API so dlopen'd app modules can resolve it.
debian95-desktop-sdl: $(SRC) xbridge_linux.c
	$(CC) $(CFLAGS) $(LDFLAGS) -rdynamic -DUSE_SDL $(shell sdl2-config --cflags) -o $@ $(SRC) xbridge_linux.c $(shell sdl2-config --libs) -lX11 $(LIBS)

# ---- hot-swappable app modules (plugins) -----------------------------------
# Built as position-independent shared objects with -DDEBIAN95_MODULE, linking
# NONE of the shell's objects (they resolve the host API at load time). Output
# goes to ./plugins; the image build installs them to /usr/lib/debian95/apps.
PLUGDIR    ?= plugins
PLUGINS    := calc minesweeper clock solitaire charmap msdos clipbrd freecell paint \
              sysinfo calendar browser regedit taskmgr services \
              editor volume soundrec wordpad reversi logview devmgr eventvwr netnbhd \
              hyperterm taskschd snip narrator remotehost firewall sysrestore backup update search
PLUGIN_SO  := $(addprefix $(PLUGDIR)/,$(addsuffix .so,$(PLUGINS)))
PLUG_CFLAGS := -I$(REPO)/sdk/include -Wall -std=gnu11 -fno-strict-aliasing -O2 \
               -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fstack-clash-protection -fPIC
PLUG_LDFLAGS := -shared -Wl,-z,relro,-z,now,-z,noexecstack

plugins: $(PLUGIN_SO)
$(PLUGDIR)/%.so: $(REPO)/subsystems/win32/%.c
	@mkdir -p $(PLUGDIR)
	$(CC) $(PLUG_CFLAGS) $(PLUG_LDFLAGS) -DDEBIAN95_MODULE $< -o $@ -lm

clean:
	rm -f debian95-desktop-sdl $(PLUGIN_SO)

.PHONY: all clean plugins
