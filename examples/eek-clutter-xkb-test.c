#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif  /* HAVE_CONFIG_H */

#include "eek/eek-clutter.h"
#include "eek/eek-xkb.h"

#define CSW 640
#define CSH 480

static gchar *symbols = NULL;
static gchar *keycodes = NULL;
static gchar *geometry = NULL;

static const GOptionEntry options[] = {
    {"symbols", '\0', 0, G_OPTION_ARG_STRING, &symbols,
     "Symbols component of the keyboard. If you omit this option, it is "
     "obtained from the X server; that is, the keyboard that is currently "
     "configured is drawn. Examples: --symbols=us or "
     "--symbols=us(pc104)+iso9995-3+group(switch)+ctrl(nocaps)", NULL},
    {"keycodes", '\0', 0, G_OPTION_ARG_STRING, &keycodes,
     "Keycodes component of the keyboard. If you omit this option, it is "
     "obtained from the X server; that is, the keyboard that is currently"
     " configured is drawn. Examples: --keycodes=xfree86+aliases(qwerty)",
     NULL},
    {"geometry", '\0', 0, G_OPTION_ARG_STRING, &geometry,
     "Geometry xkb component. If you omit this option, it is obtained from the"
     " X server; that is, the keyboard that is currently configured is drawn. "
     "Example: --geometry=kinesis", NULL},
    {NULL},
};

gfloat stage_width, stage_height;

static void
on_resize (GObject *object,
	   GParamSpec *param_spec,
	   gpointer user_data)
{
  GValue value = {0};
  gfloat width, height, scale;
  ClutterActor *stage = CLUTTER_ACTOR(object);

  g_object_get (G_OBJECT(stage), "width", &width, NULL);
  g_object_get (G_OBJECT(stage), "height", &height, NULL);

  g_value_init (&value, G_TYPE_DOUBLE);

  scale = width > height ? width / stage_width : width / stage_height;

  g_value_set_double (&value, scale);
  g_object_set_property (G_OBJECT (stage),
			 "scale-x",
			 &value);

  g_value_set_double (&value, scale);
  g_object_set_property (G_OBJECT (stage),
			 "scale-y",
			 &value);
}

static void
key_pressed_event (EekKeyboard *keyboard,
                   EekKey      *key)
{
    guint keysym = eek_key_get_keysym (key);
    g_return_if_fail (keysym != EEK_INVALID_KEYSYM);
    g_debug ("%s", eek_keysym_to_string (keysym));
}

int
main (int argc, char *argv[])
{
    EekKeyboard *keyboard;
    EekLayout *layout;
    ClutterActor *stage, *actor;
    ClutterColor stage_color = { 0xff, 0xff, 0xff, 0xff };
    GOptionContext *context;

    context = g_option_context_new ("test-xkb-clutter");
    g_option_context_add_main_entries (context, options, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

    clutter_init (&argc, &argv);
    gtk_init (&argc, &argv);

    layout = eek_xkb_layout_new (keycodes, geometry, symbols);
    if (layout == NULL) {
        fprintf (stderr, "Failed to create layout\n");
        exit(1);
    }

    keyboard = eek_clutter_keyboard_new (CSW, CSH);
    if (keyboard == NULL) {
        g_object_unref (layout);
        fprintf (stderr, "Failed to create keyboard\n");
        exit(1);
    }

    g_signal_connect (keyboard, "key-pressed",
                      G_CALLBACK(key_pressed_event), NULL);

    eek_keyboard_set_layout (keyboard, layout);
    actor = eek_clutter_keyboard_get_actor (EEK_CLUTTER_KEYBOARD(keyboard));

    stage = clutter_stage_get_default ();

    clutter_stage_set_color (CLUTTER_STAGE(stage), &stage_color);
    clutter_stage_set_user_resizable (CLUTTER_STAGE (stage), TRUE);
    clutter_actor_get_size (actor, &stage_width, &stage_height);
    clutter_actor_set_size (stage, stage_width, stage_height);

    clutter_group_add (CLUTTER_GROUP(stage), actor);

    clutter_actor_show_all (stage);

    g_signal_connect (stage, 
                      "notify::width",
                      G_CALLBACK (on_resize),
                      NULL);

    g_signal_connect (stage,
                      "notify::height",
                      G_CALLBACK (on_resize),
                      NULL);

    clutter_main ();

    return 0;
}
