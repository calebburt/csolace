solace: src/*.c src/*.h
	gcc -o solace src/*.c -Wall -Wextra -pedantic

.PHONY = run
run: solace
	./solace