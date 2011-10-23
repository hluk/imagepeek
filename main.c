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
};

struct _Application {
    ClutterActor *stage;
    ClutterLayoutManager *layout;
    ClutterActor *viewport;
    ClutterActor *view;
    Options options;

    guint argc;
    char **argv;
    gint current_offset;
    guint count;

    gboolean loading;
    gboolean restart;
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
    gfloat max_x, max_y, xx, yy, zz;
    gdouble z;

    parent = scrollable_get_offset_parent(actor);
    clutter_actor_get_scale(actor, &z, NULL);
    zz = (gfloat)z;
    max_x = clutter_actor_get_width(actor) - clutter_actor_get_width(parent)/zz;
    max_y = clutter_actor_get_height(actor) - clutter_actor_get_height(parent)/zz;
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

    scrollable_get_scroll(actor, &xx, &yy);
}

static gboolean
scrollable_on_key_press(ClutterActor *actor,
        ClutterEvent *event,
        guint *scroll_animation)
{
    gfloat x, y;
    ClutterActor *viewport;
    guint keyval;
    ClutterModifierType state;

    state = clutter_event_get_state(event);
    if ( state & CLUTTER_CONTROL_MASK || state & CLUTTER_META_MASK )
        return FALSE;

    keyval = clutter_event_get_key_symbol(event);

    scrollable_get_scroll(actor, &x, &y);
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
    scrollable_set_scroll(actor, x, y, *scroll_animation);

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
        clutter_shader_effect_set_shader_source( CLUTTER_SHADER_EFFECT(shader), FRAGMENT_SHADER );
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
update(Application *app)
{
    gfloat x, y;

    scrollable_get_scroll(app->viewport, &x, &y);
    scrollable_set_scroll(app->viewport, x, y, 0);
    sharpen(app->viewport, app->options.sharpen_strength);
}

static void
on_zoom_completed(ClutterAnimation *anim, Application *app)
{
    update(app);
}

static gboolean
load_image(Application *app, const char *filename, gint x, gint y)
{
    ClutterActor *view, *text, *text_shadow;
    ClutterTableLayout *layout;
    gfloat xx, yy, w;

    /* label shadow */
    text_shadow = clutter_text_new_full(app->options.text_font, filename, &app->options.text_shadow);
    clutter_text_set_ellipsize( CLUTTER_TEXT(text_shadow), PANGO_ELLIPSIZE_MIDDLE );
    clutter_actor_set_anchor_point(text_shadow, -2.0, -2.0);

    /* label */
    text = clutter_text_new_full(app->options.text_font, filename, &app->options.text_color);
    clutter_text_set_ellipsize( CLUTTER_TEXT(text), PANGO_ELLIPSIZE_MIDDLE );
    clutter_text_set_selectable( CLUTTER_TEXT(text) , TRUE );

    /* image */
    view = clutter_texture_new_from_file(filename, NULL);
    if (view) {
        g_printerr("imagepeek: Added \"%s\".\n", filename);
        w = clutter_actor_get_width(view);
        clutter_actor_set_width(text_shadow, w);
        clutter_actor_set_width(text, w);
    } else {
        g_printerr("imagepeek: Cannot open \"%s\"!\n", filename);
        clutter_text_set_color( CLUTTER_TEXT(text), &app->options.error_color );
    }

    /* update layout */
    clutter_threads_enter();

    layout = CLUTTER_TABLE_LAYOUT(app->layout);

    scrollable_get_scroll(app->viewport, &xx, &yy);
    w = clutter_actor_get_width(app->viewport);

    clutter_table_layout_pack(layout, view, x, y);
    clutter_table_layout_set_fill( layout, view, FALSE, FALSE );
    clutter_table_layout_set_expand( layout, view, FALSE, FALSE );
    /*clutter_table_layout_set_alignment( layout, view,*/
            /*CLUTTER_TABLE_ALIGNMENT_CENTER,*/
            /*CLUTTER_TABLE_ALIGNMENT_CENTER );*/

    /*clutter_actor_add_effect( text_shadow, clutter_blur_effect_new() );*/
    clutter_table_layout_pack(layout, text_shadow, x, y);
    clutter_table_layout_set_fill( layout, text_shadow, FALSE, FALSE );
    clutter_table_layout_set_expand( layout, text_shadow, FALSE, FALSE );
    clutter_table_layout_set_alignment( layout, text_shadow,
            CLUTTER_TABLE_ALIGNMENT_START,
            CLUTTER_TABLE_ALIGNMENT_START );

    clutter_table_layout_pack(layout, text, x, y);
    clutter_table_layout_set_fill( layout, text, FALSE, FALSE );
    clutter_table_layout_set_expand( layout, text, FALSE, FALSE );
    clutter_table_layout_set_alignment( layout, text,
            CLUTTER_TABLE_ALIGNMENT_START,
            CLUTTER_TABLE_ALIGNMENT_START );

    app->view = view;
    /*update(app);*/

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
        g_printerr("imagepeek: Restarting.\n");
        app->restart = FALSE;
        clean_container(app->viewport);
        app->count = 0;
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
    if (app->loading) {
        app->restart = TRUE;
        return;
    }
    app->loading = TRUE;
    app->restart = FALSE;

    /* clean table */
    clean_container(app->viewport);
    app->count = 0;

    /* load images (when idle) */
    /*while( load_images(app) ) load_images(app);*/
    clutter_threads_add_idle( (GSourceFunc)load_images, app );
    /*g_idle_add( (GSourceFunc)load_images, app );*/
}

static void
set_current_offset(Application *app, guint offset)
{
    if (offset+1 < app->argc) {
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
            set_zoom(app->viewport, s, app->options.zoom_animation,
                    G_CALLBACK(on_zoom_completed), app);
            update(app);
            break;
        case CLUTTER_KEY_KP_Subtract:
            s = get_zoom(app->viewport);
            if (s <= app->options.zoom_increment)
                s /= 1.0 + app->options.zoom_increment;
            else
                s -= app->options.zoom_increment;
            set_zoom(app->viewport, s, app->options.zoom_animation,
                    G_CALLBACK(on_zoom_completed), app);
            update(app);
            break;

        /* zoom to width/height */
        case CLUTTER_KEY_w:
            s = clutter_actor_get_width(stage)/clutter_actor_get_width(app->viewport);
            set_zoom(app->viewport, s, app->options.zoom_animation,
                    G_CALLBACK(on_zoom_completed), app);
            update(app);
            break;
        case CLUTTER_KEY_h:
            s = clutter_actor_get_height(stage)/clutter_actor_get_height(app->viewport);
            set_zoom(app->viewport, s, app->options.zoom_animation,
                    G_CALLBACK(on_zoom_completed), app);
            update(app);
            break;

        /* zoom to fit */
        case CLUTTER_KEY_KP_Divide:
            s = clutter_actor_get_width(stage)/clutter_actor_get_width(app->viewport);
            s2 = clutter_actor_get_height(stage)/clutter_actor_get_height(app->viewport);
            if ( s > s2 ) s = s2;
            set_zoom(app->viewport, s, app->options.zoom_animation,
                    G_CALLBACK(on_zoom_completed), app);
            update(app);
            break;

        /* zoom reset */
        case CLUTTER_KEY_KP_Multiply:
            set_zoom(app->viewport, 1.0, app->options.zoom_animation,
                    G_CALLBACK(on_zoom_completed), app);
            update(app);
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
            clutter_stage_set_fullscreen( CLUTTER_STAGE(app->stage),
                    !clutter_stage_get_fullscreen(CLUTTER_STAGE(app->stage)) );
            break;

        /* next/prev page */
        case CLUTTER_KEY_j:
        case CLUTTER_KEY_n:
        case CLUTTER_KEY_KP_Enter:
        case CLUTTER_KEY_Return:
            load_next(app);
            break;
        case CLUTTER_KEY_k:
        case CLUTTER_KEY_p:
            load_prev(app);
            break;

        /* add rows/columns */
        case CLUTTER_KEY_r:
        case CLUTTER_KEY_R:
            if ( state & CLUTTER_SHIFT_MASK ) {
                if (app->options.rows == 1) break;
                app->count = MAX(0, app->count - app->options.rows * app->options.columns);
                app->options.rows = app->options.rows - 1;
            } else {
                app->count = MAX(0, app->count - app->options.rows * app->options.columns);
                app->options.rows = app->options.rows + 1;
            }
            load_more(app);
            break;
        case CLUTTER_KEY_c:
        case CLUTTER_KEY_C:
            if ( state & CLUTTER_SHIFT_MASK ) {
                if (app->options.columns == 1) break;
                app->count = MAX(0, app->count - app->options.rows * app->options.columns);
                app->options.columns = app->options.columns - 1;
            } else {
                app->count = MAX(0, app->count - app->options.rows * app->options.columns);
                app->options.columns = app->options.columns + 1;
            }
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
}

static void
init_app(Application *app, int argc, char **argv)
{
    ClutterActor *box;
    ClutterLayoutManager *layout;

    app->argc = argc;
    app->argv = argv;
    app->count = 0;
    app->current_offset = 0;
    app->loading = FALSE;
    app->restart = FALSE;

    app->stage = clutter_stage_get_default();
    clutter_stage_set_color( CLUTTER_STAGE(app->stage), &app->options.background_color );

    /*layout = clutter_box_layout_new();*/
    layout = clutter_bin_layout_new(CLUTTER_BIN_ALIGNMENT_FIXED, CLUTTER_BIN_ALIGNMENT_FIXED);
    box = clutter_box_new(layout);
    clutter_container_add_actor( CLUTTER_CONTAINER(app->stage), box );
    clutter_actor_add_constraint( box, clutter_align_constraint_new(app->stage, CLUTTER_ALIGN_X_AXIS, 0.5) );
    clutter_actor_add_constraint( box, clutter_align_constraint_new(app->stage, CLUTTER_ALIGN_Y_AXIS, 0.5) );

    app->layout = clutter_table_layout_new();
    clutter_table_layout_set_column_spacing( CLUTTER_TABLE_LAYOUT(app->layout), app->options.item_spacing );
    clutter_table_layout_set_row_spacing( CLUTTER_TABLE_LAYOUT(app->layout), app->options.item_spacing );
    app->viewport = clutter_box_new(app->layout);
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

    app->view = NULL;

    clutter_stage_set_key_focus( CLUTTER_STAGE(app->stage), app->viewport );
    clutter_actor_show_all(app->stage);
    clutter_stage_set_user_resizable( CLUTTER_STAGE(app->stage), TRUE );
    /* TODO: scroll on resize or better layout */

    load_more(app);
}

int main(int argc, char **argv)
{
    Application app;

    /* check arguments */
    if (argc < 2)
        return 1;

    /* init app */
    if ( clutter_init(&argc, &argv) != CLUTTER_INIT_SUCCESS )
        return 1;
    init_options(&app.options);
    init_app(&app, argc, argv);

    clutter_main();

    return 0;
}
