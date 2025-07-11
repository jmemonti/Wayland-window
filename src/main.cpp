#include <stdio.h>
#include <string.h>
#include <wayland-client.h>
#include "../include/xdg-shell-client-protocol.h"
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

//Tamanio de la ventana
#define  WIDTH 300
#define HEIGHT 200

// Formato de píxel
#define BYTES_PER_PIXEL 4 // ARGB8888


//Estructura de datos para almacenar recursos(protocolos)
struct Datos {
    struct xdg_wm_base *xdg;
    struct wl_compositor *compositor;
    struct wl_shm *shm; // necesario para crear un buffer de memoria compartida y mostrar algo en la surface
};

//Para usarlo en el configure de la ventana
struct WindowData {
    struct wl_shm *shm;
    struct wl_surface *surface;
    int32_t width;
    int32_t height;
    bool running;
};

void xdg_ping(void *data, struct xdg_wm_base *xdg, uint32_t serial)
{
    xdg_wm_base_pong(xdg, serial);
}

const struct xdg_wm_base_listener xdg_listener 
{
    .ping = xdg_ping,
};

//funciones para el listener
static void registry_listener_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version)
{
    //Obtener la struct de datos
    struct Datos *datos = (struct Datos*) data; //cast a data para que pase de void* a Datos*

    //buscar recurso "xdg_wm_base" 
    if (!strcmp(interface, "xdg_wm_base")) {
        //solicitar objeto xdg_wm_base 
        datos->xdg = (struct xdg_wm_base*) wl_registry_bind(registry, id, &xdg_wm_base_interface, version); //cast porque wl_registry_bind devuelve void*
        xdg_wm_base_add_listener(datos->xdg, &xdg_listener, NULL);
    }

    //buscar el recurso "wl_compositor"
    if (!strcmp(interface, "wl_compositor")) {
        //solicitar objeto wl_compositor    
        datos->compositor = (struct wl_compositor*) wl_registry_bind(registry, id, &wl_compositor_interface, version);
    }

    //bucar el recurso "wl_shm"
    if (!strcmp(interface, "wl_shm")) {
        //solicitar objeto wl_shm    
        datos->shm = (struct wl_shm*) wl_registry_bind(registry, id, &wl_shm_interface, version);
    }
}

static void registry_listener_global_remove(void *data, struct wl_registry *registry, uint32_t id)
{
    printf("Got a registry losing event for %d\n", id);
}

//Crear el fichero de memoria compartida
int create_shm_file(size_t size) 
{
    char tmp_template[] = "/tmp/wayland-shm-XXXXXX";
    int fd = mkstemp(tmp_template);
    if (fd < 0) return -1;
    unlink(tmp_template); // se borra automáticamente cuando el programa termine
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

//Crear buffer de memoria compartida
struct wl_buffer* create_buffer (struct wl_shm *shm, uint32_t width, uint32_t height)
{
    size_t stride = width * BYTES_PER_PIXEL;
    size_t size = stride * height;

    //Asignar un fichero de memoria compartida con el tamanio correcto
    int fd = create_shm_file(size);

    //Mapear el fichero de memoria compartida
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    //Llenar la memoria con negro (ARGB: 0XFF000000)
    uint32_t *pixels = (uint32_t *)data;

    for (size_t i = 0; i < (size / 4); ++i) {
        pixels[i] = 0xFF000000; // Negro opaco
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);

    //delete
    wl_shm_pool_destroy(pool);
    close(fd); // el fd ya no es necesario después del create_buffer
    munmap(data, size);

    return buffer;
}

//Configure para evitar problemas con la ventana
static void handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    struct WindowData *win = (struct WindowData *)data;

    // Necesario para que el compositor "active" la ventana
    xdg_surface_ack_configure(xdg_surface, serial);

    // Aquí recreas el buffer y lo adjuntas con el tamaño correcto
    struct wl_buffer *buffer = create_buffer(win->shm, win->width, win->height); // asegúrate de tener el tamaño que quieres

    wl_surface_attach(win->surface, buffer, 0, 0);

    wl_surface_commit(win->surface);
}

//Surface listener para aniadir el configure
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = handle_configure
};

//Toplevel listener para evitar problemas con la ventana
static void toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) 
{
    struct WindowData *win = static_cast<WindowData *>(data);

    if (win->width != width || win->height != height)
    {
        win->width = width;
        win->height = height;
    }
}

static void toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel) 
{
    struct WindowData *win = static_cast<WindowData *>(data);

    win->running = false; //cerrar ventana
}

static void toplevel_handle_configure_bounds(void *data,
    struct xdg_toplevel *xdg_toplevel,
    int32_t width, int32_t height)
{
    // puedes ignorar este evento si no lo necesitas
    (void)data; (void)xdg_toplevel; (void)width; (void)height;
}

static void toplevel_handle_wm_capabilities(void *data,
    struct xdg_toplevel *xdg_toplevel,
    struct wl_array *capabilities)
{
    // puedes ignorar este evento si no lo necesitas
    (void)data; (void)xdg_toplevel; (void)capabilities;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_handle_configure,
    toplevel_handle_close,

    //aunque no se utilicen estas dos funciones se deben declarar, no se pueden dejar a NULL
    toplevel_handle_configure_bounds,
    toplevel_handle_wm_capabilities
};

int main (int argc, char *argv[])
{
    //Wayland client
    struct wl_display *display = wl_display_connect(NULL);
    
    //Crear struct para almacenar los recursos(protocolos)
    struct Datos datos;
    datos.xdg = NULL;
    datos.compositor = NULL;
    datos.shm = NULL;

    if (!display) 
    {
        fprintf(stderr, "Failed to connect to Wayland display.\n"); 
        return 1;
    }
    fprintf(stderr, "Connection established!\n");

    // Crear listener para el registro
    struct wl_registry_listener registry_listener; 
    registry_listener.global = registry_listener_global;
    registry_listener.global_remove = registry_listener_global_remove;

    struct wl_registry *registry = wl_display_get_registry(display); // se obtiene el objeto wl_registry 
    wl_registry_add_listener(registry, &registry_listener, &datos); // se añade el listener al registro
    
    wl_display_roundtrip(display);// se bloquea hasta que se procesen los eventos(pasos de mensajes del servidor al cliente)

    //Comprobar que todos las recursos que se requieren estan disponibles
    if (datos.xdg == NULL || datos.compositor == NULL || datos.shm == NULL) {
        fprintf(stderr, "No wl_compositor, wl_shm or xdg_wm_base support\n"); 
        return 1;
    }

    // Crear surface
    wl_surface *surface = wl_compositor_create_surface(datos.compositor);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(datos.xdg, surface);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface); // Crear xdg_toplevel para que xdg_surface sea una ventana

    //Crear struct para guardar los datos de la ventana
    struct WindowData window_data;
    window_data.surface = surface;
    window_data.shm = datos.shm;
    window_data.width = WIDTH;
    window_data.height = HEIGHT;
    window_data.running = true;
    
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, &window_data); //para evitar que el compositor redibuje mal la surface o ajuste mal el tamanio
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &window_data);

    xdg_toplevel_set_title(toplevel, "window-test"); //Titulo de la ventana
    xdg_toplevel_set_app_id(toplevel, "window-test");
    
    wl_surface_commit(surface); //Mostrar ventana

    //Event Loop(paso de mensajes)
    while (wl_display_dispatch(display) != -1 && window_data.running) // wl_display_dispatch -> se envian las peticiones desde el cliente al servidor 
    { 

    }
    
    //Eliminar 
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_display_disconnect(display);

    printf("Disconected from display\n");

    return 0;
}
