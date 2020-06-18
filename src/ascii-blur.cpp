/* Credit to TheDucker1
 * require opencv4
 * require c++11
 * require gtk2.0
 */

#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/core/types_c.h>
#include <vector>
#include <algorithm>
#include <chrono>
#include <math.h>
#include <cstdint>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include <gtk/gtk.h>

#define SIZE_LIMIT 150000000

struct pix_data {
    cv::Mat im;
    int dif;
};

typedef struct {
    gint CHAR_SIZE;
    gint _K;
    gdouble FONT_SCALE;
    gboolean preview;
} InputVals;

std::string CHAR_MAP;

static InputVals input_vals = 
{
8,
16,
0.5,
FALSE
};


void image_resize(cv::Mat& src, cv::Mat& dst, 
                  int width = 0, int height = 0,
                  cv::InterpolationFlags flag = cv::INTER_LANCZOS4) ;

int image_dif(cv::Mat& mat1, cv::Mat& mat2);

void generate_ascii(cv::Mat& src, cv::Mat& dst,
                    bool pad);

void generate_chunk(cv::Mat& src, cv::Mat& dst);

void execute_chunk(int startX, int endX,
                   int startY, int endY,
                   cv::Mat im,
                   cv::Mat& dst);

void reduce_colors(cv::Mat& src, cv::Mat& dst,
                   int k);
                   
void image_cut(cv::Mat& src, cv::Mat& dst,
               int _startX, int _startY,
               int _endX, int _endY);

                   

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

static void query                             (void);
static void run                               (const gchar      *name,
                                              gint              nparams,
                                              const GimpParam  *param,
                                              gint             *nreturn_vals,
                                              GimpParam       **return_vals);
static void asciify                           (GimpDrawable *drawable_input,
                                               GimpPreview *preview);
static cv::Mat drawableToMat                  (GimpDrawable *drawable);
static void setMatToDrawable                  (cv::Mat& mat, 
                                               GimpDrawable* drawable);
static void setMatToDrawablePreview           (cv::Mat& mat, 
                                               GimpPreview* preview);
static gboolean asciify_dialog                (GimpDrawable* drawable);
static void charmap_callback                  (GtkWidget *widget, 
                                               GtkWidget *entry);
static void charsize_callback                 (GtkWidget *button,
                                               gpointer user_data);


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
    "ascii-blur",
    "Asciify",
    "Asciify an Image",
    "TheDucker1",
    "No Copyrighted",
    "2020",
    "_Asciify",
    "RGB*, GRAY*",
    GIMP_PLUGIN,
    G_N_ELEMENTS (args), 0,
    args, NULL);

  gimp_plugin_menu_register ("ascii-blur",
                             "<Image>/Filters/Blur");
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

    gimp_progress_init ("Asciifying...");

    drawable = gimp_drawable_get(param[2].data.d_drawable);
    
    switch(run_mode) {
        case GIMP_RUN_INTERACTIVE:
            gimp_get_data("ascii-blur", &input_vals);
            
            if (! asciify_dialog(drawable))
                return;
        break;
    
        case GIMP_RUN_NONINTERACTIVE:
            if (nparams != 6)
                status = GIMP_PDB_CALLING_ERROR;
            if (status == GIMP_PDB_SUCCESS) {
                CHAR_MAP = (param[5].data.d_string);
                input_vals._K = param[3].data.d_int32;
                input_vals.CHAR_SIZE = param[4].data.d_int32;
            }
        break;
        
        case GIMP_RUN_WITH_LAST_VALS:
            gimp_get_data ("ascii-blur", &input_vals);
        break;
        
        default:
        break;
    }
    
    asciify(drawable, NULL);
    
    gimp_displays_flush ();
    gimp_drawable_detach (drawable);
    
    if (run_mode == GIMP_RUN_INTERACTIVE)
          gimp_set_data ("ascii-blur", &input_vals, sizeof (InputVals));
    
    return;
}

static void asciify (GimpDrawable *drawable_input,
                     GimpPreview *preview)
{
    if (CHAR_MAP.length() == 0)
        CHAR_MAP = "01";
    if (input_vals._K < 4)
        input_vals._K = 4;
    else if (input_vals._K > 128)
        input_vals._K = 128;
    if (input_vals.CHAR_SIZE > 16)
        input_vals.CHAR_SIZE = 16;
    else if (input_vals.CHAR_SIZE < 2)
        input_vals.CHAR_SIZE = 2;
    input_vals.FONT_SCALE = (gdouble) ((input_vals.CHAR_SIZE) * 100 / 16) / 100;
    gint channels;
    gint x1, x2, y1, y2;
    gint width, height;
    GimpPixelRgn rgnwrite;
    GimpDrawable *drawable;
    if (! preview)
        gimp_progress_init("Asciifying...");
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
    
    guint64 size = width * height * input_vals._K * CHAR_MAP.length() / 2;
    if (size > SIZE_LIMIT) {
        g_message("Selection size too big\nOr too many colors\nYou can configure SIZE_LIMIT in the source code and recompile");
        return;
    }
    channels = gimp_drawable_bpp (drawable->drawable_id);
    
    gimp_pixel_rgn_init (&rgnwrite,
                         drawable,
                         x1, y1,
                         width, height, 
                         preview == NULL, TRUE);
                         
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
    
    generate_ascii(mat, mat_proc,
                   false);
    
    if (type == GIMP_RGBA_IMAGE) {
        cv::cvtColor(mat_proc, mat_output, cv::COLOR_BGR2BGRA);
    }
    else if (type == GIMP_RGB_IMAGE) {
        mat_output = mat_proc.clone();
    }
    else if ((type == GIMP_GRAY_IMAGE) | (type == GIMP_GRAYA_IMAGE)) {
        cv::cvtColor(mat_proc, mat_output, cv::COLOR_BGR2GRAY);
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

static gboolean asciify_dialog (GimpDrawable* drawable) {
    GtkWidget *dialog;
    GtkWidget *main_vbox;
    GtkWidget *main_hbox;
    GtkWidget *preview;
    GtkWidget *frame;
    GtkWidget *K_label;
    GtkWidget *alignment;
    GtkWidget *K_spinbutton;
    GtkObject *K_spinbutton_adj;
    GtkWidget *frame_label;
    
    GtkWidget *CHAR_SIZE_label;
    GtkWidget *CHAR_SIZE_spinbutton;
    GtkObject *CHAR_SIZE_spinbutton_adj;
    
    GtkWidget *CHAR_MAP_entry;
    
    gboolean run;
    
    gimp_ui_init("ascii-blur-dialog", FALSE);
    
    dialog = gimp_dialog_new("Asciify",
                             "ascii-blur-dialog",
                             NULL, (GtkDialogFlags)0,
                             gimp_standard_help_func, "ascii-blur",
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
    
    K_label = gtk_label_new_with_mnemonic ("_Colors:");
    gtk_widget_show (K_label);
    gtk_box_pack_start (GTK_BOX (main_hbox), K_label, FALSE, FALSE, 6);
    gtk_label_set_justify (GTK_LABEL (K_label), GTK_JUSTIFY_RIGHT);
    
    K_spinbutton_adj = gtk_adjustment_new (input_vals._K, 4, 128, 1, 1, 6);
    K_spinbutton = gtk_spin_button_new (GTK_ADJUSTMENT (K_spinbutton_adj), 1, 0);
    gtk_widget_show (K_spinbutton);
    gtk_box_pack_start (GTK_BOX (main_hbox), K_spinbutton, FALSE, FALSE, 6);
    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (K_spinbutton), TRUE);
    
    CHAR_SIZE_label = gtk_label_new_with_mnemonic ("_Character Size:");
    gtk_widget_show (CHAR_SIZE_label);
    gtk_box_pack_start (GTK_BOX (main_hbox), CHAR_SIZE_label, FALSE, FALSE, 6);
    gtk_label_set_justify (GTK_LABEL (CHAR_SIZE_label), GTK_JUSTIFY_RIGHT);
    
    CHAR_SIZE_spinbutton_adj = gtk_adjustment_new (input_vals.CHAR_SIZE, 2, 16, 1, 1, 6);
    CHAR_SIZE_spinbutton = gtk_spin_button_new (GTK_ADJUSTMENT (CHAR_SIZE_spinbutton_adj), 1, 0);
    gtk_widget_show (CHAR_SIZE_spinbutton);
    gtk_box_pack_start (GTK_BOX (main_hbox), CHAR_SIZE_spinbutton, FALSE, FALSE, 6);
    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (CHAR_SIZE_spinbutton), TRUE);
    
    frame_label = gtk_label_new ("Modify Values");
    gtk_widget_show (frame_label);
    gtk_frame_set_label_widget (GTK_FRAME (frame), frame_label);
    gtk_label_set_use_markup (GTK_LABEL (frame_label), TRUE);
    
    CHAR_MAP_entry = gtk_entry_new();
    gtk_entry_set_max_length (GTK_ENTRY (CHAR_MAP_entry), 16);
    gtk_entry_set_text (GTK_ENTRY (CHAR_MAP_entry), "01");
    gtk_editable_set_editable (GTK_EDITABLE (CHAR_MAP_entry),
                               (gboolean) TRUE);
    gtk_editable_select_region (GTK_EDITABLE (CHAR_MAP_entry),
			                          0, GTK_ENTRY (CHAR_MAP_entry)->text_length);
    gtk_box_pack_start (GTK_BOX (main_hbox), CHAR_MAP_entry, TRUE, TRUE, 0);
    gtk_widget_show (CHAR_MAP_entry);
    
    preview = gimp_drawable_preview_new (drawable, &input_vals.preview);
    gtk_box_pack_start (GTK_BOX (main_vbox), preview, TRUE, TRUE, 0);
    gtk_widget_show (preview);
    
    g_signal_connect_swapped (preview, "invalidated",
                              G_CALLBACK (asciify),
                              drawable);
                              
    g_signal_connect_swapped (K_spinbutton_adj, "value_changed",
                              G_CALLBACK (gimp_preview_invalidate),
                              preview);
    
    g_signal_connect_swapped (CHAR_SIZE_spinbutton_adj, "value_changed",
                              G_CALLBACK (gimp_preview_invalidate),
                              preview);
                              
    g_signal_connect_swapped (CHAR_MAP_entry, "changed",
		                          G_CALLBACK (gimp_preview_invalidate),
		                          preview);
    
   
    asciify (drawable, GIMP_PREVIEW (preview));
    
    g_signal_connect (K_spinbutton, "value_changed",
                      G_CALLBACK (gimp_int_adjustment_update),
                      &input_vals._K);
                      
    g_signal_connect (CHAR_SIZE_spinbutton, "value_changed",
                      G_CALLBACK (charsize_callback),
                      NULL);
                      
    g_signal_connect (CHAR_MAP_entry, "changed",
		                  G_CALLBACK (charmap_callback),
		                  CHAR_MAP_entry);

                      
    gtk_widget_show (dialog);

    run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

    gtk_widget_destroy (dialog);

    return run;
}

static void
charsize_callback (GtkWidget *button,
                   gpointer user_data) 
{
    int CHAR_SIZE = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON (button) );
    input_vals.CHAR_SIZE = CHAR_SIZE;
    input_vals.FONT_SCALE = (gdouble) ((CHAR_SIZE) * 100 / 16) / 100;
    return;
}

static void
charmap_callback (GtkWidget *widget, 
                  GtkWidget *entry)
{
    const gchar *entry_text;
    entry_text = gtk_entry_get_text (GTK_ENTRY (entry));
    CHAR_MAP = entry_text;
    return;
}

void image_resize(cv::Mat& src, cv::Mat& dst, 
                  int width, int height,
                  cv::InterpolationFlags flag) 
{
    cv::Size s = src.size();
    cv::Size dim;
    int h = s.height, w = s.width;
    float ratio;
    if ((width == 0) && (height == 0)) {
        dst = src.clone();
        return;
    }
    
    if ((width == 0) || ((float(width) / float(w)) < (float(height) / float(h)))) {
        ratio = float(height) / float(h);
        dim.height = height;
        dim.width = int(w * ratio);
    }
    
    else {
        ratio = float(width) / float(w);
        dim.height = int(h * ratio);
        dim.width = width;
    }
    
    cv::resize(src, dst, dim, 0, 0, flag);
    return;
}

int image_dif(cv::Mat& mat1, cv::Mat& mat2) {
    cv::Size s1 = mat1.size(), s2 = mat2.size();
    if ((s1.width != s2.width) || (s1.height != s2.height))
        return -1;
    int dif = 0;
    cv::Point center = cv::Point(int(s1.width / 2), int(s1.height / 2));
    cv::Mat lab1, lab2;
    cv::cvtColor(mat1, lab1, cv::COLOR_BGR2Lab);
    cv::cvtColor(mat2, lab2, cv::COLOR_BGR2Lab);
    for (int y = 0; y < s2.height; ++y) {
        for (int x = 0; x < s2.width; ++x) {
            cv::Vec3b point1 = lab1.at<cv::Vec3b>(y, x);
            cv::Vec3b point2 = lab2.at<cv::Vec3b>(y, x);
            float d_pow = pow(center.x - x,2) + pow(center.y - y,2); //d^2
            if (d_pow < 0.1) {
                d_pow = 1;
            }
            dif += int(sqrt(pow(point2[0] - point1[0], 2) + pow(point2[1] - point1[1], 2) + pow(point2[2] - point1[2], 2)) / d_pow);
        }
    }
    return dif;
}

void generate_ascii(cv::Mat& src, cv::Mat& dst,
                    bool pad)
{
    cv::Mat colors = src.clone(), clone = src.clone();
    cv::Mat padded;
    std::vector<cv::Vec3b> unique_Colors = image_get_unique_value<cv::Vec3b>(colors);
    int n_colors = unique_Colors.size();
    if (n_colors < 1) {
        dst = src.clone();
        return;
    }
    cv::Size s = src.size();
    int h = s.height, w = s.width;
    int h_2 = h + input_vals.CHAR_SIZE - (h % input_vals.CHAR_SIZE);
    int w_2 = w + input_vals.CHAR_SIZE - (w % input_vals.CHAR_SIZE);
    cv::copyMakeBorder(clone, padded, 
                       0, h_2 - h, 
                       0, w_2 - w, 
                       cv::BORDER_REPLICATE);
    dst = padded.clone();
    int step_y = int(h_2 / input_vals.CHAR_SIZE);
    int step_x = int(w_2 / input_vals.CHAR_SIZE);
    if ((step_x == 0) || (step_y == 0)) {
        return;
    }
    for (int y = 0; y < step_y; ++y) {
        for (int x = 0; x < step_x; ++x) {
            
            int startX = x * input_vals.CHAR_SIZE;
            int endX = (x+1) * input_vals.CHAR_SIZE;
            int startY = y * input_vals.CHAR_SIZE;
            int endY = (y+1) * input_vals.CHAR_SIZE;
            
            execute_chunk(startX, endX,
                          startY, endY,
                          padded,
                          dst);
        }
        //update
        gimp_progress_update((gdouble) (y) / (gdouble) (step_y));
    }
    if (pad == false) {
        dst = dst(cv::Rect(0, 0,
                           src.cols, src.rows));
    }
    return;
}

void generate_chunk(cv::Mat& src, cv::Mat& dst)
{
    cv::Mat colors = src.clone(), clone = src.clone();
    std::vector<cv::Vec3b> _unique_colors = image_get_unique_value<cv::Vec3b>(colors);
    std::reverse(_unique_colors.begin(), _unique_colors.end());
    int n_colors = _unique_colors.size();
    if (n_colors < 2) {
        dst = src.clone();
        return;
    }
    if (n_colors > int(sqrt(input_vals._K))) {
        n_colors = int(sqrt(input_vals._K));
    }
    cv::Size s = src.size();
    int h = s.height, w = s.width;
    std::vector<cv::Vec3b> unique_colors(_unique_colors.begin(), _unique_colors.begin() + n_colors);
    bool flag = false;
    std::vector<struct pix_data> dic;
    dic.clear();
    for (int i = 0; i < CHAR_MAP.length(); ++i) {
        if (flag)
            break;
        for (int bg_color = 0; bg_color < n_colors; ++bg_color) {
            if (flag)
                break;
            for (int fg_color = bg_color + 1; fg_color < n_colors; ++fg_color) {
                if (flag)
                    break;
                cv::Vec3b fg = unique_colors.at(fg_color);
                cv::Vec3b bg = unique_colors.at(bg_color);
                char c = CHAR_MAP.at(i);
                cv::String str(1, c);
                cv::Mat pix(s, CV_8UC3);
                pix = bg;
                cv::Size text_size = cv::getTextSize(str, cv::FONT_HERSHEY_PLAIN, input_vals.FONT_SCALE, 1, NULL);
                cv::Point origin = cv::Point((int(input_vals.CHAR_SIZE - 1) / 2) - int(text_size.width / 2), 
                                             (int(input_vals.CHAR_SIZE - 1) / 2) + int(text_size.height / 2));
                cv::putText(pix, str, 
                            origin, 
                            cv::FONT_HERSHEY_PLAIN, 
                            input_vals.FONT_SCALE,
                            fg, 1,
                            cv::LINE_8,
                            false);
                int dif = image_dif(src, pix);
                if (dif == 0)
                    flag = true;
                struct pix_data d = {
                    pix,
                    dif
                };
                dic.push_back(d);
            }
        }
    }
    std::sort(dic.begin(), dic.end(),
              [](struct pix_data const &a, struct pix_data const &b) {
                  return a.dif < b.dif;
              });
    dst = dic.at(0).im.clone();
    dic.clear();
    return;
}

/* https://answers.opencv.org/question/74679/kmeans-segmentation/ */
void reduce_colors(cv::Mat& src, cv::Mat& dst,
                   int k)
{
    cv::TermCriteria criteria = cv::TermCriteria(CV_TERMCRIT_ITER | CV_TERMCRIT_EPS,
                                                 10, 1.0);
    cv::Mat fImage;
    src.convertTo(fImage, CV_32F);
    fImage = fImage.reshape(3, src.cols * src.rows);
    cv::Mat labels;
    cv::Mat centers;
    cv::kmeans(fImage, k, labels, criteria, 
               10, cv::KMEANS_RANDOM_CENTERS, centers);
    cv::Mat segmented = labels.reshape(1, src.rows);
    cv::Mat im = cv::Mat::zeros(src.size(), CV_8UC3);
    for (int i = 0; i < centers.rows; i++)
    {
        cv::Mat mask = (segmented == i);
        cv::Vec3b v(centers.at< float >(i, 0), centers.at< float >(i, 1), centers.at< float >(i, 2));
        im.setTo(v,mask);    
    }
    im.convertTo(dst, CV_8UC3);
    //free mem
    im.release();
    labels.release();
    centers.release();
    fImage.release();
    return;
}
/* ---------------------------------------------------------------- */

void execute_chunk(int startX, int endX,
                   int startY, int endY,
                   cv::Mat im,
                   cv::Mat& dst) {
    cv::Mat clone = im.clone();
    image_cut(im, clone,
              startX, startY,
              endX, endY);
    generate_chunk(clone, im);
    im.copyTo(dst.colRange(startX, endX)
                 .rowRange(startY, endY));
    return;
}

void image_cut(cv::Mat& src, cv::Mat& dst,
               int _startX, int _startY,
               int _endX, int _endY)
{
    cv::Size s = src.size();
    int h = s.height, w = s.width;
    int startX, endX, startY, endY;
    dst = src.clone();
    cv::Mat clone = src.clone();    
    if (_startX < 0) {
        startX = 0;
    }
    else {
        startX = _startX;
    }
    
    if (_startY < 0) {
        startY = 0;
    }
    else {
        startY = _startY;
    }
    
    if (_endX < _startX) {
        return;
    }
    else {
        endX = _endX;
    }
    
    if (_endY < _startY) {
        return;
    }
    else {
        endY = _endY;
    }
    cv::Rect r = cv::Rect(startX, startY, endX - startX, endY - startY);
    dst = clone(r);
    return;
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
