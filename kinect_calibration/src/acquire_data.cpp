/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2010 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */


#include <stdio.h>
#include <string.h>
#include <unistd.h> // getopt
#include <cstdlib>
#include "libfreenect.h"

#include <pthread.h>

#if defined(__APPLE__)
#include <GLUT/glut.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/glut.h>
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#include <math.h>

#include <opencv2/core/core.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

using namespace std;

// image size
#define ROWS 480
#define COLS 640

// checkerboard pattern
cv::Size pattern_size;

// saving images
char *fdir = NULL;
int ir_num = 0;                 // individual frames
int rgb_num = 0;
int depth_num = 0;
int s_num = 0;                  // number of frames saved
bool saveIR = false;            // flags to trigger saving
bool saveRGB = false;
bool saveDepth = false;

pthread_t freenect_thread;
volatile int die = 0;

int window;
int ir_mode = 0;

pthread_mutex_t gl_backbuf_mutex = PTHREAD_MUTEX_INITIALIZER;

uint8_t gl_depth_front[640 * 480 * 4];
uint8_t gl_depth_back[640 * 480 * 4];

uint8_t gl_rgb_front[640 * 480 * 4];
uint8_t gl_rgb_back[640 * 480 * 4];

GLuint gl_depth_tex;
GLuint gl_rgb_tex;

freenect_context *f_ctx;
freenect_device *f_dev;
int freenect_angle = 0;
int freenect_angle_last = 0;
int freenect_led;


pthread_cond_t gl_frame_cond = PTHREAD_COND_INITIALIZER;
int got_frames = 0;

void
DrawGLScene ()
{
  pthread_mutex_lock (&gl_backbuf_mutex);

  while (got_frames < 2)
  {
    pthread_cond_wait (&gl_frame_cond, &gl_backbuf_mutex);
  }

  memcpy (gl_depth_front, gl_depth_back, sizeof (gl_depth_back));
  memcpy (gl_rgb_front, gl_rgb_back, sizeof (gl_rgb_back));
  got_frames = 0;
  pthread_mutex_unlock (&gl_backbuf_mutex);

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glLoadIdentity ();

  glEnable (GL_TEXTURE_2D);

  glBindTexture (GL_TEXTURE_2D, gl_depth_tex);
  glTexImage2D (GL_TEXTURE_2D, 0, 3, 640, 480, 0, GL_RGB, GL_UNSIGNED_BYTE, gl_depth_front);

  glBegin (GL_TRIANGLE_FAN);
  glColor4f (255.0f, 255.0f, 255.0f, 255.0f);
  glTexCoord2f (0, 0);
  glVertex3f (0, 0, 0);
  glTexCoord2f (1, 0);
  glVertex3f (640, 0, 0);
  glTexCoord2f (1, 1);
  glVertex3f (640, 480, 0);
  glTexCoord2f (0, 1);
  glVertex3f (0, 480, 0);
  glEnd ();

  glBindTexture (GL_TEXTURE_2D, gl_rgb_tex);
  glTexImage2D (GL_TEXTURE_2D, 0, 3, 640, 480, 0, GL_RGB, GL_UNSIGNED_BYTE, gl_rgb_front);

  glBegin (GL_TRIANGLE_FAN);
  glColor4f (255.0f, 255.0f, 255.0f, 255.0f);
  glTexCoord2f (0, 0);
  glVertex3f (640, 0, 0);
  glTexCoord2f (1, 0);
  glVertex3f (1280, 0, 0);
  glTexCoord2f (1, 1);
  glVertex3f (1280, 480, 0);
  glTexCoord2f (0, 1);
  glVertex3f (640, 480, 0);
  glEnd ();

  glutSwapBuffers ();
}



void
keyPressed (unsigned char key, int x, int y)
{
  if (key == 27)
  {
    die = 1;
    pthread_join (freenect_thread, NULL);
    glutDestroyWindow (window);
    pthread_exit (NULL);
  }
  if (key == 'w')
  {
    freenect_angle++;
    if (freenect_angle > 30)
    {
      freenect_angle = 30;
    }
  }
  if (key == 'c')
  {
    freenect_angle = 0;
  }
  if (key == 'x')
  {
    freenect_angle--;
    if (freenect_angle < -30)
    {
      freenect_angle = -30;
    }
  }
  // Toggle the IR mode on/off
  if (key == 'i')
  {
    if (ir_mode)
    {
      freenect_set_rgb_format (f_dev, FREENECT_FORMAT_RGB);
      freenect_start_rgb (f_dev);
    }
    else
    {
      freenect_set_rgb_format (f_dev, FREENECT_FORMAT_IR);
      freenect_start_ir (f_dev);
    }
    ir_mode = !ir_mode;
  }

  // save images
  if (key == 's')
    {
      if (ir_mode) saveIR = true;
      else 
        {
          saveRGB = true;
          saveDepth = true;
        }
    }

  // back up image number
  if (key == 'b')
    {
      if (ir_mode) ir_num--;
      else { rgb_num--; depth_num--; }
    }

  if (key == '1')
  {
    freenect_set_led (f_dev, LED_GREEN);
  }
  if (key == '2')
  {
    freenect_set_led (f_dev, LED_RED);
  }
  if (key == '3')
  {
    freenect_set_led (f_dev, LED_YELLOW);
  }
  if (key == '4')
  {
    freenect_set_led (f_dev, LED_BLINK_YELLOW);
  }
  if (key == '5')
  {
    freenect_set_led (f_dev, LED_BLINK_GREEN);
  }
  if (key == '6')
  {
    freenect_set_led (f_dev, LED_BLINK_RED_YELLOW);
  }
  if (key == '0')
  {
    freenect_set_led (f_dev, LED_OFF);
  }

  if (freenect_angle != freenect_angle_last)
    freenect_set_tilt_degs (f_dev, freenect_angle);
  freenect_angle_last = freenect_angle;
}

void
ReSizeGLScene (int Width, int Height)
{
  glViewport (0, 0, Width, Height);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0, 1280, 480, 0, -1.0f, 1.0f);
  glMatrixMode (GL_MODELVIEW);
}

void
InitGL (int Width, int Height)
{
  glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
  glClearDepth (1.0);
  glDepthFunc (GL_LESS);
  glDisable (GL_DEPTH_TEST);
  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glShadeModel (GL_SMOOTH);
  glGenTextures (1, &gl_depth_tex);
  glBindTexture (GL_TEXTURE_2D, gl_depth_tex);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenTextures (1, &gl_rgb_tex);
  glBindTexture (GL_TEXTURE_2D, gl_rgb_tex);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  ReSizeGLScene (Width, Height);
}

void *
gl_threadfunc (void *arg)
{
  glutInitDisplayMode (GLUT_RGBA | GLUT_DOUBLE | GLUT_ALPHA | GLUT_DEPTH);
  glutInitWindowSize (1280, 480);
  glutInitWindowPosition (0, 0);

  window = glutCreateWindow ("Calibration Data Acquisition");

  glutDisplayFunc (&DrawGLScene);
  glutIdleFunc (&DrawGLScene);
  glutReshapeFunc (&ReSizeGLScene);
  glutKeyboardFunc (&keyPressed);

  InitGL (1280, 480);

  glutMainLoop ();

  return NULL;
}

uint16_t t_gamma[2048];
uint8_t ir_gamma[1024];         // IR gamma
uint8_t g_gamma[256];           // grayscale gamma

void
depth_cb (freenect_device * dev, void *v_depth, uint32_t timestamp)
{
  int i;
  freenect_depth *depth = (freenect_depth *)v_depth;

  // convert to cv::Mat for saving
  if (saveDepth)
    {
      cv::Mat img(ROWS,COLS,CV_16UC1);
      uint8_t *imgi = img.ptr<uint8_t>(0);
      uint8_t *rgbi = (uint8_t *)depth;
      memcpy(imgi,rgbi,FREENECT_FRAME_PIX*2);
      char fname[1024];
      sprintf(fname,"%s/img_depth_%02d.png", fdir, depth_num);
      depth_num++;
      if (cv::imwrite(fname,img))
        printf("Wrote depth image %s\n", fname);
      else
        printf("ERROR: failed to write image %s\n", fname);
      saveDepth = false;
    }


  pthread_mutex_lock (&gl_backbuf_mutex);
  for (i = 0; i < FREENECT_FRAME_PIX; i++)
  {
    int pval = t_gamma[depth[i]];
    int lb = pval & 0xff;
    switch (pval >> 8)
    {
    case 0:
      gl_depth_back[3 * i + 0] = 255;
      gl_depth_back[3 * i + 1] = 255 - lb;
      gl_depth_back[3 * i + 2] = 255 - lb;
      break;
    case 1:
      gl_depth_back[3 * i + 0] = 255;
      gl_depth_back[3 * i + 1] = lb;
      gl_depth_back[3 * i + 2] = 0;
      break;
    case 2:
      gl_depth_back[3 * i + 0] = 255 - lb;
      gl_depth_back[3 * i + 1] = 255;
      gl_depth_back[3 * i + 2] = 0;
      break;
    case 3:
      gl_depth_back[3 * i + 0] = 0;
      gl_depth_back[3 * i + 1] = 255;
      gl_depth_back[3 * i + 2] = lb;
      break;
    case 4:
      gl_depth_back[3 * i + 0] = 0;
      gl_depth_back[3 * i + 1] = 255 - lb;
      gl_depth_back[3 * i + 2] = 255;
      break;
    case 5:
      gl_depth_back[3 * i + 0] = 0;
      gl_depth_back[3 * i + 1] = 0;
      gl_depth_back[3 * i + 2] = 255 - lb;
      break;
    default:
      gl_depth_back[3 * i + 0] = 0;
      gl_depth_back[3 * i + 1] = 0;
      gl_depth_back[3 * i + 2] = 0;
      break;
    }
  }
  got_frames++;
  pthread_cond_signal (&gl_frame_cond);
  pthread_mutex_unlock (&gl_backbuf_mutex);
}

void
rgb_cb (freenect_device * dev, freenect_pixel * rgb, uint32_t timestamp)
{
  pthread_mutex_lock (&gl_backbuf_mutex);
  got_frames++;
  memcpy (gl_rgb_back, rgb, FREENECT_RGB_SIZE);

  // convert to cv::Mat, and find checkerboard
  cv::Mat img(ROWS,COLS,CV_8UC3);
  uint8_t *imgi = img.ptr<uint8_t>(0);
  uint8_t *rgbi = (uint8_t *)rgb;
  for (int i=0; i<FREENECT_FRAME_PIX; i++) // use BGR, yech
    {
      imgi[i*3+2] = rgbi[i*3];
      imgi[i*3+1] = rgbi[i*3+1];
      imgi[i*3] = rgbi[i*3+2];
    }

  vector<cv::Point2f> corners;
  bool ret = cv::findChessboardCorners(img, pattern_size, corners);

  // convert to color image and display
  cv::Mat imgc;
  imgc = img.clone();
  if (corners.size() > 0)
    drawChessboardCorners(imgc, pattern_size, cv::Mat(corners), ret);

  // redraw
  imgi = imgc.ptr<uint8_t>(0);
  for (int i=0; i<FREENECT_FRAME_PIX; i++)
  {
    gl_rgb_back[3*i + 0] = imgi[i*3+2];
    gl_rgb_back[3*i + 1] = imgi[i*3+1];
    gl_rgb_back[3*i + 2] = imgi[i*3];
  }

  if (saveRGB && ret)
    {
      char fname[1024];
      sprintf(fname,"%s/img_rgb_%02d.png", fdir, rgb_num);
      rgb_num++;
      if (cv::imwrite(fname,img))
        printf("Wrote RGB image %s\n", fname);
      else
        printf("ERROR: failed to write image %s\n", fname);
      saveRGB = false;
    }


  pthread_cond_signal (&gl_frame_cond);
  pthread_mutex_unlock (&gl_backbuf_mutex);
}

void
ir_cb (freenect_device * dev, freenect_pixel_ir * rgb, uint32_t timestamp)
{
  pthread_mutex_lock (&gl_backbuf_mutex);
  got_frames++;

  int i;
  for (i = 0; i < FREENECT_FRAME_PIX; i++)
  {
    int pval = rgb[i];
    int lb = ir_gamma[pval];

    gl_rgb_back[3 * i + 0] = lb;
    gl_rgb_back[3 * i + 1] = lb;
    gl_rgb_back[3 * i + 2] = lb;
  }

  // convert to cv::Mat, and find checkerboard
  cv::Mat img(ROWS,COLS,CV_8UC1);
  uint8_t *imgi = img.ptr<uint8_t>(0);
  for (i = 0; i < FREENECT_FRAME_PIX; i++)
    //    imgi[i] = (uint8_t)(rgb[i]>>2);
    imgi[i] = (uint8_t)ir_gamma[rgb[i]]; // use gamma-corrected for low light

  vector<cv::Point2f> corners;
  bool ret = cv::findChessboardCorners(img, pattern_size, corners);

  // convert to color image and display
  cv::Mat imgc(ROWS,COLS,CV_8UC3);
  cv::cvtColor(img,imgc,CV_GRAY2RGB,3);
  if (corners.size() > 0)
    drawChessboardCorners(imgc, pattern_size, cv::Mat(corners), ret);

  // redraw
  imgi = imgc.ptr<uint8_t>(0);
  for (i = 0; i < FREENECT_FRAME_PIX; i++)
  {
    gl_rgb_back[3*i + 0] = imgi[i*3];
    gl_rgb_back[3*i + 1] = imgi[i*3+1];
    gl_rgb_back[3*i + 2] = imgi[i*3+2];
  }

  if (saveIR && ret)
    {
      char fname[1024];
      sprintf(fname,"%s/img_ir_%02d.png", fdir, ir_num);
      ir_num++;
      if (cv::imwrite(fname,img))
        printf("Wrote IR image %s\n", fname);
      else
        printf("ERROR: failed to write image %s\n", fname);
      saveIR = false;
    }

  pthread_cond_signal (&gl_frame_cond);
  pthread_mutex_unlock (&gl_backbuf_mutex);
}

void *
freenect_threadfunc (void *arg)
{
  freenect_set_tilt_degs (f_dev, freenect_angle);
  freenect_set_led (f_dev, LED_RED);
  freenect_set_depth_callback (f_dev, depth_cb);
  freenect_set_rgb_callback (f_dev, rgb_cb);
  freenect_set_ir_callback (f_dev, ir_cb);
  freenect_set_rgb_format (f_dev, FREENECT_FORMAT_RGB);
  freenect_set_depth_format (f_dev, FREENECT_FORMAT_11_BIT);

  freenect_start_depth (f_dev);
  freenect_start_rgb (f_dev);

  printf ("'w'-tilt up, 'c'-center, 'x'-tilt down, '0'-'6'-select LED mode\n");

  while (!die && freenect_process_events (f_ctx) >= 0)
  {}

  printf ("\nshutting down streams...\n");

  freenect_stop_depth (f_dev);
  freenect_stop_rgb (f_dev);
  freenect_stop_ir (f_dev);

  printf ("-- done!\n");
  return NULL;
}

int
main(int argc, char **argv)
{
  // Parse GLUT-specific options
  glutInit (&argc, argv);

  pattern_size = cv::Size(0,0);
  opterr = 0;
  int c;
  while ((c = getopt(argc, argv, "r:c:")) != -1)
  {
    switch (c)
    {
      case 'r':
        pattern_size.height = atoi(optarg);
        break;
      case 'c':
        pattern_size.width = atoi(optarg);
        break;
    }
  }

  if (optind < argc)
    fdir = argv[optind];

  if (pattern_size.width == 0 || pattern_size.height == 0 || fdir == NULL)
  {
    printf("Must give the checkerboard width/height and data directory.\n"
           "Usage:\n"
           "%s -r ROWS -c COLS my_data_dir\n", argv[0]);
    return 1;
  }

  int i;
  for (i = 0; i < 2048; i++)
  {
    float v = i / 2048.0;
    v = powf (v, 3) * 6;
    t_gamma[i] = v * 6 * 256;
  }

  for (i = 0; i < 1024; i++)
  {
    float v = i / 1024.0;
    v = powf (v, 0.45);
    ir_gamma[i] = v * 256;
  }

  for (i = 0; i < 256; i++)
  {
    float v = i / 256.0;
    v = powf (v, 0.45);
    g_gamma[i] = v * 256;
  }

  if (freenect_init (&f_ctx, NULL) < 0)
  {
    printf ("freenect_init() failed\n");
    return 1;
  }

  freenect_set_log_level (f_ctx, FREENECT_LOG_ERROR);

  int nr_devices = freenect_num_devices (f_ctx);
  printf ("Number of devices found: %d\n", nr_devices);

  int user_device_number = 0;
  if (argc > 1)
    user_device_number = atoi (argv[1]);

  if (nr_devices < 1)
    return 1;

  if (freenect_open_device (f_ctx, &f_dev, user_device_number) < 0)
  {
    printf ("Could not open device\n");
    return 1;
  }

  if (pthread_create (&freenect_thread, NULL, freenect_threadfunc, NULL))
  {
    printf ("pthread_create failed\n");
    return 1;
  }

  // OS X requires GLUT to run on the main thread
  gl_threadfunc (NULL);

  return 0;
}
