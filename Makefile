solace: src/*.c src/*.h
	gcc -o solace src/*.c -Wall -Wextra -pedantic -Wno-unused-parameter -O2

.PHONY = run
run: solace
	./solace

.PHONY = debug
debug: src/*.c src/*.h
	gcc -o solace_dbg src/*.c -Wall -Wextra -Wno-unused-parameter -pedantic -DSLC_DEBUG -g -O0 -pg
	./solace_dbg

.PHONY = clean
clean:
	rm -f solace solace_dbg