## Variable that points to SystemC installation path
SYSTEMC_HOME = /home/moon/Tools/systemc-2.3.1

## Select the target architecture
TARGET_ARCH = linux64

## Select the architecture suffix, if necessary
ARCH_SUFFIX = -$(TARGET_ARCH)

## How to instruct the dynamic linker to locate the SystemC library

# default compiler flags
FLAGS_COMMON = -g -Wall
#FLAGS_STRICT = -pedantic -Wno-long-long
#FLAGS_WERROR = -Werror

PROJECT = main
VPATH = ./src
INCDIR = -I./include

OBJS = $(patsubst %.cpp, %.o, $(shell find $(VPATH) -name '*.cpp'))
SRCS = $(shell find $(VPATH)/*.cpp)

## Variable that points to SystemC installation path
## needs to be set, fallback to SYSTEMC otherwise
SYSTEMC_HOME?=$(SYSTEMC)

LDFLAG_RPATH     ?= -Wl,-rpath=

SYSTEMC_INC_DIR  ?= $(SYSTEMC_HOME)/include
SYSTEMC_LIB_DIR  ?= $(SYSTEMC_HOME)/lib$(ARCH_SUFFIX)

SYSTEMC_DEFINES  ?=
SYSTEMC_CXXFLAGS ?= $(FLAGS_COMMON) $(FLAGS_STRICT) $(FLAGS_WERROR)
SYSTEMC_LDFLAGS  ?= -L $(SYSTEMC_LIB_DIR) \
                    $(LDFLAG_RPATH)$(SYSTEMC_LIB_DIR)
SYSTEMC_LIBS     ?= -lsystemc -lm

## Add 'PTHREADS=1' to command line for a pthreads build
## (should not be needed in most cases)
ifdef PTHREADS
SYSTEMC_CXXFLAGS += -pthread
SYSTEMC_LIBS     += -lpthread
endif

## ***************************************************************************
## example defaults
## - basic configuration should be set from Makefile.config
INCDIR   += -I. -I.. -I$(SYSTEMC_INC_DIR)
LIBDIR   += -L. -L..

CXXFLAGS  += $(CFLAGS) $(SYSTEMC_CXXFLAGS) $(INCDIR) $(SYSTEMC_DEFINES)
LDFLAGS   += $(CFLAGS) $(SYSTEMC_CXXFLAGS) $(LIBDIR) $(SYSTEMC_LDFLAGS)
LIBS      += $(SYSTEMC_LIBS) $(EXTRA_LIBS)

# "real" Makefile needs to set PROJECT
ifeq (,$(strip $(PROJECT)))
$(error PROJECT not set. Cannot build.)
endif

# basic check for SystemC directory
ifeq (,$(wildcard $(SYSTEMC_HOME)/.))
$(error SYSTEMC_HOME [$(SYSTEMC_HOME)] is not present. \
        Please update Makefile)
endif
ifeq (,$(wildcard $(SYSTEMC_INC_DIR)/systemc.h))
$(error systemc.h [$(SYSTEMC_INC_DIR)] not found. \
        Please update Makefile)
endif
ifeq (,$(wildcard $(SYSTEMC_LIB_DIR)/libsystemc*))
$(error SystemC library [$(SYSTEMC_LIB_DIR)] not found. \
        Please update Makefile)
endif

# use g++ by default, unless user specifies CXX explicitly
ifeq (default,$(origin CXX))
CXX=g++
endif
# use CXX by default, unless user specifies CC explicitly
ifeq (default,$(origin CC))
CC=$(CXX)
endif

## ***************************************************************************
## build rules

.SUFFIXES: .cc .cpp .o .x

GOLDEN?=../results/expected.log
EXEEXT?=.x
EXE   := $(PROJECT)$(EXEEXT)

all: $(EXE)

$(EXE): $(OBJS) $(SYSTEMC_LIB_DIR)/libsystemc.a
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS) 2>&1 | c++filt
	@test -x $@

.cpp.o:
	$(CC) $(CXXFLAGS) -c $< -o $@ -DSC_INCLUDE_FX -DDEBUG

.cc.o:
	$(CC) $(CXXFLAGS) -c $< -o $@

clean: 
	rm -f $(OBJS) $(EXE) 

allclean: clean
	rm -f run.log run_trimmed.log expected_trimmed.log diff.log *.vcd
