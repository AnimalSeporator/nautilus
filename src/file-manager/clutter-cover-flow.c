/* clutter-cover-flow.c */

#include <glib.h>
#include "clutter-cover-flow.h"
#include "clutter-black-texture.h"

G_DEFINE_TYPE (ClutterCoverFlow, clutter_cover_flow, CLUTTER_TYPE_GROUP)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_TYPE_COVER_FLOW, ClutterCoverFlowPrivate))

#define VISIBLE_ITEMS		10
#define FRAMES				40
#define FPS					40
#define MAX_ANGLE			70
#define COVER_SPACE			50
#define FRONT_COVER_SPACE 	200
#define DEPTH				450

#define MAX_ITEM_HEIGHT		240

#define CIRC_BUFFER_WRAP(x)     ((x) % VISIBLE_ITEMS)
#define CIRC_BUFFER_INC(x)      (((x)+1) % VISIBLE_ITEMS)
#define CIRC_BUFFER_DEC(x)      (((x)-1) % VISIBLE_ITEMS)
#define CIRC_BUFFER_DIST(a,b)   (((a)+VISIBLE_ITEMS) - ((b)+VISIBLE_ITEMS))

typedef struct _CoverflowItem
{
	int x;	
	int y;
	int depth;
	int angle;
	int opacity;
	
	ClutterActor		*container;
	ClutterActor		*texture;
	ClutterActor		*reflection;
	char				*display_name;
	char				*display_type;
	
	ClutterBehaviour	*rotateBehaviour;
} CoverFlowItem;

typedef enum
{
    MOVE_LEFT = -1,
    MOVE_RIGHT = 1
} move_t;

struct _ClutterCoverFlowPrivate {
    //FIXME: This is not a realistic way to manage the shown items. Should use
    //a circular buffer, with a high and a low water mark, that loads other
    //items when mark is crossed.
    CoverFlowItem               *items[VISIBLE_ITEMS];
    int                         nitems;
    int       	 				m_actualItem;				//Item now in front

    GList                       *m_items;
    ClutterActor 				*m_stage;					//stage (Window)
    ClutterActor				*item_name;					//Text to display
    ClutterActor				*item_type;					//Text to display
    int         				m_nextItem;					//Next item to be in front
    ClutterAlpha 				*m_alpha;					//Alpha function
    ClutterTimeline 			*m_timeline;				//Timeline (Values in defines.h)
    int 						m_middle_x;
    int 						m_middle_y;
    ClutterActor				*m_container;
    int   						m_loaded;					//Pixbuf Loadeds
};

void fade_in(ClutterCoverFlow *coverflow, CoverFlowItem *item);
static void scale_to_fit(ClutterActor *actor);
static void add_file(ClutterCoverFlow *coverflow, GdkPixbuf *pb, const char *display_name, const char *display_type);
void set_rotation_behaviour (ClutterCoverFlow *self, CoverFlowItem *item, int final_angle, ClutterRotateDirection direction);
void move_and_rotate_covers(ClutterCoverFlow *self, move_t dir);
void start(ClutterCoverFlow *self, int direction);
void stop(ClutterCoverFlow *self);
void clear_behaviours (ClutterCoverFlow *self);

static void
clutter_cover_flow_dispose (GObject *object)
{
  ClutterCoverFlow *self = CLUTTER_COVER_FLOW(object); 

  if (self->priv)
  {
      //if (self->priv->trans != NULL)
	  //g_object_unref(self->priv->trans);
      //self->priv->trans = NULL;
  }

  G_OBJECT_CLASS (clutter_cover_flow_parent_class)->dispose (object);
}

static void
clutter_cover_flow_finalize (GObject *object)
{
  ClutterCoverFlow *self = CLUTTER_COVER_FLOW(object); 

  if (self->priv)
  {
    g_free(self->priv);
    self->priv = NULL;
  }

  G_OBJECT_CLASS (clutter_cover_flow_parent_class)->finalize (object);
}

static void
clutter_cover_flow_class_init (ClutterCoverFlowClass *klass)
{
  GObjectClass *object_class;
  //ClutterActorClass *actor_class;

  object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = clutter_cover_flow_dispose;
  object_class->finalize = clutter_cover_flow_finalize;
}

static void
clutter_cover_flow_init (ClutterCoverFlow *self)
{
  self->priv  = g_new0 (ClutterCoverFlowPrivate, 1);

  self->priv->m_timeline = clutter_timeline_new(FRAMES, FPS);
  self->priv->m_alpha = clutter_alpha_new_full(self->priv->m_timeline,CLUTTER_EASE_OUT_EXPO);
  self->priv->m_actualItem = 0;
}

static gboolean
on_stage_resized_width(ClutterStage *stage, ClutterButtonEvent *event, gpointer user_data)
{
    ClutterCoverFlow *self = CLUTTER_COVER_FLOW(user_data); 
    guint w = clutter_actor_get_width(CLUTTER_ACTOR(stage));
    
    self->priv->m_middle_x = w/2;
    clutter_actor_set_x (
                    self->priv->m_container,
                    self->priv->m_middle_x);
    clutter_actor_set_x (
                    self->priv->item_name, 
                    w/2 - clutter_actor_get_width(self->priv->item_name)/2);
    clutter_actor_set_x (
                    self->priv->item_type, 
                    w/2 - clutter_actor_get_width(self->priv->item_type)/2);
    clutter_actor_set_depth (
                    self->priv->m_container,
                    0 - self->priv->m_middle_x);

    g_debug("Resize W: %d", w);
    return TRUE;
}

static gboolean
on_stage_resized_height(ClutterStage *stage, ClutterButtonEvent *event, gpointer user_data)
{
    ClutterCoverFlow *self = CLUTTER_COVER_FLOW(user_data); 
    guint h = clutter_actor_get_height(CLUTTER_ACTOR(stage));
    
    self->priv->m_middle_y = h/2;
    clutter_actor_set_y (
                    self->priv->m_container,
                    self->priv->m_middle_y);
    clutter_actor_set_y (
                    self->priv->item_name, 
                    h - 50);
    clutter_actor_set_y (
                    self->priv->item_type, 
                    h - 25);

    g_debug("Resize H: %d", h);
    return TRUE;
}

/*
 * This functions adds a rotation behaviour from the current angle to the final angle 
 * rotating with the direction <direction> 
 */
void set_rotation_behaviour (ClutterCoverFlow *self, CoverFlowItem *item, int final_angle, ClutterRotateDirection direction)
{
	double current;

    current = clutter_actor_get_rotation(item->container,CLUTTER_Y_AXIS,0,0,0);
	if(current<0) 	current+=360;
	if(current>360) current-=360;

	if(current != final_angle)
	{
	  	item->rotateBehaviour = clutter_behaviour_rotate_new (
                                    self->priv->m_alpha,
                                    CLUTTER_Y_AXIS,
                                    direction,
                                    current,
                                    final_angle);
	  	clutter_behaviour_rotate_set_center ( 
                                    CLUTTER_BEHAVIOUR_ROTATE(item->rotateBehaviour),
                                    clutter_actor_get_width(item->container)/2,
                                    0,0);
	  	clutter_behaviour_apply (item->rotateBehaviour, item->container);
	}
}

/*
 * Moves all items that should be moved to the left to the left
 * Rotates the new center into view
 * Set opacity depending on how long from the center it is
*/
void move_and_rotate_covers(ClutterCoverFlow *self, move_t dir)
{
    unsigned int i;
    CoverFlowItem *item;

    /* Remember
     * MOVE_LEFT = -1,
     * MOVE_RIGHT = 1
     *
     * First take the object on the other (relative to dir) side of the
     * centre and rotate it so it faces the out
     */
    i = self->priv->m_actualItem - /* - = other side */ dir;     //FIXME: Loop around when circ buffer
    item = self->priv->items[i];

    /* The returned animation is collected when the animation finishes, so
     * we dont need to ref it, I think... 
     */
	clutter_actor_animate_with_alpha (
                        item->container,
                        self->priv->m_alpha,
                        "depth", DEPTH,
                        NULL);

    if (dir == MOVE_RIGHT)
      	set_rotation_behaviour(self, item, 0, CLUTTER_ROTATE_CCW);	
    else if (dir == MOVE_LEFT)
  	    set_rotation_behaviour(self, item, 0, CLUTTER_ROTATE_CW);
    else
        g_critical("Unknown Move");

  	/* Set text in the centre to the name of the file */
  	clutter_text_set_text(
                CLUTTER_TEXT(self->priv->item_name),
                item->display_name);
  	clutter_actor_set_x(
                self->priv->item_name, 
                clutter_actor_get_width(self->priv->m_stage)/2 - clutter_actor_get_width(self->priv->item_name)/2);
  	clutter_text_set_text(
                CLUTTER_TEXT(self->priv->item_type),
                item->display_type);
  	clutter_actor_set_x(
                self->priv->item_type, 
                clutter_actor_get_width(self->priv->m_stage)/2 - clutter_actor_get_width(self->priv->item_type)/2);


    /* 
     * Now move all elements that are dir of the center into a new X position, and
     * with the correct rotation
     */
    for (i=0; i<self->priv->nitems; i++)
	{
        int opacity;
		int dist;
		int abs;
		int depth = 0;
		int pos = 0;

        dist = i - self->priv->m_actualItem + dir;  //FIXME: Loop around when circ buffer
        item = self->priv->items[i];

		if( dist <  0)  abs = -dist;
		if( dist >= 0)  abs = dist;
		
		if (dist > 0 )						//Items in right
		{
			pos =   (   abs - 1 ) * COVER_SPACE + FRONT_COVER_SPACE;
		  	set_rotation_behaviour(self, item, 360- MAX_ANGLE, CLUTTER_ROTATE_CCW);
		}
		if (dist < 0 )   					//Items in left
		{
			pos = - ( ( abs - 1 ) * COVER_SPACE + FRONT_COVER_SPACE );	
		  	set_rotation_behaviour(self, item,  MAX_ANGLE, CLUTTER_ROTATE_CW);
		}
		if (dist == 0)
		{	
			pos = 0 ; 			//The one that now goes to the center
			depth = DEPTH;
		}
					
		pos -= clutter_actor_get_width(item->container)/2;
		
		/* Set opacity relative to distance from centre */
		opacity = CLAMP(255*(VISIBLE_ITEMS - abs)/VISIBLE_ITEMS, 0, 255);

        clutter_actor_animate_with_alpha (
                                item->texture,
                                self->priv->m_alpha,
                                "shade",        opacity,
                                NULL);	
		clutter_actor_animate_with_alpha (
                                item->container,
                                self->priv->m_alpha,
		                        "depth",		depth,
	                          	"x", 			pos,
	                          	NULL);
	}
}

void start(ClutterCoverFlow *self, int direction)
{
	clutter_timeline_start(self->priv->m_timeline);
	self->priv->m_actualItem += direction;
}

void stop(ClutterCoverFlow *self)
{
	clutter_timeline_stop(self->priv->m_timeline);
}

void clear_behaviours (ClutterCoverFlow *self)
{
    int i;
	//FIXME: necessari? only rotate and depth behaviours
    for (i=0; i<self->priv->nitems; i++)
	{
        CoverFlowItem *item = self->priv->items[i];

		if (    item && 
                item->rotateBehaviour && 
                CLUTTER_IS_BEHAVIOUR(item->rotateBehaviour) && 
                clutter_behaviour_is_applied(item->rotateBehaviour, item->container) )
		{	
			clutter_behaviour_remove(item->rotateBehaviour,item->container);
		}
	}
}

void fade_in(ClutterCoverFlow *self, CoverFlowItem *item)
{
    int i;
	ClutterTimeline *timeline;
	ClutterAlpha *alpha;
    ClutterActor *container;

    container = item->container;
	timeline 	= clutter_timeline_new(FRAMES, FPS);
	alpha 	= clutter_alpha_new_full (timeline,CLUTTER_EASE_OUT_EXPO);

    /* Find where this item is in the stack */
    for (i=0; i<self->priv->nitems; i++) {
        if (self->priv->items[i] == item) {
            int distance;
            int opacity;

            /* Opacity depends on distance from center */
            distance = i - self->priv->m_actualItem;
            opacity = CLAMP((255*(VISIBLE_ITEMS - distance)/VISIBLE_ITEMS), 0, 255);

            clutter_actor_animate_with_alpha (
                                item->texture,
                                alpha ,
                                "shade",    opacity,
                                NULL);

	        clutter_timeline_start (timeline);
            return;
        }
    }

    g_error("Could not find item");
}

static void
scale_to_fit(ClutterActor *actor)
{
 	int w =	clutter_actor_get_width(actor);
 	int h = clutter_actor_get_height(actor);
 	
 	if( h > MAX_ITEM_HEIGHT)
 	{
 		int temp = w*MAX_ITEM_HEIGHT/h;
 		clutter_actor_set_size(actor, temp, MAX_ITEM_HEIGHT);
 	}

}

static void
add_file(ClutterCoverFlow *self, GdkPixbuf *pb, const char *display_name, const char *display_type)
{
    int bps;
    CoverFlowItem *item;
    ClutterCoverFlowPrivate *priv = self->priv;

    if (priv->nitems >= VISIBLE_ITEMS) {
        //FIXME: Retarded and leaky...
        g_warning("ONLY %d ITEMS SUPPORTED, HA!", VISIBLE_ITEMS);
        return;
    }

    item = g_new0 (CoverFlowItem, 1);

    item->texture = black_texture_new();

	if( gdk_pixbuf_get_has_alpha(pb) )
        bps = 4;
	else
        bps = 3;

	clutter_texture_set_from_rgb_data(
                CLUTTER_TEXTURE(item->texture), 
                gdk_pixbuf_get_pixels		(pb),
                gdk_pixbuf_get_has_alpha	(pb),
                gdk_pixbuf_get_width		(pb),
                gdk_pixbuf_get_height		(pb),
                gdk_pixbuf_get_rowstride	(pb),
                bps,
                (ClutterTextureFlags)0,
                NULL);

	scale_to_fit (item->texture);
	clutter_actor_set_position(item->texture, 0, 0);

  	/* Reflection */
  	item->reflection = clutter_clone_new ( item->texture );
  	clutter_actor_set_opacity ( item->reflection, 60);
	scale_to_fit ( item->reflection );
  	clutter_actor_set_position ( item->reflection, 0, clutter_actor_get_height(item->reflection) );
	clutter_actor_set_rotation	(
                item->reflection,
                CLUTTER_Z_AXIS,180,
                clutter_actor_get_width(item->reflection)/2,
                clutter_actor_get_height(item->reflection)/2,
                0);
	clutter_actor_set_rotation (
                item->reflection,
                CLUTTER_Y_AXIS,180,	
                clutter_actor_get_width(item->reflection)/2,
                clutter_actor_get_height(item->reflection)/2,
                0);

	/* Text */
	item->display_name = g_strdup(display_name);
	item->display_type = g_strdup(display_type);
 
 	
	/* Container */
	item->container	= clutter_group_new();
	clutter_group_add_many	(
                CLUTTER_GROUP(item->container),
                item->texture, item->reflection, NULL );
	clutter_container_add_actor	(
                CLUTTER_CONTAINER(priv->m_container),
                item->container);

	if(priv->nitems == 0)
	{
		clutter_actor_set_rotation	(
                item->container,
                CLUTTER_Y_AXIS,0,
                clutter_actor_get_width(item->texture)/2,
                0,0);
		clutter_actor_set_depth	( item->container, DEPTH );
		clutter_actor_set_position 	( 
                item->container, 
                0 - clutter_actor_get_width(item->texture)/2, 
                110 - clutter_actor_get_height(item->texture));
        clutter_text_set_text(
                CLUTTER_TEXT(priv->item_name),
                item->display_name);
        clutter_text_set_text(
                CLUTTER_TEXT(priv->item_type),
                item->display_type);
	}
    else
    {
        int pos;

        clutter_actor_set_rotation(
                item->container,
                CLUTTER_Y_AXIS, 360 - MAX_ANGLE,
                clutter_actor_get_width(item->texture)/2,
                0,0);
        pos = (priv->nitems - 1) * COVER_SPACE + FRONT_COVER_SPACE;
        clutter_actor_set_position (
                item->container, 
				pos - clutter_actor_get_width(item->texture)/2,
				110 - clutter_actor_get_height(item->texture));
        clutter_actor_set_depth	(item->container, 0);
    }
	
	/* SET BEHAVIOURS AS NULL */
	item->rotateBehaviour = NULL;
	
	if(priv->nitems > 1)
        clutter_actor_lower_bottom (
            self->priv->items[self->priv->nitems - 1]->container); //Put back
    clutter_actor_lower_bottom (item->container); //Put back

    /* Store the file */
    priv->items[priv->nitems] = item;
    priv->nitems++;

	fade_in	(self, item);
}

ClutterCoverFlow*
clutter_cover_flow_new (ClutterActor *stage)
{
  ClutterCoverFlow *self;
  ClutterColor color = { 255, 255, 255, 255 }; /* white */

  g_return_val_if_fail(CLUTTER_IS_STAGE(stage), NULL);

  self = g_object_new (CLUTTER_TYPE_COVER_FLOW, NULL);
  self->priv->m_stage = stage;

  /* Add ourselves to the stage */
  clutter_container_add_actor ( CLUTTER_CONTAINER (stage), CLUTTER_ACTOR(self) );

  /* Add a container, that will hold all covers, as our child */
  self->priv->m_container = clutter_group_new();
  clutter_container_add_actor ( CLUTTER_CONTAINER (self), self->priv->m_container );

  /* Add some text as our child */	
  self->priv->item_name = clutter_text_new_full ("Lucida Grande 11", NULL, &color);
  clutter_container_add_actor (CLUTTER_CONTAINER (self), self->priv->item_name);

  self->priv->item_type = clutter_text_new_full ("Lucida Grande 8", NULL, &color);
  clutter_container_add_actor (CLUTTER_CONTAINER (self), self->priv->item_type);

  /* Track stage resizes. */
  g_signal_connect (
            stage,
            "notify::width",
            G_CALLBACK (on_stage_resized_width),
            self);
  g_signal_connect (
            stage,
            "notify::height",
            G_CALLBACK (on_stage_resized_height),
            self);

  /* Fake resize event to set item initial position */
  on_stage_resized_width(CLUTTER_STAGE(stage), NULL, self);
  on_stage_resized_height(CLUTTER_STAGE(stage), NULL, self);

  return self;
}


void clutter_cover_flow_add_gfile(ClutterCoverFlow *coverflow, GFile *file)
{
    GIcon *icon;
    GtkIconInfo *icon_info;
    GFileInfo *file_info;
    GtkIconTheme *icon_theme;
    GdkPixbuf *pb;
    const char *display_name;
    const char *display_type;

    icon_theme = gtk_icon_theme_get_default();
	file_info = g_file_query_info(
                    file,
                    G_FILE_ATTRIBUTE_STANDARD_ICON "," G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE,
                    G_FILE_QUERY_INFO_NONE, NULL , NULL);
    icon = g_file_info_get_icon(file_info);
    icon_info = gtk_icon_theme_lookup_by_gicon(
                    icon_theme,
                    icon,
                    200,    /* icon size */
                    GTK_ICON_LOOKUP_USE_BUILTIN | GTK_ICON_LOOKUP_GENERIC_FALLBACK | GTK_ICON_LOOKUP_FORCE_SIZE);

    pb = gtk_icon_info_load_icon(icon_info, NULL);
    display_name = g_file_info_get_display_name(file_info);
    display_type = g_content_type_get_description(
                    g_file_info_get_attribute_string(file_info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE));

    //FIXME: Leaks, error checking
    add_file(coverflow, pb, display_name, display_type);
}

void clutter_cover_flow_add_gicon(ClutterCoverFlow *coverflow, GIcon *icon, char *display_name)
{

}

void clutter_cover_flow_add_pixbuf(ClutterCoverFlow *coverflow, GdkPixbuf *pb, char *display_name)
{
    //FIXME: Leaks, error checking
    add_file(coverflow, pb, display_name, NULL);
}

void clutter_cover_flow_left(ClutterCoverFlow *coverflow)
{
	if(coverflow->priv->m_actualItem < (VISIBLE_ITEMS-1))
	{
        g_debug("Moving left");
		stop(coverflow);
		clear_behaviours(coverflow);
	 	move_and_rotate_covers(coverflow, MOVE_LEFT);
	 	start(coverflow, 1); 	
	 } 
}

void clutter_cover_flow_right(ClutterCoverFlow *coverflow)
{
	if(coverflow->priv->m_actualItem > 0)
	{
        g_debug("Moving right");
		stop(coverflow);
		clear_behaviours(coverflow);
	 	move_and_rotate_covers(coverflow, MOVE_RIGHT);
	 	start(coverflow, -1); 	
	}
}

