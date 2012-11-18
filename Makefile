CFLAGS?=-Wall -Os -std=c99
LDADD?=`pkg-config --cflags --libs x11 xinerama xft`

normal:
	$(CC) -o xowl xowl.c $(CFLAGS) $(LDADD) $(LDFLAGS)
	$(CC) -o xowl-debug xowl.c $(CFLAGS) -g $(LDADD) $(LDFLAGS)
	strip xowl

docs:
	pandoc -s -w man xowl.md -o xowl.1

clean:
	rm -f xowl xowl-debug

all: docs normal