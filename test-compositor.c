#include <gtk/gtk.h>
#include "wakefield-compositor.h"

int
main (int argc, char **argv)
{
  WakefieldCompositor *compositor;
  GtkWidget *window;
  const char *name;
  GError *error = NULL;
  GtkWidget *vbox, *entry;

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

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (vbox));

  if (1)
  gtk_box_pack_start (GTK_BOX (vbox),
                      gtk_entry_new (),
                      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox),
                      GTK_WIDGET (compositor),
                      TRUE, TRUE, 0);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
