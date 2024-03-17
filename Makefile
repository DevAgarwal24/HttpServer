CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread

SRCS = server.cpp
OBJS = $(SRCS:.cpp=.o)
EXEC = http_server

.PHONY: all clean

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(EXEC)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(EXEC) $(OBJS)

