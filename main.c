#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

// ======= utils

void Spawn(const char *cmd[])
{
	if (fork() == 0) {
		if (setsid() == -1)
			exit(1);
		execvp(cmd[0], (char *const *)cmd);
		exit(1);
	}
}

// ====== constants

#define SUPER Mod4Mask
#define SHIFT ShiftMask

#define NUM_WORKSPACES 10

// ======= client

typedef struct Client {
	Window win;
	struct Client *next;
} Client;

// ======= workspace

typedef struct Workspace {
	Client *clients;
} Workspace;

void Workspace_AddClientToWs(Workspace *ws, Client *c)
{
	c->next = ws->clients;
	ws->clients = c;

	Client *t = ws->clients;
	while (t) {
		printf("Client added, win id: %ld\n", t->win);
		t = t->next;
	}
}

// ======= window manager

typedef struct WindowManager {
	Display *dpy;
	Window root;
	Workspace workspaces[NUM_WORKSPACES];
	int current_ws;
	int last_ws;
	int should_close;
} WindowManager;

void WindowManager_init(WindowManager *wm)
{
	wm->dpy = XOpenDisplay(NULL);
	if (!wm->dpy) {
		fprintf(stderr, "Can't open display\n");
		exit(1);
	}

	wm->root = DefaultRootWindow(wm->dpy);
	wm->current_ws = 0;
	wm->should_close = 0;

	for (int i = 0; i < NUM_WORKSPACES; i++)
		wm->workspaces[i].clients = NULL;

	XSelectInput(wm->dpy, wm->root, SubstructureRedirectMask | SubstructureNotifyMask);
}

void WindowManager_GrabKeys(WindowManager *wm)
{
	KeyCode code = XKeysymToKeycode(wm->dpy, XK_Return);
	XGrabKey(wm->dpy, code, SUPER, wm->root, True, GrabModeAsync, GrabModeAsync);

	code = XKeysymToKeycode(wm->dpy, XK_q);
	XGrabKey(wm->dpy, code, SUPER, wm->root, True, GrabModeAsync, GrabModeAsync);

	code = XKeysymToKeycode(wm->dpy, XK_q);
	XGrabKey(wm->dpy, code, SUPER | SHIFT, wm->root, True, GrabModeAsync, GrabModeAsync);

	code = XKeysymToKeycode(wm->dpy, XK_Escape);
	XGrabKey(wm->dpy, code, SUPER, wm->root, True, GrabModeAsync, GrabModeAsync);

	for (int i = 0; i < NUM_WORKSPACES; i++) {
		code = XKeysymToKeycode(wm->dpy, XK_1 + i);
		XGrabKey(wm->dpy, code, SUPER, wm->root, True, GrabModeAsync, GrabModeAsync);
	}
}

void WindowManager_SwitchWs(WindowManager *wm, int wsIndex)
{
	if (wsIndex == wm->current_ws || wsIndex < 0 || wsIndex >= NUM_WORKSPACES)
		return;

	// hide all windows in the current ws
	Workspace *ws = &wm->workspaces[wm->current_ws];
	Client *c = ws->clients;
	while (c) {
		XUnmapWindow(wm->dpy, c->win);
		c = c->next;
	}

	// switch wm
	wm->last_ws = wm->current_ws;
	wm->current_ws = wsIndex;

	// show all windows in the new ws
	ws = &wm->workspaces[wm->current_ws];
	c = ws->clients;
	while (c) {
		XMapWindow(wm->dpy, c->win);
		c = c->next;
	}

	printf("Switched to workspace: %d\n", wsIndex + 1);
}

Window WindowManager_GetFocusedWindow(WindowManager *wm)
{
	Window root, child;
	int rx, ry, wx, wy;
	unsigned int mask;
	XQueryPointer(wm->dpy, wm->root, &root, &child, &rx, &ry, &wx, &wy, &mask);
	return child;
}

void WindowManager_CloseWindow(WindowManager *wm, Window win)
{
	Atom *protocols;
	int n;
	Atom wm_delete = XInternAtom(wm->dpy, "WM_DELETE_WINDOW", False);
	if (XGetWMProtocols(wm->dpy, win, &protocols, &n)) {
		for (int i = 0; i < n; i++) {
			if (protocols[i] == wm_delete) {
				XEvent msg = {0};
				msg.xclient.type = ClientMessage;
				msg.xclient.message_type = XInternAtom(wm->dpy, "WM_PROTOCOLS", True);
				msg.xclient.display = wm->dpy;
				msg.xclient.window = win;
				msg.xclient.format = 32;
				msg.xclient.data.l[0] = wm_delete;
				msg.xclient.data.l[1] = CurrentTime;
				XSendEvent(wm->dpy, win, False, NoEventMask, &msg);
				XFree(protocols);
				return;
			}
		}
		XFree(protocols);
	}
	XDestroyWindow(wm->dpy, win);
}

void WindowManager_TileMasterAndStack(WindowManager *wm)
{
	XWindowAttributes wa;
	XGetWindowAttributes(wm->dpy, wm->root, &wa);
	int screen_width = wa.width;
	int screen_height = wa.height;

	Workspace *ws = &wm->workspaces[wm->current_ws];

	int window_count = 0;
	for (Client *c = ws->clients; c; c = c->next) {
		window_count++;
	}

	if (window_count == 0)
		return;

	if (window_count >= 2) {
		int master_width = screen_width * 0.5; // 50%
		int master_height = screen_height;

		int stack_width = screen_width - master_width;
		int stack_height =
			window_count - 1 == 0 ? screen_height : screen_height / (window_count - 1);

		int x_offset_master = 0;
		int x_offset_stack = master_width;
		int y_offset_stack = 0;

		Client *c = ws->clients;

		if (c) {
			XMoveResizeWindow(wm->dpy, c->win, x_offset_master, 0, master_width, master_height);
			c = c->next;
		}

		while (c) {
			XMoveResizeWindow(wm->dpy, c->win, x_offset_stack, y_offset_stack, stack_width,
							  stack_height);
			y_offset_stack += stack_height;
			c = c->next;
		}
		return;
	} else {
		Client *c = ws->clients;
		XMoveResizeWindow(wm->dpy, c->win, 0, 0, screen_width, screen_height);
	}

	XFlush(wm->dpy);
}

void WindowManager_OnKeyPressed(WindowManager *wm, XKeyEvent *e)
{
	KeySym sym = XkbKeycodeToKeysym(wm->dpy, e->keycode, 0, 0);

	// switch workspace: SUPER + 1..9
	if ((e->state & SUPER) && sym >= XK_1 && sym <= XK_9)
		WindowManager_SwitchWs(wm, sym - XK_1);

	// switch to last workspace: SUPER + Escape
	if ((e->state & SUPER) && sym == XK_Escape)
		WindowManager_SwitchWs(wm, wm->last_ws);

	// launch term: SUPER + Return
	if ((e->state & SUPER) && sym == XK_Return) {
		const char *cmd[] = {"wezterm", NULL};
		Spawn(cmd);
	}

	// close window: SUPER + Q
	if ((e->state & SUPER) && sym == XK_q) {
		Window win = WindowManager_GetFocusedWindow(wm);
		if (win != None)
			WindowManager_CloseWindow(wm, win);
	}

	// close window manager: SUPER + SHIFT + Q
	if ((e->state & (SUPER | SHIFT)) && sym == XK_q)
		wm->should_close = 1;
}

void WindowManager_OnMapRequeset(WindowManager *wm, XMapRequestEvent *e)
{
	Client *c = malloc(sizeof(Client));
	c->win = e->window;
	c->next = NULL;

	Workspace_AddClientToWs(&wm->workspaces[wm->current_ws], c);
	XMapWindow(wm->dpy, e->window);
	WindowManager_TileMasterAndStack(wm);
}

void WindowManager_OnUnmap(WindowManager *wm, XUnmapEvent *e)
{
	Window w = e->window;
	Workspace *ws = &wm->workspaces[wm->current_ws];
	Client **prev = &ws->clients;

	for (Client *c = ws->clients; c; c = c->next) {
		if (c->win == w) {
			*prev = c->next;
			free(c);
			break;
		}
		prev = &c->next;
	}
	WindowManager_TileMasterAndStack(wm);
}

void WindowManager_run(WindowManager *wm)
{
	XEvent ev;

	while (!XNextEvent(wm->dpy, &ev) || wm->should_close) {
		if (ev.type == KeyPress) {
			XKeyEvent *ke = &ev.xkey;
			WindowManager_OnKeyPressed(wm, ke);
		} else if (ev.type == MapRequest) {
			XMapRequestEvent *map = &ev.xmaprequest;
			WindowManager_OnMapRequeset(wm, map);
		} else if (ev.type == UnmapNotify) {
			XUnmapEvent *unmap = &ev.xunmap;
			WindowManager_OnUnmap(wm, unmap);
		}
	}
}

void WindowManager_destroy(WindowManager *wm)
{
	for (int i = 0; i < NUM_WORKSPACES; i++) {
		Client *c = wm->workspaces[i].clients;
		while (c) {
			XDestroyWindow(wm->dpy, c->win);
			Client *temp = c;
			c = c->next;
			free(temp);
		}
	}
	XCloseDisplay(wm->dpy);
}

int main(void)
{
	WindowManager wm = {};

	WindowManager_init(&wm);
	WindowManager_GrabKeys(&wm);
	WindowManager_run(&wm);

	WindowManager_destroy(&wm);

	return 0;
}
