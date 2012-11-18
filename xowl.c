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

#define _GNU_SOURCE
#include "xowl.h"
#include "config.h"
#include "textbox.c"

// X error handler
int oops(Display *d, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
		|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
		|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
		) return 0;
	fprintf(stderr, "error: request code=%d, error code=%d\n", ee->request_code, ee->error_code);
	return xerror(display, ee);
}

int window_get_prop(Window w, Atom prop, Atom *type, int *items, void *buffer, int bytes)
{
	memset(buffer, 0, bytes);
	int format; unsigned long nitems, nbytes; unsigned char *ret = NULL;
	if (XGetWindowProperty(display, w, prop, 0, bytes/4, False, AnyPropertyType, type,
		&format, &nitems, &nbytes, &ret) == Success && ret && *type != None && format)
	{
		if (format ==  8) memmove(buffer, ret, MIN(bytes, nitems));
		if (format == 16) memmove(buffer, ret, MIN(bytes, nitems * sizeof(short)));
		if (format == 32) memmove(buffer, ret, MIN(bytes, nitems * sizeof(long)));
		*items = (int)nitems; XFree(ret);
		return 1;
	}
	return 0;
}

int window_get_atom_prop(Window w, Atom atom, Atom *list, int count)
{
	Atom type; int items;
	return window_get_prop(w, atom, &type, &items, list, count*sizeof(Atom)) && type == XA_ATOM ? items:0;
}

void window_set_atom_prop(Window w, Atom prop, Atom *atoms, int count)
{
	XChangeProperty(display, w, prop, XA_ATOM, 32, PropModeReplace, (unsigned char*)atoms, count);
}

int window_get_window_prop(Window w, Atom atom, Window *list, int count)
{
	Atom type; int items;
	return window_get_prop(w, atom, &type, &items, list, count*sizeof(Window)) && type == XA_WINDOW ? items:0;
}

void window_set_window_prop(Window w, Atom prop, Window *values, int count)
{
	XChangeProperty(display, w, prop, XA_WINDOW, 32, PropModeReplace, (unsigned char*)values, count);
}

// retrieve a text property from a window
// technically we could use window_get_prop(), but this is better for character set support
char* window_get_text_prop(Window w, Atom atom)
{
	XTextProperty prop; char *res = NULL;
	char **list = NULL; int count;
	if (XGetTextProperty(display, w, &prop, atom) && prop.value && prop.nitems)
	{
		if (prop.encoding == XA_STRING)
		{
			res = malloc(strlen((char*)prop.value)+1);
			strcpy(res, (char*)prop.value);
		}
		else
		if (XmbTextPropertyToTextList(display, &prop, &list, &count) >= Success && count > 0 && *list)
		{
			res = malloc(strlen(*list)+1);
			strcpy(res, *list);
			XFreeStringList(list);
		}
	}
	if (prop.value) XFree(prop.value);
	return res;
}

int window_send_clientmessage(Window target, Window subject, Atom atom, unsigned long protocol, unsigned long mask, Time time)
{
	XEvent e; memset(&e, 0, sizeof(XEvent));
	e.xclient.type         = ClientMessage;
	e.xclient.message_type = atom;
	e.xclient.window       = subject;
	e.xclient.data.l[0]    = protocol;
	e.xclient.data.l[1]    = time;
	e.xclient.send_event   = True;
	e.xclient.format       = 32;
	int r = XSendEvent(display, target, False, mask, &e) ?1:0;
	XFlush(display);
	return r;
}

void menu_draw(textbox *text, textbox **boxes, int max_lines, int selected, char **filtered)
{
	int i;
	textbox_draw(text);
	for (i = 0; i < max_lines; i++)
	{
		textbox_font(boxes[i], XFTFONT,
			i == selected ? COLORHLFG: COLORFG,
			i == selected ? COLORHLBG: COLORBG);
		textbox_text(boxes[i], filtered[i] ? filtered[i]: "");
		textbox_draw(boxes[i]);
	}
}

int menu(char **lines, char **input, char *prompt, int selected, Time *time)
{
	int line = -1, i, j, chosen = 0, aborted = 0;
	box *mon = &monitors[monitor];

	int num_lines = 0; for (; lines[num_lines]; num_lines++);
	int max_lines = MIN(LINES, num_lines);
	selected = MAX(MIN(num_lines-1, selected), 0);

	int w = WIDTH < 101 ? (mon->w/100)*WIDTH: WIDTH;
	int x = mon->x + (mon->w - w)/2;

	XColor color; Colormap map = DefaultColormap(display, DefaultScreen(display));
	unsigned int bc = XAllocNamedColor(display, map, COLORBORDER, &color, &color) ? color.pixel: None;
	unsigned int bg = XAllocNamedColor(display, map, COLORBG,     &color, &color) ? color.pixel: None;

	Window box = XCreateSimpleWindow(display, root, x, 0, w, 300, 1, bc, bg);
	XSelectInput(display, box, ExposureMask);

	// make it an unmanaged window
	XChangeProperty(display, box, atoms[_NET_WM_STATE], XA_ATOM, 32, PropModeReplace, (unsigned char*)&atoms[_NET_WM_STATE_ABOVE], 1);
	XSetWindowAttributes attr; attr.override_redirect = True;
	XChangeWindowAttributes(display, box, CWOverrideRedirect, &attr);

	// search text input
	textbox *text = textbox_create(box, TB_AUTOHEIGHT|TB_EDITABLE, 5, 5, w-10, 1,
		XFTFONT, COLORFG, COLORBG, "", prompt);
	textbox_show(text);

	int line_height = text->font->ascent + text->font->descent;
	line_height += line_height/10;

	// filtered list display
	textbox **boxes = calloc(max_lines, sizeof(textbox*));

	for (i = 0; i < max_lines; i++)
	{
		boxes[i] = textbox_create(box, TB_AUTOHEIGHT, 5, (i+1) * line_height + 5, w-10, 1,
			XFTFONT, COLORFG, COLORBG, lines[i], NULL);
		textbox_show(boxes[i]);
	}

	// filtered list
	char **filtered = calloc(max_lines, sizeof(char*));
	int *line_map = calloc(max_lines, sizeof(int));
	int filtered_lines = max_lines;

	for (i = 0; i < max_lines; i++)
	{
		filtered[i] = lines[i];
		line_map[i] = i;
	}

	// resize window vertically to suit
	int h = line_height * (max_lines+1) + 8;
	int y = mon->y + (mon->h - h)/2;
	XMoveResizeWindow(display, box, x, y, w, h);
	XMapRaised(display, box);

	for (i = 0; i < 1000; i++)
	{
		if (XGrabKeyboard(display, box, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
			break;
		usleep(1000);
	}
	for (;;)
	{
		XEvent ev;
		XNextEvent(display, &ev);

		if (ev.type == Expose)
		{
			while (XCheckTypedEvent(display, Expose, &ev));
			menu_draw(text, boxes, max_lines, selected, filtered);
		}
		else
		if (ev.type == KeyPress)
		{
			while (XCheckTypedEvent(display, KeyPress, &ev));
			if (time) *time = ev.xkey.time;

			int rc = textbox_keypress(text, &ev);
			if (rc < 0)
			{
				chosen = 1;
				break;
			}
			else
			if (rc)
			{
				// input changed
				for (i = 0, j = 0; i < num_lines && j < max_lines; i++)
				{
					if (strcasestr(lines[i], text->text))
					{
						line_map[j] = i;
						filtered[j++] = lines[i];
					}
				}
				filtered_lines = j;
				selected = MAX(0, MIN(selected, j-1));
				for (; j < max_lines; j++)
					filtered[j] = NULL;
			}
			else
			{
				// unhandled key
				KeySym key = XkbKeycodeToKeysym(display, ev.xkey.keycode, 0, 0);

				if (key == XK_Escape)
				{
					aborted = 1;
					break;
				}

				if (key == XK_Up)
					selected = selected ? MAX(0, selected-1): MAX(0, filtered_lines-1);

				if (key == XK_Down || key == XK_Tab)
					selected = selected < filtered_lines-1 ? MIN(filtered_lines-1, selected+1): 0;
			}
			menu_draw(text, boxes, max_lines, selected, filtered);
		}
	}
	XUngrabKeyboard(display, CurrentTime);
	XDestroyWindow(display, box);

	if (chosen && filtered[selected])
		line = line_map[selected];

	if (line < 0 && !aborted && input)
		*input = strdup(text->text);

	return line;
}

void list()
{
	int i, j;
	unsigned int nwins; Window ewmh;
	XWindowAttributes attr;

	if (window_get_window_prop(root, atoms[_NET_SUPPORTING_WM_CHECK], &ewmh, 1)
		&& XGetWindowAttributes(display, ewmh, &attr))
			nwins = window_get_window_prop(root, atoms[_NET_CLIENT_LIST_STACKING], windows, MAX_WINDOWS);
	else
	{
		// non EWMH window manager. look for non-reparented windows manually
		Window w1, w2, *wins; ewmh = None;
		if (XQueryTree(display, root, &w1, &w2, &wins, &nwins) && wins)
		{
			for (i = 0, j = 0; i < nwins && j < MAX_WINDOWS; i++)
			{
				if (XGetWindowAttributes(display, wins[i], &attr)
					&& attr.map_state == IsViewable && !attr.override_redirect)
						windows[j++] = wins[i];
			}
			nwins = j;
		}
	}

	XClassHint chint;
	Window ids[MAX_WINDOWS];
	int max_class = 5, max_name = 10;
	unsigned long desktop[MAX_WINDOWS];
	char pattern[32], *lines[MAX_WINDOWS], *name[MAX_WINDOWS], *class[MAX_WINDOWS];

	memset(lines, 0, sizeof(char*) * MAX_WINDOWS);
	memset(name,  0, sizeof(char*) * MAX_WINDOWS);
	memset(class, 0, sizeof(char*) * MAX_WINDOWS);
	memset(desktop, 0, sizeof(unsigned long) * MAX_WINDOWS);

	Atom states[10];

	for (i = MIN(MAX_WINDOWS, nwins)-1; i > -1; i--)
	{
		if (!XGetWindowAttributes(display, windows[i], &attr)
			|| attr.map_state != IsViewable || attr.override_redirect)
				continue;

		// ckeck for skip taskbar
		memset(states, 0, sizeof(Atom)*10);
		if (window_get_atom_prop(windows[i], atoms[_NET_WM_STATE], states, 10))
		{
			for (j = 0; j < 10; j++)
				if (states[j] == atoms[_NET_WM_STATE_SKIP_TASKBAR])
					break;
			if (j < 10)
				continue;
		}

		// get title
		if (!(name[i] = window_get_text_prop(windows[i], atoms[_NET_WM_NAME])))
			XFetchName(display, windows[i], &name[i]);

		// get wm_class
		if (XGetClassHint(display, windows[i], &chint))
			class[i] = chint.res_class;

		if (name[i] && class[i])
		{
			max_name  = MAX(max_name,  strlen(name[i]));
			max_class = MAX(max_class, strlen(class[i]));
		}
	}
	snprintf(pattern, 32, "%%%ds  %%s", max_class);

	for (i = MIN(MAX_WINDOWS, nwins)-1, j = 0; i > -1; i--)
	{
		if (name[i] && class[i])
		{
			char *line = calloc(1, max_class + max_name + 10);
			sprintf(line, pattern, class[i], name[i]);
			ids[j] = windows[i]; lines[j++] = line;
		}
	}

	char *input = NULL; Time time;
	int n = menu(lines, &input, "> ", 1, &time);
	if (n >= 0 && lines[n])
	{
		window_send_clientmessage(root, ids[n], atoms[_NET_ACTIVE_WINDOW], 2, // 2 = pager
			SubstructureNotifyMask | SubstructureRedirectMask, time);
		if (!ewmh)
		{
			XRaiseWindow(display, ids[n]);
			XWMHints *hints = XGetWMHints(display, ids[n]);
			XSetInputFocus(display, (hints->flags & InputHint && hints->input ? ids[n]: PointerRoot),
				RevertToPointerRoot, CurrentTime);
		}
	}
	else
	// act as a launcher
	if (input)
	{
		setsid();
		execlp("/bin/sh", "sh", "-c", input, NULL);
		exit(EXIT_FAILURE);
	}
}

void setup()
{
	int i;

	if (!(display = XOpenDisplay(0)))
		errx(EXIT_FAILURE, "cannot open display");

	XSetErrorHandler(oops);
	root = DefaultRootWindow(display);

	for (i = 0; i < ATOMS; i++) atoms[i] = XInternAtom(display, atom_names[i], False);

	// default non-multi-head setup
	monitors[0].w = WidthOfScreen(DefaultScreenOfDisplay(display));
	monitors[0].h = HeightOfScreen(DefaultScreenOfDisplay(display));

	// support multi-head.
	XineramaScreenInfo *info;
	if (XineramaIsActive(display) && (info = XineramaQueryScreens(display, &nmonitors)))
	{
		nmonitors = MIN(nmonitors, MAX_MONITORS);
		for (i = 0; i < nmonitors; i++)
		{
			monitors[i].x = info[i].x_org;
			monitors[i].y = info[i].y_org;
			monitors[i].w = info[i].width;
			monitors[i].h = info[i].height;
		}
		XFree(info);

		// display on same monitor as active window or mouse pointer
		Window active; XWindowAttributes attr; int x = 0, y = 0;
		Window rr, cr; int rxr, ryr, wxr, wyr; unsigned int mr;
		if (window_get_window_prop(root, atoms[_NET_ACTIVE_WINDOW], &active, 1)
			&& XGetWindowAttributes(display, active, &attr))
		{
			x = attr.x + attr.width/2;
			y = attr.y + attr.height/2;
		}
		else
		if (XQueryPointer(display, root, &rr, &cr, &rxr, &ryr, &wxr, &wyr, &mr))
		{
			x = rxr;
			y = ryr;
		}
		for (i = 0; i < nmonitors; i++)
		{
			if (INTERSECT(monitors[i].x, monitors[i].y, monitors[i].w, monitors[i].h, x, y, 1, 1))
				{ monitor = i; break; }
		}
	}
}

int main(int argc, char *argv[])
{
	setup();
	list();
	return EXIT_SUCCESS;
}
