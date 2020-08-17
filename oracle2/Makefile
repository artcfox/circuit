CC           = gcc
CXX          = g++
COMPILE_LINK = -flto -O3
C_CXX_FLAGS  = -Wall -Wextra -Winline -gdwarf-2
DEPGEN       = -MD -MP -MT $(*F).o -MF $(@D)/$(@F).d
DEPS         = $(OBJECTS:%.o=%.o.d)
CFLAGS       = $(COMPILE_LINK) $(DEPGEN) $(C_CXX_FLAGS)
CFLAGS      += -std=gnu11
CXXFLAGS     = $(COMPILE_LINK) $(DEPGEN) $(C_CXX_FLAGS)
CXXFLAGS    += -std=gnu++11
CPPFLAGS     = 
LDFLAGS      = $(COMPILE_LINK)
LDFLAGS     += 
EXECUTABLE  ?= main
OBJECTS      = main.o
OBJECTS     += rbtree/rbtree.o rbtree/rbtree+setinsert.o rbtree/rbtree+debug.o

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LOADLIBES) $(LDLIBS) -o $@

$(OBJECTS): Makefile

clean:
	rm -rf $(EXECUTABLE) $(OBJECTS) $(DEPS)

-include $(DEPS)
