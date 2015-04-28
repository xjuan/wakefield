#include <gtk/gtk.h>
#include "wakefield-compositor.h"

int child_count = 0;

static void
button_clicked (GtkButton *button,
                GtkStack *stack)
{
  WakefieldCompositor *compositor;
  char *name = g_strdup_printf ("Child %d", child_count++);
  int fd, fd2;
  char *fd_s;
  char **envv;
  GError *error = NULL;
  char *argv[] = { "./test-embedded", NULL };

  compositor = wakefield_compositor_new ();

  gtk_widget_set_size_request (GTK_WIDGET (compositor), 400, 400);

  gtk_stack_add_titled (stack, GTK_WIDGET (compositor), name, name);
  gtk_widget_show (GTK_WIDGET (compositor));

  fd = wakefield_compositor_create_client_fd (compositor, (GDestroyNotify)gtk_widget_destroy, compositor, &error);
  if (error)
    {
      g_print ("error: %s\n", error->message);
      return;
    }

  /* We dup the fd here to get rid of the CLOEXEC */
  fd2 = dup (fd);
  close (fd);
  fd_s = g_strdup_printf ("%d", fd2);
  envv = g_get_environ ();
  envv = g_environ_setenv (envv, "WAYLAND_SOCKET", fd_s, TRUE);
  g_free (fd_s);

  g_spawn_async (NULL,
                 argv,
                 envv,
                 G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                 NULL,
                 &fd,
                 NULL, &error);
  g_strfreev (envv);
  close (fd2);
  if (error)
    {
      g_print ("error: %s\n", error->message);
      return;
    }
}

int
main (int argc, char **argv)
{
  GtkWidget *window;
  GtkWidget *vbox, *hbox, *button, *stack, *switcher;

  gtk_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  switcher = gtk_stack_switcher_new ();
  gtk_box_pack_start (GTK_BOX (vbox), switcher, FALSE, FALSE, 0);

  stack = gtk_stack_new ();
  gtk_box_pack_start (GTK_BOX (vbox), stack, TRUE, TRUE, 0);
  gtk_stack_switcher_set_stack (GTK_STACK_SWITCHER (switcher), GTK_STACK (stack));

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  button = gtk_button_new_with_label ("Embedd");
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
  g_signal_connect (button, "clicked", G_CALLBACK (button_clicked), stack);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
