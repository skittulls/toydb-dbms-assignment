CC = cc
CFLAGS = -std=gnu89 -Wno-implicit-function-declaration -Wno-implicit-int -Wno-return-type -I. -I../common
SRC = buf.c hash.c pf.c ../common/config.c
OBJ = buf.o hash.o pf.o ../common/config.o
HDR = pftypes.h pf.h ../common/config.h

all: pflayer.o tests

pflayer.o: $(OBJ)
	ld -r -o pflayer.o $(OBJ)

../common/config.o: ../common/config.c ../common/config.h
	$(CC) $(CFLAGS) -c ../common/config.c -o ../common/config.o

buf.o: buf.c $(HDR)
	$(CC) $(CFLAGS) -c buf.c

hash.o: hash.c $(HDR)
	$(CC) $(CFLAGS) -c hash.c

pf.o: pf.c $(HDR)
	$(CC) $(CFLAGS) -c pf.c

testpf.o: testpf.c $(HDR)
	$(CC) $(CFLAGS) -c testpf.c

testhash.o: testhash.c $(HDR)
	$(CC) $(CFLAGS) -c testhash.c

tests: testhash testpf

testpf: testpf.o pflayer.o
	$(CC) $(CFLAGS) -o testpf testpf.o pflayer.o

testhash: testhash.o pflayer.o
	$(CC) $(CFLAGS) -o testhash testhash.o pflayer.o

clean:
	rm -f *.o testpf testhash ../common/*.o file1 file2 a.out
