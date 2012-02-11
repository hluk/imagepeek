#include <clutter/clutter.h>

typedef enum _OptionType OptionType;
typedef struct _Option Option;
typedef struct _Options Options;
typedef struct _Application Application;

typedef gint typeInteger;
typedef gdouble typeDouble;
typedef gboolean typeBoolean;
typedef const gchar *typeString;
typedef gchar **typeStringList;
typedef ClutterColor typeColor;

typedef void setterInteger(Application*, typeInteger);
typedef void setterDouble(Application*, typeDouble);
typedef void setterBoolean(Application*, typeBoolean);
typedef void setterString(Application*, typeString);
typedef void setterStringList(Application*, typeStringList, gsize);
typedef void setterColor(Application*, typeColor);

typedef typeInteger getterInteger(const Application*);
typedef typeDouble getterDouble(const Application*);
typedef typeBoolean getterBoolean(const Application*);
typedef typeString getterString(const Application*);
typedef typeStringList getterStringList(const Application*, gsize*);
typedef typeColor getterColor(const Application*);

struct _Options {
    gdouble zoom_increment;
    gfloat sharpen_strength;
    ClutterColor background_color;
    ClutterColor text_color;
    ClutterColor text_shadow_color;
    ClutterColor error_color;
    gchar *item_font;
    guint zoom_animation;
    guint scroll_animation;
    guint rows, columns;
    gboolean fullscreen;
    ClutterTextureQuality zoom_quality;
};

struct _Application {
    ClutterActor *stage;
    ClutterLayoutManager *layout;
    ClutterActor *viewport;
    Options options;

    /* number of items */
    guint argc;
    /* list of items */
    gchar **argv;
    /* index of first item on the page */
    guint current_offset;
    /* number of loaded items */
    guint count;

    gboolean loading;
    gboolean restart;

    const gchar *session_file;
};

enum _OptionType {
    OptionInteger,
    OptionDouble,
    OptionBoolean,
    OptionString,
    OptionStringList,
    OptionColor
};

struct _Option {
    const gchar *key;
    OptionType type;
    union {
        setterInteger *setInteger;
        setterDouble *setDouble;
        setterBoolean *setBoolean;
        setterString *setString;
        setterStringList *setStringList;
        setterColor *setColor;
    } setter;
    union {
        getterInteger *getInteger;
        getterDouble *getDouble;
        getterBoolean *getBoolean;
        getterString *getString;
        getterStringList *getStringList;
        getterColor *getColor;
    } getter;
    union {
        typeInteger valueInteger;
        typeDouble valueDouble;
        typeBoolean valueBoolean;
        typeString valueString;
        typeStringList valueStringList;
        typeColor valueColor;
    } value;
};

typedef void (*zoom_callback)(ClutterAnimation *, gpointer);

static gboolean init_app(Application *app, int argc, char **argv);

/* Application setters */
static setterDouble     set_sharpen;
static setterBoolean    set_fullscreen;
static setterStringList set_items;
static setterInteger    set_current_offset;
static setterString     set_item_font;
static setterInteger    set_rows;
static setterInteger    set_columns;
static setterDouble     set_zoom_simple;
static setterInteger    set_zoom_quality;
static setterInteger    set_item_spacing;

/* Application getters */
static getterDouble     get_sharpen;
static getterBoolean    get_fullscreen;
static getterStringList get_items;
static getterInteger    get_current_offset;
static getterString     get_item_font;
static getterInteger    get_rows;
static getterInteger    get_columns;
static getterInteger    get_count;
static getterDouble     get_zoom_simple;
static getterInteger    get_zoom_quality;
static getterInteger    get_item_spacing;
static typeString       get_item(const Application *app, guint index);

/* session */
static gboolean save_session(const Application *app, const char *filename);
static gboolean restore_session(Application *app, const char *filename);

/* items (un)loading */
static void load_prev(Application *app);
static void load_next(Application *app);
static void reload(Application *app);
static void load_more(Application *app);
static void clean_items(Application *app);
static void update(Application *app);
static gboolean load_image(Application *app, const char *filename, gint x, gint y);
static gboolean load_images(Application *app);

/* Actor methods */
static void crop_container(ClutterActor *actor, guint n);
static void clean_container(ClutterActor *actor);
static void set_zoom(ClutterActor *actor, gdouble zoom_level,
        guint zoom_animation, GCallback on_completed, gpointer user_data);
static gdouble get_zoom(ClutterActor *actor);

/* scrollable Actor */
static void init_scrollable(ClutterActor *actor, guint *scroll_animation);
static void scrollable_set_scroll(ClutterActor *actor, gfloat x, gfloat y, guint scroll_animation);
static void scrollable_get_scroll(ClutterActor *actor, gfloat *x, gfloat *y);
static ClutterActor* scrollable_get_offset_parent(ClutterActor *actor);
static gboolean scrollable_on_scroll(ClutterActor *actor, ClutterEvent *event, guint *scroll_animation);
static gboolean scrollable_on_key_press(ClutterActor *actor, ClutterEvent *event, guint *scroll_animation);
static void scrollable_on_drag_end(
        ClutterDragAction *action,
        ClutterActor *actor,
        gfloat event_x,
        gfloat event_y,
        ClutterModifierType modifiers,
        gfloat *vector );
static void scrollable_on_drag(
        ClutterDragAction *action,
        ClutterActor *actor,
        gfloat delta_x,
        gfloat delta_y,
        gfloat *vector );

/* callbacks */
static void on_allocation_changed(ClutterActor *actor, ClutterActorBox *box, ClutterAllocationFlags flags, Application *app);
static gboolean on_key_press(ClutterActor *stage, ClutterEvent *event, gpointer user_data);
static void on_zoom_completed(ClutterAnimation *anim, Application *app);

/* configuration */
static GKeyFile *key_file_new(const gchar *filename);
static void key_file_free(GKeyFile *keyfile);
static gboolean key_file_save(GKeyFile *keyfile, const gchar *filename);
static ClutterColor color_from_string(const gchar *color_string);
static const gchar *color_to_string(ClutterColor color, gchar *color_string);
static void config_integer(
        Application *app,
        GKeyFile *keyfile,
        const gchar *key,
        setterInteger set,
        typeInteger default_value,
        GError **error );
static void config_double(
        Application *app,
        GKeyFile *keyfile,
        const gchar *key,
        setterDouble set,
        typeDouble default_value,
        GError **error );
static void config_boolean(
        Application *app,
        GKeyFile *keyfile,
        const gchar *key,
        setterBoolean set,
        typeBoolean default_value,
        GError **error );
static void config_string(
        Application *app,
        GKeyFile *keyfile,
        const gchar *key,
        setterString set,
        typeString default_value,
        GError **error );
static void config_string_list(
        Application *app,
        GKeyFile *keyfile,
        const gchar *key,
        setterStringList set,
        typeStringList default_value,
        GError **error );
static void config_color(
        Application *app,
        GKeyFile *keyfile,
        const gchar *key,
        setterColor set,
        typeColor default_value,
        GError **error );
static void config_value(
        Application *app,
        GKeyFile *keyfile,
        const Option *option,
        GError **error );

