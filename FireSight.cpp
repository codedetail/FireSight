#include <string.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <math.h>
#include "FireLog.h"
#include "FireSight.hpp"
#include "version.h"
#include "jo_util.hpp"
#include "opencv2/features2d/features2d.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "jansson.h"

using namespace cv;
using namespace std;
using namespace firesight;

typedef enum{UI_STILL, UI_VIDEO} UIMode;

static void help() {
  cout << "FireSight image processing pipeline v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH << endl;
  cout << "https://github.com/firepick1/FireSight/wiki" << endl;
  cout << "OpenCV " CV_VERSION << endl;
  cout << "Platform bits: " << FIRESIGHT_PLATFORM_BITS << endl;
  cout << endl;
  cout << "Example:" << endl;
  cout << "   firesight -p json/pipeline0.json -i img/cam.jpg -o target/output.jpg" << endl;
  cout << "   firesight -p json/pipeline1.json " << endl;
  cout << "   firesight -p json/pipeline2.json " << endl;
}

bool parseArgs(int argc, char *argv[], 
  string &pipelineString, char *&imagePath, char * &outputPath, UIMode &uimode, ArgMap &argMap, bool &isTime) 
{
  char *pipelinePath = NULL;
  uimode = UI_STILL;
  isTime = false;
  firelog_level(FIRELOG_INFO);

  for (int i = 1; i < argc; i++) {
    if (strcmp("-p",argv[i]) == 0) {
      if (i+1>=argc) {
        LOGERROR("expected pipeline path after -p");
        exit(-1);
      }
      pipelinePath = argv[++i];
      LOGTRACE1("parseArgs(-p) \"%s\" is JSON pipeline path", pipelinePath);
    } else if (strcmp("-o",argv[i]) == 0) {
      if (i+1>=argc) {
        LOGERROR("expected output path after -o");
        exit(-1);
      }
      outputPath = argv[++i];
      LOGTRACE1("parseArgs(-o) \"%s\" is output image path", outputPath);
    } else if (strcmp("-time",argv[i]) == 0) {
      isTime = true;
    } else if (strncmp("-D",argv[i],2) == 0) {
      char * pEq = strchr(argv[i],'=');
      if (!pEq || (pEq-argv[i])<=2) {
        LOGERROR("expected argName=argValue pair after -D");
        exit(-1);
      }
      *pEq = 0;
      char *pName = argv[i] + 2;
      char *pVal = pEq + 1;
      argMap[pName] = pVal;
      LOGTRACE2("parseArgs(-D) argMap[%s]=\"%s\"", pName, pVal );
      *pEq = '=';
    } else if (strcmp("-i",argv[i]) == 0) {
      if (i+1>=argc) {
        LOGERROR("expected image path after -i");
        exit(-1);
      }
      imagePath = argv[++i];
      LOGTRACE1("parseArgs(-i) \"%s\" is input image path", imagePath);
    } else if (strcmp("-video", argv[i]) == 0) {
      uimode = UI_VIDEO;
      LOGTRACE("parseArgs(-video) UI_VIDEO user interface selected");
    } else if (strcmp("-info", argv[i]) == 0) {
      firelog_level(FIRELOG_INFO);
    } else if (strcmp("-debug", argv[i]) == 0) {
      firelog_level(FIRELOG_DEBUG);
    } else if (strcmp("-trace", argv[i]) == 0) {
      firelog_level(FIRELOG_TRACE);
    } else {
      LOGERROR1("unknown firesight argument: '%s'", argv[i]);
      return false;
    }
  }
  if (!pipelinePath) {
    return false;
  }

  LOGTRACE1("Reading pipeline: %s", pipelinePath);
  ifstream ifs(pipelinePath);
  stringstream pipelineStream;
  pipelineStream << ifs.rdbuf();
  pipelineString = pipelineStream.str();
  const char *pJsonPipeline = pipelineString.c_str();
  if (strlen(pJsonPipeline) < 10) {
    LOGERROR1("Invalid pipeline path: %s", pipelinePath);
    exit(-1);
  }
  return true;
}

/**
 * Single image example of FireSight lib_firesight library use
 */
static int uiStill(const char * pJsonPipeline, Mat &image, ArgMap &argMap, bool isTime) {
  Pipeline pipeline(pJsonPipeline);
  
  json_t *pModel = pipeline.process(image, argMap);

  if (isTime) {
    long tickStart = cvGetTickCount();
    int iterations = 100;
    for (int i=0; i < iterations; i++) {
      json_decref(pModel);
      pModel = pipeline.process(image, argMap);
    }
    float msElapsed = (cvGetTickCount() - tickStart)/cvGetTickFrequency()/1000.0;
    LOGINFO2("timed %d iterations with an average of %.1fms per iteration", iterations, msElapsed/iterations);
  }

  // Print out returned model 
  char *pModelStr = json_dumps(pModel, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(2));
  cout << pModelStr << endl;
  free(pModelStr);

  // Free model
  json_decref(pModel);

  return 0;
}

/**
 * Video capture example of FireSight lib_firesight library use
 */
static int uiVideo(const char * pJsonPipeline, ArgMap &argMap) {
  VideoCapture cap(0); // open the default camera
  if(!cap.isOpened()) {  // check if we succeeded
    LOGERROR("Could not open camera");
    exit(-1);
  }

  namedWindow("image",1);

  Pipeline pipeline(pJsonPipeline);

  for(;;) {
    Mat frame;
    cap >> frame; // get a new frame from camera

    json_t *pModel = pipeline.process(frame, argMap);

    // Display pipeline output
    imshow("image", frame);
    if(waitKey(30) >= 0) break;

    // Free model
    json_decref(pModel);
  }

  return 0;
}

int main(int argc, char *argv[])
{
  UIMode uimode;
  string pipelineString;
  char * imagePath = NULL;
  char * outputPath = NULL;
  ArgMap argMap;
  bool isTime;
  bool argsOk = parseArgs(argc, argv, pipelineString, imagePath, outputPath, uimode, argMap, isTime);
  if (!argsOk) {
    help();
    exit(-1);
  }

  Mat image;
  if (imagePath) {
    LOGTRACE1("Reading image: %s", imagePath);
    image = imread(imagePath);
    if (!image.data) {
      LOGERROR1("main() imread(%s) failed", imagePath);
      exit(-1);
    }
  } else {
    LOGDEBUG("No image specified.");
  }

  const char *pJsonPipeline = pipelineString.c_str();

  switch (uimode) {
    case UI_STILL: 
      uiStill(pJsonPipeline, image, argMap, isTime);
      break;
    case UI_VIDEO: 
      uiVideo(pJsonPipeline, argMap); 
      break;
    default: 
      LOGERROR("Unknown UI mode");
      exit(-1);
  }

  if (outputPath) {
    if (!imwrite(outputPath, image)) {
      LOGERROR1("Could not write image to: %s", outputPath);
      exit(-1);
    }
    LOGTRACE1("Image written to: %s", outputPath);
  }

  return 0;
}
