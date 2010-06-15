/* 
 * Copyright (C) 2006 Sergey V. Udaltsov <svu@gnome.org>
 * Copyright (C) 2010 Daiki Ueno <ueno@unixuser.org>
 * Copyright (C) 2010 Red Hat, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/**
 * SECTION:eek-clutter-key-actor
 * @short_description: Custom #ClutterActor drawing a key shape
 */

#include <cogl/cogl.h>
#include <cogl/cogl-pango.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif  /* HAVE_CONFIG_H */
#include "eek-clutter-key-actor.h"
#include "eek-keysym.h"
#include "eek-drawing.h"
#include "eek-section.h"
#include "eek-keyboard.h"

#define noKBDRAW_DEBUG

enum {
    PRESSED,
    RELEASED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (EekClutterKeyActor, eek_clutter_key_actor,
               CLUTTER_TYPE_GROUP);

#define EEK_CLUTTER_KEY_ACTOR_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), EEK_TYPE_CLUTTER_KEY_ACTOR, EekClutterKeyActorPrivate))

struct _EekClutterKeyActorPrivate
{
    EekKey *key;
    ClutterActor *texture;
    PangoFontDescription *font;
};

static struct {
    /* outline pointer -> ClutterTexture */
    GHashTable *outline_textures;

    /* manually maintain the ref-count of outline_textures to set it
       to NULL when all the EekClutterKeyActor are destroyed */
    gint outline_textures_ref_count;
} texture_cache;

static struct {
    /* keysym category -> PangoFontDescription * */
    PangoFontDescription *category_fonts[EEK_KEYSYM_CATEGORY_LAST];

    /* manually maintain the ref-count of category_fonts to set it to
       NULL when all the EekClutterKeyActor are destroyed */
    gint category_fonts_ref_count;
} font_cache;

static ClutterActor *get_texture          (EekClutterKeyActor *actor);
static void          draw_key_on_layout   (EekClutterKeyActor *actor,
                                           PangoLayout        *layout);
static void          key_enlarge          (ClutterActor       *actor);
static void          key_shrink           (ClutterActor       *actor);

static void
eek_clutter_key_actor_real_paint (ClutterActor *self)
{
    EekClutterKeyActorPrivate *priv = EEK_CLUTTER_KEY_ACTOR_GET_PRIVATE (self);
    PangoLayout *layout;
    PangoRectangle logical_rect = { 0, };
    CoglColor color;
    ClutterGeometry geom;
    EekBounds bounds;

    eek_element_get_bounds (EEK_ELEMENT(priv->key), &bounds);
    clutter_actor_set_anchor_point_from_gravity (self,
                                                 CLUTTER_GRAVITY_CENTER);
    clutter_actor_set_position (self,
                                bounds.x + bounds.width / 2,
                                bounds.y + bounds.height / 2);

    if (!priv->texture) {
        priv->texture = get_texture (EEK_CLUTTER_KEY_ACTOR(self));
        clutter_actor_set_position (priv->texture, 0, 0);
        clutter_container_add_actor (CLUTTER_CONTAINER(self), priv->texture);
    }

    CLUTTER_ACTOR_CLASS (eek_clutter_key_actor_parent_class)->
        paint (self);

    /* Draw the label on the key. */
    layout = clutter_actor_create_pango_layout (self, NULL);
    draw_key_on_layout (EEK_CLUTTER_KEY_ACTOR(self), layout);
    pango_layout_get_extents (layout, NULL, &logical_rect);

    /* FIXME: Color should be configurable through a property. */
    cogl_color_set_from_4ub (&color, 0x80, 0x00, 0x00, 0xff);
    clutter_actor_get_allocation_geometry (self, &geom);
    cogl_pango_render_layout
        (layout,
         (geom.width - logical_rect.width / PANGO_SCALE) / 2,
         (geom.height - logical_rect.height / PANGO_SCALE) / 2,
         &color,
         0);
    g_object_unref (layout);
}

/* FIXME: This is a workaround for the bug
 * http://bugzilla.openedhand.com/show_bug.cgi?id=2137 A developer
 * says this is not a right way to solve the original problem.
 */
static void
eek_clutter_key_actor_real_get_preferred_width (ClutterActor *self,
                                                gfloat        for_height,
                                                gfloat       *min_width_p,
                                                gfloat       *natural_width_p)
{
    PangoLayout *layout;

    /* Draw the label on the key - just to validate the glyph cache. */
    layout = clutter_actor_create_pango_layout (self, NULL);
    draw_key_on_layout (EEK_CLUTTER_KEY_ACTOR(self), layout);
    cogl_pango_ensure_glyph_cache_for_layout (layout);
    g_object_unref (layout);

    CLUTTER_ACTOR_CLASS (eek_clutter_key_actor_parent_class)->
        get_preferred_width (self, for_height, min_width_p, natural_width_p);
}

static void
eek_clutter_key_actor_real_pressed (EekClutterKeyActor *self)
{
    ClutterActor *actor, *section;

    actor = CLUTTER_ACTOR(self);

    /* Make sure the enlarged key show up on the keys which belong
       to other sections. */
    section = clutter_actor_get_parent (actor);
    clutter_actor_raise_top (section);
    clutter_actor_raise_top (actor);
    key_enlarge (actor);
}

static void
eek_clutter_key_actor_real_released (EekClutterKeyActor *self)
{
    ClutterActor *actor, *section;

    actor = CLUTTER_ACTOR(self);

    /* Make sure the enlarged key show up on the keys which belong
       to other sections. */
    section = clutter_actor_get_parent (actor);
    clutter_actor_raise_top (section);
    clutter_actor_raise_top (actor);
    key_shrink (actor);
}

static void
eek_clutter_key_actor_dispose (GObject *object)
{
    EekClutterKeyActorPrivate *priv = EEK_CLUTTER_KEY_ACTOR_GET_PRIVATE(object);

    if (priv->key) {
        g_object_unref (priv->key);
        priv->key = NULL;
    }
    if (priv->texture) {
        /* no need to g_object_unref (priv->texture) */
        priv->texture = NULL;
        if (--texture_cache.outline_textures_ref_count == 0) {
            g_hash_table_unref (texture_cache.outline_textures);
            texture_cache.outline_textures = NULL;
        }
    }
    if (priv->font) {
        /* no need to pango_font_description_free (priv->font) */
        priv->font = NULL;
        if (--font_cache.category_fonts_ref_count == 0) {
            gint i;

            for (i = 0; i < EEK_KEYSYM_CATEGORY_LAST; i++) {
                pango_font_description_free (font_cache.category_fonts[i]);
                font_cache.category_fonts[i] = NULL;
            }
        }
    }
    G_OBJECT_CLASS (eek_clutter_key_actor_parent_class)->dispose (object);
}

static void
eek_clutter_key_actor_class_init (EekClutterKeyActorClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

    g_type_class_add_private (gobject_class,
                              sizeof (EekClutterKeyActorPrivate));

    actor_class->paint = eek_clutter_key_actor_real_paint;
    /* FIXME: This is a workaround for the bug
     * http://bugzilla.openedhand.com/show_bug.cgi?id=2137 A developer
     * says this is not a right way to solve the original problem.
     */
    actor_class->get_preferred_width =
        eek_clutter_key_actor_real_get_preferred_width;

    gobject_class->dispose = eek_clutter_key_actor_dispose;

    /* signals */
    klass->pressed = eek_clutter_key_actor_real_pressed;
    klass->released = eek_clutter_key_actor_real_released;

    signals[PRESSED] =
        g_signal_new ("pressed",
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET(EekClutterKeyActorClass, pressed),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    signals[RELEASED] =
        g_signal_new ("released",
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET(EekClutterKeyActorClass, released),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}

static void
on_button_press_event (ClutterActor *actor,
                       ClutterEvent *event,
                       gpointer user_data)
{
    EekClutterKeyActorPrivate *priv =
        EEK_CLUTTER_KEY_ACTOR_GET_PRIVATE(actor);

    /* priv->key will send back PRESSED event of actor. */
    g_signal_emit_by_name (priv->key, "pressed");
}

static void
on_button_release_event (ClutterActor *actor,
                         ClutterEvent *event,
                         gpointer      user_data)
{
    EekClutterKeyActorPrivate *priv =
        EEK_CLUTTER_KEY_ACTOR_GET_PRIVATE(actor);

    /* priv->key will send back RELEASED event of actor. */
    g_signal_emit_by_name (priv->key, "released");
}

static void
eek_clutter_key_actor_init (EekClutterKeyActor *self)
{
    EekClutterKeyActorPrivate *priv;

    priv = self->priv = EEK_CLUTTER_KEY_ACTOR_GET_PRIVATE(self);
    priv->key = NULL;
    priv->texture = NULL;
    priv->font = NULL;
    
    clutter_actor_set_reactive (CLUTTER_ACTOR(self), TRUE);
    g_signal_connect (self, "button-press-event",
                      G_CALLBACK (on_button_press_event), NULL);
    g_signal_connect (self, "button-release-event",
                      G_CALLBACK (on_button_release_event), NULL);
}

ClutterActor *
eek_clutter_key_actor_new (EekKey *key)
{
    EekClutterKeyActor *actor;

    actor = g_object_new (EEK_TYPE_CLUTTER_KEY_ACTOR, NULL);
    actor->priv->key = key;
    g_object_ref_sink (actor->priv->key);
    return CLUTTER_ACTOR(actor);
}

#if 0
static void  
on_key_animate_complete (ClutterAnimation *animation,
                         gpointer user_data)
{
    ClutterActor *actor = (ClutterActor*)user_data;

    /* reset after effect */
    clutter_actor_set_opacity (actor, 0xff);
    clutter_actor_set_scale (actor, 1.0, 1.0);
}
#endif

static void
key_enlarge (ClutterActor *actor)
{
    clutter_actor_set_scale (actor, 1.0, 1.0);
    clutter_actor_animate (actor, CLUTTER_EASE_IN_SINE, 150,
                           "scale-x", 1.5,
                           "scale-y", 1.5,
                           NULL);
}

static void
key_shrink (ClutterActor *actor)
{
    clutter_actor_set_scale (actor, 1.5, 1.5);
    clutter_actor_animate (actor, CLUTTER_EASE_OUT_SINE, 150,
                           "scale-x", 1.0,
                           "scale-y", 1.0,
                           NULL);
}


static ClutterActor *
create_texture_for_key (EekKey *key)
{
    ClutterActor *texture;
    cairo_t *cr;
    cairo_pattern_t *pat;
    EekOutline *outline;
    EekBounds bounds;

    outline = eek_key_get_outline (EEK_KEY(key));
    eek_element_get_bounds (EEK_ELEMENT(key), &bounds);
 
    texture = clutter_cairo_texture_new (bounds.width, bounds.height);
    cr = clutter_cairo_texture_create (CLUTTER_CAIRO_TEXTURE(texture));
    cairo_set_line_width (cr, 1);
    cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

    pat = cairo_pattern_create_linear (0.0, 0.0,  0.0, 256.0);
    cairo_pattern_add_color_stop_rgba (pat, 1, 0.5, 0.5, 0.5, 1);
    cairo_pattern_add_color_stop_rgba (pat, 0, 1, 1, 1, 1);

    cairo_set_source (cr, pat);

    eek_draw_rounded_polygon (cr,
                              TRUE,
                              outline->corner_radius,
                              outline->points,
                              outline->num_points);

    cairo_pattern_destroy (pat);

    cairo_set_source_rgba (cr, 0.3, 0.3, 0.3, 0.5);
    eek_draw_rounded_polygon (cr,
                              FALSE,
                              outline->corner_radius,
                              outline->points,
                              outline->num_points);
    cairo_destroy (cr);
    return texture;
}

static ClutterActor *
get_texture (EekClutterKeyActor *actor)
{
    ClutterActor *texture;
    EekOutline *outline;

    if (!texture_cache.outline_textures)
        texture_cache.outline_textures = g_hash_table_new (g_direct_hash,
                                                           g_direct_equal);
    outline = eek_key_get_outline (actor->priv->key);
    texture = g_hash_table_lookup (texture_cache.outline_textures, outline);
    if (texture == NULL) {
        texture = create_texture_for_key (actor->priv->key);
        g_hash_table_insert (texture_cache.outline_textures, outline, texture);
    } else
        texture = clutter_clone_new (texture);
    texture_cache.outline_textures_ref_count++;
    return texture;
}

static void
draw_key_on_layout (EekClutterKeyActor *self,
                    PangoLayout *layout)
{
    EekClutterKeyActorPrivate *priv = EEK_CLUTTER_KEY_ACTOR_GET_PRIVATE (self);
    guint keysym;
    const gchar *label, *empty_label = "";
    EekKeysymCategory category;
    EekBounds bounds;

    if (font_cache.category_fonts_ref_count == 0) {
        EekContainer *keyboard, *section;
        PangoFontDescription *font;
        PangoLayout *copy;

        section = EEK_ELEMENT_GET_CLASS(priv->key)->
            get_parent (EEK_ELEMENT(priv->key));
        g_return_if_fail (EEK_IS_SECTION(section));

        keyboard = EEK_ELEMENT_GET_CLASS(section)->
            get_parent (EEK_ELEMENT(section));
        g_return_if_fail (EEK_IS_KEYBOARD(keyboard));
        
        copy = pango_layout_copy (layout);
        font = pango_font_description_from_string ("Sans");
        pango_layout_set_font_description (copy, font);

        eek_get_fonts (EEK_KEYBOARD(keyboard), copy,
                       font_cache.category_fonts);
        g_object_unref (G_OBJECT(copy));
    }

    keysym = eek_key_get_keysym (priv->key);
    if (keysym == EEK_INVALID_KEYSYM)
        return;
    category = eek_keysym_get_category (keysym);
    if (category == EEK_KEYSYM_CATEGORY_UNKNOWN)
        return;
    if (!priv->font) {
        priv->font = font_cache.category_fonts[category];
        font_cache.category_fonts_ref_count++;
    }
    pango_layout_set_font_description (layout, priv->font);

    eek_element_get_bounds (EEK_ELEMENT(priv->key), &bounds);
    pango_layout_set_width (layout, PANGO_SCALE * bounds.width);
    pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);

    label = eek_keysym_to_string (keysym);
    if (!label)
        label = empty_label;
    eek_draw_text_on_layout (layout, label);
    if (label != empty_label)
        g_free ((gpointer)label);
}