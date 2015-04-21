#include <gtk/gtk.h>
#include "wakefield-compositor.h"

int
main (int argc, char **argv)
{
  WakefieldCompositor *compositor;
  GtkWidget *window;

  gtk_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  compositor = g_object_new (WAKEFIELD_TYPE_COMPOSITOR, NULL);

  gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (compositor));
  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
