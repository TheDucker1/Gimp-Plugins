#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cassert>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include <gtk/gtk.h>

#define SIZE_LIMIT 1500000000

using namespace cv;
using namespace std;

/* https://gist.github.com/JohnWayne1986/e1aee3154d14aa3597ad0e69479838e8 */
template<typename type>
struct UniqueFunctor {
    cv::Mat in;

    std::vector<type> operator()() {
        assert(in.channels() == 1 && "This implementation is only for single-channel images");
        auto begin = in.begin<type>(), end = in.end<type>();
        auto last = std::unique(begin, end);    // remove adjacent duplicates to reduce size
        std::sort(begin, last);                 // sort remaining elements
        last = std::unique(begin, last);        // remove duplicates
        return std::vector<type>(begin, last);
    }
};

template<typename type, int cn>
struct UniqueFunctor<cv::Vec<type, cn>> {
    cv::Mat in;

    using vec_type = cv::Vec<type, cn>;
    std::vector<vec_type> operator()() {
        auto compare = [] (vec_type const& v1, vec_type const& v2) {
            return std::lexicographical_compare(&v1[0], &v1[cn], &v2[0], &v2[cn]);
        };
        auto begin = in.begin<vec_type>(), end = in.end<vec_type>();
        auto last = std::unique(begin, end);    // remove adjacent duplicates to reduce size
        std::sort(begin, last, compare);        // sort remaining elements
        last = std::unique(begin, last);        // remove duplicates
        return std::vector<vec_type>(begin, last);
    }
};

template<typename full_type>
std::vector<cv::Vec3b> image_get_unique_value(cv::Mat& img) {
    auto unique = UniqueFunctor<full_type>{img}();
    return unique;
}
/* ----------------------------------------------------------------------- */

typedef struct CENTER_DETECT_CALLBACK {
  Mat main;
  Mat sub1;
  Mat sub2;
  char info[3];
} CENTER_DETECT_CALLBACK;

Mat offset_estimate(Mat main, Mat sub, int iterations = 10, int warp_mode = MOTION_EUCLIDEAN);
Mat process(Mat i);
CENTER_DETECT_CALLBACK center_detect(Mat b, Mat g, Mat r);

typedef struct
{
    gint iters;
    gint warp_mode;
    gboolean preview;
} InputVals;

static InputVals input_vals = 
{
    10,
    MOTION_EUCLIDEAN,
    FALSE
};

static void query                             (void);
static void run                               (const gchar      *name,
                                              gint              nparams,
                                              const GimpParam  *param,
                                              gint             *nreturn_vals,
                                              GimpParam       **return_vals);
static void fixoffset                         (GimpDrawable *drawable,
                                               GimpPreview *preview);
static cv::Mat drawableToMat                  (GimpDrawable *drawable);
static void setMatToDrawable                  (cv::Mat& mat, 
                                               GimpDrawable* drawable);
static void setMatToDrawablePreview           (cv::Mat& mat, 
                                               GimpPreview* preview);
static gboolean fixoffset_dialog              (GimpDrawable* drawable);
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
    "channels-offset-fix",
    "Fix Channels Offset",
    "Fix RGB offset in image",
    "TheDucker1",
    "",
    "2021",
    "_Fix Channels Offset",
    "RGB*",
    GIMP_PLUGIN,
    G_N_ELEMENTS (args), 0,
    args, NULL);

  gimp_plugin_menu_register ("channels-offset-fix",
                             "<Image>/Filters/Misc");
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
    
    gimp_progress_init ("Fixing...");
    
    drawable = gimp_drawable_get(param[2].data.d_drawable);

    switch(run_mode) {
        case GIMP_RUN_INTERACTIVE:
            gimp_get_data("channels-offset-fix", &input_vals);
            
            if (! fixoffset_dialog(drawable))
                return;
        break;
    
        case GIMP_RUN_NONINTERACTIVE:
            if (nparams != 5)
                status = GIMP_PDB_CALLING_ERROR;
            if (status == GIMP_PDB_SUCCESS) {
                input_vals.iters = param[3].data.d_int32;
                input_vals.warp_mode = param[4].data.d_int32;
            }
        break;
        
        case GIMP_RUN_WITH_LAST_VALS:
            gimp_get_data ("channels-offset-fix", &input_vals);
        break;
        
        default:
        break;
    }

    fixoffset(drawable, NULL);
    
    gimp_displays_flush ();
    gimp_drawable_detach (drawable);
    
    if (run_mode == GIMP_RUN_INTERACTIVE)
          gimp_set_data ("channels-offset-fix", &input_vals, sizeof (InputVals));

    return;
}

static void
fixoffset (GimpDrawable *drawable_input,
           GimpPreview *preview) 
{
    gint channels;
    gint x1, x2, y1, y2;
    gint width, height;
    GimpPixelRgn rgnwrite;
    GimpDrawable *drawable;
    if (! preview)
        gimp_progress_init("Fixing...");
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
    cv::Mat img(height, width, CV_8UC3);
    if (type == GIMP_RGBA_IMAGE) {
        img = mat_input.clone();
    }
    else if (type == GIMP_RGB_IMAGE) {
        img = mat_input.clone();
    }
    else if ((type == GIMP_GRAY_IMAGE) | (type == GIMP_GRAYA_IMAGE) | (type == GIMP_INDEXEDA_IMAGE) | (type == GIMP_INDEXED_IMAGE)) {
        g_message("Indexed color image is not supported");
        return;
    }
    else {
        g_message("Unrecognized colorspace");
        return;
    }
    Mat bgr[3];
    split(img, bgr);
    if (! preview) {
        gimp_progress_set_text("Splitting...");
        gimp_progress_update((gdouble) 0.2);
    }
    img.release();
    CENTER_DETECT_CALLBACK dCallback = center_detect(bgr[0], bgr[1], bgr[2]);
    for (int i = 0; i < 3; ++i) {
      bgr[i].release();
    }
    dCallback.sub1 = offset_estimate(dCallback.main, dCallback.sub1, input_vals.iters, input_vals.warp_mode);
    dCallback.sub2 = offset_estimate(dCallback.main, dCallback.sub2, input_vals.iters, input_vals.warp_mode);
    if (! preview) {
        gimp_progress_set_text("Calculating...");
        gimp_progress_update((gdouble) 0.5);
    }
    Mat mat_output;
    Mat bgr2[3];
    if (strcmp(dCallback.info, "bgr") == 0) {
        dCallback.main.copyTo(bgr2[0]);
        dCallback.sub1.copyTo(bgr2[1]);
        dCallback.sub2.copyTo(bgr2[2]);
    }
    else if (strcmp(dCallback.info, "gbr") == 0) {
        dCallback.main.copyTo(bgr2[1]);
        dCallback.sub1.copyTo(bgr2[0]);
        dCallback.sub2.copyTo(bgr2[2]);
    }
    else {
        dCallback.main.copyTo(bgr2[2]);
        dCallback.sub1.copyTo(bgr2[1]);
        dCallback.sub2.copyTo(bgr2[0]);
    }
    merge(bgr2, 3, mat_output);
    if (! preview) {
        gimp_progress_set_text("Merging...");
        gimp_progress_update((gdouble) 0.8);
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
fixoffset_dialog(GimpDrawable* drawable)
{
    GtkWidget *dialog;
    GtkWidget *main_vbox;
    GtkWidget *main_hbox;
    GtkWidget *preview;
    GtkWidget *frame;
    GtkWidget *alignment;
    
    GtkWidget *combobox;
    
    GtkWidget *frame_label;
    GtkWidget *iter_label;
    gboolean run;
    
    GtkAdjustment *iter_adj;
    GtkWidget *iter_spin;
    
    gimp_ui_init("channels-offset-fix-ui", FALSE);
    
    dialog = gimp_dialog_new("Channels Offset Fix",
                             "channels-offset-fix-ui",
                             NULL, (GtkDialogFlags)0,
                             gimp_standard_help_func, "channels-offset-fix",
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
    const gchar *values[] = {"Warp Mode (Default: Euclidian)", "Translation", "Euclidian", "Affine", "Homography"};
    for (gint i = 0; i < G_N_ELEMENTS (values); i++){
  	    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combobox), values[i]);
    }
    gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), 0);
    gtk_widget_show(combobox);
    //gtk_container_add (GTK_CONTAINER (main_hbox), combobox);
    gtk_table_attach_defaults( GTK_TABLE (main_hbox), combobox, 0, 1, 0, 1);
    
    iter_label = gtk_label_new ("Iterations: ");
    gtk_widget_show (iter_label);
    //gtk_box_pack_start (GTK_BOX (main_hbox), sp_label, FALSE, FALSE, 6);
    gtk_table_attach_defaults( GTK_TABLE (main_hbox), iter_label, 0, 1, 1, 2);
    gtk_label_set_justify (GTK_LABEL (iter_label), GTK_JUSTIFY_RIGHT);
    
    /*
    iter_entry = gtk_entry_new();
    gtk_entry_set_max_length (GTK_ENTRY (iter_entry), 5);
    gtk_entry_set_text (GTK_ENTRY (iter_entry), std::to_string(input_vals.iters).c_str());
    gtk_editable_set_editable (GTK_EDITABLE (iter_entry),
                               (gboolean) TRUE);
    gtk_editable_select_region (GTK_EDITABLE (iter_entry),
			                          0, GTK_ENTRY (iter_entry)->text_length);
    //gtk_box_pack_start (GTK_BOX (main_hbox), sp_entry, FALSE, FALSE, 6);
    gtk_table_attach_defaults( GTK_TABLE (main_hbox), iter_entry, 1, 2, 1, 2);
    gtk_widget_show (iter_entry);
    */
    iter_adj = (GtkAdjustment *) gtk_adjustment_new (10.0, 
                                                     1.0, 5000.0,
                                                     1.0,
                                                     5.0, 0.0);
    iter_spin = gtk_spin_button_new(iter_adj, 0, 0);
    gtk_spin_button_set_wrap( GTK_SPIN_BUTTON (iter_spin), TRUE);
    gtk_table_attach_defaults( GTK_TABLE (main_hbox), iter_spin, 1, 2, 1, 2);
    gtk_widget_show (iter_spin);
    
    preview = gimp_drawable_preview_new (drawable, &input_vals.preview);
    gtk_box_pack_start (GTK_BOX (main_vbox), preview, TRUE, TRUE, 0);
    gtk_widget_show (preview);
    
    g_signal_connect_swapped (preview, "invalidated",
                              G_CALLBACK (fixoffset),
                              drawable);
                              
    g_signal_connect_swapped (combobox, "changed",
                              G_CALLBACK (gimp_preview_invalidate),
                              preview);
                              
    //g_signal_connect_swapped (iter_entry, "changed",
		//                          G_CALLBACK (gimp_preview_invalidate),
		//                          preview);
		                          
		fixoffset (drawable, GIMP_PREVIEW (preview));
    
    g_signal_connect (combobox, "changed",
                      G_CALLBACK (on_changed),
                      NULL);
                      
    g_signal_connect (iter_adj, "value_changed",
		                  G_CALLBACK (gimp_int_adjustment_update),
		                  &input_vals.iters);
		                  
		
    //g_signal_connect (spinbutton_adj, "value_changed",
    //                  G_CALLBACK (gimp_int_adjustment_update),
    //                  &input_vals.block_size);
                      
    gtk_widget_show (dialog);

    run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

    gtk_widget_destroy (dialog);

    return run;
}

static void
on_changed (GtkComboBox *widget,
            gpointer user_data)
{
    GtkComboBox *combo_box = widget;

    if (gtk_combo_box_get_active (combo_box) != 0) {
        gchar *selection = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT(combo_box));
        
        if (strcmp(selection, "Translation") == 0) {
            input_vals.warp_mode = MOTION_TRANSLATION;
        }
        else if (strcmp(selection, "Affine") == 0) {
            input_vals.warp_mode = MOTION_AFFINE;
        }
        else if (strcmp(selection, "Homography") == 0) {
            input_vals.warp_mode = MOTION_HOMOGRAPHY;
        }
        else {
            input_vals.warp_mode = MOTION_EUCLIDEAN;
        }
        
        g_free(selection);
    }
}

Mat process(Mat i) {
  Mat j;
  i.convertTo(j, CV_32F);
  j = j * 0.003383;
  j = j - mean(j) + 1.;
  return j;
}

CENTER_DETECT_CALLBACK center_detect(Mat b, Mat g, Mat r) {
  Mat i1 = process(b);
  Mat i2 = process(g);
  Mat i3 = process(r);
  double d1 = cv::sum(i2 + i3 - 2 * i1)[0];
  double d2 = cv::sum(i1 + i3 - 2 * i2)[0];
  double d3 = cv::sum(i2 + i1 - 2 * i3)[0];
  i1.release();
  i2.release();
  i3.release();
  CENTER_DETECT_CALLBACK callback;
  if (d1 == std::min(d1, std::min(d2, d3))) {
    b.copyTo(callback.main);
    g.copyTo(callback.sub1);
    r.copyTo(callback.sub2);
    strcpy(callback.info, "bgr");
  }
  else if (d2 == std::min(d1, std::min(d2, d3))) {
    g.copyTo(callback.main);
    b.copyTo(callback.sub1);
    r.copyTo(callback.sub2);
    strcpy(callback.info, "gbr");
  }
  else if (d1 == std::min(d1, std::min(d2, d3))) {
    r.copyTo(callback.main);
    b.copyTo(callback.sub1);
    g.copyTo(callback.sub2);
    strcpy(callback.info, "rbg");
  }
  return callback;
}

Mat offset_estimate(Mat main, Mat sub, int iterations, int warp_mode) {
  assert(main.size() == sub.size());
  cout << iterations << endl;
  if (iterations <= 0) {
    iterations = 10;
  }
  Mat warp_matrix;
  if (warp_mode == MOTION_HOMOGRAPHY) {
    warp_matrix = Mat::eye(3, 3, CV_32F);
	}
	else {
    warp_matrix = Mat::eye(2, 3, CV_32F);
  }
  int number_of_iterations = iterations;
  double termination_eps = 1e-10;
  TermCriteria criteria (TermCriteria::COUNT+TermCriteria::EPS, number_of_iterations, termination_eps);
  findTransformECC(
    main,
	  sub,
	  warp_matrix,
	  warp_mode,
	  criteria
	);
	Mat sub_ret;
	if (warp_mode != MOTION_HOMOGRAPHY) {
    warpAffine(sub, sub_ret, warp_matrix, main.size(), INTER_LINEAR + WARP_INVERSE_MAP);
  }
	else {
    warpPerspective(sub, sub_ret, warp_matrix, main.size(),INTER_LINEAR + WARP_INVERSE_MAP);
  }
  return sub_ret;
}
