#include <string.h>
#include <stdio.h>
#include <clutter/clutter.h>

#define FRAGMENT_SHADER \
    "uniform sampler2D tex;" \
    "uniform float W;" \
    "uniform float H;" \
    "uniform float strength;" \
    \
    "void main(void)" \
    "{" \
    "vec2 xy = gl_TexCoord[0].xy;" \
    "vec4 col = 9.0 * texture2D( tex, xy );" \
    \
    "col -= texture2D(tex, xy + vec2(-W, H));" \
    "col -= texture2D(tex, xy + vec2(0.0, H));" \
    "col -= texture2D(tex, xy + vec2(W, H));" \
    "col -= texture2D(tex, xy + vec2(-W, 0.0));" \
    \
    "col -= texture2D(tex, xy + vec2(W, 0.0));" \
    "col -= texture2D(tex, xy + vec2(0.0, -H));" \
    "col -= texture2D(tex, xy + vec2(W, -H));" \
    "col -= texture2D(tex, xy + vec2(-W, -H));" \
    "col = col * strength + (1.0 - strength) * texture2D( tex, xy);" \
    "gl_FragColor = col;" \
    "}"

typedef struct _Options Options;
typedef struct _Application Application;

struct _Options {
    gfloat zoom_increment;
    gfloat sharpen_strength;
    ClutterColor background_color;
};

struct _Application {
    ClutterActor *stage;
    ClutterLayoutManager *layout;
    ClutterActor *viewport;
    ClutterActor *view;
    Options options;
};

const gfloat scroll_amount = 100;
const gfloat scroll_skip_factor = 0.9;

static void
scrollable_set_scroll(ClutterActor *actor, gfloat x, gfloat y)
{
    gfloat max_x, max_y, xx, yy;

    ClutterActor *viewport = clutter_actor_get_parent(actor);

    max_x = (clutter_actor_get_width(viewport) - clutter_actor_get_width(actor))/2;
    max_y = (clutter_actor_get_height(viewport) - clutter_actor_get_height(actor))/2;

    /*clutter_actor_set_position( actor,*/
            /*x > 0.0 || max_x > 0.0 ? 0.0 : (x < max_x ? max_x : x),*/
            /*y > 0.0 || max_y > 0.0 ? 0.0 : (y < max_y ? max_y : y) );*/
    xx = max_x > 0 ? 0.0 : CLAMP(x, max_x, -max_x);
    yy = max_y > 0 ? 0.0 : CLAMP(y, max_y, -max_y);

    clutter_actor_set_anchor_point(actor, xx, yy);
}

static void
scrollable_get_scroll(ClutterActor *actor, gfloat *x, gfloat *y)
{
    clutter_actor_get_anchor_point(actor, x, y);
}

static gboolean
scrollable_on_key_press(ClutterActor *actor,
        ClutterEvent *event,
        gpointer      user_data)
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
        case CLUTTER_KEY_space:
            viewport = clutter_actor_get_parent(actor);
            if ( state & CLUTTER_SHIFT_MASK )
                y -= clutter_actor_get_height(viewport) * scroll_skip_factor;
            else
                y += clutter_actor_get_height(viewport) * scroll_skip_factor;
            break;
        default:
            return FALSE;
    }
    scrollable_set_scroll(actor, x, y);

    return TRUE;
}

static gboolean
scrollable_on_scroll(ClutterActor *actor,
        ClutterEvent *event,
        gpointer      user_data)
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
    scrollable_set_scroll(actor, x, y);

    return TRUE;
}

static void
scrollable_on_drag(ClutterDragAction *action,
        ClutterActor *actor,
        gfloat delta_x,
        gfloat delta_y,
        gpointer user_data)
{
    gfloat x, y;

    scrollable_get_scroll(actor, &x, &y);
    scrollable_set_scroll(actor, x-delta_x, y-delta_y);
}

static void
init_scrollable(ClutterActor *actor)
{
    ClutterActor *viewport;
    ClutterAction* drag;

    viewport = clutter_actor_get_parent(actor);

    clutter_actor_set_reactive(viewport, TRUE);
    clutter_actor_set_reactive(actor, TRUE);

    g_signal_connect( actor,
            "key-press-event",
            G_CALLBACK(scrollable_on_key_press),
            NULL );
    g_signal_connect( actor,
            "scroll-event",
            G_CALLBACK(scrollable_on_scroll),
            NULL );

    drag = clutter_drag_action_new(); 
    g_signal_connect( drag,
            "drag-motion",
            G_CALLBACK(scrollable_on_drag),
            actor );
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
    printf("%f %f\n",w,h);
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
    scrollable_set_scroll(app->viewport, x, y);
    sharpen(app->view, app->options.sharpen_strength);
}

static gboolean
on_key_press(ClutterActor *stage,
        ClutterEvent *event,
        gpointer      user_data)
{
    Application *app = (Application *)user_data;
    /*ClutterModifierType state = clutter_event_get_state (event);*/
    guint keyval = clutter_event_get_key_symbol (event);

    gdouble s, s2;
    switch (keyval)
    {
        /* zoom in/out */
        case CLUTTER_KEY_KP_Add:
            clutter_actor_get_scale(app->viewport, &s, NULL);
            if (s <= app->options.zoom_increment)
                s *= 1.0 + app->options.zoom_increment;
            else
                s += app->options.zoom_increment;
            clutter_actor_set_scale(app->viewport, s, s);
            update(app);
            break;
        case CLUTTER_KEY_KP_Subtract:
            clutter_actor_get_scale(app->viewport, &s, NULL);
            if (s <= app->options.zoom_increment)
                s /= 1.0 + app->options.zoom_increment;
            else
                s -= app->options.zoom_increment;
            clutter_actor_set_scale(app->viewport, s, s);
            update(app);
            break;

        /* zoom to width/height */
        case CLUTTER_KEY_w:
            s = clutter_actor_get_width(stage)/clutter_actor_get_width(app->viewport);
            clutter_actor_set_scale(app->viewport, s, s);
            update(app);
            break;
        case CLUTTER_KEY_h:
            s = clutter_actor_get_height(stage)/clutter_actor_get_height(app->viewport);
            clutter_actor_set_scale(app->viewport, s, s);
            update(app);
            break;

        /* zoom to fit */
        case CLUTTER_KEY_KP_Divide:
            s = clutter_actor_get_width(stage)/clutter_actor_get_width(app->viewport);
            s2 = clutter_actor_get_height(stage)/clutter_actor_get_height(app->viewport);
            if ( s > s2 ) s = s2;
            clutter_actor_set_scale(app->viewport, s, s);
            update(app);
            break;

        /* zoom reset */
        case CLUTTER_KEY_KP_Multiply:
            s = 1.0;
            clutter_actor_set_scale(app->viewport, s, s);
            update(app);
            break;

        /* sharpen filter strength */
        case CLUTTER_KEY_a:
            app->options.sharpen_strength += 0.1;
            sharpen(app->viewport, app->options.sharpen_strength);
            break;
        case CLUTTER_KEY_z:
            app->options.sharpen_strength =
                app->options.sharpen_strength > 0.1 ? app->options.sharpen_strength - 0.1 : 0.0;
            sharpen(app->viewport, app->options.sharpen_strength);
            break;

        /* fullscreen */
        case CLUTTER_KEY_f:
            clutter_stage_set_fullscreen( CLUTTER_STAGE(app->stage),
                    !clutter_stage_get_fullscreen(CLUTTER_STAGE(app->stage)) );
            break;

        default:
            return FALSE;
    }

    return TRUE;
}

static gboolean
load_image(Application *app, const gchar *filename)
{
    app->view = clutter_texture_new_from_file(filename, NULL);
    clutter_table_layout_pack( CLUTTER_TABLE_LAYOUT(app->layout), app->view, 0, 0 );
    clutter_table_layout_set_fill( CLUTTER_TABLE_LAYOUT(app->layout),
            app->view,
            FALSE,
            FALSE );
    clutter_table_layout_set_expand( CLUTTER_TABLE_LAYOUT(app->layout),
            app->view,
            FALSE,
            FALSE );
    clutter_table_layout_set_alignment( CLUTTER_TABLE_LAYOUT(app->layout),
            app->view,
            CLUTTER_TABLE_ALIGNMENT_CENTER,
            CLUTTER_TABLE_ALIGNMENT_CENTER );

    update(app);

    return TRUE;
}

static void
init_options(Options *options)
{
    /* TODO: parse configuration from file */
    options->zoom_increment = 0.125;
    options->sharpen_strength = 0.0;
    options->background_color = (ClutterColor){0x00, 0x00, 0x00, 0xff};
}

static void
init_app(Application *app)
{
    app->stage = clutter_stage_get_default();
    clutter_stage_set_color( CLUTTER_STAGE(app->stage), &app->options.background_color );

    app->layout = clutter_table_layout_new();
    app->viewport = clutter_box_new(app->layout);
    clutter_container_add_actor( CLUTTER_CONTAINER(app->stage), app->viewport );
    clutter_actor_add_constraint( app->viewport, clutter_align_constraint_new(app->stage, CLUTTER_ALIGN_X_AXIS, 0.5) );
    clutter_actor_add_constraint( app->viewport, clutter_align_constraint_new(app->stage, CLUTTER_ALIGN_Y_AXIS, 0.5) );

    app->view = NULL;

    /* interaction */
    init_scrollable(app->viewport);
    g_signal_connect( app->stage,
            "key-press-event",
            G_CALLBACK(on_key_press),
            app );

    clutter_stage_set_key_focus( CLUTTER_STAGE(app->stage), app->viewport );
    clutter_actor_show_all(app->stage);
    clutter_stage_set_user_resizable( CLUTTER_STAGE(app->stage), TRUE );
}

int main(int argc, char **argv)
{
    Application app;
    const char *filename;

    if (argc < 2)
        return 1;
    
    if ( !clutter_init(&argc, &argv) )
        return 1;

    init_options(&app.options);
    init_app(&app);

    /* TODO: load multiple images */
    filename = argv[1];
    load_image(&app, filename);

    clutter_main();

    return 0;
}
