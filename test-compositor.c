#include <gtk/gtk.h>
#include "wakefield-compositor.h"

int
main (int argc, char **argv)
{
  WakefieldCompositor *compositor;
  GtkWidget *window;
  const char *name;
  GError *error = NULL;

  gtk_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  compositor = wakefield_compositor_new ();

  if (argc >= 2)
    {
      name = argv[1];
      wakefield_compositor_add_socket (compositor, name, NULL);
    }
  else
    {
      name = wakefield_compositor_add_socket_auto (compositor, NULL);
    }

  if (error != NULL)
    {
      g_printerr ("%s", error->message);
      return 1;
    }

  g_print ("Listening on wayland display %s\n", name);

  gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (compositor));
  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
