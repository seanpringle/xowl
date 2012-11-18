/*

MIT/X11 License
Copyright (c) 2012 Sean Pringle <sean.pringle@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/Xinerama.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <err.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define NEAR(a,o,b) ((b) > (a)-(o) && (b) < (a)+(o))
#define OVERLAP(a,b,c,d) (((a)==(c) && (b)==(d)) || MIN((a)+(b), (c)+(d)) - MAX((a), (c)) > 0)
#define INTERSECT(x,y,w,h,x1,y1,w1,h1) (OVERLAP((x),(w),(x1),(w1)) && OVERLAP((y),(h),(y1),(h1)))

Display *display;
Window root;

static int (*xerror)(Display *, XErrorEvent *);

typedef struct {
	short x, y, w, h;
} box;

#define MAX_MONITORS 3
box monitors[MAX_MONITORS];
int monitor = 0, nmonitors = 1;

#define MAX_WINDOWS 32
Window windows[MAX_WINDOWS];

#define ATOM_ENUM(x) x
#define ATOM_CHAR(x) #x

#define GENERAL_ATOMS(X) \
	X(_NET_ACTIVE_WINDOW),\
	X(_NET_CLIENT_LIST_STACKING),\
	X(_NET_CLIENT_LIST),\
	X(_NET_WM_NAME),\
	X(_NET_WM_DESKTOP),\
	X(_NET_WM_STATE),\
	X(_NET_WM_STATE_ABOVE),\
	X(_NET_WM_STATE_SKIP_TASKBAR),\
	X(_NET_SUPPORTING_WM_CHECK),\
	X(WM_PROTOCOLS)

enum { GENERAL_ATOMS(ATOM_ENUM), ATOMS };
const char *atom_names[] = { GENERAL_ATOMS(ATOM_CHAR) };
Atom atoms[ATOMS];
