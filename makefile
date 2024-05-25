mat: src/mat.c
	$(CC) src/mat.c -o mat -Wall -Wextra -pedantic -std=c99 && ./mat src/mat.c

clean: 
	rm mat

rebuild:
	$(MAKE) clean
	$(MAKE) mat
