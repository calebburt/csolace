solace: src/*.c src/*.h
	gcc -o bin/solace src/*.c -Wall -Wextra -pedantic -Wno-unused-parameter -O2 -lm

.PHONY = run
run: solace
	bin/solace

.PHONY = test
test: solace
	./tests/run.sh

.PHONY = debug
debug: src/*.c src/*.h
	gcc -o bin/solace_dbg src/*.c -Wall -Wextra -Wno-unused-parameter -pedantic -DSLC_DEBUG -g -O0 -pg -lm
	bin/solace_dbg

.PHONY = prof
prof: src/*.c src/*.h profile.slc
	gcc -o bin/solace_prof src/*.c -Wall -Wextra -pedantic -Wno-unused-parameter -O2 -lm -pg
	bin/solace_prof profile.slc
	gprof bin/solace_prof gmon.out -bp

.PHONY = clean
clean:
	rm -f gmon.out
