CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS =

SRCDIR = src
BUILDDIR = .

SOURCES = $(SRCDIR)/main.c $(SRCDIR)/ui_handler.c $(SRCDIR)/registration_protocol.c $(SRCDIR)/ndn_node.c $(SRCDIR)/ndn_protocol.c $(SRCDIR)/topology_protocol.c

OBJECTS = main.o ui_handler.o registration_protocol.o ndn_node.o ndn_protocol.o topology_protocol.o

EXECUTABLE = ndn

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)