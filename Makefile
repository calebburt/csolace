solace: src/*.c src/*.h
	gcc -o solace src/*.c -Wall -Wextra -pedantic -Wno-unused-parameter -O2 -lm

.PHONY = run
run: solace
	./solace

.PHONY = debug
debug: src/*.c src/*.h
	gcc -o solace_dbg src/*.c -Wall -Wextra -Wno-unused-parameter -pedantic -DSLC_DEBUG -g -O0 -pg -lm
	./solace_dbg

.PHONY = prof
prof: src/*.c src/*.h
	gcc -o solace_prof src/*.c -Wall -Wextra -pedantic -Wno-unused-parameter -O2 -lm -pg
	echo "i = 0 while i < 10000000 \"hello \" + \"world\" i = i + 1 end" | ./solace_prof
	gprof solace_prof gmon.out -bp

.PHONY = clean
clean:
	rm -f solace solace_dbg