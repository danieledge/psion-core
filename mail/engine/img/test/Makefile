# pmimg's colour mode (and its 16 greys) on a PC: make -C mail/engine/img/test
# Needs python3 with Pillow (the test pictures) and the system's zlib.
IMG = ..
SRC = $(IMG)/pmimg.c $(IMG)/pmjpeg.c $(IMG)/pmjprog.c $(IMG)/pmpng.c $(IMG)/pmgif.c $(IMG)/picojpeg.c

test: pmimgtest pics/mean.txt
	./pmimgtest pics

pmimgtest: pmimgtest.c $(SRC) $(IMG)/pmimg.h
	$(CC) -O1 -g -Wall -fsanitize=address,undefined -I$(IMG) -o $@ pmimgtest.c $(SRC) -lz

pics/mean.txt: mkpics.py
	python3 mkpics.py pics

.PHONY: test
