CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
TARGET = examgrid

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe examgrid_state.txt candidates_export.csv seating_report.csv audit_log.txt

.PHONY: all run clean
