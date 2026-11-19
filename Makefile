.PHONY: test clean
CXX ?= g++
test:
	$(CXX) -std=c++17 -O2 -Wall -Wextra components/selectro_izar/izar_decoder.cpp tests/test_decoder.cpp -o tests/test_decoder
	./tests/test_decoder
clean:
	rm -f tests/test_decoder
