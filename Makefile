CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
SRCDIR = src
BUILDDIR = build
BOTDIR = bots

SRCS = $(wildcard $(SRCDIR)/*.cpp)
OBJS = $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SRCS))
TARGET = chaos_poker

BOT_SRCS = $(wildcard $(BOTDIR)/*.cpp)
BOT_TARGETS = $(patsubst $(BOTDIR)/%.cpp,$(BOTDIR)/%,$(BOT_SRCS))

.PHONY: all clean bots

all: $(TARGET) bots

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

bots: $(BOT_TARGETS)

$(BOTDIR)/%: $(BOTDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf $(BUILDDIR) $(TARGET) $(BOT_TARGETS)
