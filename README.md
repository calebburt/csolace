Solace is a programming language designed to be really nice to program in. It is kind of like my version of Ruby.

It is statically typed, but dynamic at heart. It has no runtime errors (except for stack overflows, I don't want to statically check for those!). It has a bytecode format that is human-readable, letting other languages have a Solace backend. I may use this in future projects, as it gives me a VM for free.

Solace is based on clox by Robert Nystrom. (munificent/craftinginterpreters)

## Building

To build, just run:

```bash
make

# OR

make solace
```

Running is as simple as:

```bash
bin/solace

# OR

make run
```

To compile the debug build and launch into the debug REPL, run:

```bash
make debug
```

## Testing

Solace has a large set of regression (and soon to be, regular) tests. They live in tests/. The test "framework" is similar to lox's. There are a bunch of solace scripts and expected outputs. These are applicable to any solace implementation, but the test runner will need to be adapted. To run the tests, execute:

```bash
make test

# OR

tests/run.sh
```

I really want to move this to bin/, but it would take a bunch of adjustments. I added the tests before I made bin/!!!

There is also a small profile script at ./profile.slc. To run it, with a profile build and gprof run automatically, run:

```bash
make prof
```

## Repo Layout

These are the main files and folders in this repository in alphabetical order, folders first.

- `bin` contains prebuilt binaries (all x86_64 linux elf, but I may change to arm64 linux elf at no notice, sorry)
- `doc` has a small amount of documentation that I have amassed.
- `src` contains all the source and header files for the project. This is the actual code for `csolace`.
- `tests` has all the test cases. It contains 3 sub-nodes:
    * `cases` has the positive cases, which test for output and lack of error.
    * `errors` contains the negative cases, which test for an error message and exit code.
    * `run.sh` is the test harness. I will make a windows `.bat` ASAP.
- `cruby-object-system.md` is just a file I got AI to generate that describes, you guessed it, CRuby's Object System. Solace is very similar to Ruby, so i wanted to take inspiration of how its internals work.
- `profile.slc` is the profile script.
