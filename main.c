#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <wayland-client.h>
#include <wayland-util.h>

struct surface {
	struct context *context;
	struct wl_surface *surface;
	struct wl_subsurface *subsurface;
	struct wl_shell_surface *shell_surface;
	struct wl_buffer *buffer;

	int width;
	int height;
	int stride;
	int fd;
	uint32_t *data;
	uint32_t color;

	struct surface *parent;
};

struct context {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_subcompositor *subcompositor;
	struct wl_shell *shell;
	struct wl_shm *shm;
};

static void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(EXIT_FAILURE);
}

static int
surface_alloc_data(struct surface *surface)
{
	char filename[] = "/tmp/wayland-shm-XXXXXX";
	uint32_t *data;
	int stride;
	int size;
	int fd;
	int i;

	fd = mkstemp(filename);
	if (fd < 0)
		die("failed to create file for SHM buffer: %m");

	unlink(filename);

	stride = surface->width * 4;
	size = surface->height * stride;

	if (fallocate(fd, 0, 0, size) < 0)
		die("failed to allocate %d bytes for SHM buffer: %m", size);

	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED)
		die("failed to map SHM buffer data: %m");

	for (i = 0; i < surface->width * surface->height; i++)
		data[i] = surface->color;

	surface->fd = fd;
	surface->data = data;
	surface->stride = stride;

	return 0;
}

static struct surface *
surface_create(struct context *context, struct surface *parent,
	       int width, int height, int x, int y, uint32_t color)
{
	struct surface *surface;
	struct wl_shm_pool *pool;

	surface = calloc(1, sizeof *surface);
	if (!surface)
		return NULL;

	surface->fd = -1;
	surface->context = context;
	surface->width = width;
	surface->height = height;
	surface->color = color;
	surface->parent = parent;

	if (surface_alloc_data(surface)) {
		free(surface);
		return NULL;
	}

	pool = wl_shm_create_pool(context->shm, surface->fd,
				  surface->stride * surface->height);

	surface->buffer = wl_shm_pool_create_buffer(pool, 0,
						    surface->width,
						    surface->height,
						    surface->stride,
						    WL_SHM_FORMAT_ARGB8888);

	wl_shm_pool_destroy(pool);

	surface->surface = wl_compositor_create_surface(context->compositor);
	if (surface->parent) {
		surface->subsurface = wl_subcompositor_get_subsurface(
			context->subcompositor, surface->surface,
			parent->surface);

		wl_subsurface_set_position(surface->subsurface, x, y);
		wl_subsurface_set_desync(surface->subsurface);
	} else {
		surface->shell_surface =
			wl_shell_get_shell_surface(context->shell,
						   surface->surface);
		wl_shell_surface_set_toplevel(surface->shell_surface);
	}

	wl_surface_attach(surface->surface, surface->buffer, 0, 0);
	wl_surface_damage(surface->surface, 0, 0,
			  surface->width, surface->height);
	wl_surface_commit(surface->surface);

	return surface;
}

static void
registry_handle_global(void *data, struct wl_registry *registry,
		       uint32_t id, const char *interface, uint32_t version)
{
	struct context *ctx = data;

	if (!strcmp(interface, "wl_compositor")) {
		ctx->compositor =
			wl_registry_bind(registry, id,
					 &wl_compositor_interface, 1);
	} else if (!strcmp(interface, "wl_subcompositor")) {
		ctx->subcompositor =
			wl_registry_bind(registry, id,
					 &wl_subcompositor_interface, 1);
	} else if (!strcmp(interface, "wl_shell")) {
		ctx->shell =
			wl_registry_bind(registry, id, &wl_shell_interface, 1);
	} else if (!strcmp(interface, "wl_shm")) {
		ctx->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

enum {
	RED,
	GREEN,
	BLUE,
	YELLOW,
	COUNT
};

static struct context *
context_init(void)
{
	struct context *ctx;
	struct surface *surf[COUNT];

	ctx = calloc(1, sizeof (*ctx));
	ctx->display = wl_display_connect(NULL);
	if (!ctx->display)
		die("failed to init wayland display");

	ctx->registry = wl_display_get_registry(ctx->display);
	wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
	wl_display_roundtrip(ctx->display);

	assert(ctx->compositor != NULL);
	assert(ctx->subcompositor != NULL);
	assert(ctx->shell != NULL);

	surf[BLUE] = surface_create(ctx, NULL, 200, 200, 0, 0, 0xff0000ff);
	if (!surf[BLUE])
		die("failed to create root surface");

	surf[RED] = surface_create(ctx, surf[BLUE], 200, 200, -25, -25,
				   0xffff0000);
	surf[GREEN] = surface_create(ctx, surf[BLUE], 200, 200, 25, 25,
				     0xff00ff00);

	// A subsurface in subsurface:
	surf[YELLOW] = surface_create(ctx, surf[GREEN], 200, 200, 25, 25, 0xffffff00);
	

#if 0
	wl_subsurface_place_below(surf[GREEN]->subsurface, surf[BLUE]->surface);
	wl_subsurface_place_below(surf[RED]->subsurface, surf[GREEN]->surface);
#else
	wl_subsurface_place_below(surf[RED]->subsurface, surf[GREEN]->surface);
	wl_subsurface_place_below(surf[GREEN]->subsurface, surf[BLUE]->surface);
#endif

	wl_surface_commit(surf[YELLOW]->surface);
	wl_surface_commit(surf[GREEN]->surface);
	wl_surface_commit(surf[RED]->surface);
	wl_surface_commit(surf[BLUE]->surface);

	return ctx;
}

static void
context_destroy(struct context *ctx)
{
	wl_display_disconnect(ctx->display);

	free(ctx);
}

static void
seed(void)
{
	int fd = open("/dev/urandom", O_RDONLY);
	unsigned int s;

	if (fd < 0)
		return;

	read(fd, &s, sizeof (s));
	srand(s);
	close(fd);
}

int main(int argc, char *argv[])
{
	struct context *ctx;

	seed();

	ctx = context_init();

	while (wl_display_dispatch(ctx->display) >= 0)
		;

	context_destroy(ctx);

	return 0;
}
