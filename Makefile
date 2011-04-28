# Executable files
TARGET = mtdev2tuio

CPPFLAGS = -O3

# compiler
CC = gcc
# linker
LD = gcc

# include directories
INCLUDES = 
# e.g. -I/usr/include

# libraries
LIBS = -lmtdev -llo

LIBDIRS =
LDFLAGS =

# Sources
SRCS = mtdev2tuio.c 

# Object file to be produced. Sources but with .o extension
OBJS=${SRCS:.c=.o}

# Link rule
$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $@ $(LIBDIRS) $(LIBS)

# Compilation rule
%.o:%.c
	$(CC) $(CPPFLAGS) $(INCLUDES) -c $<

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)

