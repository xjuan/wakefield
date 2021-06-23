#include <gtk/gtk.h>
#include "wakefield-compositor.h"

int
main (int argc, char **argv)
{
  GtkWidget *window, *label, *vbox, *entry, *button;

  gtk_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  label = gtk_label_new (g_strdup_printf ("Child, pid %d", getpid ()));
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (vbox), entry, FALSE, FALSE, 0);

  button = gtk_button_new_with_label ("quit");
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
  g_signal_connect (button, "clicked", gtk_main_quit, NULL);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
