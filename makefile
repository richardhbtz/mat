mat: mat.c
	$(CC) mat.c -o mat -Wall -Wextra -pedantic -std=c99 && ./mat mat.c

clean: 
	rm mat

rebuild:
	$(MAKE) clean
	$(MAKE) mat
