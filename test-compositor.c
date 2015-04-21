#include <gtk/gtk.h>
#include "wakefield-compositor.h"

int
main (int argc, char **argv)
{
  //WakefieldCompositor *compositor;

  gtk_init (&argc, &argv);

  /*compositor = */ g_object_new (WAKEFIELD_TYPE_COMPOSITOR, NULL);

  gtk_main ();

  return 0;
}
