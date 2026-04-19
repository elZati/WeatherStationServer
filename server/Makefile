# Compiler and high-performance flags
CXX = g++
CXXFLAGS = -Ofast -std=c++11 -I/usr/local/include/RF24

# Libraries to link against
LIBS = -lrf24 -lcurl

# Target executable name (Matching your manual command)
TARGET = Server

# Source file name
SRC = Server_v15.cpp

# Default rule: Compile
all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LIBS)

# Clean rule: Remove the binary to force a recompile
clean:
	rm -f $(TARGET)
