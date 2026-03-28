cli_objects = src/translate_helper_cli.o src/pdf_text_extractor.o

CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
CPPFLAGS ?=
LDFLAGS ?=
LIBS ?= -lz

host-cli: build/translate-pdf-cli

build/translate-pdf-cli: $(cli_objects)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(cli_objects) $(LIBS) -o $@

src/translate_helper_cli.o: src/translate_helper_cli.cpp src/pdf_text_extractor.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

src/pdf_text_extractor.o: src/pdf_text_extractor.cpp src/pdf_text_extractor.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

.PHONY: clean host-cli

clean:
	rm -f build/translate-pdf-cli build/translate-pdf-cli.aarch64 $(cli_objects)
