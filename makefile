mat: mat.c
	$(CC) mat.c -o mat -Wall -Wextra -pedantic -std=c99 && ./mat
