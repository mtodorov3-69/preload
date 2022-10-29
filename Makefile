#
# Makefile for preload
#
# mtodorov 2022-10-28
#
# Copyright (C) Mirsad Todorovac 2022
# Copying by license GPLv2
#

OBJS=mmap-preload.o argument_list.o mapped_file.o signal_handling.o memfree.o
DEPS=mmap-preload.h argument_list.h mapped_file.h signal_handling.h my_aux.h

CC = gcc
CXX = g++
CFLAGS = -g -O2
CXXFLAGS = -g -O2
LDFLAGS = -lstdc++

preload: $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

memfree: memfree.cc
	gcc -o memfree -DDEBUG memfree.cc

clean:
	rm -f *.o preload memfree

include Makefile.depend

depend: $(DEPS)
	$(CXX) -MM *.cc | tee Makefile.depend

