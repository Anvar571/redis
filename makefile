CXX = g++

CXXFLAGS = -Wall -Wextra -std=c++17

TARGET = redis

SOURCES = src/main.cpp

OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

# linking
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^
# compiling
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# cleaning
clean:
	rm -f $(OBJECTS) $(TARGET)
