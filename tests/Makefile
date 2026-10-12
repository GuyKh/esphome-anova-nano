# Host-side protocol tests. Run from the repo root: make -C tests
CXX ?= g++
CXXFLAGS ?= -std=gnu++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
ROOT := ..

test: test_protocol
	./test_protocol

test_protocol: test_protocol.cpp golden_vectors.h $(ROOT)/components/anova_nano/anova_nano_protocol.cpp $(ROOT)/components/anova_nano/anova_nano_protocol.h
	$(CXX) $(CXXFLAGS) -I$(ROOT)/components/anova_nano -I. test_protocol.cpp $(ROOT)/components/anova_nano/anova_nano_protocol.cpp -o $@

golden: ## regenerate golden vectors from the reference (needs pyanova-nano installed)
	python3 gen_golden.py > golden_vectors.h

clean:
	rm -f test_protocol

.PHONY: test golden clean
