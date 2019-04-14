CC = musl-gcc
LD = musl-gcc

NAME = edrename
CFLAGS = -Wall -Wextra -pedantic -std=c99 -s

LDFLAGS += -static

all : $(NAME)

$(NAME) : edrename.o
	$(LD) $(LDFLAGS) $^ -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean :
	rm -rf *.o $(NAME)

