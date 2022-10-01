SRCDIR := src
OBJDIR := obj
MAIN := $(SRCDIR)/Main.cpp
SRCS := $(filter-out $(MAIN) $(SRCDIR)/Gem5Wrapper.cpp, $(wildcard $(SRCDIR)/*.cpp))
OBJS := $(patsubst $(SRCDIR)/%.cpp, $(OBJDIR)/%.o, $(SRCS))

# Ramulator currently supports g++ 5.1+ or clang++ 3.4+.  It will NOT work with
# g++ 4.x due to an internal compiler error when processing lambda functions.
# CXX := clang++
CXXFLAGS := -O3 -std=c++11 -g -w -Wall
INCFLAGS := -I$(CONDA_PREFIX)/include -I../common/DRAMPower/src
LDFLAGS := -L../common/DRAMPower/src -ldrampowerxml -ldrampower -L$(CONDA_PREFIX)/lib -lxerces-c -lboost_program_options

.PHONY: all clean depend

all: depend ramulator

clean:
	rm -f ramulator
	rm -rf $(OBJDIR)

depend: $(OBJDIR)/.depend

$(OBJDIR)/.depend: $(SRCS)
	@mkdir -p $(OBJDIR)
	@rm -f $(OBJDIR)/.depend
	@$(foreach SRC, $(SRCS), ${CXX} $(CXXFLAGS) $(INCFLAGS) -DRAMULATOR -MM -MT $(patsubst $(SRCDIR)/%.cpp, $(OBJDIR)/%.o, $(SRC)) $(SRC) >> $(OBJDIR)/.depend ;)

ifneq ($(MAKECMDGOALS),clean)
-include $(OBJDIR)/.depend
endif

ramulator: $(MAIN) $(OBJS) $(SRCDIR)/*.h | depend
	${CXX} $(CXXFLAGS) -DRAMULATOR -o $@ $(MAIN) $(INCFLAGS) $(OBJS) $(LDFLAGS)

ramulator_debug: $(MAIN) $(OBJS) $(SRCDIR)/*.h | depend
	${CXX} $(CXXFLAGS) -DRAMULATOR -o $@ $(MAIN) $(INCFLAGS) $(OBJS) $(LDFLAGS)

$(OBJS): | $(OBJDIR)

$(OBJDIR): 
	@mkdir -p $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	${CXX} $(CXXFLAGS) -DRAMULATOR -c -o $@ $(INCFLAGS) $(LDFLAGS) $<
