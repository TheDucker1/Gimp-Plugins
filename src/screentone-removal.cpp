/* Credit to natethegreate
 * (https://github.com/natethegreate/Screentone-Remover/)
 * require opencv4
 * require gtk2.0
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

#define SIZE_LIMIT 1500000000

typedef struct
{
    gint blur_amount;
    gfloat sp_strength;
    gfloat sl_strength;
    gboolean preview;
} InputVals;

static InputVals input_vals = 
{
    2,
    5.56,
    -1.14,
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
static void blur                              (cv::Mat &src, 
                                               cv::Mat &dst, 
                                               gint blur_amount);
static void sharp                             (cv::Mat &src,
                                               cv::Mat &dst,
                                               gfloat sp,
                                               gfloat sl);
static void sp_entry_callback                 (GtkWidget *widget,
                                               GtkWidget *entry);
static void sl_entry_callback                 (GtkWidget *widget,
                                               GtkWidget *entry);

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
    "screentone-removal",
    "Screentone Removal",
    "Remove screentone from an image",
    "TheDucker1",
    "Credit to natethegreate",
    "2020",
    "_Screentone Remove",
    "RGB*, GRAY*",
    GIMP_PLUGIN,
    G_N_ELEMENTS (args), 0,
    args, NULL);

  gimp_plugin_menu_register ("screentone-removal",
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
            gimp_get_data("screentone-removal", &input_vals);
            
            if (! denoise_dialog(drawable))
                return;
        break;
    
        case GIMP_RUN_NONINTERACTIVE:
            if (nparams != 6)
                status = GIMP_PDB_CALLING_ERROR;
            if (status == GIMP_PDB_SUCCESS) {
                input_vals.blur_amount = param[3].data.d_int32;
                input_vals.sp_strength = param[4].data.d_float;
                input_vals.sl_strength = param[5].data.d_float;
            }
        break;
        
        case GIMP_RUN_WITH_LAST_VALS:
            gimp_get_data ("screentone-removal", &input_vals);
        break;
        
        default:
        break;
    }
    
    denoise(drawable, NULL);
    
    gimp_displays_flush ();
    gimp_drawable_detach (drawable);
    
    if (run_mode == GIMP_RUN_INTERACTIVE)
          gimp_set_data ("screentone-removal", &input_vals, sizeof (InputVals));

    return;
}

static void
denoise (GimpDrawable *drawable_input,
         GimpPreview *preview) 
{
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
    cv::Mat mat(height, width, CV_8UC1);
    cv::Mat mat_proc1(height, width, CV_8UC1);
    cv::Mat mat_proc2(height, width, CV_8UC1);
    cv::Mat mat_output;
    if (type == GIMP_RGBA_IMAGE) {
        cv::cvtColor(mat_input, mat, cv::COLOR_BGRA2GRAY);
    }
    else if (type == GIMP_RGB_IMAGE) {
        cv::cvtColor(mat_input, mat, cv::COLOR_BGRA2GRAY);
    }
    else if ((type == GIMP_GRAY_IMAGE) | (type == GIMP_GRAYA_IMAGE)) {
        mat = mat_input.clone();
    }
    else if ((type == GIMP_INDEXEDA_IMAGE) | (type == GIMP_INDEXED_IMAGE)) {
        g_message("Indexed color image is not supported");
        return;
    }
    else {
        g_message("Unrecognized colorspace");
        return;
    }
       
    cv::Size s = mat.size(); 
    mat_proc1 = mat.clone();
    mat_proc2 = mat.clone();
    mat_output = mat_input.clone();
    
    /* Update progress */
    if (! preview) {
        gimp_progress_set_text("Blurring...");
        gimp_progress_update((gdouble) 0.2);
    }
    
    int bs_amount = 0;
    switch (input_vals.blur_amount) {
        case 1:
        bs_amount = 5;
        break;
        case 3:
        bs_amount = 7;
        break;
        case 2:
        default:
        bs_amount = 5;
        break;
    }
    
    blur(mat, mat_proc1, bs_amount); 
                        
    /* Update progress */
    if (! preview) {
        gimp_progress_set_text("Sharpening...");
        gimp_progress_update((gdouble) 0.5);
    }
    
    sharp(mat_proc1, mat_proc2, input_vals.sp_strength, input_vals.sl_strength);
                         
    if (type == GIMP_RGBA_IMAGE) {
        cv::cvtColor(mat_proc2, mat_output, cv::COLOR_GRAY2BGRA);
    }
    else if (type == GIMP_RGB_IMAGE) {
        cv::cvtColor(mat_proc2, mat_output, cv::COLOR_GRAY2BGR);
    }
    else if ((type == GIMP_GRAY_IMAGE) | (type == GIMP_GRAYA_IMAGE)) {
        mat_output = mat_proc2.clone();
    }
    
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
    GtkWidget *alignment;
    
    GtkWidget *combobox;
    
    GtkWidget *frame_label;
    GtkWidget *sp_label;
    GtkWidget *sl_label;
    gboolean run;
    
    GtkWidget *sp_entry;
    GtkWidget *sl_entry;
    
    gimp_ui_init("screentone-removal-ui", FALSE);
    
    dialog = gimp_dialog_new("Screentone Removal",
                             "screentone-removal-ui",
                             NULL, (GtkDialogFlags)0,
                             gimp_standard_help_func, "screentone-removal",
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

    main_hbox = gtk_table_new (2, 3, FALSE);
    gtk_widget_show (main_hbox);
    gtk_container_add (GTK_CONTAINER (alignment), main_hbox);
    
    frame_label = gtk_label_new ("Modify Values");
    gtk_widget_show (frame_label);
    gtk_frame_set_label_widget (GTK_FRAME (frame), frame_label);
    gtk_label_set_use_markup (GTK_LABEL (frame_label), TRUE);
    
    combobox = gtk_combo_box_text_new();
    const gchar *values[] = {"Blur Amount (Default: 2)", "1", "2", "3"};
    for (gint i = 0; i < G_N_ELEMENTS (values); i++){
  	    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combobox), values[i]);
    }
    gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), 0);
    gtk_widget_show(combobox);
    //gtk_container_add (GTK_CONTAINER (main_hbox), combobox);
    gtk_table_attach_defaults( GTK_TABLE (main_hbox), combobox, 0, 1, 0, 1);
    
    sp_label = gtk_label_new ("Sharpening Point Strength:");
    gtk_widget_show (sp_label);
    //gtk_box_pack_start (GTK_BOX (main_hbox), sp_label, FALSE, FALSE, 6);
    gtk_table_attach_defaults( GTK_TABLE (main_hbox), sp_label, 0, 1, 1, 2);
    gtk_label_set_justify (GTK_LABEL (sp_label), GTK_JUSTIFY_RIGHT);
    
    sp_entry = gtk_entry_new();
    gtk_entry_set_max_length (GTK_ENTRY (sp_entry), 5);
    gtk_entry_set_text (GTK_ENTRY (sp_entry), std::to_string(input_vals.sp_strength).c_str());
    gtk_editable_set_editable (GTK_EDITABLE (sp_entry),
                               (gboolean) TRUE);
    gtk_editable_select_region (GTK_EDITABLE (sp_entry),
			                          0, GTK_ENTRY (sp_entry)->text_length);
    //gtk_box_pack_start (GTK_BOX (main_hbox), sp_entry, FALSE, FALSE, 6);
    gtk_table_attach_defaults( GTK_TABLE (main_hbox), sp_entry, 1, 2, 1, 2);
    gtk_widget_show (sp_entry);
    
    sl_label = gtk_label_new ("Sharpening Low Strength (Must be negative): ");
    gtk_widget_show (sl_label);
    //gtk_box_pack_start (GTK_BOX (main_hbox), sl_label, FALSE, FALSE, 6);
    gtk_table_attach_defaults( GTK_TABLE (main_hbox), sl_label, 0, 1, 2, 3);
    gtk_label_set_justify (GTK_LABEL (sp_label), GTK_JUSTIFY_RIGHT);
    
    sl_entry = gtk_entry_new();
    gtk_entry_set_max_length (GTK_ENTRY (sl_entry), 5);
    gtk_entry_set_text (GTK_ENTRY (sl_entry), std::to_string(input_vals.sl_strength).c_str());
    gtk_editable_set_editable (GTK_EDITABLE (sl_entry),
                               (gboolean) TRUE);
    gtk_editable_select_region (GTK_EDITABLE (sl_entry),
			                          0, GTK_ENTRY (sl_entry)->text_length);
    //gtk_box_pack_start (GTK_BOX (main_hbox), sl_entry, FALSE, FALSE, 6);
    gtk_table_attach_defaults( GTK_TABLE (main_hbox), sl_entry, 1, 2, 2, 3);
    gtk_widget_show (sl_entry);
    
    
    
    preview = gimp_drawable_preview_new (drawable, &input_vals.preview);
    gtk_box_pack_start (GTK_BOX (main_vbox), preview, TRUE, TRUE, 0);
    gtk_widget_show (preview);
    
    g_signal_connect_swapped (preview, "invalidated",
                              G_CALLBACK (denoise),
                              drawable);
                              
    g_signal_connect_swapped (combobox, "changed",
                              G_CALLBACK (gimp_preview_invalidate),
                              preview);
                              
    g_signal_connect_swapped (sp_entry, "changed",
		                          G_CALLBACK (gimp_preview_invalidate),
		                          preview);
		                          
		g_signal_connect_swapped (sl_entry, "changed",
		                          G_CALLBACK (gimp_preview_invalidate),
		                          preview);
                              
    denoise (drawable, GIMP_PREVIEW (preview));
    
    g_signal_connect (combobox, "changed",
                      G_CALLBACK (on_changed),
                      NULL);
                      
    g_signal_connect (sp_entry, "changed",
		                  G_CALLBACK (sp_entry_callback),
		                  sp_entry);
		                  
		g_signal_connect (sl_entry, "changed",
		                  G_CALLBACK (sl_entry_callback),
		                  sl_entry);
    
    //g_signal_connect (spinbutton_adj, "value_changed",
    //                  G_CALLBACK (gimp_int_adjustment_update),
    //                  &input_vals.block_size);
                      
    gtk_widget_show (dialog);

    run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

    gtk_widget_destroy (dialog);

    return run;
}

static void
sp_entry_callback (GtkWidget *widget,
                   GtkWidget *entry)
{
    float sp = std::atof (gtk_entry_get_text ( GTK_ENTRY (entry) ) );
    input_vals.sp_strength = sp;
    return;
}

static void
sl_entry_callback (GtkWidget *widget,
                   GtkWidget *entry)
{
    float sl = std::atof (gtk_entry_get_text ( GTK_ENTRY (entry) ) );
    input_vals.sl_strength = sl;
    return;
}

static void
on_changed (GtkComboBox *widget,
            gpointer user_data)
{
    GtkComboBox *combo_box = widget;

    if (gtk_combo_box_get_active (combo_box) != 0) {
        gint blur_amount = g_ascii_strtoll(gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT(combo_box)),
                                           NULL,
                                           0);
        switch (blur_amount) {
            case 1:
            case 3:
                input_vals.blur_amount = blur_amount;
            break;
            case 2:
            default:
                input_vals.blur_amount = 2;
            break;
        }
    }
}

static void blur (cv::Mat &src, 
                  cv::Mat &dst, 
                  gint blur_amount)
{
    dst = src.clone();
    if (blur_amount == 7) {
        cv::Mat dst2 = src.clone();
        cv::GaussianBlur(src, dst2, cv::Size(7, 7), 0);
        cv::bilateralFilter(dst2, dst, 7, 80, 80);
    }
    else {
        cv::Mat dst2 = src.clone();
        cv::GaussianBlur(src, dst2, cv::Size(5, 5), 0);
        cv::bilateralFilter(dst2, dst, 7, 10 * blur_amount, 80);
    }
}
static void sharp (cv::Mat &src,
                   cv::Mat &dst,
                   gfloat sp,
                   gfloat sl)
{
    dst = src.clone();
    
    cv::Mat s_kernel = (cv::Mat_<float>(3,3) << 0,  sl,  0, 
                                                sl, sp, sl, 
                                                0,  sl,  0);
    
    cv::filter2D(src, dst, -1, s_kernel);
}
