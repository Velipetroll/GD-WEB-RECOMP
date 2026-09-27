MAKEFLAGS += -j$(shell nproc 2>/dev/null || echo 4)

ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
else
    UNAME_S := $(shell uname -s 2>/dev/null)
    ifeq ($(UNAME_S),Linux)
        DETECTED_OS := Linux
    else ifeq ($(UNAME_S),Darwin)
        DETECTED_OS := macOS
    else
        DETECTED_OS := Linux
    endif
endif

SRC_DIR = src
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

CXX = g++
CXXFLAGS = -std=c++17 -O3 -march=native -flto -fno-math-errno -MMD -MP

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))
DEPS = $(OBJS:.o=.d)

ifeq ($(DETECTED_OS),Windows)
    TARGET = $(BUILD_DIR)/GeometryDash.exe
    WINDRES = windres
    RC_SRC = resource.rc
    RC_OBJ = $(if $(wildcard $(RC_SRC)),$(OBJ_DIR)/resource.o,)
    
    SDL_CFLAGS = $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
    
    # Get all native Windows libraries required by SDL2 in static mode
    SDL_STATIC_LIBS = $(shell sdl2-config --static-libs 2>/dev/null || pkg-config --static --libs sdl2 2>/dev/null)
    ifeq ($(SDL_STATIC_LIBS),)
        SDL_STATIC_LIBS = -lmingw32 -lSDL2main -lSDL2 -mwindows -ldinput8 -ldxguid -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid
    endif
    
    # -static embeds libstdc++, libgcc, pthreads, zlib and SDL2 inside the final executable
    LDFLAGS = -flto -O3 -static -static-libgcc -static-libstdc++ $(SDL_STATIC_LIBS) -lopengl32 -lz -lpthread -lm
else ifeq ($(DETECTED_OS),macOS)
    TARGET = $(BUILD_DIR)/GeometryDash
    SDL_CFLAGS = $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
    SDL_LIBS   = $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null)
    ifeq ($(SDL_LIBS),)
        SDL_LIBS = -lSDL2
    endif
    LDFLAGS = -flto -O3 $(SDL_LIBS) -framework OpenGL -lz -lpthread
else
    # Linux
    TARGET = $(BUILD_DIR)/GeometryDash
    SDL_CFLAGS = $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
    SDL_LIBS   = $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null)
    ifeq ($(SDL_LIBS),)
        SDL_LIBS = -lSDL2
    endif
    LDFLAGS = -flto -O3 $(SDL_LIBS) -lGL -lz -lpthread -ldl -lm
endif

CXXFLAGS += $(SDL_CFLAGS)

.PHONY: all clean copy_assets print_os win32 win64

all: print_os $(TARGET) copy_assets

win32:
	@./build_win32.sh

win64:
	@./build_win64.sh

print_os:
	@echo "==> Compiling static executable for: $(DETECTED_OS)"

$(TARGET): $(OBJS) $(RC_OBJ) | $(BUILD_DIR)
	$(CXX) $(OBJS) $(RC_OBJ) -o $(TARGET) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/resource.o: $(RC_SRC) | $(OBJ_DIR)
	$(WINDRES) $< -O coff -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

copy_assets: | $(BUILD_DIR)
	@if [ -d "assets" ]; then \
		mkdir -p $(BUILD_DIR)/assets; \
		cp -rf assets/. $(BUILD_DIR)/assets/; \
	fi

-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR)
