#include <string.h>
#include <stdio.h>
#include <clutter/clutter.h>

#define FRAGMENT_SHADER \
    "uniform sampler2D tex;" \
    "uniform float W, H, strength;" \
    \
    "void main(void)" \
    "{" \
    "vec4 col = 9.0 * texture2D( tex, gl_TexCoord[0].xy );" \
    \
    "col -= texture2D(tex, gl_TexCoord[0].xy + vec2(-W, H));" \
    "col -= texture2D(tex, gl_TexCoord[0].xy + vec2(0.0, H));" \
    "col -= texture2D(tex, gl_TexCoord[0].xy + vec2(W, H));" \
    "col -= texture2D(tex, gl_TexCoord[0].xy + vec2(-W, 0.0));" \
    \
    "col -= texture2D(tex, gl_TexCoord[0].xy + vec2(W, 0.0));" \
    "col -= texture2D(tex, gl_TexCoord[0].xy + vec2(0.0, -H));" \
    "col -= texture2D(tex, gl_TexCoord[0].xy + vec2(W, -H));" \
    "col -= texture2D(tex, gl_TexCoord[0].xy + vec2(-W, -H));" \
    "col = col * strength + (1.0 - strength) * texture2D( tex, gl_TexCoord[0].xy);" \
    "gl_FragColor = col;" \
    "}"

typedef struct _Options Options;
typedef struct _Application Application;

struct _Options {
    gdouble zoom_increment;
    gfloat sharpen_strength;
    ClutterColor background_color;
    ClutterColor text_color;
    ClutterColor text_shadow;
    ClutterColor error_color;
    gchar *text_font;
    guint item_spacing;
    guint zoom_animation;
    guint scroll_animation;
    guint rows, columns;
    gboolean fullscreen;
};

struct _Application {
    ClutterActor *stage;
    ClutterLayoutManager *layout;
    ClutterActor *viewport;
    Options options;

    guint argc;
    char **argv;
    gint current_offset;
    guint count;

    gboolean loading;
    gboolean restart;

    const gchar *session_file;
};

static const gfloat scroll_amount = 100.0;
static const gfloat scroll_skip_factor = 0.9;

static gdouble
get_zoom(ClutterActor *actor)
{
    gdouble z;
    clutter_actor_get_scale(actor, &z, NULL);
    return z;
}

typedef void (*zoom_callback)(ClutterAnimation *, gpointer);
static void
set_zoom(ClutterActor *actor,
        gdouble zoom_level,
        guint zoom_animation,
        GCallback on_completed,
        gpointer user_data)
{
    if (zoom_animation > 0) {
        clutter_actor_animate(actor, CLUTTER_LINEAR, zoom_animation,
                "scale-x", zoom_level,
                "scale-y", zoom_level,
                "signal::completed", G_CALLBACK(on_completed), user_data,
                NULL);
    } else {
        clutter_actor_set_scale(actor, zoom_level, zoom_level);
        (*(zoom_callback)on_completed)(NULL, user_data);
    }
}

static ClutterActor*
scrollable_get_offset_parent(ClutterActor *actor)
{
    ClutterActor *parent;
    gfloat w, h;

    w = clutter_actor_get_width(actor);
    h = clutter_actor_get_height(actor);
    parent = clutter_actor_get_parent(actor);
    while ( parent &&
            clutter_actor_get_width(parent) == w &&
            clutter_actor_get_height(parent) == h ) {
        parent = clutter_actor_get_parent(parent);
    }

    return parent ? parent : actor;
}

static void
scrollable_get_scroll(ClutterActor *actor, gfloat *x, gfloat *y)
{
    ClutterActor *parent;
    gfloat max_x, max_y, zz;
    gdouble z;

    clutter_actor_get_anchor_point(actor, x, y);
    parent = scrollable_get_offset_parent(actor);
    clutter_actor_get_scale(actor, &z, NULL);
    zz = (gfloat)z;
    max_x = clutter_actor_get_width(actor) - clutter_actor_get_width(parent)/zz;
    max_y = clutter_actor_get_height(actor) - clutter_actor_get_height(parent)/zz;
    if (x)
        *x += max_x/2;
    if (y)
        *y += max_y/2;
}

static void
scrollable_set_scroll(ClutterActor *actor, gfloat x, gfloat y, guint scroll_animation)
{
    ClutterActor *parent;
    gfloat max_x, max_y, xx, yy, zz, w, h;
    gdouble z;

    parent = scrollable_get_offset_parent(actor);
    clutter_actor_get_size(parent, &w, &h);
    clutter_actor_get_scale(actor, &z, NULL);
    zz = (gfloat)z;
    max_x = clutter_actor_get_width(actor) - w/zz;
    max_y = clutter_actor_get_height(actor) - h/zz;
    xx = max_x <= 0.0 ? 0.0 : ( CLAMP(x, 0.0, max_x) - max_x/2 );
    yy = max_y <= 0.0 ? 0.0 : ( CLAMP(y, 0.0, max_y) - max_y/2 );

    if (scroll_animation > 0) {
        clutter_actor_animate(actor, CLUTTER_EASE_OUT_CUBIC, scroll_animation,
                "anchor-x", xx,
                "anchor-y", yy,
                NULL);
    } else {
        clutter_actor_set_anchor_point(actor, xx, yy);
    }
}

static gboolean
scrollable_on_key_press(ClutterActor *actor,
        ClutterEvent *event,
        guint *scroll_animation)
{
    gfloat x, y, x1, y1, x2, y2;
    ClutterActor *viewport = NULL;
    guint keyval;
    ClutterModifierType state;

    state = clutter_event_get_state(event);
    if ( state & CLUTTER_CONTROL_MASK || state & CLUTTER_META_MASK )
        return FALSE;

    keyval = clutter_event_get_key_symbol(event);

    scrollable_get_scroll(actor, &x1, &y1);
    x = x1;
    y = y1;
    switch (keyval)
    {
        case CLUTTER_KEY_Up:
            y -= scroll_amount;
            break;
        case CLUTTER_KEY_Down:
            y += scroll_amount;
            break;
        case CLUTTER_KEY_Left:
            x -= scroll_amount;
            break;
        case CLUTTER_KEY_Right:
            x += scroll_amount;
            break;
        case CLUTTER_KEY_Prior:
            viewport = scrollable_get_offset_parent(actor);
            y -= clutter_actor_get_height(viewport) - scroll_amount;
            break;
        case CLUTTER_KEY_Next:
            viewport = scrollable_get_offset_parent(actor);
            y += clutter_actor_get_height(viewport) - scroll_amount;
            break;
        case CLUTTER_KEY_space:
            viewport = scrollable_get_offset_parent(actor);
            if ( state & CLUTTER_SHIFT_MASK )
                y -= clutter_actor_get_height(viewport) * scroll_skip_factor;
            else
                y += clutter_actor_get_height(viewport) * scroll_skip_factor;
            break;
        default:
            return FALSE;
    }

    scrollable_set_scroll(actor, x, y, 0);
    scrollable_get_scroll(actor, &x2, &y2);
    if ( (gint)x1 != (gint)x2 || (gint)y1 != (gint)y2 ) {
        scrollable_set_scroll(actor, x, y, 0);
        scrollable_set_scroll(actor, x, y, *scroll_animation);
    } else {
        return FALSE;
    }

    return TRUE;
}

static gboolean
scrollable_on_scroll(ClutterActor *actor,
        ClutterEvent *event,
        guint *scroll_animation)
{
    gfloat x, y;

    ClutterScrollDirection direction = clutter_event_get_scroll_direction(event);
    scrollable_get_scroll(actor, &x, &y);
    switch (direction)
    {
        case CLUTTER_SCROLL_UP:
            y -= scroll_amount;
            break;
        case CLUTTER_SCROLL_DOWN:
            y += scroll_amount;
            break;
        case CLUTTER_SCROLL_LEFT:
            x -= scroll_amount;
            break;
        case CLUTTER_SCROLL_RIGHT:
            x += scroll_amount;
            break;
        default:
            return FALSE;
    }
    scrollable_set_scroll(actor, x, y, *scroll_animation);

    return TRUE;
}

static void
scrollable_on_drag(ClutterDragAction *action,
        ClutterActor *actor,
        gfloat delta_x,
        gfloat delta_y,
        gfloat *vector)
{
    gfloat x, y;

    clutter_actor_set_position(actor, -delta_x, -delta_y);

    scrollable_get_scroll(actor, &x, &y);
    scrollable_set_scroll(actor, x-delta_x, y-delta_y, 0);
    vector[0] = delta_x;
    vector[1] = delta_y;
}

static void
scrollable_on_drag_end(ClutterDragAction *action,
        ClutterActor *actor,
        gfloat event_x,
        gfloat event_y,
        ClutterModifierType modifiers,
        gfloat *vector)
{
    gfloat x, y;
    scrollable_get_scroll(actor, &x, &y);
    scrollable_set_scroll(actor, x-vector[0]*16, y-vector[1]*16, 500);
}

static void
init_scrollable(ClutterActor *actor, guint *scroll_animation)
{
    ClutterAction* drag;
    gfloat *vector;

    clutter_actor_set_reactive(actor, TRUE);

    g_signal_connect( actor,
            "key-press-event",
            G_CALLBACK(scrollable_on_key_press),
            scroll_animation );
    g_signal_connect( actor,
            "scroll-event",
            G_CALLBACK(scrollable_on_scroll),
            scroll_animation );

    vector = (gfloat *)g_malloc( 2*sizeof(gfloat) );
    drag = clutter_drag_action_new();
    g_signal_connect( drag,
            "drag-motion",
            G_CALLBACK(scrollable_on_drag),
            vector );
    g_signal_connect( drag,
            "drag-end",
            G_CALLBACK(scrollable_on_drag_end),
            vector );
    clutter_actor_add_action(actor, drag);
}

static void
sharpen(ClutterActor *actor, gfloat sharpen_strength)
{
    gfloat w, h;
    ClutterEffect *shader;

    if (sharpen_strength == 0.0) {
        clutter_actor_remove_effect_by_name(actor, "SHARPEN");
        return;
    }

    shader = clutter_actor_get_effect(actor, "SHARPEN");
    if (!shader) {
        shader = clutter_shader_effect_new(CLUTTER_FRAGMENT_SHADER);
        if ( !clutter_shader_effect_set_shader_source(
                    CLUTTER_SHADER_EFFECT(shader), FRAGMENT_SHADER) )
            g_printerr("imagepeek: Cannot set fragment shader!\n");
        clutter_actor_add_effect_with_name(actor, "SHARPEN", shader);
    }

    clutter_actor_get_transformed_size(actor, &w, &h);
    w = 1.0/w;
    h = 1.0/h;
    clutter_shader_effect_set_uniform(
            CLUTTER_SHADER_EFFECT(shader), "W", G_TYPE_FLOAT, 1, w );
    clutter_shader_effect_set_uniform(
            CLUTTER_SHADER_EFFECT(shader), "H", G_TYPE_FLOAT, 1, h );

    clutter_shader_effect_set_uniform(
            CLUTTER_SHADER_EFFECT(shader), "strength", G_TYPE_FLOAT, 1,
            sharpen_strength );
}

static void
update(Application *app, gboolean update_filters)
{
    gfloat x, y;

    scrollable_get_scroll(app->viewport, &x, &y);
    scrollable_set_scroll(app->viewport, x, y, 0);

    if (update_filters)
        sharpen(app->viewport, app->options.sharpen_strength);
}

static void
on_zoom_completed(ClutterAnimation *anim, Application *app)
{
    update(app, TRUE);
}

static void
set_zoom_simple(Application *app, gdouble zoom_level)
{
    set_zoom(app->viewport, zoom_level, app->options.zoom_animation,
            G_CALLBACK(on_zoom_completed), app);
}


static gboolean
load_image(Application *app, const char *filename, gint x, gint y)
{
    ClutterActor *item, *view, *label, *text, *text_shadow;
    ClutterTableLayout *layout;
    gfloat xx, yy, w;

    item = clutter_group_new();

    /* image */
    view = clutter_texture_new_from_file(filename, NULL);
    if (view) {
        clutter_container_add_actor( CLUTTER_CONTAINER(item), view );
    } else {
        g_printerr("imagepeek: Cannot open \"%s\"!\n", filename);
    }

    if (app->options.rows > 1 || app->options.columns > 1 || !view) {
        /* text shadow */
        text_shadow = clutter_text_new_full(app->options.text_font, filename, &app->options.text_shadow);
        clutter_text_set_ellipsize( CLUTTER_TEXT(text_shadow), PANGO_ELLIPSIZE_MIDDLE );
        clutter_actor_set_anchor_point(text_shadow, -2.0, -2.0);
        clutter_actor_add_effect( text_shadow, clutter_blur_effect_new() );

        /* text */
        text = clutter_text_new_full(app->options.text_font, filename, &app->options.text_color);
        clutter_text_set_ellipsize( CLUTTER_TEXT(text), PANGO_ELLIPSIZE_MIDDLE );
        clutter_text_set_selectable( CLUTTER_TEXT(text) , TRUE );

        /* label */
        label = clutter_group_new();
        clutter_container_add_actor( CLUTTER_CONTAINER(label), text_shadow );
        clutter_container_add_actor( CLUTTER_CONTAINER(label), text );

        if (view) {
            w = clutter_actor_get_width(view);
            clutter_actor_set_width(text_shadow, w);
            clutter_actor_set_width(text, w);
        } else {
            clutter_text_set_color( CLUTTER_TEXT(text), &app->options.error_color );
        }
        clutter_container_add_actor( CLUTTER_CONTAINER(item), label );
    }

    /* begin updating layout */
    clutter_threads_enter();

    layout = CLUTTER_TABLE_LAYOUT(app->layout);

    /* save scroll */
    scrollable_get_scroll(app->viewport, &xx, &yy);
    w = clutter_actor_get_width(app->viewport);

    /* add item and label */
    clutter_table_layout_pack(layout, item, x, y);
    clutter_table_layout_set_fill( layout, view, FALSE, FALSE );
    clutter_table_layout_set_expand( layout, view, FALSE, FALSE );

    /* restore scroll */
    w = (clutter_actor_get_width(app->viewport)-w)/2;
    scrollable_set_scroll(app->viewport, xx+w, yy, 0);

    clutter_threads_leave();

    return view != NULL;
}

static void
clean_container(ClutterActor *actor)
{
    GList *children, *it;

    children = clutter_container_get_children( CLUTTER_CONTAINER(actor) );
    for( it = children; it; it = it->next )
        clutter_container_remove_actor( CLUTTER_CONTAINER(actor), CLUTTER_ACTOR(it->data) );
    g_list_free(children);
}

static void
crop_container(ClutterActor *actor, guint n)
{
    GList *children, *it;
    guint i;

    children = clutter_container_get_children( CLUTTER_CONTAINER(actor) );
    for( i = 0, it = children; it && i < n; ++i, it = it->next );
    for( ; it; it = it->next )
        clutter_container_remove_actor( CLUTTER_CONTAINER(actor), CLUTTER_ACTOR(it->data) );
    g_list_free(children);
}

static void
clean_items(Application *app)
{
    /* clean container and reset scroll offset */
    clean_container(app->viewport);
    scrollable_set_scroll( app->viewport, 0, 0, 0 );
    app->count = 0;
}

static gboolean
load_images(Application *app)
{
    guint i, x, y, count, rows, columns, max;

    clutter_threads_enter();
    count = app->count;
    i = app->current_offset + count;
    rows = app->options.rows;
    columns = app->options.columns;
    max = app->argc-1;
    x = count % columns;
    y = clutter_table_layout_get_row_count( CLUTTER_TABLE_LAYOUT(app->layout) )-1;

    if (app->restart) {
        app->restart = FALSE;
        clean_items(app);
    }
    clutter_threads_leave();

    if( i >= max ) {
        clutter_threads_enter();
        app->loading = FALSE;
        clutter_threads_leave();
        return FALSE;
    }

    if (x == 0) {
        ++y;
        if (y >= rows ) {
            clutter_threads_enter();
            app->loading = FALSE;
            clutter_threads_leave();
            return FALSE;
        }
    }
    if (y < 0) y = 0;

    clutter_threads_enter();
    ++app->count;
    clutter_threads_leave();
    load_image( app, app->argv[i+1], x, y );
    return TRUE;
}

static void
load_more(Application *app)
{
    guint r1, c1, r2, c2;
    GString *title;
    gchar* title2;

    if (app->loading) {
        app->restart = TRUE;
        return;
    }
    app->restart = FALSE;

    r1 = clutter_table_layout_get_row_count( CLUTTER_TABLE_LAYOUT(app->layout) );
    c1 = clutter_table_layout_get_column_count( CLUTTER_TABLE_LAYOUT(app->layout) );
    r2 = app->options.rows;
    c2 = app->options.columns;

    if ( (r1 > r2 && c1 == c2) || (c1 > c2 && r1 == r2 && r2 == 1) ) {
        /* remove last items */
        app->count = r2*c2;
        crop_container(app->viewport, r2*c2);
    } else if (r1 != r2 || c1 != c2) {
        if ( r1 == 0 || (r1 > 1 && c1 != c2) ) {
            /* reaload all items */
            clean_items(app);

            /* set window title */
            title = g_string_new(app->argv[app->current_offset+1]);
            g_string_append(title, " - imagepeek");
            title2 = g_string_free(title, FALSE);
            clutter_stage_set_title( CLUTTER_STAGE(app->stage), title2 );
            g_free(title2);
        }

        app->loading = TRUE;
        if ( load_images(app) )
            g_idle_add( (GSourceFunc)load_images, app );
    }
}

static void
set_current_offset(Application *app, guint offset)
{
    if (offset+1 < app->argc) {
        clean_items(app);
        app->current_offset = offset;
        load_more(app);
    }
}

static void
load_next(Application *app)
{
    set_current_offset(app, app->current_offset + app->options.rows * app->options.columns);
}

static void
load_prev(Application *app)
{
    guint d = app->options.rows * app->options.columns;
    if ( app->current_offset >= d ) {
        set_current_offset(app, app->current_offset - d);
    } else if ( app->current_offset > 0 ) {
        set_current_offset(app, 0);
    }
}

static gboolean
on_key_press(ClutterActor *stage,
        ClutterEvent *event,
        gpointer      user_data)
{
    Application *app;
    guint keyval;
    gdouble s, s2;
    gfloat x;

    app = (Application *)user_data;
    ClutterModifierType state = clutter_event_get_state(event);
    keyval = clutter_event_get_key_symbol (event);

    switch (keyval)
    {
        /* zoom in/out */
        case CLUTTER_KEY_KP_Add:
            s = get_zoom(app->viewport);
            if (s <= app->options.zoom_increment)
                s *= 1.0 + app->options.zoom_increment;
            else
                s += app->options.zoom_increment;
            set_zoom_simple(app, s);
            break;
        case CLUTTER_KEY_KP_Subtract:
            s = get_zoom(app->viewport);
            if (s <= app->options.zoom_increment)
                s /= 1.0 + app->options.zoom_increment;
            else
                s -= app->options.zoom_increment;
            set_zoom_simple(app, s);
            break;
        case CLUTTER_KEY_KP_9:
        case CLUTTER_KEY_KP_8:
        case CLUTTER_KEY_KP_7:
        case CLUTTER_KEY_KP_6:
        case CLUTTER_KEY_KP_5:
        case CLUTTER_KEY_KP_4:
        case CLUTTER_KEY_KP_3:
        case CLUTTER_KEY_KP_2:
        case CLUTTER_KEY_KP_1:
        case CLUTTER_KEY_9:
        case CLUTTER_KEY_8:
        case CLUTTER_KEY_7:
        case CLUTTER_KEY_6:
        case CLUTTER_KEY_5:
        case CLUTTER_KEY_4:
        case CLUTTER_KEY_3:
        case CLUTTER_KEY_2:
        case CLUTTER_KEY_1:
            if (keyval > CLUTTER_KEY_KP_0 && keyval <= CLUTTER_KEY_KP_9 )
                s = (gdouble)(keyval - CLUTTER_KEY_KP_0);
            else
                s = (gdouble)(keyval - CLUTTER_KEY_0);
            if (state & CLUTTER_CONTROL_MASK)
                s = 1.0/s;
            set_zoom_simple(app, s);
            break;

        /* zoom to fit vertically/horizontally */
        case CLUTTER_KEY_w:
            s = clutter_actor_get_width(stage)/clutter_actor_get_width(app->viewport);
            set_zoom_simple(app, s);
            break;
        case CLUTTER_KEY_h:
            s = clutter_actor_get_height(stage)/clutter_actor_get_height(app->viewport);
            set_zoom_simple(app, s);
            break;

        /* zoom to fit */
        case CLUTTER_KEY_0:
        case CLUTTER_KEY_KP_0:
        case CLUTTER_KEY_KP_Divide:
            s = clutter_actor_get_width(stage)/clutter_actor_get_width(app->viewport);
            s2 = clutter_actor_get_height(stage)/clutter_actor_get_height(app->viewport);
            if ( s > s2 ) s = s2;
            set_zoom_simple(app, s);
            break;

        /* zoom reset */
        case CLUTTER_KEY_KP_Multiply:
            set_zoom_simple(app, 1.0);
            break;

        /* sharpen filter strength */
        case CLUTTER_KEY_a:
            app->options.sharpen_strength += 0.05;
            sharpen(app->viewport, app->options.sharpen_strength);
            break;
        case CLUTTER_KEY_z:
            app->options.sharpen_strength =
                app->options.sharpen_strength > 0.05 ? app->options.sharpen_strength - 0.05 : 0.0;
            sharpen(app->viewport, app->options.sharpen_strength);
            break;

        /* fullscreen */
        case CLUTTER_KEY_f:
            app->options.fullscreen = !clutter_stage_get_fullscreen(CLUTTER_STAGE(stage));
            clutter_stage_set_fullscreen( CLUTTER_STAGE(stage),
                    app->options.fullscreen );
            break;

        /* next page */
        case CLUTTER_KEY_j:
        case CLUTTER_KEY_n:
        case CLUTTER_KEY_KP_Enter:
        case CLUTTER_KEY_Return:
            load_next(app);
            break;
        case CLUTTER_KEY_Right:
            if ( (gint)clutter_actor_get_width(stage) >= (gint)(clutter_actor_get_width(app->viewport)*get_zoom(app->viewport)) )
                load_next(app);
            break;
        case CLUTTER_KEY_Down:
            if ( (gint)clutter_actor_get_height(stage) >= (gint)(clutter_actor_get_height(app->viewport)*get_zoom(app->viewport)) )
                load_next(app);
            break;

        /* prev page */
        case CLUTTER_KEY_k:
        case CLUTTER_KEY_p:
        case CLUTTER_KEY_N:
        case CLUTTER_KEY_b:
        case CLUTTER_KEY_BackSpace:
            load_prev(app);
            break;
        case CLUTTER_KEY_Left:
            if ( (gint)clutter_actor_get_width(stage) >= (gint)(clutter_actor_get_width(app->viewport)*get_zoom(app->viewport)) )
                load_prev(app);
            break;
        case CLUTTER_KEY_Up:
            if ( (gint)clutter_actor_get_height(stage) >= (gint)(clutter_actor_get_height(app->viewport)*get_zoom(app->viewport)) )
                load_prev(app);
            break;

        case CLUTTER_KEY_space:
            if ( state & CLUTTER_SHIFT_MASK ) {
                if (app->current_offset == 0) break;
                load_prev(app);
                scrollable_get_scroll( app->viewport, &x, NULL );
                scrollable_set_scroll( app->viewport,
                        x,
                        clutter_actor_get_height(app->viewport),
                        0 );
            } else {
                load_next(app);
            }
            break;

        /* add rows/columns */
        case CLUTTER_KEY_r:
        case CLUTTER_KEY_R:
            if ( state & CLUTTER_SHIFT_MASK ) {
                if (app->options.rows == 1) break;
                app->options.rows = app->options.rows - 1;
            } else {
                app->options.rows = app->options.rows + 1;
            }
            load_more(app);
            break;
        case CLUTTER_KEY_c:
        case CLUTTER_KEY_C:
            if (app->options.rows > 1) {
                if ( state & CLUTTER_SHIFT_MASK ) {
                    if (app->options.columns == 1) break;
                    app->options.columns = app->options.columns - 1;
                } else {
                    app->options.columns = app->options.columns + 1;
                }
                load_more(app);
            } else {
                if ( state & CLUTTER_SHIFT_MASK ) {
                    app->options.columns = app->options.columns - 1;
                } else {
                    app->options.columns = app->options.columns + 1;
                }
                load_more(app);
            }
            break;

        /* reload */
        case CLUTTER_KEY_F5:
            clean_items(app);
            load_more(app);
            break;

        /* exit */
        case CLUTTER_KEY_q:
        case CLUTTER_KEY_Escape:
            clutter_main_quit();
            break;

        default:
            return FALSE;
    }

    return TRUE;
}

static void
init_options(Options *options)
{
    /* TODO: parse configuration from file */
    options->zoom_increment = 0.125;
    options->sharpen_strength = 0.0;
    options->background_color = (ClutterColor){0x00, 0x00, 0x00, 0xff};
    options->text_color = (ClutterColor){0xff, 0xff, 0xff, 0xff};
    options->text_shadow = (ClutterColor){0x00, 0x00, 0x00, 0xa0};
    options->text_font = (gchar *)"Aller bold 16px";
    options->error_color = (ClutterColor){0xff, 0x90, 0x00, 0xff};
    options->item_spacing = 4;
    options->zoom_animation = 100;
    options->scroll_animation = 100;
    options->rows = 1;
    options->columns = 1;
    options->fullscreen = FALSE;
}

static void
on_allocation_changed(ClutterActor *actor,
        ClutterActorBox *box,
        ClutterAllocationFlags flags,
        Application *app)
{
    update(app, FALSE);
}

static gdouble
load_key_double(GKeyFile *keyfile, const gchar *key, gdouble default_value)
{
    double dval = default_value;
    GError *err = NULL;

    if ( g_key_file_has_key(keyfile, "general", key, NULL) ) {
        dval = g_key_file_get_double(keyfile, "general", key, &err);
        if (err) {
            g_error_free(err);
            return default_value;
        }
    }

    return dval;
}

static guint64
load_key_uint(GKeyFile *keyfile, const gchar *key, guint64 default_value)
{
    guint64 ival = default_value;
    GError *err = NULL;

    if ( g_key_file_has_key(keyfile, "general", key, NULL) ) {
        ival = g_key_file_get_uint64(keyfile, "general", key, &err);
        if (err) {
            g_error_free(err);
            return default_value;
        }
    }

    return ival;
}

static char**
load_key_string_list(GKeyFile *keyfile, const gchar *key, gsize *size, char **default_value)
{
    char **list = default_value;
    GError *err = NULL;

    if ( g_key_file_has_key(keyfile, "general", key, NULL) ) {
        list = g_key_file_get_string_list(keyfile, "general", key, size, &err);
        if (err) {
            g_error_free(err);
            return default_value;
        }
    }

    return list;
}

static gboolean
load_key_boolean(GKeyFile *keyfile, const gchar *key, gboolean default_value)
{
    gboolean bval = default_value;
    GError *err = NULL;

    if ( g_key_file_has_key(keyfile, "general", key, NULL) ) {
        bval = g_key_file_get_boolean(keyfile, "general", key, &err);
        if (err) {
            g_error_free(err);
            return default_value;
        }
    }

    return bval;
}

static gboolean
restore_session(const char *filename, Application *app)
{
    GKeyFile *keyfile;
    double dval;

    keyfile = g_key_file_new();

    if ( !g_key_file_load_from_file(keyfile, filename, G_KEY_FILE_KEEP_COMMENTS, NULL) )
        return FALSE;

    app->argv = load_key_string_list(keyfile, "items", (gsize *)&(app->argc), NULL );
    if (!app->argv)
        return FALSE;
    dval = load_key_double(keyfile, "zoom", 1.0);
    set_zoom_simple(app, dval);
    dval = load_key_double(keyfile, "sharpen", 0.0);
    app->options.sharpen_strength = (gfloat)dval;
    sharpen(app->viewport, app->options.sharpen_strength);
    app->current_offset = load_key_uint(keyfile, "current", 0);
    app->options.rows = load_key_uint(keyfile, "rows", 1);
    app->options.columns = load_key_uint(keyfile, "columns", 1);
    app->options.fullscreen = load_key_boolean(keyfile, "fullscreen", FALSE);
    clutter_stage_set_fullscreen( CLUTTER_STAGE(app->stage),
            app->options.fullscreen );

    return TRUE;
}

static gboolean
save_session(const char *filename, const Application *app)
{
    GKeyFile *keyfile;
    FILE *f;
    gchar *data;
    gsize size;
    gboolean ret = TRUE;

    f = fopen(filename, "w");
    if (f) {
        keyfile = g_key_file_new();

        g_key_file_set_string_list( keyfile, "general",
                "items", (const gchar **)app->argv, (gsize)app->argc );
        g_key_file_set_double( keyfile, "general",
                "zoom", get_zoom(app->viewport) );
        g_key_file_set_double( keyfile, "general",
                "sharpen", (gdouble)app->options.sharpen_strength );
        g_key_file_set_uint64( keyfile, "general",
                "current", (guint64)app->current_offset );
        g_key_file_set_uint64( keyfile, "general",
                "rows", (guint64)app->options.rows );
        g_key_file_set_uint64( keyfile, "general",
                "columns", (guint64)app->options.columns );
        g_key_file_set_boolean( keyfile, "general",
                "fullscreen", app->options.fullscreen );

        data = g_key_file_to_data(keyfile, &size, NULL);
        if ( fwrite(data, size, 1, f) != 1 ) {
            ret = FALSE;
        }
        g_free(data);

        fclose(f);
    } else {
        ret = FALSE;
    }

    return ret;
}


static gboolean
init_app(Application *app, int argc, char **argv)
{
    ClutterActor *box;
    ClutterLayoutManager *layout;

    app->count = 0;
    app->current_offset = 0;
    app->loading = FALSE;
    app->restart = FALSE;
    app->session_file = g_getenv("IMAGEPEEK_SESSION");

    app->stage = clutter_stage_get_default();
    clutter_stage_set_color( CLUTTER_STAGE(app->stage), &app->options.background_color );

    /*layout = clutter_box_layout_new();*/
    layout = clutter_bin_layout_new(CLUTTER_BIN_ALIGNMENT_FIXED, CLUTTER_BIN_ALIGNMENT_FIXED);
    box = clutter_box_new(layout);
    clutter_container_add_actor( CLUTTER_CONTAINER(app->stage), box );
    clutter_actor_add_constraint( box, clutter_align_constraint_new(app->stage, CLUTTER_ALIGN_X_AXIS, 0.5) );
    clutter_actor_add_constraint( box, clutter_align_constraint_new(app->stage, CLUTTER_ALIGN_Y_AXIS, 0.5) );
    g_signal_connect( box,
            "allocation-changed",
            G_CALLBACK(on_allocation_changed),
            app );

    app->layout = clutter_table_layout_new();
    clutter_table_layout_set_column_spacing( CLUTTER_TABLE_LAYOUT(app->layout), app->options.item_spacing );
    clutter_table_layout_set_row_spacing( CLUTTER_TABLE_LAYOUT(app->layout), app->options.item_spacing );
    app->viewport = clutter_box_new(app->layout);
    /*clutter_actor_set_offscreen_redirect(app->viewport, CLUTTER_OFFSCREEN_REDIRECT_ALWAYS);*/
    clutter_container_add_actor( CLUTTER_CONTAINER(box), app->viewport );
    g_object_set( app->viewport,
            "scale-gravity", CLUTTER_GRAVITY_CENTER,
            NULL );

    /* interaction */
    init_scrollable(app->viewport, &app->options.scroll_animation);
    g_signal_connect( app->stage,
            "key-press-event",
            G_CALLBACK(on_key_press),
            app );

    clutter_stage_set_key_focus( CLUTTER_STAGE(app->stage), app->viewport );
    clutter_actor_show_all(app->stage);
    clutter_stage_set_user_resizable( CLUTTER_STAGE(app->stage), TRUE );

    /* load session */
    if ( app->session_file && restore_session(app->session_file, app) )
        g_printerr("imagepeek: Session file \"%s\" loaded.\n", app->session_file);

    /* images from arguments or session */
    if ( argc > 1 ) {
        app->argc = argc;
        app->argv = argv;
        app->current_offset = 0;
    }
    if (app->argc < 2)
        return FALSE;

    set_current_offset(app, app->current_offset);

    return TRUE;
}

int main(int argc, char **argv)
{
    Application app;
    int error = 0;

    /* init app */
    if ( clutter_init(&argc, &argv) != CLUTTER_INIT_SUCCESS )
        return 1;
    init_options(&app.options);
    if ( !init_app(&app, argc, argv) )
        return 1;

    /* main loop */
    clutter_main();

    /* save session */
    if (app.session_file) {
        if ( save_session(app.session_file, &app) ) {
            g_printerr("imagepeek: Session file \"%s\" saved.\n", app.session_file);
        } else {
            g_printerr("imagepeek: Cannot save session file!\n");
            ++error;
        }
    }

    return error;
}
