#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ctype.h>
#include <sys/poll.h>

#include "connection.h"
#include "wayland-client.h"

static const char socket_name[] = "\0wayland";

struct wl_buffer {
	char data[4096];
	int head, tail;
};

struct wl_proxy {
	struct wl_display *display;
	uint32_t id;
};

struct wl_display {
	struct wl_proxy proxy;
	struct wl_connection *connection;
	int fd;
	uint32_t id;
};

struct wl_surface {
	struct wl_proxy proxy;
};

struct wl_display *
wl_display_create(const char *address,
		  wl_connection_update_func_t update, void *data)
{
	struct wl_display *display;
	struct sockaddr_un name;
	socklen_t size;
	char buffer[256];
	uint32_t id, length;

	display = malloc(sizeof *display);
	if (display == NULL)
		return NULL;

	memset(display, 0, sizeof *display);
	display->fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (display->fd < 0) {
		free(display);
		return NULL;
	}

	name.sun_family = AF_LOCAL;
	memcpy(name.sun_path, address, strlen(address + 1) + 2);

	size = offsetof (struct sockaddr_un, sun_path) + sizeof socket_name;

	if (connect(display->fd, (struct sockaddr *) &name, size) < 0) {
		close(display->fd);
		free(display);
		return NULL;
	}

	/* FIXME: We'll need a protocol for getting a new range, I
	 * guess... */
	read(display->fd, &display->id, sizeof display->id);

	/* FIXME: actually discover advertised objects here. */
	read(display->fd, &id, sizeof id);
	read(display->fd, &length, sizeof length);
	read(display->fd, buffer, (length + 3) & ~3);

	display->proxy.display = display;
	display->proxy.id = id;

	display->connection = wl_connection_create(display->fd,
						   update, data);

	return display;
}

void
wl_display_destroy(struct wl_display *display)
{
	wl_connection_destroy(display->connection);
	close(display->fd);
	free(display);
}

int
wl_display_get_fd(struct wl_display *display)
{
	return display->fd;
}

static void
handle_event(struct wl_connection *connection)
{
	uint32_t p[2], opcode, size;

	wl_connection_copy(connection, p, sizeof p);
	opcode = p[1] & 0xffff;
	size = p[1] >> 16;
	printf("signal from object %d, opcode %d, size %d\n",
	       p[0], opcode, size);
	wl_connection_consume(connection, sizeof p);
}

void
wl_display_iterate(struct wl_display *display, uint32_t mask)
{
	uint32_t p[2], opcode, size;
	int len;

	len = wl_connection_data(display->connection, mask);
	while (len > 0) {
		if (len < sizeof p)
			break;
		
		wl_connection_copy(display->connection, p, sizeof p);
		opcode = p[1] & 0xffff;
		size = p[1] >> 16;
		if (len < size)
			break;

		handle_event(display->connection);
	}

	if (len < 0) {
		fprintf(stderr, "read error: %m\n");
		exit(EXIT_FAILURE);
	}
}

#define WL_DISPLAY_CREATE_SURFACE 0

struct wl_surface *
wl_display_create_surface(struct wl_display *display)
{
	struct wl_surface *surface;
	uint32_t request[3];

	surface = malloc(sizeof *surface);
	if (surface == NULL)
		return NULL;

	surface->proxy.id = display->id++;
	surface->proxy.display = display;

	request[0] = display->proxy.id;
	request[1] = WL_DISPLAY_CREATE_SURFACE | ((sizeof request) << 16);
	request[2] = surface->proxy.id;
	wl_connection_write(display->connection, request, sizeof request);

	return surface;
}

#define WL_SURFACE_DESTROY	0
#define WL_SURFACE_ATTACH	1
#define WL_SURFACE_MAP		2

void wl_surface_destroy(struct wl_surface *surface)
{
	uint32_t request[2];

	request[0] = surface->proxy.id;
	request[1] = WL_SURFACE_DESTROY | ((sizeof request) << 16);

	wl_connection_write(surface->proxy.display->connection,
			    request, sizeof request);
}

void wl_surface_attach(struct wl_surface *surface,
		       uint32_t name, int width, int height, int stride)
{
	uint32_t request[6];

	request[0] = surface->proxy.id;
	request[1] = WL_SURFACE_ATTACH | ((sizeof request) << 16);
	request[2] = name;
	request[3] = width;
	request[4] = height;
	request[5] = stride;

	wl_connection_write(surface->proxy.display->connection,
			    request, sizeof request);
}


void wl_surface_map(struct wl_surface *surface,
		    int32_t x, int32_t y, int32_t width, int32_t height)
{
	uint32_t request[6];

	request[0] = surface->proxy.id;
	request[1] = WL_SURFACE_MAP | ((sizeof request) << 16);
	request[2] = x;
	request[3] = y;
	request[4] = width;
	request[5] = height;

	wl_connection_write(surface->proxy.display->connection,
			    request, sizeof request);
}