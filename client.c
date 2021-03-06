/*
 * This is most of the abstraction for dealing with clients.
*/
#include "client.h"

client_t *head, *focused;

// Get SmallWM to ignore this window (icons or resizing & moving)
void ignore(Display * dpy, Window win)
{
	XSetWindowAttributes attr;
	attr.override_redirect = True;
	XChangeWindowAttributes(dpy, win, CWOverrideRedirect, &attr);
}

// Tail of the client list
client_t *tail()
{
	client_t *c = head;
	while (c->next)
		c = c->next;
	return c;
}

// Get the client based upon the icon
client_t *fromicon(Window icon)
{
	client_t *c = head;
	while (c) {
		if (c->state == Hidden && c->icon->win == icon)
			return c;
		c = c->next;
	}
	return NULL;
}

// Get the client based upon the window
client_t *fromwin(Window win)
{
	client_t *c = head;
	while (c) {
		if (c->state == Visible && c->win == win)
			return c;
		c = c->next;
	}
	return NULL;
}

// Register a new client
client_t *create(Display * dpy, Window win)
{
	// Take care of things like dialogs who we
	// shouldn't manage anyway
	XWindowAttributes attr;
	XGetWindowAttributes(dpy, win, &attr);

	if (attr.override_redirect)
		return NULL;

	// Also make sure this window doesn't already exist
	if (fromwin(win))
		return NULL;

	client_t *cli = malloc(sizeof(client_t));
	XSetWindowBorderWidth(dpy, win, 3);

	cli->dpy = dpy;
	cli->win = win;
	cli->pholder = None;
	cli->icon = NULL;
	cli->x = attr.x;
	cli->y = attr.y;
	cli->w = attr.width;
	cli->h = attr.height;
	cli->state = Visible;
	cli->class = attr.class;
	cli->next = NULL;

	if (!head)
		head = cli;
	else {
		client_t *tl = tail();
		tl->next = cli;
	}

	chfocus(cli);

	return cli;
}

// Get rid of an existing client
void destroy(client_t * client, int danger)
{
	client_t *prec = head;
	client_t *succ = client->next;
	while (prec && prec->next != client)
		prec = prec->next;

	if (!prec)
		return;

	if (focused == client)
		focused = None;

	prec->next = succ;
	if (client->state == Hidden)
		unhide(client, 1);

	if (!danger)
		XDestroyWindow(client->dpy, client->win);

	free(client);

	updicons();
}

// Hide (iconify) a client
void hide(client_t * client)
{
	if (client->state != Visible)
		return;

	client->icon = malloc(sizeof(icon_t));

	client->icon->win = XCreateSimpleWindow(client->dpy,
						RootWindow(client->dpy,
							   DefaultScreen
							   (client->dpy)),
						-200, -200, ICON_WIDTH,
						ICON_HEIGHT, 1,
						BLACK(client->dpy),
						WHITE(client->dpy));

	ignore(client->dpy, client->icon->win);

	XSelectInput(client->dpy, client->icon->win,
		     ButtonPressMask | ButtonReleaseMask | ExposureMask);

	XMapWindow(client->dpy, client->icon->win);

	client->icon->gc = XCreateGC(client->dpy, client->icon->win, 0, NULL);

	XUnmapWindow(client->dpy, client->win);

	client->state = Hidden;

	updicons();
}

// Unhide (deiconify) a client
void unhide(client_t * client, int danger)
{
	if (client->state != Hidden)
		return;

	if (!danger) {
		XMapWindow(client->dpy, client->win);
		XRaiseWindow(client->dpy, client->win);
	}

	XDestroyWindow(client->dpy, client->icon->win);
	XFreeGC(client->dpy, client->icon->gc);
	free(client->icon);

	client->state = Visible;

	updicons();
}

// Begin moving/resizing a client
void beginmvrsz(client_t * client)
{
	if (client->state != Visible)
		return;
	client->state = MoveResz;

	XUnmapWindow(client->dpy, client->win);

	client->pholder = XCreateSimpleWindow(client->dpy,
					      RootWindow(client->dpy,
							 DefaultScreen
							 (client->dpy)),
					      client->x, client->y, client->w,
					      client->h, 1, BLACK(client->dpy),
					      WHITE(client->dpy));

	ignore(client->dpy, client->pholder);

	XMapWindow(client->dpy, client->pholder);

	XGrabPointer(client->dpy, client->pholder, True,
		     PointerMotionMask | ButtonReleaseMask,
		     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

	XRaiseWindow(client->dpy, client->pholder);
}

// Stop moving/resizing a client
void endmoversz(client_t * client)
{
	if (client->state != MoveResz)
		return;
	client->state = Visible;

	XUngrabPointer(client->dpy, CurrentTime);

	XWindowAttributes attr;
	XGetWindowAttributes(client->dpy, client->pholder, &attr);
	XDestroyWindow(client->dpy, client->pholder);

	client->x = attr.x;
	client->y = attr.y;
	client->w = attr.width;
	client->h = attr.height;

	XMapWindow(client->dpy, client->win);
	XMoveResizeWindow(client->dpy, client->win, client->x, client->y,
			  client->w, client->h);

	raise_(client);
}

// Raise a client to the toplevel
void raise_(client_t * client)
{
	if (client->state != Visible)
		return;
	XRaiseWindow(client->dpy, client->win);
}

// Lower a client to the bottomlevel
void lower(client_t * client)
{
	if (client->state != Visible)
		return;
	XLowerWindow(client->dpy, client->win);
}

// Maximize a client (doesn't work with xrandr)
void maximize(client_t * client)
{
	client->x = 0;
	client->y = ICON_HEIGHT;
	client->w = SCREEN_WIDTH(client->dpy);
	client->h = SCREEN_HEIGHT(client->dpy) - ICON_HEIGHT;

	XMoveResizeWindow(client->dpy, client->win, client->x, client->y,
			  client->w, client->h);
}

// Update all icon positions, and redraw them
void updicons()
{
	client_t *curr = head;
	int x = 0;
	int y = 0;

	while (curr) {
		if (curr->state != Hidden) {
			curr = curr->next;
			continue;
		}

		if (x + ICON_WIDTH > SCREEN_WIDTH(curr->dpy)) {
			x = 0;
			y += ICON_HEIGHT;
		}

		curr->icon->x = x;
		curr->icon->y = y;

		XMoveWindow(curr->dpy, curr->icon->win, curr->icon->x,
			    curr->icon->y);

		paint(curr);

		curr = curr->next;
		x += ICON_WIDTH;
	}
}

// Paint a specific icon
void paint(client_t * client)
{
	char *title = malloc(200);
	XFetchName(client->dpy, client->win, &title);

	XClearWindow(client->dpy, client->icon->win);
	XDrawString(client->dpy, client->icon->win, client->icon->gc, 0,
		    ICON_HEIGHT, (title ? title : " "),
		    MIN((title ? strlen(title) : 0), 10));

	free(title);
}

/* Change focus
 *
 * The idea for this was snatched (blatantly) from 9wm, which has a really
 * neat click-to-focus implementation that's insanely easy to follow.
*/
void chfocus(client_t * client)
{
	if (focused) {
		XGrabButton(focused->dpy, AnyButton, AnyModifier, focused->win,
			    False, ButtonPressMask | ButtonReleaseMask,
			    GrabModeAsync, GrabModeAsync, None, None);
	}

	if (client && client->class != InputOnly) {
		XWindowAttributes attr;
		XGetWindowAttributes(client->dpy, client->win, &attr);
		if (attr.map_state != IsViewable)
			return;

		XUngrabButton(client->dpy, AnyButton, AnyModifier, client->win);
		XSetInputFocus(client->dpy, client->win, RevertToPointerRoot,
			       CurrentTime);

		Window focusr;
		int revert;
		XGetInputFocus(client->dpy, &focusr, &revert);
		if (focusr != client->win) {
			XGrabButton(client->dpy, AnyButton, AnyModifier,
				    client->win, False,
				    ButtonPressMask | ButtonReleaseMask,
				    GrabModeAsync, GrabModeAsync, None, None);
			focused = None;
		} else {
			raise_(client);
			focused = client;
		}
	}
}
