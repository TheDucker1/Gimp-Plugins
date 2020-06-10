/* Credit to nagadomi for the original waifu2x
 * Credit to amigo(white luckers), tanakamura, DeadSix27, YukihoAA and contributors for the cpp implimentation
 * require opencv4
 * require picojson (https://github.com/kazuho/picojson)
 * require gtk2.0
 * require waifu2x-converter-cpp (https://github.com/DeadSix27/waifu2x-converter-cpp)
 * require waifu2x's model(s) (https://github.com/nagadomi/waifu2x)
 * require models : noise0_model.json
 *                  noise1_model.json
 *                  noise2_model.json
 *                  noise3_model.json
 *                  scale2.0x_model.json
 *                  noise0_scale2.0x_model.json
 *                  noise1_scale2.0x_model.json
 *                  noise2_scale2.0x_model.json
 *                  noise3_scale2.0x_model.json
 *                  (all model are vgg7 model)
 */

#include <cassert>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include <gtk/gtk.h>

#include <w2xconv.h>

#include "picojson.h"

#define MODEL_DIR "/DIRECTORY/TO/THE/MODEL" 
/* The models' directory here, will have to be recompiled if you want to move */
#define SIZE_LIMIT 150000000

typedef struct
{
    gint denoise_level;
    gint block_size;
    gboolean preview;
} InputVals;

static InputVals input_vals = 
{
    1,
    512,
    FALSE
};

static void query                             (void);
static void run                               (const gchar      *name,
                                              gint              nparams,
                                              const GimpParam  *param,
                                              gint             *nreturn_vals,
                                              GimpParam       **return_vals);
static void denoise                           (GimpDrawable *drawable,
                                               GimpPreview *preview);
static cv::Mat drawableToMat                  (GimpDrawable *drawable);
static void setMatToDrawable                  (cv::Mat& mat, 
                                               GimpDrawable* drawable);
static void setMatToDrawablePreview           (cv::Mat& mat, 
                                               GimpPreview* preview);
static gboolean denoise_dialog                (GimpDrawable* drawable);
static void on_changed                        (GtkComboBox *widget, 
                                               gpointer   user_data);

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,
  NULL,
  query,
  run
};

MAIN()

static void
query (void)
{
  static GimpParamDef args[] =
  {
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
    "waifu2x-converter-cpp-denoise",
    "Waifu2x Denoise",
    "Denoise an (anime) image",
    "TheDucker1",
    "Credit to nagadomi for the original waifu2x. Credit to amigo(white luckers), tanakamura, DeadSix27, YukihoAA and contributors for waifu2x-converter-cpp.",
    "2020",
    "_Denoise (Waifu2x)",
    "RGB*, GRAY*",
    GIMP_PLUGIN,
    G_N_ELEMENTS (args), 0,
    args, NULL);

  gimp_plugin_menu_register ("waifu2x-converter-cpp-denoise",
                             "<Image>/Filters/Enhance");
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
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

    gimp_progress_init ("Denoising...");

    drawable = gimp_drawable_get(param[2].data.d_drawable);
    
    switch(run_mode) {
        case GIMP_RUN_INTERACTIVE:
            gimp_get_data("waifu2x-converter-cpp-denoise", &input_vals);
            
            if (! denoise_dialog(drawable))
                return;
        break;
    
        case GIMP_RUN_NONINTERACTIVE:
            if (nparams != 5)
                status = GIMP_PDB_CALLING_ERROR;
            if (status == GIMP_PDB_SUCCESS) {
                input_vals.denoise_level = param[3].data.d_int32;
                input_vals.block_size = param[4].data.d_int32;
            }
        break;
        
        case GIMP_RUN_WITH_LAST_VALS:
            gimp_get_data ("waifu2x-converter-cpp-denoise", &input_vals);
        break;
        
        default:
        break;
    }
    
    denoise(drawable, NULL);
    
    gimp_displays_flush ();
    gimp_drawable_detach (drawable);
    
    if (run_mode == GIMP_RUN_INTERACTIVE)
          gimp_set_data ("waifu2x-converter-cpp-denoise", &input_vals, sizeof (InputVals));

    return;
}

static void
denoise (GimpDrawable *drawable_input,
         GimpPreview *preview) 
{
    gint denoise_level, block_size;
    switch (input_vals.denoise_level) {
        case 2:
        case 3:
            denoise_level = input_vals.denoise_level;
        break;
        default:
            denoise_level = 1;
        break;
    }
    if (input_vals.block_size < 128)
        block_size = 128;
    else if (input_vals.block_size > 2048)
        block_size = 2048;
    else
        block_size = (gint) input_vals.block_size;
    gint channels;
    gint x1, x2, y1, y2;
    gint width, height;
    GimpPixelRgn rgnwrite;
    GimpDrawable *drawable;
    if (! preview)
        gimp_progress_init("Denoising...");
    /* Gets upper left and lower right coordinates,
     * and layers number in the image */
    if (preview) {
        gimp_preview_get_position (preview, &x1, &y1);
        gimp_preview_get_size (preview, &width, &height);
        x2 = x1 + width;
        y2 = y1 + height;
        drawable = gimp_drawable_preview_get_drawable(GIMP_DRAWABLE_PREVIEW (preview) );
     }
     else {
        drawable = drawable_input;
        gimp_drawable_mask_bounds (drawable->drawable_id,
                                   &x1, &y1,
                                   &x2, &y2);
        width = x2 - x1;
        height = y2 - y1;
    }
    
    GimpImageType type = gimp_drawable_type(drawable->drawable_id);
    
    gint64 size = width * height;
    if (size > SIZE_LIMIT) {
        std::cout << "size: " << size << std::endl << "width: " << width << std::endl << "height: " << height << std::endl;
        g_message("Selection size too big\nYou can configure SIZE_LIMIT in the source code and recompile");
        return;
    }
    channels = gimp_drawable_bpp (drawable->drawable_id);
       
    gimp_pixel_rgn_init (&rgnwrite,
                         drawable,
                         x1, y1,
                         width, height, 
                         preview == NULL, TRUE);
    
    /* Update progress */
    if (! preview) {
        gimp_progress_set_text("Initializing...");
        gimp_progress_update((gdouble) 0.1);
    }
    
    cv::Mat mat_input = drawableToMat(drawable);
    cv::Mat mat(height, width, CV_8UC3);
    cv::Mat mat_proc(height, width, CV_8UC3);
    cv::Mat mat_output;
    if (type == GIMP_RGBA_IMAGE) {
        cv::cvtColor(mat_input, mat, cv::COLOR_BGRA2BGR);
    }
    else if (type == GIMP_RGB_IMAGE) {
        mat = mat_input.clone();
    }
    else if ((type == GIMP_GRAY_IMAGE) | (type == GIMP_GRAYA_IMAGE)) {
        cv::cvtColor(mat_input, mat, cv::COLOR_GRAY2BGR);
    }
    else if ((type == GIMP_INDEXEDA_IMAGE) | (type == GIMP_INDEXED_IMAGE)) {
        g_message("Indexed color image is not supported");
        return;
    }
    else {
        g_message("Unrecognized colorspace");
        return;
    }
    
    std::string model_dir = MODEL_DIR;
    W2XConv *converter = w2xconv_init_with_processor(0, 0, 0);
    if (w2xconv_load_models(converter, model_dir.c_str()) == -1)
        return;    
        
    cv::Size s = mat.size(); 
    mat_proc = mat.clone();
    mat_output = mat.clone();
    
    /* Update progress */
    if (! preview) {
        gimp_progress_set_text("Loading Model...");
        gimp_progress_update((gdouble) 0.2);
    }
    
    w2xconv_convert_rgb (converter,
                         mat_proc.data, mat_proc.step[0],
                         mat.data, mat.step[0],
                         s.width, s.height,
                         denoise_level,
                         (double) 1.0,
                         block_size);
                         
    /* Update progress */
    if (! preview) {
        gimp_progress_set_text("Converting...");
        gimp_progress_update((gdouble) 0.5);
    }
                         
    if (type == GIMP_RGBA_IMAGE) {
        cv::cvtColor(mat_proc, mat_output, cv::COLOR_BGR2BGRA);
    }
    else if (type == GIMP_RGB_IMAGE) {
        mat_output = mat_proc.clone();
    }
    
    //cv::Mat  mat3 = mat2.clone();                        
    //cv::cvtColor(mat2, mat3, cv::COLOR_BGR2RGBA );
    //cv::imwrite("/home/huynhduc/Desktop/test.png", mat3);
    
    /* Update progress */
    if (! preview) {
        gimp_progress_set_text("Writing...");
        gimp_progress_update((gdouble) 0.9);
    }
    
    if (preview) {
        setMatToDrawablePreview(mat_output,
                                preview);
    }
    else {
        setMatToDrawable(mat_output,
                         drawable);
    }
    
    w2xconv_fini(converter);
    
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

static void 
setMatToDrawable(cv::Mat& mat, GimpDrawable* drawable)
{
    gint x1, y1, x2, y2;
    gimp_drawable_mask_bounds(drawable->drawable_id,
                              &x1, &y1,
                              &x2, &y2);

    GimpPixelRgn rgn;
    gimp_pixel_rgn_init(&rgn,
                        drawable,
                        x1, y1,
                        x2 - x1, y2 - y1,
                        TRUE, TRUE);   

    gimp_pixel_rgn_set_rect(&rgn,
                            mat.data,
                            x1, y1,
                            x2 - x1, y2 - y1);  

    gimp_drawable_flush(drawable);
    gimp_drawable_merge_shadow(drawable->drawable_id, TRUE);
    gimp_drawable_update(drawable->drawable_id,
                         x1, y1,
                         x2 - x1, y2 - y1);
}

static void 
setMatToDrawablePreview(cv::Mat& mat, GimpPreview *preview)
{
    if (! preview)
        return;
    gint x1, y1, x2, y2;
    GimpDrawable *drawable = gimp_drawable_preview_get_drawable(GIMP_DRAWABLE_PREVIEW (preview) );
    gimp_drawable_mask_bounds(drawable->drawable_id,
                              &x1, &y1,
                              &x2, &y2);
    
    GimpPixelRgn rgn;
    gimp_pixel_rgn_init(&rgn,
                        drawable,
                        x1, y1,
                        x2 - x1, y2 - y1,
                        TRUE, TRUE);   
                        
    gimp_pixel_rgn_set_rect(&rgn,
                            mat.data,
                            x1, y1,
                            x2 - x1, y2 - y1);  

    gimp_drawable_preview_draw_region (GIMP_DRAWABLE_PREVIEW (preview),
                                       &rgn);
                                       
}

static gboolean
denoise_dialog(GimpDrawable* drawable)
{
    GtkWidget *dialog;
    GtkWidget *main_vbox;
    GtkWidget *main_hbox;
    GtkWidget *preview;
    GtkWidget *frame;
    GtkWidget *block_size_label;
    GtkWidget *alignment;
    GtkWidget *spinbutton;
    GtkObject *spinbutton_adj;
    GtkWidget *combobox;
    GtkWidget *frame_label;
    gboolean run;
    
    gimp_ui_init("waifu2x-denoise", FALSE);
    
    dialog = gimp_dialog_new("Waifu2x Denoise",
                             "waifu2x-denoise",
                             NULL, (GtkDialogFlags)0,
                             gimp_standard_help_func, "waifu2x-converter-cpp-denoise",
                             GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                             GTK_STOCK_OK, GTK_RESPONSE_OK,
                             NULL);
                             
    main_vbox = gtk_vbox_new(FALSE, 6);
    gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), main_vbox);
    gtk_widget_show (main_vbox);
    
    frame = gtk_frame_new (NULL);
    gtk_widget_show (frame);
    gtk_box_pack_start (GTK_BOX (main_vbox), frame, TRUE, TRUE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (frame), 6);

    alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
    gtk_widget_show (alignment);
    gtk_container_add (GTK_CONTAINER (frame), alignment);
    gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 6, 6, 6, 6);

    main_hbox = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (main_hbox);
    gtk_container_add (GTK_CONTAINER (alignment), main_hbox);
    
    block_size_label = gtk_label_new_with_mnemonic ("_Block Size:");
    gtk_widget_show (block_size_label);
    gtk_box_pack_start (GTK_BOX (main_hbox), block_size_label, FALSE, FALSE, 6);
    gtk_label_set_justify (GTK_LABEL (block_size_label), GTK_JUSTIFY_RIGHT);
    
    spinbutton_adj = gtk_adjustment_new (512, 128, 2048, 1, 6, 6);
    spinbutton = gtk_spin_button_new (GTK_ADJUSTMENT (spinbutton_adj), 1, 0);
    gtk_widget_show (spinbutton);
    gtk_box_pack_start (GTK_BOX (main_hbox), spinbutton, FALSE, FALSE, 6);
    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinbutton), TRUE);

    combobox = gtk_combo_box_text_new();
    const gchar *values[] = {"Denoise Level", "1", "2", "3"};
    for (gint i = 0; i < G_N_ELEMENTS (values); i++){
  	    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combobox), values[i]);
    }
    gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), 0);
    g_signal_connect (combobox, "changed",
                      G_CALLBACK (on_changed),
                      NULL);
    gtk_widget_show(combobox);
    gtk_container_add (GTK_CONTAINER (main_hbox), combobox);

    frame_label = gtk_label_new ("Modify Values");
    gtk_widget_show (frame_label);
    gtk_frame_set_label_widget (GTK_FRAME (frame), frame_label);
    gtk_label_set_use_markup (GTK_LABEL (frame_label), TRUE);
    
    preview = gimp_drawable_preview_new (drawable, &input_vals.preview);
    gtk_box_pack_start (GTK_BOX (main_vbox), preview, TRUE, TRUE, 0);
    gtk_widget_show (preview);
    
    g_signal_connect_swapped (preview, "invalidated",
                              G_CALLBACK (denoise),
                              drawable);
    g_signal_connect_swapped (combobox, "changed",
                              G_CALLBACK (gimp_preview_invalidate),
                              preview);
                              
    //g_signal_connect_swapped (spinbutton_adj, "value_changed",
    //                          G_CALLBACK (gimp_preview_invalidate),
    //                          preview);
    //block size only change the speed so no need to rerun

    denoise (drawable, GIMP_PREVIEW (preview));
    
    g_signal_connect (combobox, "changed",
                      G_CALLBACK (on_changed),
                      NULL);
    
    g_signal_connect (spinbutton_adj, "value_changed",
                      G_CALLBACK (gimp_int_adjustment_update),
                      &input_vals.block_size);
                      
    gtk_widget_show (dialog);

    run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

    gtk_widget_destroy (dialog);

    return run;
}

static void
on_changed (GtkComboBox *widget,
            gpointer   user_data)
{
    GtkComboBox *combo_box = widget;

    if (gtk_combo_box_get_active (combo_box) != 0) {
        gint denoise_level = g_ascii_strtoll(gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT(combo_box)),
                                             NULL,
                                             0);
        switch (denoise_level) {
            case 2:
            case 3:
                input_vals.denoise_level = denoise_level;
            break;
            default:
                input_vals.denoise_level = 1;
            break;
        }
    }
}
