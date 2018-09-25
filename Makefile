CC = gcc
LD = gcc

LIBS =
NAME = edrename
UNITS = edrename
CFLAGS = -Wall -Wextra -pedantic -std=c11 -g

LDFLAGS += $(foreach L,$(LIBS),-l$(L))
C_FILES = $(foreach u,$(UNITS),$(u).c)
OBJ_FILES = $(foreach u,$(UNITS),$(u).o)

all : $(NAME)

$(NAME) : $(OBJ_FILES)
	$(LD) $(LDFLAGS) $^ -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean :
	rm -rf $(OBJ_FILES) $(NAME)

