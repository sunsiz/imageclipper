/** @file */
/* The MIT License
 *
 * Copyright (c) 2008, Naotoshi Seo <sonots(at)gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifdef _MSC_VER // MS Visual Studio
#pragma warning(disable:4996)
#pragma warning(disable:4244) // possible loss of data
#pragma warning(disable:4819) // Save the file in Unicode format to prevent data loss
#pragma comment(lib, "cv.lib")
#pragma comment(lib, "cvaux.lib")
#pragma comment(lib, "cxcore.lib")
#pragma comment(lib, "highgui.lib")
#endif

#include "cv.h"
#include "cvaux.h"
#include "cxcore.h"
#include "highgui.h"
#include <stdio.h>
#include <math.h>
#include <iostream>
#include <string>
#include <vector>
#include "filesystem.h"
#include "icformat.h"
#include "cvdrawwatershed.h"
#include "opencvx/cvrect32f.h"
#include "opencvx/cvdrawrectangle.h"
#include "opencvx/cvcropimageroi.h"
#include "opencvx/cvpointnorm.h"
using namespace std;

/************************************ Structure ******************************/

/**
* A Callback function structure
*/
typedef struct CvCallbackParam {
    const char* w_name;        /**< main window name */
    const char* miniw_name;    /**< sub window name */
    IplImage* img_src;             /**< image to be shown */
    // config
    vector<string> imtypes;    /**< image file types */
    const char* output_format; /**< output filename format */
    int inc;                   /**< incremental speed via keyboard operations */
    // rectangle region
    CvRect rect;               /**< rectangle parameter to be shown */
    int rotate;                /**< rotation angle */
    CvPoint shear;             /**< shear deformation */
    // watershed
    CvRect circle;             /**< x,y as center, width as radius */
    bool watershed;            /**< watershed flag */
    // filelist iterators
    vector<string> filelist;            /**< directory reading */
    vector<string>::iterator fileiter;  /**< iterator */
    CvCapture* cap;                     /**< video reading */
    int frame;                          /**< iterator */
    CvSize screen_size;								// Cache - screen resolution
    IplImage* img_display;							// Cache - Current image pointer
    float scale_factor;								// Cache - global scale factor
    float cap_scale_factor;							// Cache - scale factor of capture
} CvCallbackParam ;

/**
* Command Argument structure
*/
typedef struct ArgParam {
    const char* name;
    string reference;
    const char* imgout_format;
    const char* vidout_format;
    const char* output_format;
    int   frame;
} ArgParam;

/************************* Function Prototypes ******************************/
void arg_parse( int argc, char** argv, ArgParam* arg = NULL );
void usage( const ArgParam* arg );
void gui_usage();
void mouse_callback( int event, int x, int y, int flags, void* _param );
void load_reference( const ArgParam* arg, CvCallbackParam* param );
void key_callback( const ArgParam* arg, CvCallbackParam* param );

/************************* Main **********************************************/

int main( int argc, char *argv[] )
{
    // initialization
    CvCallbackParam init_param = {
        "<S> Save <F> Forward <SPACE> s and f <B> Backward <ESC> Exit",
        "Cropped",
        NULL,
        vector<string>(),
        nullptr,
        1,
        cvRect(0,0,0,0),
        0,
        cvPoint(0,0),
        cvRect(0,0,0,0),
        false,
        vector<string>(),
        vector<string>::iterator(),
        NULL,
        0,


        cvSize(0, 0),
        NULL,
        1.0f,		// global scale factor
        2.0f		// scale factor of capture
    };
    {
        init_param.imtypes.push_back( "bmp" );
        init_param.imtypes.push_back( "dib" );
        init_param.imtypes.push_back( "jpeg" );
        init_param.imtypes.push_back( "jpg" );
        init_param.imtypes.push_back( "jpe" );
        init_param.imtypes.push_back( "png" );
        init_param.imtypes.push_back( "pbm" );
        init_param.imtypes.push_back( "pbm" );
        init_param.imtypes.push_back( "ppm" );
        init_param.imtypes.push_back( "sr" );
        init_param.imtypes.push_back( "ras" );
        init_param.imtypes.push_back( "tiff" );
        init_param.imtypes.push_back( "exr" );
        init_param.imtypes.push_back( "jp2" );
    }
    CvCallbackParam* param = &init_param;

    ArgParam init_arg = {
        argv[0],
        ".",
        "%d/image_clipper/%i.%e_%04r_%04x_%04y_%04w_%04h.png",
        "%d/image_clipper/%i.%e_%04f_%04r_%04x_%04y_%04w_%04h.png",
        NULL,
        1
    };
    ArgParam *arg = &init_arg;

    // parse arguments
    arg_parse( argc, argv, arg );
    gui_usage();
    load_reference( arg, param );

    // Mouse and Key callback
    cvNamedWindow( param->w_name, CV_WINDOW_AUTOSIZE );
    cvNamedWindow( param->miniw_name, CV_WINDOW_AUTOSIZE );
    cvSetMouseCallback( param->w_name, mouse_callback, param );
    key_callback( arg, param );
    cvDestroyWindow( param->w_name );
    cvDestroyWindow( param->miniw_name );
}

/**
 * Read a directory or video
 */
void load_reference( const ArgParam* arg, CvCallbackParam* param )
{
    bool is_directory   = fs::is_directory( arg->reference );
    bool is_image = fs::match_extensions( arg->reference, param->imtypes );
    bool is_video = !is_directory & !is_image;
    param->output_format = ( arg->output_format != NULL ? arg->output_format :
                                                          ( is_video ? arg->vidout_format : arg->imgout_format ) );
    param->frame = arg->frame;

    if( is_directory || is_image )
    {
        cerr << "Now reading a directory..... ";
        if( is_directory )
        {
            param->filelist = fs::filelist( arg->reference, param->imtypes, "file" );
            if( param->filelist.empty() )
            {
                cerr << "No image file exist under a directory " << fs::realpath( arg->reference ) << endl << endl;
                usage( arg );
                exit(1);
            }
            sort(param->filelist.begin(),param->filelist.end());
            param->fileiter = param->filelist.begin();
        }
        else
        {
            if( !fs::exists( arg->reference ) )
            {
                cerr << "The image file " << fs::realpath( arg->reference ) << " does not exist." << endl << endl;
                usage( arg );
                exit(1);
            }
            param->filelist = fs::filelist( fs::dirname( arg->reference ), param->imtypes, "file" );
            // step up till specified file
            for( param->fileiter = param->filelist.begin(); param->fileiter != param->filelist.end(); param->fileiter++ )
            {
                if( fs::realpath( *param->fileiter ) == fs::realpath( arg->reference ) ) break;
            }
        }
        cerr << "Done!" << endl;
        cerr << "Now showing " << fs::realpath( *param->fileiter ) << endl;
        param->img_src = cvLoadImage( fs::realpath( *param->fileiter ).c_str() );
    }
    else if( is_video )
    {
        if ( !fs::exists( arg->reference ) )
        {
            cerr << "The file " << fs::realpath( arg->reference ) << " does not exist or is not readable." << endl << endl;
            usage( arg );
            exit(1);
        }
        cerr << "Now reading a video..... ";
        param->cap = cvCaptureFromFile( fs::realpath( arg->reference ).c_str() );
        cvSetCaptureProperty( param->cap, CV_CAP_PROP_POS_FRAMES, arg->frame - 1 );
        param->img_src = cvQueryFrame( param->cap );
        if( param->img_src == NULL )
        {
            cerr << "The file " << fs::realpath( arg->reference ) << " was assumed as a video, but not loadable." << endl << endl;
            usage( arg );
            exit(1);
        }
        cerr << "Done!" << endl;
        cerr << cvGetCaptureProperty( param->cap, CV_CAP_PROP_FRAME_COUNT ) << " frames totally." << endl;
        cerr << "Now showing " << fs::realpath( arg->reference ) << " " << arg->frame << endl;
#if (defined(WIN32) || defined(WIN64)) && (CV_MAJOR_VERSION < 1 || (CV_MAJOR_VERSION == 1 && CV_MINOR_VERSION < 1))
        param->img->origin = 0;
        cvFlip( param->img );
#endif
    }
    else
    {
        cerr << "The directory " << fs::realpath( arg->reference ) << " does not exist." << endl << endl;
        usage( arg );
        exit(1);
    }

    // get screen resolution
#ifdef WIN32
    param->screen_size.width = GetSystemMetrics(SM_CXSCREEN);
    param->screen_size.height = GetSystemMetrics(SM_CYSCREEN);
#else
    param->screen_size.width = 1366;
    param->screen_size.height = 768;
#endif // WIN32

    // resize image to fit screen resolution
    {
        CvSize _size = cvSize(param->img_src->width, param->img_src->height);
        while (_size.width > param->screen_size.width || _size.height > param->screen_size.height) {
            _size.width /= 2;
            _size.height /= 2;
            param->scale_factor /= 2;
        }
        param->img_display = cvCreateImage(_size, param->img_src->depth, param->img_src->nChannels);
        cvResize(param->img_src, param->img_display);
    }
}

/**
 * Keyboard operations
 */
void key_callback( const ArgParam* arg, CvCallbackParam* param )
{
    string filename = param->cap == NULL ? *param->fileiter : arg->reference;

    param->scale_factor=1.0f;
    CvSize _size = cvSize(param->img_src->width, param->img_src->height);
    if (_size.width > param->screen_size.width || _size.height > param->screen_size.height) {
        _size.width /= 2;
        _size.height /= 2;
        param->scale_factor /= 2;
    }else{
        param->scale_factor=1.0f;
    }
    cout<<"Scale factorchanged to "<<param->scale_factor<<endl;
    cvReleaseImage(&param->img_display);
    param->img_display = cvCreateImage(
                cvSize(param->img_src->width * param->scale_factor, param->img_src->height * param->scale_factor),
                param->img_src->depth, param->img_src->nChannels);
    cvResize(param->img_src, param->img_display);
    cvShowImageAndRectangle(param->w_name, param->img_display, cvRect32fFromRect(param->rect, param->rotate), cvPointTo32f(param->shear));

    if(param->scale_factor!=1.0f){
        cvShowCroppedImage( param->miniw_name, param->img_src,
                            cvRect32f(param->rect.x*(1/param->scale_factor),param->rect.y*(1/param->scale_factor),param->rect.width*(1/param->scale_factor),param->rect.height*(1/param->scale_factor), param->rotate ),
                            cvPointTo32f( param->shear ) );
        cout<<"Scale factor is "<<param->scale_factor<<", Scaled crop image size is "<<param->rect.x*(1/param->scale_factor)<<", "<<param->rect.y*(1/param->scale_factor)<<", "<<param->rect.width*(1/param->scale_factor)<<", "<<param->rect.height*(1/param->scale_factor)<<endl;
    }else{
        cvShowCroppedImage( param->miniw_name, param->img_src,
                            cvRect32fFromRect( param->rect, param->rotate ),
                            cvPointTo32f( param->shear ) );
        cout<<"Scale factor is "<<param->scale_factor<<", Unscaled crop image size is "<<param->rect.x<<", "<<param->rect.y<<", "<<param->rect.width<<", "<<param->rect.height<<endl;
    }
    cvShowImageAndRectangle( param->w_name, param->img_display,
                             cvRect32fFromRect( param->rect, param->rotate ),
                             cvPointTo32f( param->shear ) );

    //    cvShowCroppedImage( param->miniw_name, param->img_src,
    //        cvRect32fFromRect( param->rect, param->rotate ),
    //        cvPointTo32f( param->shear ) );
    //    cvShowImageAndRectangle( param->w_name, param->img_display,
    //        cvRect32fFromRect( param->rect, param->rotate ),
    //        cvPointTo32f( param->shear ) );

    while( true ) // key callback
    {
        char key = cvWaitKey( 0 );
        cout << "Key pressed: " << (int)key << endl;

        if (key == '+') {
            param->scale_factor *= 1.05;

            cout<<"Scale factorchanged to "<<param->scale_factor<<endl;
            cvReleaseImage(&param->img_display);
            param->img_display = cvCreateImage(
                        cvSize(param->img_src->width * param->scale_factor, param->img_src->height * param->scale_factor),
                        param->img_src->depth, param->img_src->nChannels);
            cvResize(param->img_src, param->img_display);
            cvShowImageAndRectangle(param->w_name, param->img_display, cvRect32fFromRect(param->rect, param->rotate), cvPointTo32f(param->shear));
        }

        if (key == '-') {
            param->scale_factor *= 0.95;

            cout<<"Scale factorchanged to "<<param->scale_factor<<endl;
            cvReleaseImage(&param->img_display);
            param->img_display = cvCreateImage(
                        cvSize(param->img_src->width * param->scale_factor, param->img_src->height * param->scale_factor),
                        param->img_src->depth, param->img_src->nChannels);
            cvResize(param->img_src, param->img_display);
            cvShowImageAndRectangle(param->w_name, param->img_display, cvRect32fFromRect(param->rect, param->rotate), cvPointTo32f(param->shear));
        }


        // 32 is SPACE
        if( key == 's' || key == 32 ) // Save
        {
            if( param->rect.width > 0 && param->rect.height > 0 )
            {
                string output_path;
                if(param->scale_factor!=1.0f){
                    output_path= icFormat(
                                param->output_format, fs::dirname( filename ),
                                fs::filename( filename ), fs::extension( filename ),
                                param->rect.x*(1/param->scale_factor), param->rect.y*(1/param->scale_factor), param->rect.width*(1/param->scale_factor), param->rect.height*(1/param->scale_factor),
                                param->frame, param->rotate );
                    cout<<"Scale factor is "<<param->scale_factor<<", Scaled Rect is "<<param->rect.x*(1/param->scale_factor)<<", "<<param->rect.y*(1/param->scale_factor)<<", "<<param->rect.width*(1/param->scale_factor)<<", "<<param->rect.height*(1/param->scale_factor)<<endl;
                }else{
                    output_path = icFormat(
                                param->output_format, fs::dirname( filename ),
                                fs::filename( filename ), fs::extension( filename ),
                                param->rect.x, param->rect.y, param->rect.width, param->rect.height,
                                param->frame, param->rotate );
                    cout<<"Scale factor is "<<param->scale_factor<<", Unscaled Rect is "<<param->rect.x<<", "<<param->rect.y<<", "<<param->rect.width<<", "<<param->rect.height<<endl;
                }
                if( !fs::match_extensions( output_path, param->imtypes ) )
                {
                    cerr << "The image type " << fs::extension( output_path ) << " is not supported." << endl;
                    exit(1);
                }
                fs::create_directories( fs::dirname( output_path ) );

                IplImage* crop;
                if(param->scale_factor!=1.0f){
                    crop = cvCreateImage(
                                cvSize( param->rect.width*(1/param->scale_factor), param->rect.height*(1/param->scale_factor) ),
                                param->img_src->depth, param->img_src->nChannels );
                    cvCropImageROI( param->img_src, crop,
                                    cvRect32f( param->rect.x*(1/param->scale_factor), param->rect.y*(1/param->scale_factor), param->rect.width*(1/param->scale_factor), param->rect.height*(1/param->scale_factor), param->rotate ),
                                    cvPointTo32f( param->shear ) );
                    cout<<"Scale factor is "<<param->scale_factor<<", Scaled crop image size is "<<param->rect.x*(1/param->scale_factor)<<", "<<param->rect.y*(1/param->scale_factor)<<", "<<param->rect.width*(1/param->scale_factor)<<", "<<param->rect.height*(1/param->scale_factor)<<endl;
                }else{
                    crop = cvCreateImage(
                                cvSize( param->rect.width, param->rect.height ),
                                param->img_src->depth, param->img_src->nChannels );
                    cvCropImageROI( param->img_src, crop,
                                    cvRect32fFromRect( param->rect, param->rotate ),
                                    cvPointTo32f( param->shear ) );
                    cout<<"Scale factor is "<<param->scale_factor<<", Unscaled crop image size is "<<param->rect.x<<", "<<param->rect.y<<", "<<param->rect.width<<", "<<param->rect.height<<endl;
                }
                cvSaveImage( fs::realpath( output_path ).c_str(), crop );
                cout << fs::realpath( output_path ) << endl;
                cvReleaseImage( &crop );
            }
        }
        // Forward
        if( key == 'f' || key == 32 ) // 32 is SPACE
        {
            if( param->cap )
            {
                IplImage* tmpimg = cvQueryFrame( param->cap );
                if( tmpimg != NULL )
                    //if( frame < cvGetCaptureProperty( param->cap, CV_CAP_PROP_FRAME_COUNT ) )
                {
                    param->img_src = tmpimg;
                    {
                        CvSize _size = cvSize(param->img_src->width, param->img_src->height);
                        while (_size.width > param->screen_size.width || _size.height > param->screen_size.height) {
                            _size.width /= 2;
                            _size.height /= 2;
                            param->scale_factor /= 2;
                        }
                        param->img_display = cvCreateImage(_size, param->img_src->depth, param->img_src->nChannels);
                        cvResize(param->img_src, param->img_display);
                    }
#if (defined(WIN32) || defined(WIN64)) && (CV_MAJOR_VERSION < 1 || (CV_MAJOR_VERSION == 1 && CV_MINOR_VERSION < 1))
                    param->img_src->origin = 0;
                    cvFlip( param->img_src );
#endif
                    param->frame++;
                    cout << "Now showing " << fs::realpath( filename ) << " " <<  param->frame << endl;
                }
            }
            else
            {
                if( param->fileiter + 1 != param->filelist.end() )
                {
                    cvReleaseImage( &param->img_src );
                    param->fileiter++;
                    filename = *param->fileiter;
                    param->img_src = cvLoadImage( fs::realpath( filename ).c_str() );
                    {
                        param->scale_factor=1.0f;
                        CvSize _size = cvSize(param->img_src->width, param->img_src->height);
                        if (_size.width > param->screen_size.width || _size.height > param->screen_size.height) {
                            _size.width /= 2;
                            _size.height /= 2;
                            param->scale_factor /= 2;
                        }else{
                            param->scale_factor=1.0f;
                        }
                        param->img_display = cvCreateImage(_size, param->img_src->depth, param->img_src->nChannels);
                        cvResize(param->img_src, param->img_display);
                    }
                    cout << "Now showing " << fs::realpath( filename ) << endl;
                }
            }
        }
        // Backward
        else if( key == 'b' )
        {
            if( param->cap )
            {
                IplImage* tmpimg;
                param->frame = max( 1, param->frame - 1 );
                cvSetCaptureProperty( param->cap, CV_CAP_PROP_POS_FRAMES, param->frame - 1 );
                if( tmpimg = cvQueryFrame( param->cap ) )
                {
                    param->img_src = tmpimg;
                    {
                        CvSize _size = cvSize(param->img_src->width, param->img_src->height);
                        if (_size.width > param->screen_size.width || _size.height > param->screen_size.height) {
                            _size.width /= 2;
                            _size.height /= 2;
                            param->scale_factor /= 2;
                        }else{
                            param->scale_factor=1.0f;
                        }
                        param->img_display = cvCreateImage(_size, param->img_src->depth, param->img_src->nChannels);
                        cvResize(param->img_src, param->img_display);
                    }
#if (defined(WIN32) || defined(WIN64)) && (CV_MAJOR_VERSION < 1 || (CV_MAJOR_VERSION == 1 && CV_MINOR_VERSION < 1))
                    param->img_src->origin = 0;
                    cvFlip( param->img_src );
#endif
                    cout << "Now showing " << fs::realpath( filename ) << " " <<  param->frame << endl;
                }
            }
            else
            {
                if( param->fileiter != param->filelist.begin() )
                {
                    cvReleaseImage( &param->img_src );
                    param->fileiter--;
                    filename = *param->fileiter;
                    param->img_src = cvLoadImage( fs::realpath( filename ).c_str() );
                    {
                        param->scale_factor=1.0f;
                        CvSize _size = cvSize(param->img_src->width, param->img_src->height);
                        while (_size.width > param->screen_size.width || _size.height > param->screen_size.height) {
                            _size.width /= 2;
                            _size.height /= 2;
                            param->scale_factor /= 2;
                        }
                        param->img_display = cvCreateImage(_size, param->img_src->depth, param->img_src->nChannels);
                        cvResize(param->img_src, param->img_display);
                    }
                    cout << "Now showing " << fs::realpath( filename ) << endl;
                }
            }
        }
        // Exit
        else if( key == 'q' || key == 27 ) // 27 is ESC
        {
            break;
        }
        else if( key == '+' )
        {
            param->inc += 1;
            cout << "Inc: " << param->inc << endl;
        }
        else if( key == '-' )
        {
            param->inc = max( 1, param->inc - 1 );
            cout << "Inc: " << param->inc << endl;
        }

        if( param->watershed ) // watershed
        {
            // Rectangle Movement (Vi like hotkeys)
            if( key == 'h' ) // Left
            {
                param->circle.x -= param->inc;
            }
            else if( key == 'j' ) // Down
            {
                param->circle.y += param->inc;
            }
            else if( key == 'k' ) // Up
            {
                param->circle.y -= param->inc;
            }
            else if( key == 'l' ) // Right
            {
                param->circle.x += param->inc;
            }
            // Rectangle Resize
            else if( key == 'y' ) // Shrink width
            {
                param->circle.width -= param->inc;
            }
            else if( key == 'u' ) // Expand height
            {
                param->circle.width += param->inc;
            }
            else if( key == 'i' ) // Shrink height
            {
                param->circle.width -= param->inc;
            }
            else if( key == 'o' ) // Expand width
            {
                param->circle.width += param->inc;
            }
            // Shear Deformation
            else if( key == 'n' ) // Left
            {
                param->shear.x -= param->inc;
            }
            else if( key == 'm' ) // Down
            {
                param->shear.y += param->inc;
            }
            else if( key == ',' ) // Up
            {
                param->shear.y -= param->inc;
            }
            else if( key == '.' ) // Right
            {
                param->shear.x += param->inc;
            }
            // Rotation
            else if( key == 'r' ) // Counter-Clockwise
            {
                param->rotate += param->inc;
                param->rotate = (param->rotate >= 360) ? param->rotate - 360 : param->rotate;
            }
            else if( key == 'R' ) // Clockwise
            {
                param->rotate -= param->inc;
                param->rotate = (param->rotate < 0) ? 360 + param->rotate : param->rotate;
            }
            else if( key == 'e' ) // Expand
            {
                param->circle.width += param->inc;
            }
            else if( key == 'E' ) // Shrink
            {
                param->circle.width -= param->inc;
            }

            if( param->img_src )
            {
                param->rect = cvShowImageAndWatershed( param->w_name, param->img_display, param->circle );
                cvShowCroppedImage( param->miniw_name, param->img_src,
                                    cvRect32fFromRect( param->rect, param->rotate ),
                                    cvPointTo32f( param->shear ) );
            }
        }
        else
        {
            // Rectangle Movement (Vi like hotkeys)
            if( key == 'h' ) // Left
            {
                param->rect.x -= param->inc;
            }
            else if( key == 'j' ) // Down
            {
                param->rect.y += param->inc;
            }
            else if( key == 'k' ) // Up
            {
                param->rect.y -= param->inc;
            }
            else if( key == 'l' ) // Right
            {
                param->rect.x += param->inc;
            }
            // Rectangle Resize
            else if( key == 'y' ) // Shrink width
            {
                param->rect.width = max( 0, param->rect.width - param->inc );
            }
            else if( key == 'u' ) // Expand height
            {
                param->rect.height += param->inc;
            }
            else if( key == 'i' ) // Shrink height
            {
                param->rect.height = max( 0, param->rect.height - param->inc );
            }
            else if( key == 'o' ) // Expand width
            {
                param->rect.width += param->inc;
            }
            // Shear Deformation
            else if( key == 'n' ) // Left
            {
                param->shear.x -= param->inc;
            }
            else if( key == 'm' ) // Down
            {
                param->shear.y += param->inc;
            }
            else if( key == ',' ) // Up
            {
                param->shear.y -= param->inc;
            }
            else if( key == '.' ) // Right
            {
                param->shear.x += param->inc;
            }
            // Rotation
            else if( key == 'r' ) // Counter-Clockwise
            {
                param->rotate += param->inc;
                param->rotate = (param->rotate >= 360) ? param->rotate - 360 : param->rotate;
            }
            else if( key == 'R' ) // Clockwise
            {
                param->rotate -= param->inc;
                param->rotate = (param->rotate < 0) ? 360 + param->rotate : param->rotate;
            }
            else if( key == 'e' ) // Expand
            {
                param->rect.x = max( 0, param->rect.x - param->inc );
                param->rect.width += 2 * param->inc;
                param->rect.y = max( 0, param->rect.y - param->inc );
                param->rect.height += 2 * param->inc;
            }
            else if( key == 'E' ) // Shrink
            {
                param->rect.x = min( param->img_src->width, param->rect.x + param->inc );
                param->rect.width = max( 0, param->rect.width - 2 * param->inc );
                param->rect.y = min( param->img_src->height, param->rect.y + param->inc );
                param->rect.height = max( 0, param->rect.height - 2 * param->inc );
            }
            /*
              if( key == 'e' || key == 'E' ) // Expansion and Shrink so that ratio does not change
              {
              if( param->rect.height != 0 && param->rect.width != 0 )
              {
              int gcd, a = param->rect.width, b = param->rect.height;
              while( 1 )
              {
              a = a % b;
              if( a == 0 ) { gcd = b; break; }
              b = b % a;
              if( b == 0 ) { gcd = a; break; }
              }
              int ratio_width = param->rect.width / gcd;
              int ratio_height = param->rect.height / gcd;
              if( key == 'e' ) gcd += param->inc;
              else if( key == 'E' ) gcd -= param->inc;
              if( gcd > 0 )
              {
              cout << ratio_width << ":" << ratio_height << " * " << gcd << endl;
              param->rect.width = ratio_width * gcd;
              param->rect.height = ratio_height * gcd;
              cvShowImageAndRectangle( param->w_name, param->img,
              cvRect32fFromRect( param->rect, param->rotate ),
              cvPointTo32f( param->shear ) );
              }
              }
              }*/

            if( param->img_src )
            {
                cvShowImageAndRectangle( param->w_name, param->img_display,
                                         cvRect32fFromRect( param->rect, param->rotate ),
                                         cvPointTo32f( param->shear ) );
                cvShowCroppedImage( param->miniw_name, param->img_src,
                                    cvRect32fFromRect( param->rect, param->rotate ),
                                    cvPointTo32f( param->shear ) );
            }
        }
    }
}

/**
* cvSetMouseCallback function
*/
void mouse_callback( int event, int x, int y, int flags, void* _param )
{
    CvCallbackParam* param = (CvCallbackParam*)_param;
    static CvPoint point0          = cvPoint( 0, 0 );
    static bool move_rect          = false;
    static bool resize_rect_left   = false;
    static bool resize_rect_right  = false;
    static bool resize_rect_top    = false;
    static bool resize_rect_bottom = false;
    static bool move_watershed     = false;
    static bool resize_watershed   = false;

    if( !param->img_src || !param->img_display )
        return;

    if( x >= 32768 ) x -= 65536; // change left outsite to negative
    if( y >= 32768 ) y -= 65536; // change top outside to negative

    // MBUTTON or LBUTTON + SHIFT is to draw wathershed
    if( event == CV_EVENT_MBUTTONDOWN ||
            ( event == CV_EVENT_LBUTTONDOWN && flags & CV_EVENT_FLAG_SHIFTKEY ) ) // initialization
    {
        param->circle.x = x;
        param->circle.y = y;
    }
    else if( event == CV_EVENT_MOUSEMOVE && flags & CV_EVENT_FLAG_MBUTTON ||
             ( event == CV_EVENT_MOUSEMOVE && flags & CV_EVENT_FLAG_LBUTTON && flags & CV_EVENT_FLAG_SHIFTKEY ) )
    {
        param->watershed = true;
        param->rotate  = 0;
        param->shear.x = param->shear.y = 0;

        param->circle.width = (int) cvPointNorm( cvPoint( param->circle.x, param->circle.y ), cvPoint( x, y ) );
        param->rect = cvShowImageAndWatershed( param->w_name, param->img_display, param->circle );
        cvShowCroppedImage( param->miniw_name, param->img_src,
                            cvRect32fFromRect( param->rect, param->rotate ),
                            cvPointTo32f( param->shear ) );
    }

    // LBUTTON is to draw rectangle
    else if( event == CV_EVENT_LBUTTONDOWN ) // initialization
    {
        point0 = cvPoint( x, y );
    }
    else if( event == CV_EVENT_MOUSEMOVE && flags & CV_EVENT_FLAG_LBUTTON )
    {
        param->watershed = false; // disable watershed
        param->rotate       = 0;
        param->shear.x      = param->shear.y = 0;

        param->rect.x = min( point0.x, x );
        param->rect.y = min( point0.y, y );
        param->rect.width =  abs( point0.x - x );
        param->rect.height = abs( point0.y - y );

        cvShowImageAndRectangle( param->w_name, param->img_display,
                                         cvRect32fFromRect( param->rect, param->rotate ),
                                         cvPointTo32f( param->shear ) );
        cvShowCroppedImage( param->miniw_name, param->img_src,
                            cvRect32f( param->rect.x*(1/param->scale_factor), param->rect.y*(1/param->scale_factor), param->rect.width*(1/param->scale_factor), param->rect.height*(1/param->scale_factor), param->rotate ),
                            cvPointTo32f( param->shear ) );
//        cvShowImageAndRectangle( param->w_name, param->img_display,
//                                 cvRect32fFromRect( param->rect, param->rotate ),
//                                 cvPointTo32f( param->shear ) );
//        cvShowCroppedImage( param->miniw_name, param->img_src,
//                            cvRect32fFromRect( param->rect, param->rotate ),
//                            cvPointTo32f( param->shear ) );
    }

    // RBUTTON to move rentangle or watershed marker
    else if( event == CV_EVENT_RBUTTONDOWN )
    {
        point0 = cvPoint( x, y );

        if( param->watershed )
        {
            CvPoint center = cvPoint( param->circle.x, param->circle.y );
            int radius = (int) cvPointNorm( center, point0 );
            if( param->circle.width - 1 <= radius && radius <= param->circle.width )
            {
                resize_watershed = true;
            }
            else if( radius <= param->circle.width )
            {
                move_watershed = true;
            }
        }
        if( !resize_watershed && !move_watershed )
        {
            param->watershed = false;

            if( ( param->rect.x < x && x < param->rect.x + param->rect.width ) &&
                    ( param->rect.y < y && y < param->rect.y + param->rect.height ) )
            {
                move_rect = true;
            }
            if( x <= param->rect.x )
            {
                resize_rect_left = true;
            }
            else if( x >= param->rect.x + param->rect.width )
            {
                resize_rect_right = true;
            }
            if( y <= param->rect.y )
            {
                resize_rect_top = true;
            }
            else if( y >= param->rect.y + param->rect.height )
            {
                resize_rect_bottom = true;
            }
        }
    }
    else if( event == CV_EVENT_MOUSEMOVE && flags & CV_EVENT_FLAG_RBUTTON && param->watershed ) // Move or resize for watershed
    {
        if( move_watershed )
        {
            CvPoint move = cvPoint( x - point0.x, y - point0.y );
            param->circle.x += move.x;
            param->circle.y += move.y;

            param->rect = cvShowImageAndWatershed( param->w_name, param->img_display, param->circle );
            cvShowCroppedImage( param->miniw_name, param->img_src,
                                cvRect32fFromRect( param->rect, param->rotate ),
                                cvPointTo32f( param->shear ) );

            point0 = cvPoint( x, y );
        }
        else if( resize_watershed )
        {
            param->circle.width = (int) cvPointNorm( cvPoint( param->circle.x, param->circle.y ), cvPoint( x, y ) );
            param->rect = cvShowImageAndWatershed( param->w_name, param->img_display, param->circle );
            cvShowCroppedImage( param->miniw_name, param->img_src,
                                cvRect32fFromRect( param->rect, param->rotate ),
                                cvPointTo32f( param->shear ) );
        }
    }
    else if( event == CV_EVENT_MOUSEMOVE && flags & CV_EVENT_FLAG_RBUTTON ) // Move or resize for rectangle
    {
        if( move_rect )
        {
            CvPoint move = cvPoint( x - point0.x, y - point0.y );
            param->rect.x += move.x;
            param->rect.y += move.y;
        }
        if( resize_rect_left )
        {
            int move_x = x - point0.x;
            param->rect.x += move_x;
            param->rect.width -= move_x;
        }
        else if( resize_rect_right )
        {
            int move_x = x - point0.x;
            param->rect.width += move_x;
        }
        if( resize_rect_top )
        {
            int move_y = y - point0.y;
            param->rect.y += move_y;
            param->rect.height -= move_y;
        }
        else if( resize_rect_bottom )
        {
            int move_y = y - point0.y;
            param->rect.height += move_y;
        }

        // assure width is positive
        if( param->rect.width <= 0 )
        {
            param->rect.x += param->rect.width;
            param->rect.width *= -1;
            bool tmp = resize_rect_right;
            resize_rect_right = resize_rect_left;
            resize_rect_left  = tmp;
        }
        // assure height is positive
        if( param->rect.height <= 0 )
        {
            param->rect.y += param->rect.height;
            param->rect.height *= -1;
            bool tmp = resize_rect_top;
            resize_rect_top    = resize_rect_bottom;
            resize_rect_bottom = tmp;
        }

        cvShowImageAndRectangle( param->w_name, param->img_display,
                                 cvRect32fFromRect( param->rect, param->rotate ),
                                 cvPointTo32f( param->shear ) );
        cvShowCroppedImage( param->miniw_name, param->img_src,
                            cvRect32fFromRect( param->rect, param->rotate ),
                            cvPointTo32f( param->shear ) );
        point0 = cvPoint( x, y );
    }

    // common finalization
    else if( event == CV_EVENT_LBUTTONUP || event == CV_EVENT_MBUTTONUP || event == CV_EVENT_RBUTTONUP )
    {
        move_rect          = false;
        resize_rect_left   = false;
        resize_rect_right  = false;
        resize_rect_top    = false;
        resize_rect_bottom = false;
        move_watershed     = false;
        resize_watershed   = false;
    }
}

/**
 * Arguments Processing
 */
void arg_parse( int argc, char** argv, ArgParam *arg )
{
    arg->name = argv[0];
    for( int i = 1; i < argc; i++ )
    {
        if( !strcmp( argv[i], "-h" ) || !strcmp( argv[i], "--help" ) )
        {
            usage( arg );
            return;
        }
        else if( !strcmp( argv[i], "-o" ) || !strcmp( argv[i], "--output_format" ) )
        {
            arg->output_format = argv[++i];
        }
        else if( !strcmp( argv[i], "-i" ) || !strcmp( argv[i], "--imgout_format" ) )
        {
            arg->imgout_format = argv[++i];
        }
        else if( !strcmp( argv[i], "-v" ) || !strcmp( argv[i], "--vidout_format" ) )
        {
            arg->vidout_format = argv[++i];
        }
        else if( !strcmp( argv[i], "-f" ) || !strcmp( argv[i], "--frame" ) )
        {
            arg->frame = atoi( argv[++i] );
        }
        else
        {
            arg->reference = string( argv[i] );
        }
    }
}

/**
* Print out usage
*/
void usage( const ArgParam* arg )
{
    cout << "ImageClipper - image clipping helper tool." << endl;
    cout << "Command Usage: " << fs::basename( arg->name );
    cout << " [option]... [arg_reference]" << endl;
    cout << "  <arg_reference = " << arg->reference << ">" << endl;
    cout << "    <arg_reference> would be a directory or an image or a video filename." << endl;
    cout << "    For a directory, image files in the directory will be read sequentially." << endl;
    cout << "    For an image, it starts to read a directory from the specified image file. " << endl;
    cout << "    (A file is judged as an image based on its filename extension.)" << endl;
    cout << "    A file except images is tried to be read as a video and read frame by frame. " << endl;
    cout << endl;
    cout << "  Options" << endl;
    cout << "    -o <output_format = imgout_format or vidout_format>" << endl;
    cout << "        Determine the output file path format." << endl;
    cout << "        This is a syntax sugar for -i and -v. " << endl;
    cout << "        Format Expression)" << endl;
    cout << "            %d - dirname of the original" << endl;
    cout << "            %i - filename of the original without extension" << endl;
    cout << "            %e - filename extension of the original" << endl;
    cout << "            %x - upper-left x coord" << endl;
    cout << "            %y - upper-left y coord" << endl;
    cout << "            %w - width" << endl;
    cout << "            %h - height" << endl;
    cout << "            %r - rotation degree" << endl;
    cout << "            %. - shear deformation in x coord" << endl;
    cout << "            %, - shear deformation in y coord" << endl;
    cout << "            %f - frame number (for video)" << endl;
    cout << "        Example) ./$i_%04x_%04y_%04w_%04h.%e" << endl;
    cout << "            Store into software directory and use image type of the original." << endl;
    cout << "    -i <imgout_format = " << arg->imgout_format << ">" << endl;
    cout << "        Determine the output file path format for image inputs." << endl;
    cout << "    -v <vidout_format = " << arg->vidout_format << ">" << endl;
    cout << "        Determine the output file path format for a video input." << endl;
    cout << "    -f" << endl;
    cout << "    --frame <frame = 1> (video)" << endl;
    cout << "        Determine the frame number of video to start to read." << endl;
    cout << "    -h" << endl;
    cout << "    --help" << endl;
    cout << "        Show this help" << endl;
    cout << endl;
    cout << "  Supported Image Types" << endl;
    cout << "      bmp|dib|jpeg|jpg|jpe|png|pbm|pgm|ppm|sr|ras|tiff|exr|jp2" << endl;
}

/**
* Print Application Usage
*/
void gui_usage()
{
    cout << "Application Usage:" << endl;
    cout << "  Mouse Usage:" << endl;
    cout << "    Left  (select)          : Select or initialize a rectangle region." << endl;
    cout << "    Right (move or resize)  : Move by dragging inside the rectangle." << endl;
    cout << "                              Resize by draggin outside the rectangle." << endl;
    cout << "    Middle or SHIFT + Left  : Initialize the watershed marker. Drag it. " << endl;
    cout << "  Keyboard Usage:" << endl;
    cout << "    s (save)                : Save the selected region as an image." << endl;
    cout << "    f (forward)             : Forward. Show next image." << endl;
    cout << "    SPACE                   : Save and Forward." << endl;
    cout << "    b (backward)            : Backward. " << endl;
    cout << "    q (quit) or ESC         : Quit. " << endl;
    cout << "    r (rotate) R (opposite) : Rotate rectangle in counter-clockwise." << endl;
    cout << "    e (expand) E (shrink)   : Expand the recntagle size." << endl;
    cout << "    + (incl)   - (decl)     : Increment the step size to increment." << endl;
    cout << "    h (left) j (down) k (up) l (right) : Move rectangle. (vi-like keybinds)" << endl;
    cout << "    y (left) u (down) i (up) o (right) : Resize rectangle. (Move boundaries)" << endl;
    cout << "    n (left) m (down) , (up) . (right) : Shear deformation." << endl;
}

