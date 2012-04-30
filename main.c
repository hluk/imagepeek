#include <string.h>
#include <stdio.h>
#include "main.h"

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

#define PROPERTY(name, type) \
    static type \
    get_##name(const Application *app) \
    { \
        return app->options.name; \
    } \
    static void \
    set_##name(Application *app, type value) \
    { \
        app->options.name = value; \
    }
PROPERTY(background_color, typeColor)
PROPERTY(text_color, typeColor)
PROPERTY(text_shadow_color, typeColor)
PROPERTY(error_color, typeColor)
PROPERTY(zoom_increment, typeDouble)
PROPERTY(zoom_animation, typeInteger)
PROPERTY(scroll_animation, typeInteger)

#define OPTION(key, type, fn, val) \
    {key, Option##type, {.set##type = set_##fn}, {.get##type = get_##fn}, {.value##type = val}},
#define COLOR(r,g,b,a) ((ClutterColor){r,g,b,a})

static const Option options[] = {
    OPTION("zoom_animation",    Integer,    zoom_animation,    100)
    OPTION("scroll_animation",  Integer,    scroll_animation,  100)
    OPTION("zoom",              Double,     zoom_simple,       1.0)
    OPTION("sharpen",           Double,     sharpen,           0.0)
    OPTION("current",           Integer,    current_offset,    0)
    OPTION("rows",              Integer,    rows,              1)
    OPTION("columns",           Integer,    columns,           1)
    OPTION("items",             StringList, items,             NULL)
    OPTION("fullscreen",        Boolean,    fullscreen,        FALSE)
    OPTION("background_color",  Color,      background_color,  COLOR(0x00,0x00,0x00,0xff))
    OPTION("text_color",        Color,      text_color,        COLOR(0xff,0xff,0xff,0xff))
    OPTION("text_shadow_color", Color,      text_shadow_color, COLOR(0x00,0x00,0x00,0xa0))
    OPTION("error_color",       Color,      error_color,       COLOR(0xff,0x90,0x00,0xff))
    OPTION("item_font",         String,     item_font,         "sans bold 16px")
    OPTION("item_spacing",      Integer,    item_spacing,      4)
    OPTION("zoom_increment",    Double,     zoom_increment,    0.125)
    OPTION("zoom_quality",      Integer,    zoom_quality,      1)
    {NULL}
};

/* TODO: remove globals */
static const gfloat scroll_amount = 100.0;
static const gfloat scroll_skip_factor = 0.9;


static typeInteger
get_count(const Application *app)
{
    return app->argc;
}

static typeInteger
get_columns(const Application *app)
{
    return MIN(app->options.columns, get_count(app));
}

static void
set_columns(Application *app, typeInteger columns)
{
    app->options.columns = columns > 0 ? columns : 1;
}

static typeInteger
get_rows(const Application *app)
{
    return MIN(app->options.rows, get_count(app));
}

static void
set_rows(Application *app, typeInteger rows)
{
    app->options.rows = rows > 0 ? rows : 1;
}

static typeInteger
get_current_offset(const Application *app)
{
    guint offset = app->current_offset;
    guint count = get_count(app);

    return MIN(offset, count-1);
}

static void
set_current_offset(Application *app, typeInteger offset)
{
    app->current_offset = offset > 0 ? offset : 0;
}

static typeString
get_item_font(const Application *app)
{
    return app->options.item_font;
}

static void
set_item_font(Application *app, typeString font_name)
{
    if ( app->options.item_font ) {
        g_free(app->options.item_font);
    }
    app->options.item_font = g_strdup(font_name);
}

static typeInteger
get_item_spacing(const Application *app)
{
    return clutter_table_layout_get_column_spacing( CLUTTER_TABLE_LAYOUT(app->layout) );
}

static void
set_item_spacing(Application *app, typeInteger spacing)
{
    guint s = spacing > 0 ? spacing : 0;
    clutter_table_layout_set_column_spacing( CLUTTER_TABLE_LAYOUT(app->layout), s );
    clutter_table_layout_set_row_spacing( CLUTTER_TABLE_LAYOUT(app->layout), s );
}

static typeStringList
get_items(const Application *app, gsize *size)
{
    if (size)
        *size = get_count(app);
    return app->argv;
}

static typeString
get_item(const Application *app, guint index)
{
    if ( index < get_count(app) )
        return get_items(app, NULL)[index];
    return NULL;
}

static void
set_items(Application *app, typeStringList items, gsize count)
{
    /* TODO: delete previous list of items if necessary */
    app->argc = count;
    app->argv = items;
    set_rows( app, get_rows(app) );
    set_columns( app, get_columns(app) );
}

static gdouble
get_zoom(ClutterActor *actor)
{
    gdouble z;
    clutter_actor_get_scale(actor, &z, NULL);
    return z;
}

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
scrollable_get_max(ClutterActor *actor, gfloat *max_x, gfloat *max_y)
{
    ClutterActor *parent;
    gfloat pw, ph, w, h, zz;
    gdouble z;

    parent = scrollable_get_offset_parent(actor);
    clutter_actor_get_size(parent, &pw, &ph);
    clutter_actor_get_size(actor, &w, &h);
    clutter_actor_get_scale(actor, &z, NULL);
    zz = (gfloat)z;
    if (max_x)
        *max_x = MAX(0.0, w - pw/zz);
    if (max_y)
        *max_y = MAX(0.0, h - ph/zz);
}

static void
scrollable_get_scroll(ClutterActor *actor, gfloat *x, gfloat *y)
{
    gfloat max_x, max_y;

    clutter_actor_get_anchor_point(actor, x, y);
    scrollable_get_max(actor, &max_x, &max_y);
    if (x)
        *x = MAX(0.0, *x+max_x/2);
    if (y)
        *y = MAX(0.0, *y+max_y/2);
}

static void
scrollable_set_scroll(ClutterActor *actor, gfloat x, gfloat y, guint scroll_animation)
{
    gfloat max_x, max_y, xx, yy;

    scrollable_get_max(actor, &max_x, &max_y);
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
    gfloat x, y, x1, y1, max_x, max_y;
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
    scrollable_get_max(actor, &max_x, &max_y);
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
        case CLUTTER_KEY_Home:
            if ( state & CLUTTER_SHIFT_MASK )
                return FALSE;
            y = x = 0;
            break;
        case CLUTTER_KEY_End:
            if ( state & CLUTTER_SHIFT_MASK )
                return FALSE;
            x = max_x;
            y = max_y;
            break;
        case CLUTTER_KEY_space:
            if ( state & CLUTTER_SHIFT_MASK ) {
        case CLUTTER_KEY_k:
                viewport = scrollable_get_offset_parent(actor);
                y -= clutter_actor_get_height(viewport) * scroll_skip_factor;
            } else {
        case CLUTTER_KEY_j:
                viewport = scrollable_get_offset_parent(actor);
                y += clutter_actor_get_height(viewport) * scroll_skip_factor;
            }
            break;
        default:
            return FALSE;
    }

    x = CLAMP(x, 0.0, max_x);
    y = CLAMP(y, 0.0, max_y);

    if ( (gint)x1 != (gint)x || (gint)y1 != (gint)y ) {
        scrollable_set_scroll(actor, x, y, *scroll_animation);
        return TRUE;
    }

    return FALSE;
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

static typeBoolean
get_fullscreen(const Application *app)
{
    return clutter_stage_get_fullscreen( CLUTTER_STAGE(app->stage) );
}

static void
set_fullscreen(Application *app, typeBoolean fullscreen)
{
    clutter_stage_set_fullscreen( CLUTTER_STAGE(app->stage), fullscreen );
}

static typeDouble
get_sharpen(const Application *app)
{
    return app->options.sharpen_strength;
}

static void
set_sharpen(Application *app, typeDouble sharpen_strength)
{
    gfloat w, h;
    ClutterEffect *shader;
    ClutterActor *actor = app->viewport;

    if (sharpen_strength <= 0.0) {
        app->options.sharpen_strength = 0.0;
        clutter_actor_remove_effect_by_name(actor, "SHARPEN");
        return;
    }

    app->options.sharpen_strength = sharpen_strength;

    clutter_actor_get_transformed_size(actor, &w, &h);
    if (w == 0 || h == 0)
        return;

    shader = clutter_actor_get_effect(actor, "SHARPEN");
    if (!shader) {
        shader = clutter_shader_effect_new(CLUTTER_FRAGMENT_SHADER);
        if ( !clutter_shader_effect_set_shader_source(
                    CLUTTER_SHADER_EFFECT(shader), FRAGMENT_SHADER) )
            g_printerr("imagepeek: Cannot set fragment shader!\n");
        clutter_actor_add_effect_with_name(actor, "SHARPEN", shader);
    }

    clutter_shader_effect_set_uniform(
            CLUTTER_SHADER_EFFECT(shader), "W", G_TYPE_FLOAT, 1, 1.0/w );
    clutter_shader_effect_set_uniform(
            CLUTTER_SHADER_EFFECT(shader), "H", G_TYPE_FLOAT, 1, 1.0/h );

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

    set_sharpen( app, get_sharpen(app) );
}

static void
on_zoom_completed(ClutterAnimation *anim, Application *app)
{
    update(app);
}


static typeDouble
get_zoom_simple(const Application *app)
{
    return get_zoom(app->viewport);
}

static void
set_zoom_simple(Application *app, typeDouble zoom_level)
{
    set_zoom(app->viewport, zoom_level, app->options.zoom_animation,
            G_CALLBACK(on_zoom_completed), app);
}

static typeInteger
get_zoom_quality(const Application *app)
{
    switch( app->options.zoom_quality ) {
        case CLUTTER_TEXTURE_QUALITY_LOW:
            return 0;
        case CLUTTER_TEXTURE_QUALITY_MEDIUM:
            return 1;
        case CLUTTER_TEXTURE_QUALITY_HIGH:
            return 2;
        default:
            return 1;
    }
}

static void
set_zoom_quality(Application *app, typeInteger quality)
{
    if (quality <= 0)
        app->options.zoom_quality = CLUTTER_TEXTURE_QUALITY_LOW;
    else if (quality == 1)
        app->options.zoom_quality = CLUTTER_TEXTURE_QUALITY_MEDIUM;
    else
        app->options.zoom_quality = CLUTTER_TEXTURE_QUALITY_HIGH;
}

static gboolean
load_image(Application *app, const char *filename, gint x, gint y)
{
    ClutterActor *item, *view, *label, *text, *text_shadow_color;
    ClutterTableLayout *layout;
    gfloat xx, yy, w;
    GError *error = NULL;

    layout = CLUTTER_TABLE_LAYOUT( clutter_table_layout_new() );
    item = clutter_box_new( CLUTTER_LAYOUT_MANAGER(layout) );

    /* image */
    /* FIXME: SIGBUS when image is larger than 4094
     * -- workaround is to disable slicing */
    view = g_object_new(CLUTTER_TYPE_TEXTURE, "disable-slicing", TRUE, NULL);
    /*view = clutter_texture_new();*/
    clutter_texture_set_filter_quality( CLUTTER_TEXTURE(view), app->options.zoom_quality );
    clutter_texture_set_load_async( CLUTTER_TEXTURE(view), FALSE );
    clutter_texture_set_from_file( CLUTTER_TEXTURE(view), filename, &error );
    if (error) {
        g_printerr("imagepeek: %s\n", error->message);
        g_error_free(error);
        error = NULL;
        g_object_unref(view);
        view = NULL;
    } else {
        clutter_container_add_actor( CLUTTER_CONTAINER(item), view );
        clutter_table_layout_set_fill( layout, view, FALSE, FALSE );
        clutter_table_layout_set_expand( layout, view, FALSE, FALSE );
    }

    if ( get_rows(app) > 1 || get_columns(app) > 1 || !view ) {
        /* text shadow */
        text_shadow_color = clutter_text_new_full(app->options.item_font, filename, &app->options.text_shadow_color);
        clutter_text_set_ellipsize( CLUTTER_TEXT(text_shadow_color), PANGO_ELLIPSIZE_MIDDLE );
        clutter_actor_set_anchor_point(text_shadow_color, -2.0, -2.0);
        clutter_actor_add_effect( text_shadow_color, clutter_blur_effect_new() );

        /* text */
        text = clutter_text_new_full(app->options.item_font, filename, &app->options.text_color);
        clutter_text_set_ellipsize( CLUTTER_TEXT(text), PANGO_ELLIPSIZE_MIDDLE );
        clutter_text_set_selectable( CLUTTER_TEXT(text) , TRUE );

        /* label */
        label = clutter_group_new();
        clutter_container_add_actor( CLUTTER_CONTAINER(label), text_shadow_color );
        clutter_container_add_actor( CLUTTER_CONTAINER(label), text );

        clutter_actor_set_width(label, 0.0);
        clutter_actor_add_constraint( text, clutter_bind_constraint_new(item, CLUTTER_BIND_WIDTH, 0.0) );
        clutter_actor_add_constraint( text_shadow_color, clutter_bind_constraint_new(text, CLUTTER_BIND_WIDTH, 4.0) );

        if (!view)
            clutter_text_set_color( CLUTTER_TEXT(text), &app->options.error_color );

        clutter_container_add_actor( CLUTTER_CONTAINER(item), label );
    }

    layout = CLUTTER_TABLE_LAYOUT(app->layout);

    /* save scroll */
    scrollable_get_scroll(app->viewport, &xx, &yy);
    w = clutter_actor_get_width(app->viewport);

    /* add item and label */
    clutter_table_layout_pack(layout, item, x, y);

    /* restore scroll */
    w = (clutter_actor_get_width(app->viewport)-w)/2;
    scrollable_set_scroll(app->viewport, xx+w, yy, 0);

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

    children = clutter_container_get_children( CLUTTER_CONTAINER(actor) );
    it = g_list_nth(children, n);
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
    guint i, x, y, count;

    count = app->count;
    i = get_current_offset(app) + count;
    x = count % get_columns(app);
    y = clutter_table_layout_get_row_count( CLUTTER_TABLE_LAYOUT(app->layout) )-1;

    if (app->restart) {
        app->restart = FALSE;
        clean_items(app);
    }

    if( i >= get_count(app) ) {
        app->loading = FALSE;
        return FALSE;
    }

    if (x == 0) {
        ++y;
        if (y >= get_rows(app) ) {
            app->loading = FALSE;
            return FALSE;
        }
    }

    ++app->count;
    load_image( app, get_item(app, i), x, y );
    return TRUE;
}

static void
load_more(Application *app)
{
    guint r1, c1, r2, c2;
    GString *title;
    gchar* title2;
    typeInteger count, current;

    if (app->loading) {
        app->restart = TRUE;
        return;
    }
    app->restart = FALSE;

    r1 = clutter_table_layout_get_row_count( CLUTTER_TABLE_LAYOUT(app->layout) );
    c1 = clutter_table_layout_get_column_count( CLUTTER_TABLE_LAYOUT(app->layout) );
    r2 = get_rows(app);
    c2 = get_columns(app);

    if ( (r1 > r2 && c1 == c2) || (c1 > c2 && r1 == r2 && r2 == 1) ) {
        /* remove last items */
        app->count = r2*c2;
        crop_container(app->viewport, r2*c2);
    } else if (r1 != r2 || c1 != c2) {
        if ( r1 == 0 || (r1 > 1 && c1 != c2) || (r1 != r2 && c1 != c2) ) {
            /* reaload all items */
            clean_items(app);

            /* set window title */
            count = get_count(app);
            current = get_current_offset(app);
            title = g_string_new("");
            g_string_printf(title, "[%d/%d] %s - imagepeek",
                    (int)current+1, (int)count, get_item(app, current) );
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
reload(Application *app)
{
    clean_items(app);
    load_more(app);
}

static void
load_next(Application *app)
{
    typeInteger offset, items_on_page, count;

    offset = get_current_offset(app);
    items_on_page = get_rows(app) * get_columns(app);
    count = get_count(app);

    offset += items_on_page;
    if ( offset < count ) {
        if ( offset >= count )
            offset = count - items_on_page;
        set_current_offset(app, offset);
        reload(app);
    }
}

static void
load_prev(Application *app)
{
    typeInteger offset, items_on_page;

    offset = get_current_offset(app);
    items_on_page = get_rows(app) * get_columns(app);

    if ( offset >= items_on_page ) {
        offset -= get_rows(app) * get_columns(app);
    } else if ( offset > 0 ) {
        offset = 0;
    } else {
        return;
    }

    set_current_offset(app, offset);
    reload(app);
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
            set_sharpen( app, get_sharpen(app) + 0.05 );
            break;
        case CLUTTER_KEY_z:
            set_sharpen( app, get_sharpen(app) - 0.05 );
            break;

        /* fullscreen */
        case CLUTTER_KEY_f:
        case CLUTTER_KEY_F:
            set_fullscreen( app, !get_fullscreen(app) );
            break;

        /* next page */
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
        case CLUTTER_KEY_k:
                if (get_current_offset(app) == 0) break;
                load_prev(app);
                scrollable_get_scroll( app->viewport, &x, NULL );
                scrollable_set_scroll( app->viewport,
                        x,
                        clutter_actor_get_height(app->viewport),
                        0 );
            } else {
        case CLUTTER_KEY_j:
                load_next(app);
            }
            break;

        case CLUTTER_KEY_Home:
            if ( state & CLUTTER_SHIFT_MASK ) {
                if ( get_current_offset(app) != 0 ) {
                    set_current_offset(app, 0);
                    reload(app);
                }
            }
            break;
        case CLUTTER_KEY_End:
            if ( state & CLUTTER_SHIFT_MASK ) {
                typeInteger offset = get_count(app) -
                    get_rows(app) * get_columns(app);
                if ( offset > get_current_offset(app) ) {
                    set_current_offset(app, offset);
                    reload(app);
                }
            }
            break;

        /* shift items on page */
        case CLUTTER_KEY_S:
        case CLUTTER_KEY_s:
            set_current_offset( app, get_current_offset(app) +
                    ((state & CLUTTER_SHIFT_MASK) ? -1 : 1) );
            reload(app);
            break;

        /* add rows/columns */
        case CLUTTER_KEY_r:
        case CLUTTER_KEY_R:
            set_rows( app, get_rows(app) +
                    ((state & CLUTTER_SHIFT_MASK) ? -1 : 1) );
            load_more(app);
            break;
        case CLUTTER_KEY_c:
        case CLUTTER_KEY_C:
            set_columns( app, get_columns(app) +
                    ((state & CLUTTER_SHIFT_MASK) ? -1 : 1) );
            load_more(app);
            break;

        /* reload */
        case CLUTTER_KEY_F5:
            reload(app);
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
on_allocation_changed(ClutterActor *actor,
        ClutterActorBox *box,
        ClutterAllocationFlags flags,
        Application *app)
{
    update(app);
}

static ClutterColor
color_from_string(const gchar *color_string)
{
    const ClutterColor bad_color = {0xff,0,0xff,0xff};
    ClutterColor color;
    guint color_value = 0;

    if ( sscanf(color_string, "#%x", &color_value) == 1 && color_value <= 0xffffffff ) {
        if ( strlen(color_string) == 9 ) {
            color.alpha = 0xff & color_value;
            color_value >>= 8;
        } else {
            color.alpha = 0xff;
        }

        color.blue  = 0xff & color_value;
        color_value >>= 8;
        color.green = 0xff & color_value;
        color_value >>= 8;
        color.red   = 0xff & color_value;

        return color;
    }

    return bad_color;
}

static const
gchar *color_to_string(ClutterColor color, gchar *color_string)
{
    sprintf(color_string, "#%02x%02x%02x%02x",
            (guint)color.red, (guint)color.green,
            (guint)color.blue, (guint)color.alpha);

    return color_string;
}

static void
config_integer( Application *app,
                GKeyFile *keyfile,
                const gchar *key,
                setterInteger set,
                typeInteger default_value,
                GError **error )
{
    typeInteger value;

    if ( keyfile && g_key_file_has_key(keyfile, "general", key, NULL) ) {
        value = g_key_file_get_integer(keyfile, "general", key, error);
        if (!*error) {
            (*set)(app, value);
            return;
        }
    }

    (*set)(app, default_value);
}

static void
config_double( Application *app,
               GKeyFile *keyfile,
               const gchar *key,
               setterDouble set,
               typeDouble default_value,
               GError **error )
{
    typeDouble value;

    if ( keyfile && g_key_file_has_key(keyfile, "general", key, NULL) ) {
        value = g_key_file_get_double(keyfile, "general", key, error);
        if (!*error) {
            (*set)(app, value);
            return;
        }
    }

    (*set)(app, default_value);
}

static void
config_boolean( Application *app,
                GKeyFile *keyfile,
                const gchar *key,
                setterBoolean set,
                typeBoolean default_value,
                GError **error )
{
    typeBoolean value;

    if ( keyfile && g_key_file_has_key(keyfile, "general", key, NULL) ) {
        value = g_key_file_get_boolean(keyfile, "general", key, error);
        if (!*error) {
            (*set)(app, value);
            return;
        }
    }

    (*set)(app, default_value);
}

static void
config_string( Application *app,
               GKeyFile *keyfile,
               const gchar *key,
               setterString set,
               typeString default_value,
               GError **error )
{
    gchar *value;

    if ( keyfile && g_key_file_has_key(keyfile, "general", key, NULL) ) {
        value = g_key_file_get_string(keyfile, "general", key, error);
        if (!*error) {
            (*set)(app, value);
            g_free(value);
            return;
        }
    }

    (*set)(app, default_value);
}

static void
config_string_list( Application *app,
                    GKeyFile *keyfile,
                    const gchar *key,
                    setterStringList set,
                    typeStringList default_value,
                    GError **error )
{
    typeStringList value;
    gsize size;

    if ( keyfile && g_key_file_has_key(keyfile, "general", key, NULL) ) {
        value = g_key_file_get_string_list(keyfile, "general", key, &size, error);
        if (!*error) {
            (*set)(app, value, size);
            return;
        }
    }

    if (default_value) {
        /* size from null teerminated list */
        for (size = 0; default_value[size]; ++size);
        (*set)(app, default_value, size);
    }
}

static void
config_color( Application *app,
              GKeyFile *keyfile,
              const gchar *key,
              setterColor set,
              typeColor default_value,
              GError **error )
{
    gchar *value;

    if ( keyfile && g_key_file_has_key(keyfile, "general", key, NULL) ) {
        value = g_key_file_get_string(keyfile, "general", key, error);
        if (!*error) {
            (*set)(app, color_from_string(value));
            g_free(value);
            return;
        }
    }

    (*set)(app, default_value);
}

static void
config_value( Application *app,
              GKeyFile *keyfile,
              const Option *option,
              GError **error )
{
    const gchar *key = option->key;

    switch(option->type) {
        case OptionInteger:
            config_integer( app, keyfile, key,
                    option->setter.setInteger,
                    option->value.valueInteger,
                    error );
            break;
        case OptionDouble:
            config_double( app, keyfile, key,
                    option->setter.setDouble,
                    option->value.valueDouble,
                    error );
            break;
        case OptionBoolean:
            config_boolean( app, keyfile, key,
                    option->setter.setBoolean,
                    option->value.valueBoolean,
                    error );
            break;
        case OptionString:
            config_string( app, keyfile, key,
                    option->setter.setString,
                    option->value.valueString,
                    error );
            break;
        case OptionStringList:
            config_string_list( app, keyfile, key,
                    option->setter.setStringList,
                    option->value.valueStringList,
                    error );
            break;
        case OptionColor:
            config_color( app, keyfile, key,
                    option->setter.setColor,
                    option->value.valueColor,
                    error );
            break;
    }
}

static GKeyFile*
key_file_new(const gchar *filename)
{
    GError *error = NULL;
    GKeyFile *keyfile = NULL;

    keyfile = g_key_file_new();
    if (filename) {
        g_key_file_load_from_file(keyfile, filename,
                G_KEY_FILE_KEEP_COMMENTS, &error);
        if (error) {
            g_error_free(error);
            error = NULL;
            g_key_file_free(keyfile);
            keyfile = NULL;
        }
    }

    return keyfile;
}

static void
key_file_free(GKeyFile *keyfile)
{
    if (keyfile)
        g_key_file_free(keyfile);
}

static gboolean
key_file_save(GKeyFile *keyfile, const gchar *filename)
{
    gboolean ret = FALSE;
    FILE *f;
    gchar *data;
    gsize size;

    if (keyfile && filename) {
        f = fopen(filename, "w");
        if (f) {
            data = g_key_file_to_data(keyfile, &size, NULL);
            if ( fwrite(data, size, 1, f) == 1 )
                ret = TRUE;
            g_free(data);
            fclose(f);
        }
    }

    return ret;
}

static gboolean
restore_session(Application *app, const char *filename)
{
    GKeyFile *keyfile;
    GError *error = NULL;
    const Option *option = options;

    keyfile = key_file_new(filename);

    while(option->key) {
        config_value(app, keyfile, option, &error);
        if (error) {
            g_printerr("imagepeek: Error while parsing session file! (%s)\n", error->message);
            g_error_free(error);
            error = NULL;
        }
        ++option;
    }

    if (keyfile) {
        key_file_free(keyfile);
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean
save_session(const Application *app, const char *filename)
{
    GKeyFile *keyfile;
    gboolean ret = TRUE;
    const Option *option = options;
    OptionType type;
    const gchar *key;

    keyfile = key_file_new(filename);
    if (!keyfile) {
        keyfile = key_file_new(NULL);
        if (!keyfile)
            return FALSE;
    }

    while(option->key) {
        type = option->type;
        key = option->key;
        if (type == OptionInteger) {
            getterInteger *get = option->getter.getInteger;
            if (get)
                g_key_file_set_integer( keyfile, "general", key, (*get)(app) );
        } else if (type == OptionDouble) {
            getterDouble *get = option->getter.getDouble;
            if (get)
                g_key_file_set_double( keyfile, "general", key, (*get)(app) );
        } else if (type == OptionBoolean) {
            getterBoolean *get = option->getter.getBoolean;
            if (get)
                g_key_file_set_boolean( keyfile, "general", key, (*get)(app) );
        } else if (type == OptionString) {
            getterString *get = option->getter.getString;
            if (get)
                g_key_file_set_string( keyfile, "general", key, (*get)(app) );
        } else if (type == OptionStringList) {
            getterStringList *get = option->getter.getStringList;
            typeStringList list;
            gsize size;
            if (get) {
                list = (*get)(app, &size);
                g_key_file_set_string_list(keyfile, "general", key,
                        (const gchar **)list, size);
            }
        } else if (type == OptionColor) {
            gchar color[9];
            getterColor *get = option->getter.getColor;
            if (get)
                g_key_file_set_string( keyfile, "general",
                    key, color_to_string((*get)(app), &color[0]) );
        }
        ++option;
    }

    ret = key_file_save(keyfile, filename);
    key_file_free(keyfile);

    return ret;
}


static gboolean
init_app(Application *app, int argc, char **argv)
{
    ClutterActor *box;
    ClutterLayoutManager *layout;

    app->count = 0;
    app->loading = FALSE;
    app->restart = FALSE;
    app->argc = 0;
    app->argv = NULL;
    app->options.item_font = NULL;

    app->stage = clutter_stage_get_default();

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
    app->viewport = clutter_box_new(app->layout);
    /*clutter_actor_set_offscreen_redirect(app->viewport, CLUTTER_OFFSCREEN_REDIRECT_ALWAYS);*/
    clutter_container_add_actor( CLUTTER_CONTAINER(box), app->viewport );
    g_object_set( app->viewport,
            "scale-gravity", CLUTTER_GRAVITY_CENTER,
            NULL );

    clutter_stage_set_key_focus( CLUTTER_STAGE(app->stage), app->viewport );
    clutter_actor_show_all(app->stage);
    clutter_stage_set_user_resizable( CLUTTER_STAGE(app->stage), TRUE );

    /* load session */
    app->session_file = g_getenv("IMAGEPEEK_SESSION");
    restore_session(app, app->session_file);

    /* images from arguments or session */
    if ( argc > 1 ) {
        set_items( app, argv+1, argc-1 );
        set_current_offset(app, 0);
    }
    if ( get_count(app) < 1 )
        return FALSE;

    /* set correct rows, columns and offset value */
    set_rows( app, get_rows(app) );
    set_columns( app, get_columns(app) );

    /* background color */
    clutter_stage_set_color( CLUTTER_STAGE(app->stage), &app->options.background_color );

    /* interaction */
    init_scrollable(app->viewport, &app->options.scroll_animation);
    g_signal_connect( app->stage,
            "key-press-event",
            G_CALLBACK(on_key_press),
            app );

    /* load items */
    load_more(app);

    return TRUE;
}

int main(int argc, char **argv)
{
    Application app;
    int error = 0;

    /* initialization */
    if ( clutter_init(&argc, &argv) != CLUTTER_INIT_SUCCESS )
        return 1;
    if ( !init_app(&app, argc, argv) ) {
        g_printerr("imagepeek: No images loaded!\n");
        return 1;
    }

    /* main loop */
    clutter_main();

    /* save session */
    if (app.session_file && app.session_file[0] != '\0') {
        if ( save_session(&app, app.session_file) ) {
            g_printerr("imagepeek: Session file \"%s\" saved.\n", app.session_file);
        } else {
            g_printerr("imagepeek: Cannot save session file '%s'!\n", app.session_file);
            ++error;
        }
    }

    g_printerr("imagepeek: Exiting.\n");
    return error;
}
