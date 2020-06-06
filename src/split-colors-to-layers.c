#include<libgimp/gimp.h>
#include<gmodule.h>

#define MAX_COLOR 262144 


static void query                             (void);
static void run                               (const gchar      *name,
                                              gint              nparams,
                                              const GimpParam  *param,
                                              gint             *nreturn_vals,
                                              GimpParam       **return_vals);
static gboolean compare                       (guchar a[4], guchar b[4], gboolean alpha);
static gboolean gimp_extended_color_in_g_list (GList *list, 
                                              guchar color[4],
                                              gint channels) ;
GimpPlugInInfo PLUG_IN_INFO = {
    NULL,
    NULL,
    query,
    run
};

MAIN()

static void
query (void)
{
    static GimpParamDef args[] = {
        {
            GIMP_PDB_INT32,
            "run-mode",
            "Run mode"
        },
        {
            GIMP_PDB_IMAGE,
            "image",
            "Input image"
        },
        {
            GIMP_PDB_DRAWABLE,
            "drawable",
            "Input drawable"
        }
    };

    gimp_install_procedure (
        "split-colors-to-layers",
        "Colors Split",
        "Split individual colors into seperate layers",
        "TheDucker1",
        "Copyright TheDucker1",
        "2020",
        "_Colors Split",
        "RGB*, GRAY*",
        GIMP_PLUGIN,
        G_N_ELEMENTS (args), 0,
        args, NULL);

    gimp_plugin_menu_register ("split-colors-to-layers",
        "<Image>/Filters/Misc"); 
}

static gboolean
compare(guchar a[4], guchar b[4], gboolean alpha) 
{
    gboolean flag = FALSE;
    if (alpha)
        flag = ((a[0] == b[0]) & (a[1] == b[1]) & (a[2] == b[2]) & (a[3] == b[3]));
    else
        flag = ((a[0] == b[0]) & (a[1] == b[1]) & (a[2] == b[2]));
    return flag;
}

static gboolean
gimp_extended_color_in_g_list(GList *list, 
                             guchar color[4],
                             gint channels) 
{
    gboolean flag = FALSE;
    gboolean alpha_flag = (channels == 4);
    for (guint i = 0; i < g_list_length(list) ; ++i) {
        gpointer index_data = g_list_nth_data(list, i);
        guchar data[4] = {
            ((guchar *)index_data)[0],
            ((guchar *)index_data)[1],
            ((guchar *)index_data)[2],
            ((guchar *)index_data)[3]
        };
        if (compare(data, color, alpha_flag) == TRUE)
            return TRUE;
    }
    /* DEBUG
    printf("-----BEGIN-----\n");
    printf("color: \t%d\t%d\t%d\t%d\n", color[0], color[1], color[2], color[3]);
    for (guint i = 0; i < g_list_length(list) ; ++i) {
        gpointer index_data = g_list_nth_data(list, i);
        guchar data[4] = {
            ((guchar *)index_data)[0],
            ((guchar *)index_data)[1],
            ((guchar *)index_data)[2],
            ((guchar *)index_data)[3]
        };
        printf("data: \t%d\t%d\t%d\t%d\n", data[0], data[1], data[2], data[3]);
    }
    if (g_list_length(list) > 2) {
        gpointer index_data = g_list_nth_data(list, 2);
        guchar data[4] = {
            ((guchar *)index_data)[0],
            ((guchar *)index_data)[1],
            ((guchar *)index_data)[2],
            ((guchar *)index_data)[3]
        };
        printf("2: \t%d\t%d\t%d\t%d\n", data[0], data[1], data[2], data[3]);
    }
    printf("------END------\n");
     */
    return flag;
}

static void
split(GimpDrawable *drawable)
{
    GList *pixel_list = NULL;
    gint i, j, k, channels;
    gint x1, x2, y1, y2;
    GimpPixelRgn rgn_read;
    gint32 layer_group, current_image, current_selection;
    guchar* row;
    gint counter = 0;
    
    /* Gets upper left and lower right coordinates,
     * and layers number in the image */
    gimp_drawable_mask_bounds (drawable->drawable_id,
                               &x1, &y1,
                               &x2, &y2);
    
    channels = gimp_drawable_bpp (drawable->drawable_id);
    
    /* Initialises a PixelRgns to read original data.
     * Then create new layers from the data that have been read. */
    gimp_pixel_rgn_init (&rgn_read,
                         drawable,
                         x1, y1,
                         x2 - x1, y2 - y1, 
                         FALSE, FALSE);
    
    /* Initialise enough memory for row */
    row = g_new(guchar, channels * (x2-x1));
                      
    /* Create new image layer group */
    current_image = gimp_item_get_image(drawable->drawable_id);
    layer_group = gimp_layer_group_new(current_image);
    
    gimp_image_insert_layer(current_image,
                            layer_group,
                            0,
                            -1);             
    
    /* If not select anything, select all */
    if (gimp_selection_is_empty(current_image)) {
        gimp_selection_all(current_image);
    }
    
    /* Save current selection */
    current_selection = gimp_selection_save(current_image);
    
    /* Initialize pixel map */   
    guchar pixel[MAX_COLOR][4];
                             
    for (i = y1; i < y2; ++i) {
            /* Get row i */
            
        gimp_pixel_rgn_get_row(&rgn_read,
                               row,
                               x1, i,
                               x2 - x1);
        for (j = x1; j < x2; ++j) {                           

            /* Get pixel and color */
            for (k = 0; k < channels; ++k) {
                pixel[counter][k] = row[channels * (j-x1) + k];
            }
            
            /* If pixel is unused, create new layer */
            if (gimp_extended_color_in_g_list(pixel_list, pixel[counter], channels) == FALSE) {
            
                GimpRGB pixel_color;
                gint32 new_layer;
                
                gimp_rgba_set_uchar(&pixel_color,
                                    pixel[counter][0],
                                    pixel[counter][1],
                                    pixel[counter][2],
                                    pixel[counter][3]);                   
            
                pixel_list = g_list_append(pixel_list, pixel[counter]);
                
                counter++;
                
                new_layer = gimp_layer_new(current_image,
                                           gimp_item_get_name(drawable->drawable_id),
                                           drawable->width,
                                           drawable->height,
                                           gimp_drawable_type_with_alpha(drawable->drawable_id),
                                           (gdouble) 100.0,
                                           GIMP_NORMAL_MODE);
                                               
                gimp_image_insert_layer(current_image,
                                        new_layer,
                                        layer_group,
                                        -1);                                             
                
                GimpDrawable* selection = gimp_drawable_get (new_layer);
                
                gimp_image_select_item (current_image,
                                        GIMP_CHANNEL_OP_REPLACE,
                                        current_selection);
                
                gimp_image_select_color(current_image,
                                        GIMP_CHANNEL_OP_INTERSECT,
                                        drawable->drawable_id,
                                        &pixel_color);
                
                gimp_context_set_foreground(&pixel_color);
                                        
                gimp_edit_fill(selection->drawable_id,
                               GIMP_FOREGROUND_FILL);
                               
                gimp_drawable_flush(selection);
                gimp_drawable_update (selection->drawable_id,
                                      x1, y1,
                                      x2 - x1, y2 - y1);            
            }
        }
        if (i % 10 == 0) {
            gimp_progress_update ((gdouble) (i - y1) / (gdouble) (y2-y1));
        }
    } 
    
    /*  Update the modified region */
    gimp_drawable_flush(drawable);
    // gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
    // gimp_drawable_update (drawable->drawable_id,
    //                       x1, y1,
    //                       x2 - x1, y2 - y1);
     
    /* Clean Data */
    g_free(row);
    g_list_free(pixel_list); 
    gimp_selection_none(current_image);
}
        
static void
run (const gchar      *name,
    gint              nparams,
    const GimpParam   *param,
    gint              *nreturn_vals,
    GimpParam         **return_vals)
{
    static GimpParam  values[1];
    GimpPDBStatusType status = GIMP_PDB_SUCCESS;
    GimpRunMode       run_mode;
    GimpDrawable      *drawable;
    
    
    /* Setting mandatory output values */
    *nreturn_vals = 1;
    *return_vals  = values;
    
    values[0].type = GIMP_PDB_STATUS;
    values[0].data.d_status = status;
    
    /* Getting run_mode - we won't display a dialog if 
     * we are in NONINTERACTIVE mode */
    run_mode = param[0].data.d_int32;

    /*  Get the specified drawable  */
    drawable = gimp_drawable_get (param[2].data.d_drawable);
    
    gimp_progress_init ("Splitting...");
    
    split(drawable);
    
    gimp_displays_flush ();
    gimp_drawable_detach (drawable);
}
