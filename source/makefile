SHELL := cmd.exe

CXX = g++

TARGET = DuckerNative.dll

BUILD_DIR = build

SRCS = DuckerNative.cpp \
       GLAD/src/glad.c

OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(filter %.cpp,$(SRCS))) \
       $(patsubst %.c,$(BUILD_DIR)/%.o,$(filter %.c,$(SRCS)))

DIRS_TO_CREATE := $(subst /,\,$(sort $(dir $(OBJS))))

INCLUDES = -IGLAD/include

CXXFLAGS = -std=c++17 -Wall -Wextra -DNDEBUG -O2 -shared

LIBS = -lopengl32

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo Linking $@...
	$(CXX) -o $@ $(OBJS) $(CXXFLAGS) $(LIBS)
	@echo Build finished: $@

$(BUILD_DIR)/%.o: %.cpp | prepare_dirs
	@echo Compiling C++: $<
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: %.c | prepare_dirs
	@echo Compiling C:   $<
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: prepare_dirs
prepare_dirs:
	@echo Preparing build directories...
	@for %%d in ($(DIRS_TO_CREATE)) do if not exist %%d mkdir %%d

clean:
	@echo Cleaning project...
	@if exist $(subst /,\,$(BUILD_DIR)) ( rmdir /s /q $(subst /,\,$(BUILD_DIR)) && echo Deleted: $(subst /,\,$(BUILD_DIR)) )
	@if exist $(TARGET) ( del $(TARGET) && echo Deleted: $(TARGET) )
	@echo Clean complete.

.PHONY: all clean