/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2009, Willow Garage, Inc.
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/**

\author Kurt Konolige, Patrick Mihelich

**/

#include <stdio.h>
#include <string.h>
#include <iostream>
#include <unistd.h> // getopt
#include <cstdlib>

#include <math.h>
#include <opencv2/core/core.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <Eigen/Core>
#include <Eigen/LU>

using namespace cv;
using namespace Eigen;
using namespace std;

// image size
#define ROWS 480
#define COLS 640

// Pixel offset from IR image to depth image
cv::Point2f ir_depth_offset = cv::Point2f(-4, -3);

#define SHIFT_SCALE 0.125

// change shift to disparity
double shift2disp(int shift, double shift_offset)
{
  return SHIFT_SCALE*(double)(shift_offset - shift);
}

// colorizes a depth pixel
uint16_t t_gamma[2048];         // gamma conversion for depth

void setDepthColor(uint8_t *cptr, int d)
{
  int pval = t_gamma[d];
  int lb = pval & 0xff;
  switch (pval >> 8)
    {
    case 0:
      cptr[2] = 255;
      cptr[1] = 255 - lb;
      cptr[0] = 255 - lb;
      break;
    case 1:
      cptr[2] = 255;
      cptr[1] = lb;
      cptr[0] = 0;
      break;
    case 2:
      cptr[2] = 255 - lb;
      cptr[1] = 255;
      cptr[0] = 0;
      break;
    case 3:
      cptr[2] = 0;
      cptr[1] = 255;
      cptr[0] = lb;
      break;
    case 4:
      cptr[2] = 0;
      cptr[1] = 255 - lb;
      cptr[0] = 255;
      break;
    case 5:
      cptr[2] = 0;
      cptr[1] = 0;
      cptr[0] = 255 - lb;
      break;
    default:
      cptr[2] = 0;
      cptr[1] = 0;
      cptr[0] = 0;
      break;
    }
}

void writeCalibration(FILE *f, const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs)
{
  const double *K = cameraMatrix.ptr<double>();
  const double *D = distCoeffs.ptr<double>();
  fprintf(f, "image_width: 640\n");
  fprintf(f, "image_height: 480\n");
  fprintf(f, "camera_name: kinect\n");
  fprintf(f,
          "camera_matrix:\n"
          "   rows: 3\n"
          "   cols: 3\n"
          "   data: [ %.8f, %.8f, %.8f, %.8f, %.8f, %.8f, %.8f, %.8f, %.8f ]\n",
          K[0], K[1], K[2], K[3], K[4], K[5], K[6], K[7], K[8]);
  fprintf(f,
          "distortion_coefficients:\n"
          "   rows: 1\n"
          "   cols: 5\n"
          "   data: [ %.8f, %.8f, %.8f, %.8f, %.8f ]\n",
          D[0], D[1], D[2], D[3], D[4]);
  fprintf(f,
          "rectification_matrix:\n"
          "   rows: 3\n"
          "   cols: 3\n"
          "   data: [ 1., 0., 0., 0., 1., 0., 0., 0., 1. ]\n");
  fprintf(f,
          "projection_matrix:\n"
          "   rows: 3\n"
          "   cols: 4\n"
          "   data: [ %.8f, %.8f, %.8f, 0., %.8f, %.8f, %.8f, 0., %.8f, %.8f, %.8f, 0. ]\n",
          K[0], K[1], K[2], K[3], K[4], K[5], K[6], K[7], K[8]);
}

// 
// read in IR images, perform monocular calibration, check distortion
// arguments:
//   [dir]          data directory (without trailing slash); default cwd
//   [cell size, m] size of edge of each square in chessboard
//   [#rows #cols]  number of rows and cols of interior chessboard;
//                  default 6x7
//

int
main(int argc, char **argv)
{
  // checkerboard pattern
  int ccols = 0;
  int crows = 0;

  // cell size
  double csize = 0.0;

  char *fdir = NULL;

  opterr = 0;
  int c;
  while ((c = getopt(argc, argv, "r:c:s:")) != -1)
  {
    switch (c)
    {
      case 'r':
        crows = atoi(optarg);
        break;
      case 'c':
        ccols = atoi(optarg);
        break;
      case 's':
        csize = atof(optarg);
        break;
    }
  }

  if (optind < argc)
    fdir = argv[optind];

  if (crows == 0 || ccols == 0 || csize == 0.0 || fdir == NULL)
  {
    printf("Must give the checkerboard dimensions and data directory.\n"
           "Usage:\n"
           "%s -r ROWS -c COLS -s SQUARE_SIZE my_data_dir\n", argv[0]);
    return 1;
  }
    
  // construct the planar pattern
  vector<Point3f> pat;
  for (int i=0; i<ccols; i++)
    for (int j=0; j<crows; j++)
      pat.push_back(Point3f(i*csize,j*csize,0));

  // read in images, set up feature points and patterns
  vector<vector<Point3f> > pats;
  vector<vector<Point2f> > points;

  int fnum = 0;

  while (1)
    {
      char fname[1024];
      sprintf(fname,"%s/img_ir_%02d.png",fdir,fnum++);
      Mat img = imread(fname,-1);
      if (img.data == NULL) break; // no data, not read, break out

      vector<cv::Point2f> corners;
      bool ret = cv::findChessboardCorners(img,Size(crows,ccols),corners);

      if (ret)
        printf("Found corners in image %s\n",fname);
      else {
        printf("*** Didn't find corners in image %s\n",fname);
        return 1;
      }

      cv::cornerSubPix(img, corners, Size(5,5), Size(-1,-1),
                       TermCriteria(TermCriteria::MAX_ITER+TermCriteria::EPS, 30, 0.1));

      // Adjust corners detected in IR image to where they would appear in the depth image
      for (int i = 0; i < corners.size(); ++i)
        corners[i] += ir_depth_offset;

      pats.push_back(pat);
      points.push_back(corners);
    }


  // Monocular calibration of depth camera
  Mat camMatrix;
  Mat distCoeffs;
  vector<Mat> rvecs;
  vector<Mat> tvecs;
  double rp_err;
  // Currently assuming zero distortion
  rp_err = calibrateCamera(pats, points, Size(COLS,ROWS), camMatrix, distCoeffs,
                           rvecs, tvecs,
                           CV_CALIB_FIX_K3 | 
                           CV_CALIB_FIX_K2 | 
                           CV_CALIB_FIX_K1 | 
                           CV_CALIB_ZERO_TANGENT_DIST |
                           //CV_CALIB_FIX_PRINCIPAL_POINT |
                           CV_CALIB_FIX_ASPECT_RATIO
                          );

  printf("\nCalibration results:\n");

  // print camera matrix
  printf("\nCamera matrix\n");
  double *dptr = camMatrix.ptr<double>(0);
  for (int i=0; i<3; i++)
    {
      for (int j=0; j<3; j++)
        printf("%f ",*dptr++);
      printf("\n");
    }
  //printf("\nAssuming zero distortion\n");
  dptr = distCoeffs.ptr<double>(0);
  printf("\nDistortion coefficients:\n"
         "k1: %f\n"
         "k2: %f\n"
         "t1: %f\n"
         "t2: %f\n"
         "k3: %f\n", dptr[0], dptr[1], dptr[2], dptr[3], dptr[4]);
  
  printf("\nReprojection error = %f\n\n", rp_err);

  char depth_fname[1024];
  sprintf(depth_fname, "%s/calibration_depth.yaml", fdir);
  FILE *depth_file = fopen(depth_fname, "w");
  if (depth_file) {
    writeCalibration(depth_file, camMatrix, distCoeffs);
    printf("Wrote depth camera calibration to %s\n\n", depth_fname);
  }
  
  // Read in depth images, fit readings to computed depths
  /// @todo Not checking that we actually got depth readings!
  fnum = 0;
  std::vector<cv::Vec2d> ls_src1;
  std::vector<double> ls_src2;
  //printf("Z\tr\n");
  while (1)
    {
      // Load raw depth readings
      char fname[1024];
      sprintf(fname,"%s/img_depth_%02d.png",fdir,fnum);
      Mat img_depth = imread(fname,-1);
      if (img_depth.data == NULL) break; // no data, not read, break out

      // Get corner points and extrinsic parameters
      const cv::Mat pattern(pats[fnum]); // 3-channel matrix view of vector<Point3f>
      vector<Point2f> &corners = points[fnum];
      cv::Mat rvec = rvecs[fnum];
      cv::Mat tvec = tvecs[fnum];
      cv::Mat rot3x3;
      cv::Rodrigues(rvec, rot3x3);

      // Transform object points into camera coordinates using (rvec, tvec)
      cv::Mat world_points;
      cv::Mat xfm(3, 4, cv::DataType<double>::type);
      cv::Mat xfm_rot = xfm.colRange(0,3);
      cv::Mat xfm_trans = xfm.col(3);
      rot3x3.copyTo(xfm_rot);
      tvec.reshape(1,3).copyTo(xfm_trans);
      cv::transform(pattern, world_points, xfm);
 
      for (int j = 0; j < corners.size(); ++j) {
        double Z = world_points.at<cv::Vec3f>(j)[2];   // actual depth
        double r = img_depth.at<uint16_t>(corners[j]); // sensor reading
        ls_src1.push_back(cv::Vec2d(-1.0, Z));
        ls_src2.push_back(Z*r);
        //printf("%.4f\t%.0f\n", Z, r);
      }

      fnum++;
    }

  cv::Mat depth_params;
  double A, B;
  double b; // baseline
  if (cv::solve(cv::Mat(ls_src1).reshape(1), cv::Mat(ls_src2), depth_params,
                DECOMP_LU | DECOMP_NORMAL)) {
    A = depth_params.at<double>(0);
    B = depth_params.at<double>(1);
    double f = camMatrix.ptr<double>()[0];
    b = SHIFT_SCALE * A / f;
    printf("Reading to depth fitting parameters:\n"
           "A = %f\n"
           "B = %f\n"
           "Baseline between projector and depth camera = %f\n",
           A, B, b);
  }
  else {
    printf("**** Failed to solve least-squared problem ****\n");
    return 1;
  }

  // 
  // calibrate IR to RGB images
  //

  // read in rgb files
  fnum = 0;
  vector<vector<Point2f> > pointsRGB; // RGB corners
  printf("\n");
  while (1)
    {
      char fname[1024];
      sprintf(fname,"%s/img_rgb_%02d.png",fdir,fnum);
      Mat img = imread(fname,1);
      if (img.data == NULL) break; // no data, not read, break out

      vector<cv::Point2f> corners;
      bool ret = cv::findChessboardCorners(img,Size(crows,ccols),corners);

      if (ret)
        printf("Found corners in image %s\n",fname);
      else {
        printf("*** Didn't find corners in image %s\n",fname);
        return 1;
      }

      Mat gray;
      cv::cvtColor(img, gray, CV_RGB2GRAY);
      cv::cornerSubPix(gray, corners, Size(5,5), Size(-1,-1),
                       TermCriteria(TermCriteria::MAX_ITER+TermCriteria::EPS, 30, 0.1));

      pointsRGB.push_back(corners);

      fnum++;
    }

  // calibrate monocular camera
  Mat camMatrixRGB;
  Mat distCoeffsRGB = Mat::zeros(5,1,CV_64F);
  // initialize camera matrix
  camMatrixRGB = (Mat_<double>(3,3) << 1, 0, 320, 0, 1, 240, 0, 0, 1);

  rp_err = calibrateCamera(pats, pointsRGB, Size(COLS,ROWS), camMatrixRGB, distCoeffsRGB,
                           rvecs, tvecs,
                           //CV_CALIB_FIX_K1 |
                           //CV_CALIB_FIX_K2 |
                           CV_CALIB_FIX_K3 |
                           CV_CALIB_ZERO_TANGENT_DIST |
                           //CV_CALIB_FIX_PRINCIPAL_POINT |
                           CV_CALIB_FIX_ASPECT_RATIO
                          );

  // distortion results
  printf("\nCalibration results:\n");

  // print camera matrix
  printf("\nCamera matrix\n");
  dptr = camMatrixRGB.ptr<double>(0);
  for (int i=0; i<3; i++)
    {
      for (int j=0; j<3; j++)
        printf("%f ",*dptr++);
      printf("\n");
    }

  dptr = distCoeffsRGB.ptr<double>(0);
  printf("\nDistortion coefficients:\n"
         "k1: %f\n"
         "k2: %f\n"
         "t1: %f\n"
         "t2: %f\n"
         "k3: %f\n", dptr[0], dptr[1], dptr[2], dptr[3], dptr[4]);
  
  printf("\nReprojection error = %f\n\n", rp_err);

  char rgb_fname[1024];
  sprintf(rgb_fname, "%s/calibration_rgb.yaml", fdir);
  FILE *rgb_file = fopen(rgb_fname, "w");
  if (rgb_file) {
    writeCalibration(rgb_file, camMatrixRGB, distCoeffsRGB);
    printf("Wrote RGB camera calibration to %s\n\n", rgb_fname);
  }

  // stereo calibration between IR and RGB
  Mat R,T,E,F;
  rp_err = stereoCalibrate(pats,points,pointsRGB,camMatrix,distCoeffs,
                           camMatrixRGB,distCoeffsRGB,Size(crows,ccols),
                           R,T,E,F);
  
  dptr = T.ptr<double>(0);
  printf("\nTranslation between depth and RGB sensors (m):\n");
  for (int i=0; i<3; i++)
    printf("%f ",dptr[i]);
  printf("\n");

  printf("\nRotation matrix:\n");
  dptr = R.ptr<double>(0);
  for (int i=0; i<3; i++)
    {
      for (int j=0; j<3; j++)
        printf("%f ",*dptr++);
      printf("\n");
    }
  printf("\nReprojection error = %f\n\n", rp_err);


  Matrix4d Q,S;                 // transformations
  Matrix<double,3,4> P;         // projection

  // from u,v,d of depth camera to XYZ
  double *cptr = camMatrix.ptr<double>(0);
  Q << 1, 0, 0,    -cptr[2],  // -cx
       0, 1, 0,    -cptr[5],  // -cy
       0, 0, 0,     cptr[0],  // focal length
       0, 0, 1.0/b, 0;        // baseline

  // from XYZ of depth camera to XYZ of RGB camera
  dptr = R.ptr<double>(0);
  double *tptr = T.ptr<double>(0);
  S << dptr[0], dptr[1], dptr[2], tptr[0],
       dptr[3], dptr[4], dptr[5], tptr[1],
       dptr[6], dptr[7], dptr[8], tptr[2],
       0,       0,       0,       1;

  // from XYZ to u,v in RGB camera
  cptr = camMatrixRGB.ptr<double>(0);
  P << cptr[0], 0,       cptr[2], 0,
       0,       cptr[4], cptr[5], 0,
       0,       0,       1,       0;

  Matrix<double,3,4> D = P*S*Q;
  std::cout << "Transform matrix:" << std::endl << D << std::endl << std::endl;

  char params_fname[1024];
  sprintf(params_fname, "%s/kinect_params.yaml", fdir);
  FILE *params_file = fopen(params_fname, "w");
  if (params_file) {
    fprintf(params_file, "shift_offset: %.4f\n", B);
    fprintf(params_file, "projector_depth_baseline: %.5f\n", b);
    dptr = R.ptr<double>(0);
    fprintf(params_file,
            "depth_rgb_rotation: [ %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f ]\n",
            dptr[0], dptr[1], dptr[2], dptr[3], dptr[4], dptr[5], dptr[6], dptr[7], dptr[8]);
    dptr = T.ptr<double>(0);
    fprintf(params_file,
            "depth_rgb_translation: [ %.6f, %.6f, %.6f ]\n", dptr[0], dptr[1], dptr[2]);
    printf("Wrote additional calibration parameters to %s\n", params_fname);
  }
  
  //
  // create rectified disparity images and save
  //

  // set up gamma for depth colorizer
  for (int i = 0; i < 2048; i++)
  {
    float v = i / 2048.0;
    v = powf (v, 3) * 6;
    t_gamma[i] = v * 6 * 256;
  }


  fnum = 0;
  printf("Creating output images\n");
  while (1)
    {
      char fname[1024];
      sprintf(fname,"%s/img_depth_%02d.png",fdir,fnum);
      Mat img = imread(fname,-1);
      if (img.data == NULL) break; // no data, not read, break out

      sprintf(fname,"%s/img_rgb_%02d.png",fdir,fnum);
      Mat imgRGB = imread(fname,1);
      if (imgRGB.data == NULL) break; // no data, not read, break out

      // Rectify RGB image
      cv::Mat imgRgbRect;
      cv::undistort(imgRGB, imgRgbRect, camMatrixRGB, distCoeffsRGB);

      uint16_t *dptr = img.ptr<uint16_t>(0);

      Mat imgr  = Mat::zeros(ROWS,COLS,CV_16UC1); // depth image mapped to RGB image
      Mat imgrc = Mat::zeros(ROWS,COLS,CV_8UC3); // depth image mapped to RGB image, colorized
      Mat imgc  = Mat::zeros(ROWS,COLS,CV_8UC3); // original depth image colorized
      Mat imgdc = Mat::zeros(ROWS,COLS,CV_8UC3); // RGB mapped to depth image

      uint16_t *rptr = imgr.ptr<uint16_t>(0);
      uint8_t *rcptr = imgrc.ptr<uint8_t>(0);
      uint8_t *cptr  = imgc.ptr<uint8_t>(0);
      uint8_t *dcptr = imgdc.ptr<uint8_t>(0);
      uint8_t *rgbptr = imgRgbRect.ptr<uint8_t>(0);

      int k=0;
      for (int i=0; i<ROWS; i++)
        for (int j=0; j<COLS; j++,k++) // k is depth image index
          {
            double d = shift2disp(dptr[k], B);
            if (d <= 0)
              d = 0.0;          // not valid
            Vector4d p;
            p << j,i,d,1;
            Vector3d q;
            q = D*p;
            int u = (int)(q[0]/q[2]+0.5);
            int v = (int)(q[1]/q[2]+0.5);
            setDepthColor(&cptr[3*(i*COLS+j)],dptr[k]);
            if (u < 0 || v < 0 || u >= COLS || v >= ROWS)
              continue;
            int disp = (int)(d*16+0.499);
            int kk = v*COLS+u;  // kk is corresponding RGB image index
            if (rptr[kk] < disp) // z-buffer check
              {
                rptr[kk] = disp;
                setDepthColor(&rcptr[3*kk],dptr[k]);
              }
            if (d != 0.0)
              memcpy(&dcptr[3*k],&rgbptr[3*kk],3); // RGB mapped to depth image
          }

      sprintf(fname,"%s/img_depth_rect_%02d.png",fdir,fnum);
      printf("Writing %s\n", fname);
      imwrite(fname,imgr);
      sprintf(fname,"%s/img_depth_rect_color_%02d.png",fdir,fnum);
      printf("Writing %s\n", fname);
      imwrite(fname,imgrc);
      sprintf(fname,"%s/img_depth_color_%02d.png",fdir,fnum);
      printf("Writing %s\n", fname);
      imwrite(fname,imgc);
      sprintf(fname,"%s/img_rgb_mapped_%02d.png",fdir,fnum);
      printf("Writing %s\n", fname);
      imwrite(fname,imgdc);
      sprintf(fname,"%s/img_rgb_rect_%02d.png",fdir,fnum);
      printf("Writing %s\n", fname);
      imwrite(fname,imgRgbRect);

      fnum++;
    }

  return 0;
}
