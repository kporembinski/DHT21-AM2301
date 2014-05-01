CC	= @gcc
RM	= @rm
MKDIR	= @mkdir
ECHO 	= @echo
INSTALL = @install

OE		= o

vpath	%.c ../
vpath	%.h ../

PROG	= am2301
DEPS	=

OBJS	= am2301.$(OE)

OBJDIR	= obj

_OBJS	= $(patsubst %,$(OBJDIR)/%,$(OBJS))
LIBS	= -lwiringPi

INCLUDE = -I. -I/include -I../

CFLAGS	= -Wall
ifeq ($(DEBUG),yes)
CFLAGS	+= -g
CFLAGS	+= -O0
else
CFLAGS	+= -O3
endif


.PHONY: all clean

all: $(OBJDIR) $(_OBJS)
	$(ECHO) "Building $(PROG)"
	$(CC) -o $(PROG) $(_OBJS) $(INCLUDE) $(LIBS)

install: $(PROG)
	$(INSTALL) -m 0655 $(PROG) /usr/sbin

uninstall:
	$(RM) -f /usr/sbin/$(PROG)

$(OBJDIR)/%.$(OE): %.c $(DEPS)
	$(ECHO) "Compiling $<"
	$(CC) -c -o $@ $< $(CFLAGS)

$(OBJDIR):
	$(MKDIR) -p $(OBJDIR)
clean:
	$(RM) -fr $(PROG)
	$(RM) -fr $(OBJDIR)
	$(RM) -fr '*~'
