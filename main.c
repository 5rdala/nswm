#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
} WindowManager;

void WindowManager_GrabKeys(WindowManager *wm)
{
	KeyCode code = XKeysymToKeycode(wm->dpy, XK_Return);
	XGrabKey(wm->dpy, code, SUPER, wm->root, True, GrabModeAsync, GrabModeAsync);

	code = XKeysymToKeycode(wm->dpy, XK_q);
	XGrabKey(wm->dpy, code, SUPER, wm->root, True, GrabModeAsync, GrabModeAsync);

	for (int i = 0; i < NUM_WORKSPACES; i++) {
		code = XKeysymToKeycode(wm->dpy, XK_1 + i);
		XGrabKey(wm->dpy, code, SUPER, wm->root, True, GrabModeAsync, GrabModeAsync);
	}
}

Window WindowManager_GetFocusedWindow(WindowManager *wm)
{
	Window root, child;
	int rx, ry, wx, wy;
	unsigned int mask;
	XQueryPointer(wm->dpy, wm->root, &root, &child, &rx, &ry, &wx, &wy, &mask);
	return child;
}

void WindowManager_OnKeyPressed(WindowManager *wm, XKeyEvent *e)
{
	KeySym sym = XkbKeycodeToKeysym(wm->dpy, e->keycode, 0, 0);
	printf("KeyCode: %d, Keysym: %s, State: %u\n", e->keycode, XKeysymToString(sym), e->state);

	if ((e->state & SUPER) && sym == XK_Return) {
		const char *cmd[] = {"wezterm", NULL};
		Spawn(cmd);
	}
}

void WindowManager_OnMapRequeset(WindowManager *wm, XMapRequestEvent *e)
{
	Client *c = malloc(sizeof(Client));
	c->win = e->window;
	c->next = NULL;

	Workspace_AddClientToWs(&wm->workspaces[wm->current_ws], c);
	XMapWindow(wm->dpy, e->window);
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
}

void WindowManager_run(WindowManager *wm)
{
	XEvent ev;

	while (!XNextEvent(wm->dpy, &ev)) {
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

void WindowManager_init(WindowManager *wm)
{
	wm->dpy = XOpenDisplay(NULL);
	if (!wm->dpy) {
		fprintf(stderr, "Can't open display\n");
		exit(1);
	}

	wm->root = DefaultRootWindow(wm->dpy);
	wm->current_ws = 0;

	for (int i = 0; i < NUM_WORKSPACES; i++)
		wm->workspaces[i].clients = NULL;

	XSelectInput(wm->dpy, wm->root, SubstructureRedirectMask | SubstructureNotifyMask);
}

int main(void)
{
	WindowManager wm = {};

	WindowManager_init(&wm);
	WindowManager_GrabKeys(&wm);
	WindowManager_run(&wm);

	XCloseDisplay(wm.dpy);
	return 0;
}
