PLATFORM=linux
DYNAMIC_LIB_EXT=.so
ARCH=x86_64
PREFIX=/home/jasonk/Desktop/FYP/openMHA-DEV/openMHA
WITH_ALSA=yes
WITH_JACK=yes
WITH_LSL=yes
WITH_OSC=yes
WITH_EIGEN=yes
CXXSTANDARD=c++17
GCC_VER=-9
BUILD_DIR=x86_64-linux-gcc-9
CXXFLAGS+=-Wmisleading-indentation  -Wlogical-op -Wduplicated-cond -Wduplicated-branches  -Wformat-signedness
CFLAGS+=-Wmisleading-indentation  -Wlogical-op -Wduplicated-cond -Wduplicated-branches
SSE+=-msse -msse2 -mfpmath=sse
OPTIM=-O3 $(SSE) -ffast-math -fomit-frame-pointer -fno-finite-math-only
CXXFLAGS+=-Wall -Wextra -Wnon-virtual-dtor -Wformat -Werror -std=$(CXXSTANDARD) -fPIC $(OPTIM) -g
CFLAGS+=-Wall -Wextra -Werror -std=gnu11 -fPIC $(OPTIM) -g
MHA_TEST_COMMAND=octave --no-gui --no-window-system --eval "set_environment;exit(~run_mha_tests())"
