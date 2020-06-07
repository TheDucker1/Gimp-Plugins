/* Require nagadomi's lbpcascade_animeface.xml
 * get it by using " wget https://raw.githubusercontent.com/
 * nagadomi/lbpcascade_animeface/master/lbpcascade_animeface.xml "
 * Require opencv4
 */ 
#include <libgimp/gimp.h>

#include<opencv2/objdetect.hpp>
#include<opencv2/imgproc.hpp>

#include<iostream>
#include<vector>

extern "C" {

static void query                             (void);
static void run                               (const gchar      *name,
                                              gint              nparams,
                                              const GimpParam  *param,
                                              gint             *nreturn_vals,
                                              GimpParam       **return_vals);
static void detect                            (GimpDrawable *drawable);
static cv::Mat drawableToMat                  (GimpDrawable *drawable);

GimpPlugInInfo PLUG_IN_INFO = 
{
	NULL,
	NULL,
	query,
	run
};

MAIN()

static void 
query(void)
{
	static GimpParamDef args[] =
	{
		{
			GIMP_PDB_INT32,
			"run_mode",
			"Run mode"
		},{
			GIMP_PDB_IMAGE,
			"image",
			"Input image"
		},{
			GIMP_PDB_DRAWABLE,
			"drawable",
			"Input drawable"
		}
	};

    gimp_install_procedure (
        "anime-face-detection",
        "Anime Face Detection",
        "Copy Detected Face(s) To New Layer(s)",
        "TheDucker1",
        "Credit to nagadomi for the method",
        "2020",
        "<Image>/Filters/Misc/Anime Face Detect",
        "RGB*, GRAY*",
        GIMP_PLUGIN,
        G_N_ELEMENTS (args), 0,
        args, NULL);
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
    run_mode = (GimpRunMode)param[0].data.d_int32;

    /*  Get the specified drawable  */
    drawable = gimp_drawable_get (param[2].data.d_drawable);
    
    gimp_progress_init ("Detecting...");
    
    detect(drawable);
    
    gimp_displays_flush ();
    gimp_drawable_detach (drawable);
}

static void
detect (GimpDrawable *drawable)
{
    cv::CascadeClassifier face_cascade;
    const cv::String face_cascade_name = "lbpcascade_animeface.xml";
    cv::Mat mat;
    gint channels;
    gint x1, x2, y1, y2;
    gint32 layer_group, current_image, current_selection;
    gboolean empty_select = FALSE;
    
    /* Gets upper left and lower right coordinates,
     * and layers number in the image */
    gimp_drawable_mask_bounds (drawable->drawable_id,
                               &x1, &y1,
                               &x2, &y2);
    
    channels = gimp_drawable_bpp (drawable->drawable_id);
    
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
        empty_select = TRUE;
    }
    
    /* Save current selection */
    current_selection = gimp_selection_save(current_image);
    
    /* Create cv Mat */
    mat = drawableToMat(drawable);
    if (face_cascade.load(face_cascade_name)) {
        cv::Mat gray_unequal, gray;
        cv::cvtColor(mat, gray_unequal, cv::COLOR_BGR2GRAY);
        cv::equalizeHist(gray_unequal, gray);
        std::vector<cv::Rect> faces;
        face_cascade.detectMultiScale( gray, 
                                       faces, 
                                       1.1,
                                       5, 
                                       0,
                                       cv::Size(24, 24));
                                       
        for ( size_t i = 0; i < faces.size(); i++ ) {
            gint32 new_layer;
            
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
                
            gimp_image_select_rectangle(current_image,
                                        GIMP_CHANNEL_OP_REPLACE,
                                        (gint)faces[i].x + x1,
                                        (gint)faces[i].y + y1,
                                        (gint)faces[i].width,
                                        (gint)faces[i].height);
            
            std::cout << "x: " << faces[i].x << std::endl << "y: " << faces[i].y << std::endl << "width: " << faces[i].width << std::endl << "height: " << faces[i].height << std::endl;
                                    
            gimp_edit_copy(drawable->drawable_id);
            gimp_edit_paste(selection->drawable_id,
                            FALSE);
                            
            gimp_drawable_flush(selection);
            gimp_drawable_update (selection->drawable_id,
                                  x1, y1,
                                  x2 - x1, y2 - y1);
                                  
            if (i % 5 == 0)
                gimp_progress_update((gdouble)(i) / (gdouble)(faces.size()));
        }
    }
    
    /*  Update the modified region */
    gimp_drawable_flush(drawable);
     
    /* Clean Data */
    if (empty_select)
      gimp_selection_none(current_image);
    else
      gimp_image_select_item(current_image,
                             GIMP_CHANNEL_OP_REPLACE,
                             current_selection);
}

static cv::Mat 
drawableToMat(GimpDrawable* drawable)
{
    gint x1, y1, x2, y2;
    gimp_drawable_mask_bounds(drawable->drawable_id,
                              &x1, &y1,
                              &x2, &y2);

    GimpPixelRgn rgnread;
    gimp_pixel_rgn_init(&rgnread,
                        drawable,
                        x1, y1,
                        x2 - x1, y2 - y1,
                        FALSE, FALSE);

    cv::Mat mat(y2 - y1, x2 - x1, CV_MAKETYPE(CV_8U, drawable->bpp));
    gimp_pixel_rgn_get_rect(&rgnread,
                            mat.data,
                            x1, y1,
                            x2 - x1, y2 - y1);

    return mat;
}

}
