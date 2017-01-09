# compile main.c

CC = gcc

test: main.c
	$(CC) -lwayland-client -D_GNU_SOURCE $< -o $@

clean:
	rm -rf test
