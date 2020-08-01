CC           = gcc
CXX          = g++
COMPILE_LINK = -flto -O3
C_CXX_FLAGS  = -Wall -Wextra -Winline -gdwarf-2
DEPGEN       = -MD -MP -MT $(*F).o -MF $(@F).d
DEPS         = $(OBJECTS:%.o=%.o.d)
CFLAGS       = $(COMPILE_LINK) $(DEPGEN) $(C_CXX_FLAGS)
CFLAGS      += -std=gnu11
CXXFLAGS     = $(COMPILE_LINK) $(DEPGEN) $(C_CXX_FLAGS)
CXXFLAGS    += -std=gnu++11
CPPFLAGS     = 
LDFLAGS      = $(COMPILE_LINK)
LDFLAGS     += 
EXECUTABLE  ?= sparkle_gen
OBJECTS      = sparkle_gen.o
OBJECTS     += 

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LOADLIBES) $(LDLIBS) -o $@

$(OBJECTS): Makefile

clean:
	rm -rf $(EXECUTABLE) $(OBJECTS) $(DEPS)

-include $(DEPS)
