#include <map.h>
#include <random>

namespace dummy {

Map::Map()
    : map_grid_(vector<string>({"layer"})),
      nav_grid_(vector<string>({"layer"})),
      planning_grid_(vector<string>({"layer"})),
      rfid_grid_(vector<string>({"layer"})) {}

Map::Map(float plan_resolution, nav_msgs::OccupancyGrid occupancyGrid)
    : map_grid_(vector<string>({"layer"})),
      nav_grid_(vector<string>({"layer"})),
      planning_grid_(vector<string>({"layer"})),
      rfid_grid_(vector<string>({"layer"})) {
  Map::createMap(occupancyGrid);
  plotMyGrid("/tmp/initial_map.pgm", &map_grid_);

  Map::createGrid(map_grid_.getResolution());
  plotMyGrid("/tmp/initial_nav.pgm", &nav_grid_);
  Map::createNewMap();

  Map::createPathPlanningGrid(plan_resolution);
  plotMyGrid("/tmp/initial_planning.pgm", &planning_grid_);

  // once both are created, we can obtain the ratio
  gridToPathGridScale =
      planning_grid_.getResolution() / nav_grid_.getResolution();
  ROS_ASSERT_MSG(gridToPathGridScale >= 1, "[Map.cpp@map] Planning resolution "
                                           "lower than navigation resolution = "
                                           "%3.3f",
                 gridToPathGridScale);

  Map::createRFIDGrid(map_grid_.getResolution());
  ROS_DEBUG("[Map.cpp@map] map created from OccupancyGrid");
}

Map::Map(std::ifstream &infile, float resolution)
    : map_grid_(vector<string>({"layer"})),
      nav_grid_(vector<string>({"layer"})),
      planning_grid_(vector<string>({"layer"})),
      rfid_grid_(vector<string>({"layer"})) {
  Map::createMap(infile);
  Map::createGrid(resolution);
  Map::createNewMap();
  ROS_DEBUG("[Map.cpp@map] map created from ifstream");
}

Map::Map(float plan_resolution, float map_resolution, int width, int height,
         vector<int> data, geometry_msgs::Pose origin)
    : map_grid_(vector<string>({"layer"})),
      nav_grid_(vector<string>({"layer"})),
      planning_grid_(vector<string>({"layer"})),
      rfid_grid_(vector<string>({"layer"})) {
  Map::createMap(width, height, map_resolution, data, origin);
  plotMyGrid("/tmp/initial_map.pgm", &map_grid_);
  Map::createGrid(map_resolution);
  plotMyGrid("/tmp/initial_nav.pgm", &nav_grid_);
  Map::createNewMap();

  Map::createPathPlanningGrid(plan_resolution);
  plotMyGrid("/tmp/initial_planning.pgm", &planning_grid_);

  // once both are created, we can obtain the ratio
  gridToPathGridScale =
      planning_grid_.getResolution() / nav_grid_.getResolution();
  ROS_ASSERT_MSG(gridToPathGridScale >= 1, "[Map.cpp@map] Planning resolution "
                                           "lower than navigation resolution = "
                                           "%3.3f",
                 gridToPathGridScale);

  Map::createRFIDGrid(map_resolution);
  ROS_DEBUG("[Map.cpp@map] map created from data vector");
}

void Map::createMap(nav_msgs::OccupancyGrid occupancyGrid) {
  GridMapRosConverter::fromOccupancyGrid(occupancyGrid, "layer", map_grid_);

  float maxValue = map_grid_.get("layer").maxCoeffOfFinites();
  float minValue = map_grid_.get("layer").minCoeffOfFinites();
  //        maxValue = 20;
  ROS_DEBUG("[Map.cpp@createMap] encoding map grid between values: %3.3f, %3.3f",maxValue, minValue);
  encodeGrid(&map_grid_, maxValue, minValue);

  printGridData("map", &map_grid_);
}

void Map::createMap(std::ifstream &infile) {
  // load an image from cv
  ROS_DEBUG("[Map.cpp@createMap] Loading image into cv mat.");
  cv::Mat imageCV = MreadImage(infile);
  geometry_msgs::Pose origin;
  // orig will be placed at bottom left position
  // TODO : CHECK THIS!!!
  origin.position.x = 0;
  origin.position.y = 0;
  // harcoded values!!!!
  std::string map_frame_id = "map";
  double map_resolution = 0.1;
  ROS_WARN("[Map.cpp@createMap] MAP RESOLUTION MUST BE 0.1 m/cell !!!.");

  createMap(imageCV, origin, map_frame_id, map_resolution);
}

void Map::createMap(cv::Mat imageCV, geometry_msgs::Pose origin,
                    std::string map_frame_id, double map_resolution) {
  // map data WAS stored in map internal var, now is map_grid_
  // this method is simiarl to RFIDGridmap::createGrid

  // 2D position of the grid map in the grid map frame [m].
  double orig_x;
  double orig_y;

  // cell value ranges
  double minValue;
  double maxValue;

  // grayscale opencv mat where 0 are free and 1 are obstacles.
  imageCV = binarizeImage(imageCV);

  // grid size in pixels
  Map::numRows = imageCV.rows;
  Map::numCols = imageCV.cols;

  // map origin in rangeInMeters
  orig_x = origin.position.x + imageCV.rows * map_resolution / 2;
  orig_y = origin.position.y + imageCV.cols * map_resolution / 2;

  ROS_DEBUG("[Map.cpp@createMap] Image size [%lu, %lu]", numRows, numCols);

  cv::minMaxLoc(imageCV, &minValue, &maxValue);
  ROS_DEBUG("[Map.cpp@createMap] Min, max values [%3.3f %3.3f]", minValue,
            maxValue);
  ROS_DEBUG("[Map.cpp@createMap] Channels [%d]", imageCV.channels());
  ROS_DEBUG("[Map.cpp@createMap] Encoding [%s]",
            type2str(imageCV.type()).c_str());
  ROS_DEBUG("[Map.cpp@createMap] Map resolution [%3.3f]", map_resolution);
  ROS_DEBUG("[Map.cpp@createMap] Map resolution [%3.3f]", map_resolution);

  // create empty grid map
  ROS_DEBUG("[Map.cpp@createMap] Creating empty grid");

  grid_map::GridMap tempMap(vector<string>({"layer"}));
  // map lenth MUST be given in meters!
  tempMap.setGeometry(
      Length(numRows * map_resolution, numCols * map_resolution),
      map_resolution, Position(orig_x, orig_y));
  tempMap.setFrameId(map_frame_id);
  tempMap.clearAll();

  // Convert cv image to grid map.
  ROS_DEBUG("[Map.cpp@createMap] Storing cv mat into emtpy grid");
  string format = ("mono8");
  sensor_msgs::ImagePtr imageROS =
      cv_bridge::CvImage(std_msgs::Header(), format, imageCV).toImageMsg();
  GridMapRosConverter::addLayerFromImage(*imageROS, "layer", tempMap);

  ROS_DEBUG("[Map.cpp@createMap] encoding map grid between values: %3.3f, %3.3f",1.0,0.0);
  encodeGrid(&tempMap, 1, 0);

  map_grid_ = tempMap;
  printGridData("map", &map_grid_);
}

void Map::encodeGrid(grid_map::GridMap *gm, int obstValue, int freeValue) {
  long n_obsts = 0;
  long n_free = 0;
  long n_others = 0;
  long total = 0;
  for (grid_map::GridMapIterator iterator(*gm); !iterator.isPastEnd();
       ++iterator) {
    //            if (gm->at("layer", *iterator) == obstValue) {
    //                gm->at("layer", *iterator) = Map::CellValue::OBST;
    //                n_obsts++;
    //            } else if (gm->at("layer", *iterator) == freeValue) {
    //                gm->at("layer", *iterator) = Map::CellValue::FREE;
    //                n_free++;
    //            } else //free by default
    //            {
    //                gm->at("layer", *iterator) = Map::CellValue::FREE;
    //                n_others++;
    //            }
    if (gm->at("layer", *iterator) >= obstValue) {
      gm->at("layer", *iterator) = Map::CellValue::OBST;
      n_obsts++;
      //            } else if (gm->at("layer", *iterator) == freeValue) {
      //                gm->at("layer", *iterator) = Map::CellValue::FREE;
      //                n_free++;
    } else // free by default
    {
      gm->at("layer", *iterator) = Map::CellValue::FREE;
      n_others++;
    }
  }

  total = n_obsts + n_free + n_others;
  ROS_DEBUG("[Map.cpp@encodeGrid] Encoded grid with %3.3f %% of free cells and "
            "%3.3f %% of occupied cells",
            100.0 * n_free / (1.0 * total), 100.0 * n_obsts / (1.0 * total));
  if (n_others)
    ROS_WARN("[Map.cpp@encodeGrid] Found %3.3f %% undefined cells, CASTED to "
             "free space",
             100.0 * n_others / (1.0 * total));
}

void Map::createMap(int width, int height, double resolution, vector<int> data,
                    geometry_msgs::Pose origin) {
  // load an image from vector
  ROS_DEBUG("[Map.cpp@createMap] Loading vector into cv mat.");
  cv::Mat imageCV = MreadImage(width, height, data);

  // TODO : CHECK THIS!!!
  // harcoded values!!!!
  std::string map_frame_id = "map";

  createMap(imageCV, origin, map_frame_id, resolution);
}

cv::Mat Map::MreadImage(int width, int height, vector<int> data) {
  cv::Mat mImg(width, height, CV_8UC1);

  uchar pv[data.size()];
  for (unsigned int i = 0; i < data.size(); i++) {
    pv[i] = (uchar)data.at(i);
  }

  memcpy(mImg.data, &pv, data.size() * sizeof(uchar));

  return mImg;
}

// solution proposed https://codeday.me/es/qa/20190427/576956.html
cv::Mat Map::MreadImage(std::ifstream &input) {
  input.seekg(0, std::ios::end);
  size_t fileSize = input.tellg();
  input.seekg(0, std::ios::beg);

  if (fileSize == 0) {
    return cv::Mat();
    ROS_FATAL("[Map.cpp@MreadImage] Image input stream size is 0.");
  }

  std::vector<unsigned char> data(fileSize);
  input.read(reinterpret_cast<char *>(&data[0]),
             sizeof(unsigned char) * fileSize);

  if (!input) {
    return cv::Mat();
    ROS_FATAL("[Map.cpp@MreadImage] Cant cast input data");
  }

  cv::Mat mImg = cv::imdecode(cv::Mat(data), CV_LOAD_IMAGE_UNCHANGED);

  return mImg;
}

cv::Mat Map::binarizeImage(cv::Mat mImg) {
  // how many different values does it have?
  ROS_DEBUG("[Map.cpp@binarize] Input grid values:");
  std::vector<uchar> diffValues;
  for (int y = 0; y < mImg.rows; ++y) {
    const uchar *row_ptr = mImg.ptr<uchar>(y);
    for (int x = 0; x < mImg.cols; ++x) {
      uchar value = row_ptr[x];

      if (std::find(diffValues.begin(), diffValues.end(), value) ==
          diffValues.end()) {
        diffValues.push_back(value);
        ROS_DEBUG("[Map.cpp@binarize] \t 0x%x", value);
      }
    }
  }
  std::sort(diffValues.begin(), diffValues.end());
  if (diffValues.size() > 2) {

    // Apply thresholding to binarize!
    // cv::adaptiveThreshold(mImg, mImg, 255,
    // cv::ADAPTIVE_THRESH_GAUSSIAN_C,cv::THRESH_BINARY,3,5);
    cv::threshold(mImg, mImg, diffValues[diffValues.size() - 2], 255,
                  cv::THRESH_BINARY);
    diffValues.clear();
    // how many different values does it have?
    for (int y = 0; y < mImg.rows; ++y) {
      const uchar *row_ptr = mImg.ptr<uchar>(y);
      for (int x = 0; x < mImg.cols; ++x) {
        uchar value = row_ptr[x];

        if (std::find(diffValues.begin(), diffValues.end(), value) ==
            diffValues.end())
          diffValues.push_back(value);
      }
    }

    if (diffValues.size() != 2) {
      ROS_ERROR("[Map.cpp@createMap] After binarizing, we still have [%lu] "
                "different values: ",
                diffValues.size());
      for (int i = 0; i < diffValues.size(); i++) {
        ROS_ERROR("[Map.cpp@createMap] \t\t [0x%x]==[%d] ", diffValues[i],
                  diffValues[i]);
      }
    } else {
      ROS_DEBUG(
          "[Map.cpp@createMap] Input image only has [%lu] different values: ",
          diffValues.size());
    }

  } else {
    ROS_ERROR(
        "[Map.cpp@createMap] Input image only has [%lu] different values: ",
        diffValues.size());
  }

  return mImg;
}

void Map::createGrid(float resolution) {
  // change gridmap resolution from map_resolution to gridResolution
  ROS_DEBUG("[Map.cpp@createGrid] creating grid with resolution %3.3f",
            resolution);

  GridMapCvProcessing::changeResolution(map_grid_, nav_grid_, resolution);

  float maxValue = nav_grid_.get("layer").maxCoeffOfFinites();
  float minValue = nav_grid_.get("layer").minCoeffOfFinites();
  //        cout << "maxValue(obstacle) = " << maxValue << ", minValue(free) = "
  //        << minValue << endl;

  ROS_DEBUG("[Map.cpp@createGrid] encoding nav grid between values: %3.3f, %3.3f",maxValue, minValue);
  encodeGrid(&nav_grid_, maxValue, minValue);

  Map::numGridRows = nav_grid_.getSize()(0);
  Map::numGridCols = nav_grid_.getSize()(1);
  printGridData("nav", &nav_grid_);
//  createSecondNavigationGrid(map_grid_);  // TODO: Enable it only when fixed
}

void Map::createPathPlanningGrid(float resolution) {
  // change gridmap resolution from map_resolution to gridResolution
  ROS_DEBUG("[Map.cpp@createPathPlanningGrid] creating planning grid with resolution %3.3f",
            resolution);

  GridMapCvProcessing::changeResolution(map_grid_, planning_grid_, resolution);

  float maxValue = planning_grid_.get("layer").maxCoeffOfFinites();
  float minValue = planning_grid_.get("layer").minCoeffOfFinites();
  encodeGrid(&planning_grid_, maxValue, minValue);

  Map::numPathPlanningGridRows = planning_grid_.getSize()(0);
  Map::numPathPlanningGridCols = planning_grid_.getSize()(1);
  printGridData("plan", &planning_grid_);
}

void Map::createRFIDGrid(float resolution) {
  // change gridmap resolution from map_resolution to gridResolution
  ROS_DEBUG("[Map.cpp@createGrid] creating RFID grid with resolution %3.3f",
            resolution);

  GridMapCvProcessing::changeResolution(map_grid_, rfid_grid_, resolution);
  rfid_grid_["layer"].setConstant(0.0);
  printGridData("rfid", &rfid_grid_);
}

void Map::updatePathPlanningGrid(int cellX_pp, int cellY_pp,
                                 int rangeInCells_pp, double power) {
  int minX_pp = cellX_pp - rangeInCells_pp;
  int maxX_pp = cellX_pp + rangeInCells_pp;
  if (minX_pp < 0)
    minX_pp = 0;
  if (maxX_pp > numPathPlanningGridRows - 1)
    maxX_pp = numPathPlanningGridRows - 1;
  int minY_pp = cellY_pp - rangeInCells_pp;
  int maxY_pp = cellY_pp + rangeInCells_pp;
  if (minY_pp < 0)
    minY_pp = 0;
  if (maxY_pp > numPathPlanningGridCols - 1)
    maxY_pp = numPathPlanningGridCols - 1;

  std::cout << endl;
  std::cout << "[Map.cpp@updatePathPlanningGrid] rangeInCells_pp = "
            << rangeInCells_pp << endl;
  std::cout << "[Map.cpp@updatePathPlanningGrid] [numPathPlanningGridRows, "
               "numPathPlanningGridCols] = ["
            << numPathPlanningGridRows << "," << numPathPlanningGridCols << "]"
            << endl;
  std::cout << "[Map.cpp@updatePathPlanningGrid] [cellX_pp, cellY_pp] = ["
            << cellX_pp << "," << cellY_pp << "]" << endl;
  std::cout << "[Map.cpp@updatePathPlanningGrid] [minX_pp, maxX_pp] = ["
            << minX_pp << "," << maxX_pp << "]" << endl;
  std::cout << "[Map.cpp@updatePathPlanningGrid] [minY_pp, maxY_pp] = ["
            << minY_pp << "," << maxY_pp << "]" << endl;

  grid_map::Index planningStartIndex(minX_pp, minX_pp);
  grid_map::Index planningBufferSize(2 * rangeInCells_pp, 2 * rangeInCells_pp);

  Position position_pp, upper_left_pp;

  // this is the number of navigation cells inside a planning cell
  grid_map::Index navBufferSize(gridToPathGridScale, gridToPathGridScale);

  grid_map::Index navStartIndex, rfid_index;

  int countScanned = 0;
  int setToOne = 0;
  double k =
      (this->planning_grid_.getResolution() / 2) - nav_grid_.getResolution();
  int counter_planning_grid_scanned = 0;

  // iterate over the submap in the planning grid (lower res)
  for (grid_map::SubmapIterator planning_iterator(
           this->planning_grid_, planningStartIndex, planningBufferSize);
       !planning_iterator.isPastEnd(); ++planning_iterator) {

    // get the centre of the current planning cell
    this->planning_grid_.getPosition(*planning_iterator, position_pp);
    //        cout << endl;
    //        std::cout << "[Map.cpp@updatePathPlanningGrid] Current
    //        position(meters) in PLANNING grid = [ " << position_pp.x() << ","
    //        << position_pp.y() << "]" << endl;
    //        std::cout << "[Map.cpp@updatePathPlanningGrid] Current
    //        position(cells) in PLANNING grid = [ " << *planning_iterator <<
    //        endl;

    countScanned = 0;
    setToOne = 0;

    // obtain the position of the upper-left  navigation cell INSIDE current
    // planning cell
    upper_left_pp.x() = position_pp.x() - k;
    upper_left_pp.y() = position_pp.y() - k;

    // and corresponding index in nav_grid_
    nav_grid_.getIndex(upper_left_pp, navStartIndex);
    //        std::cout << "[Map.cpp@updatePathPlanningGrid] Upper_left index of
    //        NAVIGATION submap = [ " << navStartIndex << endl;

    int counter = 0;
    for (grid_map::SubmapIterator nav_iterator(nav_grid_, navStartIndex,
                                               navBufferSize);
         !nav_iterator.isPastEnd(); ++nav_iterator) {
      //          std::cout << "[Map.cpp@updatePathPlanningGrid] Inside : " <<
      //          getGridValue((*nav_iterator)(0),(*nav_iterator)(1)) << " at "
      //          << (*nav_iterator)(0) << ", " << (*nav_iterator)(0) << endl;
      counter++;

      if (isGridValueObst(*nav_iterator)) {
        setToOne = 1;
        //            std::cout << "[Map.cpp@updatePathPlanningGrid] 1" << endl;
      }

      if (isGridValueVist(*nav_iterator)) {
        countScanned++;
      }
    }

    //        std::cout << "[Map.cpp@updatePathPlanningGrid] countScanned: " <<
    //        countScanned << endl;
    if (countScanned >= 0.9 * gridToPathGridScale * gridToPathGridScale) {
      setPathPlanningGridValue(Map::CellValue::VIST, (*planning_iterator)(0),
                               (*planning_iterator)(1));

      grid_map::Index ind;
      rfid_grid_.getIndex(position_pp, ind);
      setRFIDGridValue(power, ind(0), ind(1));

      counter_planning_grid_scanned++;
      //          std::cout << "[Map.cpp@updatePathPlanningGrid] CountScanned"
      //          << endl;
    }
    //        if (setToOne == 1) {
    //          setPathPlanningGridValue(Map::CellValue::OBST,
    //          (*planning_iterator)(0),(*planning_iterator)(1));
    ////          std::cout << "[Map.cpp@updatePathPlanningGrid] SetToOne" <<
    ///endl;
    //        }
  }

  std::cout << "[Map.cpp@updatePathPlanningGrid] PlanningGrid scanned cells: "
            << counter_planning_grid_scanned << endl;
}

void Map::printSubmapBoundaries(grid_map::Index startIndex,
                                grid_map::Index bufferSize,
                                const grid_map::GridMap *gm) const {
  int rowInc;
  int colInc;
  grid_map::Index indexInc;
  grid_map::Index topLeftI;
  grid_map::Index topRightI;
  grid_map::Index bottomRightI;
  grid_map::Index bottomLeftI;

  indexInc = bufferSize - grid_map::Index(1, 1);
  topLeftI = startIndex;
  topRightI = startIndex + grid_map::Index(0, indexInc(1));
  bottomRightI = startIndex + indexInc;
  bottomLeftI = startIndex + grid_map::Index(indexInc(0), 0);

  grid_map::Position topLeftP, topRightP, bottomRightP, bottomLeftP;
  gm->getPosition(topLeftI, topLeftP);
  gm->getPosition(topRightI, topRightP);
  gm->getPosition(bottomRightI, bottomRightP);
  gm->getPosition(bottomLeftI, bottomLeftP);
  ROS_DEBUG("Submap boundaries position: [%3.3f, %3.3f], [%3.3f, %3.3f], "
            "[%3.3f, %3.3f], [%3.3f, %3.3f]",
            topLeftP.x(), topLeftP.y(), topRightP.x(), topRightP.y(),
            bottomRightP.x(), bottomRightP.y(), bottomLeftP.x(),
            bottomLeftP.y());
}

void Map::updatePathPlanningGrid(float posX, float posY, float rangeInMeters) {
  int rangeInCells_pp;
  int numVistNavCellsPerPathCell;
  int numObstNavCellsPerPathCell;
  double k;
  int numVistPathCell;
  //      long upper_left_pp_X, upper_left_pp_X;

  grid_map::Index planningStartIndex;
  grid_map::Index planningBufferSize;
  Position upper_left_pp;
  Position position_pp;
  Position robot_pp(posX, posY);
  grid_map::Index robot_index;
  planning_grid_.getIndex(robot_pp, robot_index);

  grid_map::Index navStartIndex;
  grid_map::Index navBufferSize;

  // get the boundaries of the planning submap
  rangeInCells_pp = (int)std::ceil(rangeInMeters / planning_grid_.getResolution());
  planningBufferSize = grid_map::Index(2 * rangeInCells_pp, 2 * rangeInCells_pp);
  planningStartIndex(0) = robot_index(0) - rangeInCells_pp;
  planningStartIndex(1) = robot_index(1) - rangeInCells_pp;
//        std::cout << "\n[Map.cpp@updatePathPlanningGrid] upper_left_pp = [" <<
//        upper_left_pp.x() << "," << upper_left_pp.y() << "]" << endl;
//        std::cout << "[Map.cpp@updatePathPlanningGrid] robotIndex = "
//              << (robot_index)(0)  << "," << (robot_index)(1) << endl;
//        std::cout << "[Map.cpp@updatePathPlanningGrid] planningStartIndex = "
//        << (planningStartIndex)(0)  << "," << (planningStartIndex)(1) << endl;

  //      planningStartIndex = Position(posX - rangeInMeters, posY -
  //      rangeInMeters);
  // this is the number of navigation cells inside a planning cell
  navBufferSize = grid_map::Index(gridToPathGridScale, gridToPathGridScale);

  // distance from the center of a planning grid cell to the center
  // of the furthest navigation cell. IN METERS
  k = (planning_grid_.getResolution() / 2) - nav_grid_.getResolution();

  numVistPathCell = 0;

  printSubmapBoundaries(planningStartIndex, planningBufferSize, &planning_grid_);
//  std::cout << "[Map.cpp@updatePathPlanningGrid] rangeInCells_pp = " << rangeInCells_pp << endl;
//  std::cout << "[Map.cpp@updatePathPlanningGrid] robot position = " << posX << "," << posY << endl;
//  std::cout << "[Map.cpp@updatePathPlanningGrid] k = " << k << endl;
//  std::cout << "[Map.cpp@updatePathPlanningGrid] navGridResolution = "
//    << nav_grid_.getResolution() << ", pathPlanningGridResolution =" << planning_grid_.getResolution()<< endl;
//  std::cout << "[Map.cpp@updatePathPlanningGrid] navBufferSize: " << navBufferSize << endl;

  // iterate over the submap in the planning grid (lower res)
  //    cout << "PlanningGrid cell selected: " << endl;
  for (grid_map::SubmapIterator planning_iterator(planning_grid_, planningStartIndex, planningBufferSize);
       !planning_iterator.isPastEnd(); ++planning_iterator) {

    // get the centre of the current planning cell
    planning_grid_.getPosition(*planning_iterator, position_pp);
//    std::cout << "\n[Map.cpp@updatePathPlanningGrid] planning_iterator= [" <<
//        (*planning_iterator)(0) << "," << (*planning_iterator)(1) << "]" << endl;
//    std::cout << "[Map.cpp@updatePathPlanningGrid] position_pp = " <<
//        position_pp.x()  << "," << position_pp.y() << endl;
    numVistNavCellsPerPathCell = 0;
    numObstNavCellsPerPathCell = 0;


    // obtain the position of the upper-left  navigation cell INSIDE current
    // planning cell
    upper_left_pp.x() = position_pp.x() - k;
    upper_left_pp.y() = position_pp.y() - k;
//    std::cout << "[Map.cpp@updatePathPlanningGrid] upper_left_pp = "
//            << upper_left_pp.x()  << "," << upper_left_pp.y() << endl;
    // and corresponding index in nav_grid_
    nav_grid_.getIndex(upper_left_pp, navStartIndex);

    for (grid_map::SubmapIterator nav_iterator(nav_grid_, navStartIndex,navBufferSize); !nav_iterator.isPastEnd(); ++nav_iterator) {
//      std::cout << "[Map.cpp@updatePathPlanningGrid] Visited : " << isGridValueVist(*nav_iterator) << " at " <<
//          (*nav_iterator)(0) << ", " << (*nav_iterator)(1) << endl;
      if (isGridValueVist(*nav_iterator)) {
        numVistNavCellsPerPathCell++;
      }

      if (isGridValueObst(*nav_iterator)) {
        numObstNavCellsPerPathCell++;
      }
    }

//    if (numObstNavCellsPerPathCell <= 0.6 * gridToPathGridScale * gridToPathGridScale){
      if (numVistNavCellsPerPathCell >= 0.9 * gridToPathGridScale * gridToPathGridScale && !containsNavObstacles(position_pp)) {
        setPathPlanningGridValue(Map::CellValue::VIST, (*planning_iterator)(0) +1,
                                 (*planning_iterator)(1) +1) ;  // TODO: the +1 has been added because the cells where not correctly marked, it needs further investigation
        numVistPathCell++;
//      cout << "(" << (*planning_iterator)(0) <<"," << (*planning_iterator)(1) + 1 <<")" << endl;
      }
//    }

  }

//  ROS_DEBUG("[Map.cpp@updatePathPlanningGrid] PlanningGrid scanned cells: %d", numVistPathCell);
//  cout << "[Map.cpp@updatePathPlanningGrid] PlanningGrid scanned cells: "  << numVistPathCell << endl;
}

bool Map::containsNavObstacles(Position position_pp)
{
  Position upper_left_pp;
  grid_map::Index navStartIndex;
  grid_map::Index navBufferSize;
  int numObstNavCellsPerPathCell = 0;
  bool result = false;
  // this is the number of navigation cells inside a planning cell
  navBufferSize = grid_map::Index( gridToPathGridScale, gridToPathGridScale);
  // distance from the center of a planning grid cell to the center
  // of the furthest navigation cell. IN METERS
  double k = (planning_grid_.getResolution() / 2) - nav_grid_.getResolution();
//  cout << "k: " << k << endl;
  upper_left_pp.x() = position_pp.x() - k;// - planning_grid_.getResolution();
  upper_left_pp.y() = position_pp.y() - k;// - planning_grid_.getResolution();

  // and corresponding index in nav_grid_
  nav_grid_.getIndex(upper_left_pp, navStartIndex);

  for (grid_map::SubmapIterator nav_iterator(nav_grid_, navStartIndex, navBufferSize); !nav_iterator.isPastEnd(); ++nav_iterator) {
    if (isGridValueObst(*nav_iterator)) {
      numObstNavCellsPerPathCell++;
    }
  }
//  cout << "numObstNavCellsPerPathCell: " << numObstNavCellsPerPathCell << endl;
  if (numObstNavCellsPerPathCell >= 0.01 * gridToPathGridScale * gridToPathGridScale) {
    result =  true;
  }
//  cout << "(" << position_pp.x() << "," << position_pp.y() << ") contains Obstacle: " << result << endl;
  return result;
}

bool Map::checkWallsPathPlanningGrid(float posX, float posY, float rangeInMeters) {
  int rangeInCells_pp = 3;
  int numVistNavCellsPerPathCell;
  double k;
  int numVistPathCell;
  bool result = false;
  //      long upper_left_pp_X, upper_left_pp_X;

  grid_map::Index planningStartIndex;
  grid_map::Index planningBufferSize;
  Position upper_left_pp;
  Position position_pp;
  Position robot_pp(posX, posY);
  grid_map::Index robot_index;
  planning_grid_.getIndex(robot_pp, robot_index);

  grid_map::Index navStartIndex;
  grid_map::Index navBufferSize;

  // get the boundaries of the planning submap
  planningBufferSize =
      grid_map::Index(2 * rangeInCells_pp, 2 * rangeInCells_pp);
  planningStartIndex(0) = robot_index(0) - rangeInCells_pp;
  planningStartIndex(1) = robot_index(1) - rangeInCells_pp;

  // this is the number of navigation cells inside a planning cell
  navBufferSize = grid_map::Index(gridToPathGridScale, gridToPathGridScale);

  // distance from the center of a planning grid cell to the center
  // of the furthest navigation cell. IN METERS
  k = (planning_grid_.getResolution() / 2) - nav_grid_.getResolution();

  numVistPathCell = 0;

  printSubmapBoundaries(planningStartIndex, planningBufferSize,
                        &planning_grid_);
  //      std::cout << "[Map.cpp@updatePathPlanningGrid] rangeInCells_pp = " <<
  //      rangeInCells_pp << endl;
  //      std::cout << "[Map.cpp@updatePathPlanningGrid] robot position = " <<
  //      posX << "," << posY << endl;
  //      std::cout << "[Map.cpp@updatePathPlanningGrid] k = " << k << endl;
  //      std::cout << "[Map.cpp@updatePathPlanningGrid] navGridResolution = "
  //      << nav_grid_.getResolution() <<
  //          ", pathPlanningGridResolution =" <<
  //          planning_grid_.getResolution()<< endl;

  // iterate over the submap in the planning grid (lower res)
  for (grid_map::SubmapIterator planning_iterator(
      planning_grid_, planningStartIndex, planningBufferSize);
       !planning_iterator.isPastEnd(); ++planning_iterator) {

    // get the centre of the current planning cell
    planning_grid_.getPosition(*planning_iterator, position_pp);
    //        std::cout << "\n[Map.cpp@updatePathPlanningGrid] planning_iterator
    //        = [" << (*planning_iterator)(0) << "," << (*planning_iterator)(1)
    //        << "]" << endl;
    //        std::cout << "[Map.cpp@updatePathPlanningGrid] position_pp = " <<
    //        position_pp.x()  << "," << position_pp.y() << endl;
    int numObstNavCellsPerPathCell = 0;

    // obtain the position of the upper-left  navigation cell INSIDE current
    // planning cell
    upper_left_pp.x() = position_pp.x() - k;
    upper_left_pp.y() = position_pp.y() - k;
    //        std::cout << "[Map.cpp@updatePathPlanningGrid] upper_left_pp = "
    //        << upper_left_pp.x()  << "," << upper_left_pp.y() << endl;
    // and corresponding index in nav_grid_
    nav_grid_.getIndex(upper_left_pp, navStartIndex);

    for (grid_map::SubmapIterator nav_iterator(nav_grid_, navStartIndex,
                                               navBufferSize);
         !nav_iterator.isPastEnd(); ++nav_iterator) {
      //          std::cout << "[Map.cpp@updatePathPlanningGrid] Visited : " <<
      //          isGridValueVist(*nav_iterator) << " at " <<
      //            (*nav_iterator)(0) << ", " << (*nav_iterator)(1) << endl;
      if (isGridValueObst(*nav_iterator)) {
        numObstNavCellsPerPathCell++;
      }
    }

    if (numObstNavCellsPerPathCell >=
        0.1 * gridToPathGridScale * gridToPathGridScale) {
      setPathPlanningGridValue(Map::CellValue::OBST, (*planning_iterator)(0),
                               (*planning_iterator)(1));
      result = true;
    }
  }
  return result;
}

float Map::getGridToPathGridScale() const {
  return planning_grid_.getResolution() / nav_grid_.getResolution();
}

// Getter shortcuts ........................................................
bool Map::getPathPlanningPosition(double &x, double &y, long i, long j) {
  return getPosition(x, y, i, j, &planning_grid_);
}

bool Map::getPathPlanningIndex(double x, double y, long &i, long &j) {
  return getIndex(x, y, i, j, &planning_grid_);
}

Map::CellValue Map::getPathPlanningGridValue(long i, long j) const {
  return toCellValue(getValue(i, j, &planning_grid_), "planning");
}

Map::CellValue
Map::getPathPlanningGridValue(geometry_msgs::PoseStamped ps) const {
  return toCellValue(getValue(ps, &planning_grid_), "planning");
}

Map::CellValue Map::getPathPlanningGridValue(long i) const {
  return toCellValue(getValue(i, &planning_grid_), "planning");
}

int Map::getPathPlanningNumCols() const {
  return getGridNumCols(&planning_grid_);
}

int Map::getPathPlanningNumRows() const {
  return getGridNumRows(&planning_grid_);
}

bool Map::getGridPosition(double &x, double &y, long i, long j) {
  return getPosition(x, y, i, j, &nav_grid_);
}

bool Map::getGridIndex(double x, double y, long &i, long &j) {
  return getIndex(x, y, i, j, &nav_grid_);
}

Map::CellValue Map::getGridValue(long i, long j) const {
  return toCellValue(getValue(i, j, &nav_grid_), "nav");
}

Map::CellValue Map::getGridValue(geometry_msgs::PoseStamped ps) const {
  return toCellValue(getValue(ps, &nav_grid_), "nav");
}

Map::CellValue Map::getGridValue(long i) const {
  return toCellValue(getValue(i, &nav_grid_), "nav");
}

long Map::getNumGridCols() const { return getGridNumCols(&nav_grid_); }

long Map::getNumGridRows() const { return getGridNumRows(&nav_grid_); }

bool Map::getMapPosition(double &x, double &y, long i, long j) {
  return getPosition(x, y, i, j, &map_grid_);
}

bool Map::getMapIndex(double x, double y, long &i, long &j) {
  return getIndex(x, y, i, j, &map_grid_);
}

Map::CellValue Map::getMapValue(long i, long j) {
  return toCellValue(getValue(i, j, &map_grid_), "map");
}

Map::CellValue Map::getMapValue(geometry_msgs::PoseStamped ps) const {
  return toCellValue(getValue(ps, &map_grid_), "map");
}

Map::CellValue Map::getMapValue(long i) const {
  return toCellValue(getValue(i, &map_grid_), "map");
}

long Map::getNumCols() { return getGridNumCols(&map_grid_); }

long Map::getNumRows() { return getGridNumRows(&map_grid_); }

bool Map::getRFIDPosition(double &x, double &y, long i, long j) {
  return getPosition(x, y, i, j, &rfid_grid_);
}

bool Map::getRFIDIndex(double x, double y, long &i, long &j) {
  return getIndex(x, y, i, j, &rfid_grid_);
}

float Map::getRFIDGridValue(long i, long j) const {
  return getValue(i, j, &rfid_grid_);
}

float Map::getRFIDGridValue(geometry_msgs::PoseStamped ps) const {
  return getValue(ps, &rfid_grid_);
}

float Map::getRFIDGridValue(long i) const { return getValue(i, &rfid_grid_); }

long Map::getRFIDGridNumCols() { return getGridNumCols(&rfid_grid_); }

long Map::getRFIDGridNumRows() { return getGridNumRows(&map_grid_); }

// Generic (internal) getters ..............................................
bool Map::getIndex(double x, double y, long &i, long &j,
                   grid_map::GridMap *gm) {
  grid_map::Index resIndex;
  Position inPose(x, y);
  bool success;
  if (gm->isInside(inPose)) {
    gm->getIndex(inPose, resIndex);
    i = resIndex(0);
    j = resIndex(1);
    success = true;
  } else {
    printErrorReason(inPose, gm);
    i = -1;
    j = -1;
    success = false;
  }
  return success;
}

bool Map::getPosition(double &x, double &y, long i, long j,
                      grid_map::GridMap *gm) {
  grid_map::Index inIndex(i, j);
  Position inPose;
  bool success;
  if (gm->getPosition(inIndex, inPose)) {
    x = inPose.x();
    y = inPose.y();
    success = true;
  } else {
    ROS_ERROR("[1]Requested [%lu, %lu] index is outside grid boundaries: [0,0] "
              "- [%d, %d]",
              i, j, gm->getSize()(0) - 1, gm->getSize()(1) - 1);
    x = std::numeric_limits<double>::max();
    y = std::numeric_limits<double>::max();
    success = false;
  }
  return success;
}

float Map::getValue(long i, long j, const grid_map::GridMap *gm) const {
  float val;
  grid_map::Index ind(i, j);
  Position position;

  if (gm->getPosition(ind, position)) {
    val = gm->at("layer", ind);
  } else {
    ROS_ERROR("[2]Requested [%lu, %lu] index is outside grid boundaries: [0,0] "
              "- [%d, %d]",
              i, j, gm->getSize()(0) - 1, gm->getSize()(1) - 1);
    val = -1;
  }
  return val;
}

float Map::getValue(geometry_msgs::PoseStamped ps,
                    const grid_map::GridMap *gm) const {
  float val;
  if (ps.header.frame_id != gm->getFrameId()) {
    ROS_ERROR("Pose given in different frame id: grid==[%s], point ==[%s]",
              ps.header.frame_id.c_str(), gm->getFrameId().c_str());
    val = -1;
  } else {
    grid_map::Position point(ps.pose.position.x, ps.pose.position.y);

    if (gm->isInside(point)) {
      val = gm->atPosition("layer", point);
    } else {
      printErrorReason(point, gm);
      val = -1;
    }
  }
  return val;
}

float Map::getValue(long i, const grid_map::GridMap *gm) const {
  long rows = getGridNumRows(gm);
  std::ldiv_t result = std::div(i, rows);
  rows = result.quot;
  long cols = result.rem;

  return getValue(rows, cols, gm);
}

int Map::getGridNumCols(const grid_map::GridMap *gm) const {
  return gm->getSize()(1);
}

int Map::getGridNumRows(const grid_map::GridMap *gm) const {
  return gm->getSize()(0);
}
// E.O. Generic (internal) getters .........................................

// Generic (internal) setters ..............................................
void Map::setValue(float value, long i, long j, grid_map::GridMap *gm) {
  grid_map::Index ind(i, j);
  Position position;

  if (gm->getPosition(ind, position)) {
    //        cout << "Set value: " << value <<" at index: [" << i << "," << j
    //        <<"]" << endl;
    (gm->at("layer", ind)) = value;
  } else {
    ROS_ERROR("[3]Requested [%lu, %lu] index is outside grid boundaries: [0,0] "
              "- [%d, %d]",
              i, j, gm->getSize()(0) - 1, gm->getSize()(1) - 1);
  }
}

void Map::setValue(float value, geometry_msgs::PoseStamped ps,
                   grid_map::GridMap *gm) {
  if (ps.header.frame_id != gm->getFrameId()) {
    ROS_ERROR("Pose given in different frame id: grid==[%s], point ==[%s]",
              ps.header.frame_id.c_str(), gm->getFrameId().c_str());
  } else {
    grid_map::Position point(ps.pose.position.x, ps.pose.position.y);

    if (gm->isInside(point)) {
      gm->atPosition("layer", point) = value;
    } else {
      printErrorReason(point, gm);
    }
  }
}

void Map::setValue(float value, long i, grid_map::GridMap *gm) {
  long rows = getGridNumRows(gm);
  std::ldiv_t result = std::div(i, rows);
  rows = result.quot;
  long cols = result.rem;

  setValue((value), rows, cols, gm);
}

// E.O. Generic (internal) setters .........................................

void Map::setPathPlanningGridValue(Map::CellValue value, int i, int j) {
  setValue(toFloat(value), i, j, &planning_grid_);
}

void Map::setPathPlanningGridValue(Map::CellValue value,
                                   geometry_msgs::PoseStamped ps) {
  setValue(toFloat(value), ps, &planning_grid_);
}

void Map::setPathPlanningGridValue(Map::CellValue value, long i) {
  setValue(toFloat(value), i, &planning_grid_);
}

void Map::setGridValue(Map::CellValue value, long i, long j) {
  setValue(toFloat(value), i, j, &nav_grid_);
}

void Map::setGridValue(Map::CellValue value, geometry_msgs::PoseStamped ps) {
  setValue(toFloat(value), ps, &nav_grid_);
}

void Map::setGridValue(Map::CellValue value, long i) {
  setValue(toFloat(value), i, &nav_grid_);
}

void Map::setRFIDGridValue(float power, int i, int j) {
  setValue(getValue(i, j, &rfid_grid_) + power, i, j, &rfid_grid_);
}

void Map::setRFIDGridValue(float power, geometry_msgs::PoseStamped ps) {
  setValue(getValue(ps, &rfid_grid_) + power, ps, &rfid_grid_);
}

void Map::setRFIDGridValue(float power, long i) {
  setValue(getValue(i, &rfid_grid_) + power, i, &rfid_grid_);
}

void Map::createNewMap() {
  storeBinary("/tmp/test.pgm");
  storeAscii("/tmp/freeCell.txt");
}

void Map::storeBinary(std::string fileURI) {
  std::ofstream imgNew(fileURI.c_str(), ios::out);

  long columns = numGridCols;
  long rows = numGridRows;

  imgNew << "P2\n" << columns << " " << rows << "\n255\n";

  for (long row = 0; row < rows; ++row) {
    for (long col = 0; col < columns; ++col) {
      if (isGridValueFree(grid_map::Index(row, col))) {
        imgNew << 255 << " ";
      } else {
        imgNew << 0 << " ";
      }
    }
  }
  imgNew.close();
}

void Map::storeAscii(std::string fileURI) {
  std::ofstream txt(fileURI.c_str());
  long columns = numGridCols;
  long rows = numGridRows;

  for (long row = 0; row < rows; ++row) {
    for (long col = 0; col < columns; ++col) {
      if (isGridValueFree(grid_map::Index(row, col))) {
        txt << col << ": " << row << endl;
      }
    }
  }
  txt.close();
}

void Map::addEdgePoint(int x, int y) {
  std::pair<int, int> pair(x, y);
  edgePoints.push_back(pair);
  ROS_FATAL("[Map.cpp@addEdgePoint]: THIS METHOD SHOULD NEVER BE CALLED");
}

std::vector<vector<long> > Map::getMap2D() {
  vector<vector<long> > map2D;

  for (long gridRow = 0; gridRow < numGridRows; ++gridRow) {
    for (long gridCol = 0; gridCol < numGridCols; ++gridCol) {
      map2D[gridRow].at(gridCol) = getGridValue(gridRow, gridCol);
    }
  }

  return map2D;
}

Pose Map::getRobotPosition() { return currentPose; }

void Map::setCurrentPose(Pose &p) { currentPose = p; }

long Map::getTotalFreeCells() {
  long n_obsts = 0;
  long n_free = 0;
  long n_vist = 0;
  long n_others = 0;

  countCells(&n_obsts, &n_free, &n_vist, &n_others, &obst_cells, &free_cells, &vist_cells, &nav_grid_);

  return n_free;
}

void Map::decreaseFreeCells() { totalFreeCells--; }

void Map::drawVisitedCells() { storeBinary("/tmp/result.pgm"); }

void Map::printVisitedCells(vector<string> &history) {
  std::ofstream txt("/tmp/finalResult.txt");
  for (int i = 0; i < history.size(); i++) {
    string encoding = history.at(i);
    string s;
    stringstream ss;
    ss << s;
    char delimiter('/');
    string x, y, orientation, r, phi;
    std::string::size_type pos = encoding.find('/');
    x = encoding.substr(0, pos);
    int xValue = atoi(x.c_str());
    string newString = encoding.substr(pos + 1, encoding.size());
    std::string::size_type newPos = newString.find('/');
    y = newString.substr(0, newPos);
    int yValue = atoi(y.c_str());
    newString = newString.substr(newPos + 1, encoding.size());
    newPos = newString.find('/');
    orientation = newString.substr(0, newPos);
    int orientationValue = atoi(orientation.c_str());
    newString = newString.substr(newPos + 1, encoding.size());
    newPos = newString.find('/');
    r = newString.substr(0, newPos);
    int rValue = atoi(r.c_str());
    newString = newString.substr(newPos + 1, encoding.size());
    newPos = newString.find('/');
    phi = newString.substr(0, newPos);
    double phiValue = atoi(phi.c_str());

    txt << xValue << "  " << yValue << " " << orientationValue << " " << r
        << endl;
  }
}

void Map::drawRFIDScan() {
  std::ofstream resultMap("/tmp/rfid_result.pgm", ios::out);
  long columns = numPathPlanningGridCols;
  long rows = numPathPlanningGridRows;

  resultMap << "P2\n" << columns << " " << rows << "\n255\n";

  for (long row = 0; row < rows; ++row) {
    for (long col = 0; col < columns; ++col) {
      int value = getRFIDGridValue(row, col);
      value = std::min(value, 255);
      value = std::max(value, 0);
      resultMap << value << " ";
    }
  }
  resultMap.close();
}

void Map::drawRFIDGridScan(RFIDGridmap grid) {
  std::ofstream resultMap("/tmp/rfid_result_gridmap.pgm", ios::out);
  long columns = numPathPlanningGridCols;
  long rows = numPathPlanningGridRows;

  resultMap << "P2\n" << columns << " " << rows << "\n255\n";

  for (long row = 0; row < rows; ++row) {
    for (long col = 0; col < columns; ++col) {
      int value = static_cast<int>(grid.getCell(row, col));
      //     cout << value << endl;
      value = std::min(value, 255);
      value = std::max(value, 0);
      resultMap << value << " ";
    }
  }

  resultMap.close();
}

std::pair<int, int> Map::getRelativeTagCoord(int absTagX, int absTagY,
                                             int antennaX, int antennaY) {
  std::pair<int, int> relTagCoord;
  relTagCoord.first = std::abs(absTagX - antennaX);
  relTagCoord.second = std::abs(absTagY - antennaY);
  return relTagCoord;
}

std::pair<int, int> Map::findTag() {
  std::pair<int, int> tag(0, 0);
  double powerRead = 0;
  int numRFIDRows = rfid_grid_.getSize()(0);
  int numRFIDCols = rfid_grid_.getSize()(1);

  for (int row = 0; row < numRFIDRows; row++) {
    for (int col = 0; col < numRFIDCols; col++) {
      if (getRFIDGridValue(row, col) > powerRead) {
        powerRead = getRFIDGridValue(row, col);
        tag.first = row;
        tag.second = col;
      }
    }
  }
  return tag;
}

std::pair<int, int> Map::findTagfromGridMap(RFIDGridmap grid) {
  std::pair<int, int> tag(0, 0);
  double powerRead = 0;
  int numRFIDRows = grid.getNumRows();
  int numRFIDCols = grid.getNumCols();

  for (int row = 0; row < numRFIDRows; row++) {
    for (int col = 0; col < numRFIDCols; col++) {
      if (grid.getCell(row, col) > powerRead) {
        powerRead = getRFIDGridValue(row, col);
        //        cout << "Value read: " << powerRead << endl;
        tag.first = row;
        tag.second = col;
      }
    }
  }
  return tag;
}

void Map::printErrorReason(grid_map::Position point,
                           const grid_map::GridMap *gm) const {
  int maxRow = gm->getSize()(0) - 1;
  int maxCol = gm->getSize()(1) - 1;
  grid_map::Index topLeftI(0, 0);
  grid_map::Index topRightI(0, maxCol);
  grid_map::Index bottomRightI(maxRow, maxCol);
  grid_map::Index bottomLeftI(maxRow, 0);

  grid_map::Position topLeftP, topRightP, bottomRightP, bottomLeftP;
  gm->getPosition(topLeftI, topLeftP);
  gm->getPosition(topRightI, topRightP);
  gm->getPosition(bottomRightI, bottomRightP);
  gm->getPosition(bottomLeftI, bottomLeftP);
  ROS_ERROR("Point [%3.3f, %3.3f] is outside grid boundaries: grid: [%3.3f, "
            "%3.3f], [%3.3f, %3.3f], [%3.3f, %3.3f], [%3.3f, %3.3f]",
            point.x(), point.y(), topLeftP.x(), topLeftP.y(), topRightP.x(),
            topRightP.y(), bottomRightP.x(), bottomRightP.y(), bottomLeftP.x(),
            bottomLeftP.y());
}

string Map::type2str(int type) {
  string r;

  uchar depth = type & CV_MAT_DEPTH_MASK;
  uchar chans = 1 + (type >> CV_CN_SHIFT);

  switch (depth) {
  case CV_8U:
    r = "8U";
    break;
  case CV_8S:
    r = "8S";
    break;
  case CV_16U:
    r = "16U";
    break;
  case CV_16S:
    r = "16S";
    break;
  case CV_32S:
    r = "32S";
    break;
  case CV_32F:
    r = "32F";
    break;
  case CV_64F:
    r = "64F";
    break;
  default:
    r = "User";
    break;
  }

  r += "C";
  r += (chans + '0');

  return r;
}

void Map::printGridData(std::string grid_name, const grid_map::GridMap *gm) {
  long n_obsts = 0;
  long n_free = 0;
  long n_others = 0;
  long n_vist = 0;
  long total = 0;

  bool doIt=false;
  if (doIt)
  {
      int nRow = gm->getSize()(0);
      int nCol = gm->getSize()(1);
      grid_map::Index topLeftI(0, 0);
      grid_map::Index topRightI(0, nCol - 1);
      grid_map::Index bottomRightI(nRow - 1, nCol - 1);
      grid_map::Index bottomLeftI(nRow - 1, 0);

      grid_map::Position topLeftP, topRightP, bottomRightP, bottomLeftP;
      gm->getPosition(topLeftI, topLeftP);
      gm->getPosition(topRightI, topRightP);
      gm->getPosition(bottomRightI, bottomRightP);
      gm->getPosition(bottomLeftI, bottomLeftP);

      printf("[Map.cpp@printGridData] .........................................\n");
      printf("[Map.cpp@printGridData] Grid Name: [%s]\n", grid_name.c_str());
      printf("[Map.cpp@printGridData] Num Cols, Num Rows [%d,  %d]\n", nCol, nRow);
      printf("[Map.cpp@printGridData] Frame id [%s]\n", gm->getFrameId().c_str());
      printf("[Map.cpp@printGridData] Grid Center [%3.3f %3.3f] m. \n",
                gm->getPosition().x(), gm->getPosition().y());
      printf("[Map.cpp@printGridData] Resolution [%3.3f] m./cell \n",
                gm->getResolution());
      printf("[Map.cpp@printGridData] Map Size [%3.3f x %3.3f] m.\n",
                gm->getLength()(0), gm->getLength()(1));
      printf("[Map.cpp@printGridData] Bounding box positions (m.):\n");
      printf(" \t\t topLeft cell: [%d, %d] == [%3.3f, %3.3f] m.\n", topLeftI.x(),
                topLeftI.y(), topLeftP.x(), topLeftP.y());
      printf(" \t\t topRight cell: [%d, %d] == [%3.3f, %3.3f] m.\n", topRightI.x(),
                topRightI.y(), topRightP.x(), topRightP.y());
      printf(" \t\t bottomRight cell: [%d, %d] == [%3.3f, %3.3f] m.\n",
                bottomRightI.x(), bottomRightI.y(), bottomRightP.x(),
                bottomRightP.y());
      printf(" \t\t bottomLeft cell: [%d, %d] == [%3.3f, %3.3f] m.\n",
                bottomLeftI.x(), bottomLeftI.y(), bottomLeftP.x(), bottomLeftP.y());

      countCells(&n_obsts, &n_free, &n_vist, &n_others, &obst_cells, &free_cells, &vist_cells,gm);
      total = n_obsts + n_free + n_vist + n_others;
      printf("[Map.cpp@printGridData] Grid has (%d) %3.3f %% of free cells, (%d) %3.3f %% "
                "of occupied cells and (%d) %3.3f %% of visited cells\n",
                n_free, 100.0 * n_free / (1.0 * total), n_obsts, 100.0 * n_obsts / (1.0 * total),
                n_vist, 100.0 * n_vist / (1.0 * total));
      if (n_others)
        printf("[Map.cpp@printGridData] Found %3.3f %% undefined cells\n",
                  100.0 * n_others / (1.0 * total));

      printf("[Map.cpp@printGridData] .........................................\n");
    }
  // .............................................................
}

void Map::countGridCells(long *n_obsts, long *n_free, long *n_vist,
                         long *n_others) {
  countCells(n_obsts, n_free, n_vist, n_others, &obst_cells, &free_cells, &vist_cells,&nav_grid_);
}

void Map::countPathPlanningCells(long *n_obsts, long *n_free, long *n_vist,
                                 long *n_others) {
  countCells(n_obsts, n_free, n_vist, n_others, &obst_cells, &free_cells, &vist_cells, &planning_grid_);
}

void Map::countCells(long *n_obsts, long *n_free, long *n_vist, long *n_others,
                     vector<grid_map::Index> *obst, vector<grid_map::Index> *free, vector<grid_map::Index> *vist,
                     const grid_map::GridMap *gm) {
  *n_obsts = 0;
  *n_free = 0;
  *n_others = 0;
  *n_vist = 0;
  obst->clear();
  free->clear();
  vist->clear();
  for (grid_map::GridMapIterator iterator(*gm); !iterator.isPastEnd();
       ++iterator) {
    if (gm->at("layer", *iterator) == Map::CellValue::OBST) {
      (*n_obsts)++;
      obst->push_back(*iterator);
    } else if (gm->at("layer", *iterator) == Map::CellValue::FREE) {
      (*n_free)++;
      free->push_back(*iterator);
    } else if (gm->at("layer", *iterator) == Map::CellValue::VIST) {
      (*n_vist)++;
      vist->push_back(*iterator);
    } else {
      (*n_others)++;
    }
  }
}

grid_map::Position Map::getRandomFreeCellPosition(){
  long tot_free_cells = free_cells.size();
  std::random_device rd;     // only used once to initialise (seed) engine
  std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
  std::uniform_int_distribution<int> uni(0, tot_free_cells-1); // guaranteed unbiased

  auto random_integer = uni(rng);
  grid_map::Index random_cell = free_cells[random_integer];
  grid_map::Position random_cell_pp;
  planning_grid_.getPosition(random_cell, random_cell_pp);
  return random_cell_pp;

}

void Map::plotMyGrid(std::string fileURI, const grid_map::GridMap *gm) {

  cv::Mat imageCV;
  double minValue = 1e20;
  double maxValue = -1e20;
  sensor_msgs::ImagePtr imageROS;
  cv_bridge::CvImagePtr cv_ptr;

  for (grid_map::GridMapIterator iterator(*gm); !iterator.isPastEnd();
       ++iterator) {

    if (gm->at("layer", *iterator) > maxValue)
      maxValue = gm->at("layer", *iterator);
    if (gm->at("layer", *iterator) < minValue)
      minValue = gm->at("layer", *iterator);
  }
  ROS_DEBUG("Ranges [%3.3f, %3.3f]", minValue, maxValue);
  GridMapCvConverter::toImage<unsigned short, 1>(*gm, "layer", CV_16UC1,
                                                 minValue, maxValue, imageCV);
  cv::imwrite(fileURI.c_str(), imageCV);
}

/*
int Map::getPathPlanningNumRows() const
{
    return getGridNumRows(&planning_grid_);
}

bool  Map::getGridPosition(double &x, double &y, long i, long j)
{
  return getPosition(x, y,  i, j, &nav_grid_);
}
 */
void Map::plotPathPlanningGridColor(std::string fileURI) {
  plotMyGridColor(fileURI, &planning_grid_);
}

void Map::plotGridColor(std::string fileURI) {
  plotMyGridColor(fileURI, &nav_grid_);
}

void Map::plotMyGridColor(std::string fileURI, const grid_map::GridMap *gm) {
  // "channels" is a vector of 3 Mat arrays:
  vector<cv::Mat> channels(3);

  grid_map::GridMap tempGrid;

  cv::Mat imageCV, color_imageCV, exploredCV;

  sensor_msgs::ImagePtr imageROS;

  tempGrid = *gm;
  tempGrid.add("obstacles", 0.0);
  tempGrid.add("explored", 0.0);

  // get values:
  for (grid_map::GridMapIterator iterator(tempGrid); !iterator.isPastEnd();
       ++iterator) {
    if (tempGrid.at("layer", *iterator) == Map::CellValue::OBST)
      tempGrid.at("obstacles", *iterator) = 1;
    if (tempGrid.at("layer", *iterator) == Map::CellValue::VIST)
      tempGrid.at("explored", *iterator) = 1;
  }

  // we cast obstacles to a grayscale image (hopefully, binary)
  GridMapCvConverter::toImage<unsigned short, 1>(tempGrid, "obstacles",
                                                 CV_16UC1, 0, 1, imageCV);

  // we cast explored to a grayscale image (hopefully, binary)
  GridMapCvConverter::toImage<unsigned short, 1>(tempGrid, "explored", CV_16UC1,
                                                 0, 1, exploredCV);

  // change grayscale representation into color representation.
  cv::cvtColor(imageCV, color_imageCV, cv::COLOR_GRAY2BGR);
  // split it into channels
  cv::split(color_imageCV, channels);

  // add the exploredCV image as channel into the color image
  // BGR order in OpenCV)
  channels[2] = channels[2] + exploredCV;

  // then merge them back
  cv::merge(channels, color_imageCV);

  cv::imwrite(fileURI.c_str(), color_imageCV);
}

int Map::isCandidate(long i, long j) { return isCandidate_inner(i, j, 1); }

int Map::planning_iterate_func(grid_map::SubmapIterator planning_iterator) {
  int candidate = 0;
  for (; !planning_iterator.isPastEnd(); ++planning_iterator) {
    if (isPathPlanningGridValueFree(*planning_iterator)) {
//    if (isPathPlanningGridValueVist(*planning_iterator)) {
      candidate = 1;
      break;
    }
  }
//  cout <<"Iterator = (" << (*planning_iterator)(0) << "," << (*planning_iterator)(1) << ") ==>" << candidate << endl;
  return candidate;
}

int Map::isCandidate2(long i, long j) { return isCandidate_inner(i, j, 2); }

int Map::nav_iterate_func(grid_map::SubmapIterator iterator) {
  int candidate = 0;
  grid_map::Position position_pp;
  grid_map::Index navStartIndex;
  grid_map::Size navBufferSize;
  double nav_cells_per_plan_cell;

  for (; !iterator.isPastEnd(); ++iterator) {

    nav_cells_per_plan_cell =
        planning_grid_.getResolution() / nav_grid_.getResolution();
    navBufferSize = Size(nav_cells_per_plan_cell, nav_cells_per_plan_cell);

    // get position inside planning grid
    planning_grid_.getPosition(*iterator, position_pp);
    // obtain the position of the upper-left  navigation cell INSIDE current
    // planning cell
    position_pp.x() = position_pp.x() - nav_cells_per_plan_cell / 2;
    position_pp.y() = position_pp.y() - nav_cells_per_plan_cell / 2;

    // and corresponding index in nav_grid_
    nav_grid_.getIndex(position_pp, navStartIndex);

    for (grid_map::SubmapIterator nav_iterator(nav_grid_, navStartIndex,
                                               navBufferSize);
         !nav_iterator.isPastEnd(); ++nav_iterator) {
      if (isGridValueFree(*nav_iterator)) {
        candidate = 1;
        break;
      }
    }
    if (candidate == 1)
      break;
  }
  return candidate;
}

int Map::isCandidate_inner(long i, long j, int mode) {
  grid_map::Position position_pp;
  int candidate = 0;
  grid_map::Size mapSize = planning_grid_.getSize();

  grid_map::Index submapStartIndex(std::max(i - 1, (long)0),
                                   std::max(j - 1, (long)0));
  grid_map::Index submapEndIndex(std::min(i + 1, (long)(mapSize(0) - 1)),
                                 std::min(j + 1, (long)(mapSize(1) - 1)));
  grid_map::Index submapBufferSize = submapEndIndex - submapStartIndex;

  if (!planning_grid_.getPosition(grid_map::Index(i, j), position_pp)) {
    ROS_ERROR("[4]Requested [%lu, %lu] index is outside path planning grid "
              "boundaries: [0,0] - [%d, %d]",
              i, j, mapSize(0) - 1, mapSize(1) - 1);
    candidate = 0;
  } else {
    grid_map::SubmapIterator iterator(planning_grid_, submapStartIndex,
                                      submapBufferSize);
    ROS_DEBUG("Iterating over a submap size [%d x %d]]",
              iterator.getSubmapSize()(0), iterator.getSubmapSize()(1));

    if (mode == 1) {
      candidate = planning_iterate_func(iterator);
    } else {
      candidate = nav_iterate_func(iterator);
    }
  }
  return candidate;
}

void Map::findCandidatePositions(double pos_X_m, double pos_Y_m,
                                 double heading_rad, double FOV_rad,
                                 double range_m) {
  findCandidatePositions_inner(1, pos_X_m, pos_Y_m, heading_rad, FOV_rad,
                               range_m);
}

void Map::findCandidatePositions2(double pos_X_m, double pos_Y_m,
                                  double heading_rad, double FOV_rad,
                                  double range_m) {
  findCandidatePositions_inner(2, pos_X_m, pos_Y_m, heading_rad, FOV_rad,
                               range_m);
}

void Map::findCandidatePositions_inner(int mode, double pos_X_m, double pos_Y_m,
                                       double heading_rad, double FOV_rad,
                                       double range_m) {
  //        cout << "[map.cpp@findCandidatePositions] LET'S GOOO!" << endl;
  grid_map::Index candidateIndex, rayIndex;
  grid_map::Position candidatePos, rayPos;
  double rel_y, rel_x, distance, rel_a;
  grid_map::Position centerPos(pos_X_m, pos_Y_m);
  bool hit;
  bool isOk;
  // Iterate cells over a circle with radius range, center pos in path
  // planning grid and FOV angle
  for (grid_map::CircleIterator iterator(planning_grid_, centerPos, range_m);
       !iterator.isPastEnd(); ++iterator) {
    planning_grid_.getPosition(*iterator, candidatePos);
    // cout << endl << "[map.cpp@findCandidatePosition_inner]
    // Position = " << candidatePos.x() << "," <<
    // candidatePos.y() << endl;

    // relative cell position respect to center
    rel_x = candidatePos.x() - pos_X_m;
    rel_y = candidatePos.y() - pos_Y_m;
    // distance respect to the center
    distance = sqrt(pow(rel_x, 2) + pow(rel_y, 2));
    //    cout << "[map.cpp@findCandidatePosition_inner] distance: " << distance << endl;

    // angle between heading and cell position
    rel_a = std::atan2(rel_y, rel_x) - heading_rad;
    rel_a = constrainAnglePI(rel_a);
    //                    cout << "[map.cpp@findCandidatePosition_inner] rel_a =
    //                    " << rel_a << ", FOV_rad/2 = " << FOV_rad/2 << endl;

    // cell is inside circle AND arc
    if (std::abs(rel_a) <= (FOV_rad / 2)){// and distance >= 0.8*range_m) {
      // cell is candidate
//      cout << "[map.cpp@findCandidatePosition_inner]Cell is candidate" << endl;
      candidateIndex = (*iterator);
//      cout << "\n" << endl;
      // A cell is candidate if FREE and on the edge between scanned and unscanned area
      if (mode == 1)
        isOk = (isCandidate(candidateIndex(0), candidateIndex(1)) == 1);
      if (mode == 2)
        isOk = (isCandidate2(candidateIndex(0), candidateIndex(1)) == 1);
//      cout << " ("<< candidatePos(0) << "," << candidatePos(1) << ") is a candidate cell: " << isOk << endl;
      isOk = !containsNavObstacles(candidatePos);
      if (isOk) {
//        cout << "[map.cpp@findCandidatePosition_inner] is OK" << endl;
        hit = false;
        // trace a ray between that cell and robot cell:
        for (grid_map::LineIterator lin_iterator(planning_grid_, candidatePos, centerPos);
             !lin_iterator.isPastEnd(); ++lin_iterator) {

          planning_grid_.getPosition(*lin_iterator, rayPos);
//          cout <<"[map.cpp@findCandidatePositions]Pos: " << rayPos(0) << "," << rayPos(1) << endl;
          rayIndex = (*lin_iterator);
          // if an obstacle is found, end
          if (isPathPlanningGridValueObst(rayIndex)) {
            hit = true;
//            printf("[map.cpp@findCandidatePositions] HIT! cell [%d, %d]- "
//                      "[%3.3f m., %3.3f m.] -  Hit point: [%d, %d]- [%3.3f m., "
//                      "%3.3f m.]\n",
//                      candidateIndex(0), candidateIndex(1), candidatePos(0),
//                      candidatePos(1), rayIndex(0), rayIndex(1), rayPos(0),
//                      rayPos(1));
            //                                  cout << "HIT! cell" << endl;
            break;
          }
        }
//        cout << "[map.cpp@findCandidatePositions] [1]hit = " << hit << endl;
//        if (hit == false)
//        {
//          // additional check: the candidate position dmust not contain obstacles
////          hit = containsNavObstacles(candidatePos);
//        }
//        cout << "[map.cpp@findCandidatePositions] [2]hit = " << hit << endl;
        //                          cout << "Going to be scanned" << endl;
        // if we reach robot pose without obstacles: add cell it to pair list
        if (hit == false) {
          //                            double tmp_x, tmp_y;
          //                            getPathPlanningPosition(tmp_x, tmp_y,
          //                            rayIndex(0), rayIndex(1));
          std::pair<float, float> temp =
              std::make_pair(candidatePos(0), candidatePos(1));
          edgePoints.push_back(temp);
//          printf("[map.cpp@findCandidatePositions] Cell scanned: [%d, %d]- "
//                    "[%3.3f m., %3.3f m.] ",
//                    candidateIndex(0), candidateIndex(1), candidatePos(0),
//                    candidatePos(1));
          //                            cout <<"[map.cpp@findCandidatePositions]
          //                            Cell scanned: " << candidatePos(0) <<
          //                            "," << candidatePos(1) << endl;
        }
      }
    }
  }
//          cout << "[map.cpp@findCandidatePositions] CandidatePosition found: " << edgePoints.size() << endl;
}

void Map::findFrontierPosition() {
  std::vector<std::pair<float, float>> frontiers;
  // cout << "[map.cpp@findFrontierPosition] candidatePosition: " << edgePoints.size() << endl;
  long i, j;
  grid_map::Index index;
  grid_map::Position position;
  int up, left, right, down;
  int increment = 2;
  // for each candidate position already identified (edgepoints)
  for (auto it = edgePoints.begin(); it!= edgePoints.end(); it++){
    position(0) = it->first;
    position(1) = it->second;
    planning_grid_.getIndex(position, index );
//    cout << index(0) << " " << index(1) << endl;

    right = getPathPlanningGridValue(index(0) + increment, index(1));
    up    = getPathPlanningGridValue(index(0)            , index(1) + increment);
    left  = getPathPlanningGridValue(index(0) - increment, index(1));
    down  = getPathPlanningGridValue(index(0)            , index(1) - increment);

    // find the 4-connected neighbour and verifies they are not obstacles or already scanned
    if (up != Map::CellValue::OBST and down != Map::CellValue::OBST and left != Map::CellValue::OBST and right != Map::CellValue::OBST){
      if (up == Map::CellValue::FREE or down == Map::CellValue::FREE or left == Map::CellValue::FREE or right == Map::CellValue::FREE){
//        cout << "up:    "    << up    << endl;
//        cout << "down:  "  << down  << endl;
//        cout << "left:  "  << left  << endl;
//        cout << "right: " << right << endl;
//    if (getPathPlanningGridValue(index(0) + 1, index(1)) == Map::CellValue::FREE or
//        getPathPlanningGridValue(index(0) + 1, index(1)) != Map::CellValue::OBST){
//      if (getPathPlanningGridValue(index(0), index(1) + 1) == Map::CellValue::FREE or
//          getPathPlanningGridValue(index(0), index(1) + 1) != Map::CellValue::OBST) {
//        if (getPathPlanningGridValue(index(0) - 1, index(1)) == Map::CellValue::FREE or
//            getPathPlanningGridValue(index(0) - 1, index(1)) != Map::CellValue::OBST){
//          if (getPathPlanningGridValue(index(0), index(1) - 1) == Map::CellValue::FREE or
//              getPathPlanningGridValue(index(0), index(1) - 1) != Map::CellValue::OBST){
//    if (getPathPlanningGridValue(index(0) + 1, index(1)) != Map::CellValue::OBST){
//      if (getPathPlanningGridValue(index(0), index(1) + 1) != Map::CellValue::OBST) {
//        if (getPathPlanningGridValue(index(0) - 1, index(1)) != Map::CellValue::OBST){
//          if (getPathPlanningGridValue(index(0), index(1) - 1) != Map::CellValue::OBST){
            frontiers.push_back(*it);
//          }
//        }
      }
    }
  }
  // Update the frontiers list
  edgePoints = frontiers;
  // cout << "[map.cpp@findFrontierPosition] frontiers: " << edgePoints.size() << endl;
}

vector<std::pair<float, float> > Map::getCandidatePositions() {
  return edgePoints;
}

void Map::emptyCandidatePositions() { edgePoints.clear(); }

std::pair<double, double> Map::getSensingTime(double pos_X_m, double pos_Y_m,
                                              double heading_rad,
                                              double FOV_rad, double range_m) {
  grid_map::Index candidateIndex, rayIndex;
  grid_map::Position candidatePos, rayPos;
  double rel_y, rel_x, rel_a;
  grid_map::Position centerPos(pos_X_m, pos_Y_m);
  bool hit;
  double minFOV;
  double maxFOV;

  // max field of view is limited by input ...
  minFOV = -FOV_rad / 2;
  maxFOV = FOV_rad / 2;

  // Iterate cells over a circle ARC with radius range, center pos in nav
  // planning grid and FOV angle
  for (grid_map::CircleIterator nav_iterator(nav_grid_, centerPos, range_m);
       !nav_iterator.isPastEnd(); ++nav_iterator) {

    // If cell is empty
    if (isGridValueFree(*nav_iterator)) {
      nav_grid_.getPosition(*nav_iterator, candidatePos);

      // relative cell position respect to center
      rel_x = candidatePos.x() - pos_X_m;
      rel_y = candidatePos.y() - pos_Y_m;

      // angle between heading and cell position
      rel_a = std::atan2(rel_y, rel_x) - heading_rad;
      rel_a = constrainAnglePI(rel_a);

      // cell is also inside FOV arc
      if (std::abs(rel_a) <= (FOV_rad / 2)) {
        candidateIndex = (*nav_iterator);
        hit = false;
        // trace a ray betwen that cell and robot cell:
        for (grid_map::LineIterator lin_iterator(nav_grid_, candidatePos,
                                                 centerPos);
             !lin_iterator.isPastEnd(); ++lin_iterator) {

          nav_grid_.getPosition(*lin_iterator, rayPos);
          rayIndex = (*lin_iterator);
          // if an obstacle is found, end
          if (isGridValueObst(*nav_iterator)) {
            hit = true;
            // ROS_DEBUG("[map.cpp@getSensingTime] HIT! cell [%d, %d]- [%3.3f
            // m., %3.3f m.] -  Hit point: [%d, %d]- [%3.3f m., %3.3f m.]",
            //            candidateIndex(0),candidateIndex(1),candidatePos(0),candidatePos(1),rayIndex(0),rayIndex(1),rayPos(0),rayPos(1)
            //            );
            break;
          }
        }

        // if we reach robot pose without obstacles: add cell it to pair list
        if (!hit) {
          if (rel_a < minFOV)
            minFOV = rel_a;
          if (rel_a > maxFOV)
            maxFOV = rel_a;

          ROS_DEBUG("[map.cpp@getSensingTime] Cell scanned: [%d, %d]- [%3.3f "
                    "m., %3.3f m.] ",
                    rayIndex(0), rayIndex(1), rayPos(0), rayPos(1));
        }
      }
    }
  }

  std::pair<double, double> angles;
  angles.first = minFOV;
  angles.second = maxFOV;
  return angles;
}

int Map::performSensingOperation(double pos_X_m, double pos_Y_m,
                                 double heading_rad, double FOV_rad,
                                 double range_m, double minAngle_rad,
                                 double maxAngle_rad) {

  int modifiedCells = 0;
  ROS_DEBUG("[map.cpp@performSensingOperation]");

  grid_map::Index candidateIndex, rayIndex;
  grid_map::Position candidatePos, rayPos;
  double rel_y, rel_x, rel_a;
  grid_map::Position centerPos(pos_X_m, pos_Y_m);
  bool hit;

  // Iterate cells over a circle ARC with radius range, center pos in nav
  // planning grid and FOV angle
  for (grid_map::CircleIterator nav_iterator(nav_grid_, centerPos, range_m);
       !nav_iterator.isPastEnd(); ++nav_iterator) {
    // cout << "[map.cpp@performSensingOperation] [i, j] : [" <<
    // (*nav_iterator)(0) << "," << (*nav_iterator)(1) << "] = " <<
    // getGridValue((*nav_iterator)(0), (*nav_iterator)(1)) << endl;
    // If cell is empty
    if (isGridValueFree(*nav_iterator)) {

      //                      cout << "[map.cpp@performSensingOperation] Cell is
      //                      free" << endl;
      nav_grid_.getPosition(*nav_iterator, candidatePos);

      // relative cell position respect to center
      rel_x = candidatePos.x() - pos_X_m;
      rel_y = candidatePos.y() - pos_Y_m;

      // angle between heading and cell position
      rel_a = std::atan2(rel_y, rel_x) - heading_rad;
      rel_a = constrainAnglePI(rel_a);
      //                      cout << "   [map.cpp@performSensingOperation]
      //                      rel_a" << rel_a << endl;
      // cell is also inside FOV arc
      if ((rel_a <= maxAngle_rad) & (rel_a >= minAngle_rad)) {
        candidateIndex = (*nav_iterator);
        hit = false;
        // trace a ray between that cell and robot cell:
        for (grid_map::LineIterator lin_iterator(nav_grid_, centerPos,
                                                 candidatePos);
             !lin_iterator.isPastEnd(); ++lin_iterator) {
          //                        cout << "CenterPos: " << centerPos.x() <<
          //                        "," << centerPos.y() << endl;
          //                        cout << "CandidatePos: " << candidatePos.x()
          //                        << "," << candidatePos.y() << endl;
          //                                cout <<
          //                                "[map.cpp@performSensingOperation]
          //                                line element [i, j] : [" <<
          //                                (*lin_iterator)(0) << "," <<
          //                                (*lin_iterator)(1) << "] = " <<
          //                                getGridValue((*lin_iterator)(0),
          //                                (*lin_iterator)(1)) << endl;

          nav_grid_.getPosition(*lin_iterator, rayPos);
          rayIndex = (*lin_iterator);
          // if an obstacle is found, end
          if (isGridValueObst(*lin_iterator)) {
            hit = true;
            //                                      ROS_DEBUG("[map.cpp@performSensingOperation]
            //                                      HIT! cell [%d, %d]- [%3.3f
            //                                      m., %3.3f m.] -  Hit point:
            //                                      [%d, %d]- [%3.3f m., %3.3f
            //                                      m.]",
            //                                                  candidateIndex(0),candidateIndex(1),candidatePos(0),candidatePos(1),rayIndex(0),rayIndex(1),rayPos(0),rayPos(1)
            //                                                  );
            // cout << "[map.cpp@performSensingOperation] HIT! cell [ " <<
            // candidateIndex(0) << "," << candidateIndex(1) << "] - [" <<
            //    candidatePos(0) <<"," << candidatePos(1) << " -  Hit point: [
            //    "<< rayIndex(0) << "," << rayIndex(1) << "] - [" << rayPos(0)
            //    <<"," << rayPos(1) << "]" << endl;
            break;
          }

          if (!hit) {
            // Update the cells and the counter only if the cells is free and
            // not already visited
            if (isGridValueFree(*lin_iterator)) {
              setGridValue(Map::CellValue::VIST, (*lin_iterator)(0),
                           (*lin_iterator)(1));
              modifiedCells++;
            }

            //                                ROS_DEBUG("[map.cpp@performSensingOperation]
            //                                Cell scanned: [%d, %d]- [%3.3f m.,
            //                                %3.3f m.] ",
            //                                          rayIndex(0),rayIndex(1),rayPos(0),rayPos(1)
            //                                          );
            // cout << "[map.cpp@performSensingOperation] Cell scanned:  [ " <<
            // rayIndex(0) << "," << rayIndex(1) << "] - [" << rayPos(0) << ","
            // << rayPos(1) << "]" << endl;
          }
        }

        // if the free cell is reached, set its value to 2 and stop the ray
        //                    if (!hit) {
        //                        setGridValue(Map::CellValue::VIST,
        //                        (*nav_iterator)(0), (*nav_iterator)(1));
        //                        modifiedCells++;
        ////
        ///ROS_DEBUG("[map.cpp@performSensingOperation] Cell scanned: [%d, %d]-
        ///[%3.3f m., %3.3f m.] ",
        //// rayIndex(0),rayIndex(1),rayPos(0),rayPos(1)  );
        //                        //cout << "[map.cpp@performSensingOperation]
        //                        Cell scanned:  [ " << rayIndex(0) << "," <<
        //                        rayIndex(1) << "] - [" << rayPos(0) << "," <<
        //                        rayPos(1) << "]" << endl;
        //                    }
      }
    }
  }
  // cout << "[map.cpp@performSensingOperation] Totals cells in nav_grid
  // modified: " << modifiedCells << endl;
  return modifiedCells;
}


int Map::performSensingOperationEllipse(double pos_X_m, double pos_Y_m,
                              double heading_rad, double FOV_rad,
                              double range_m, double minAngle_rad,
                              double maxAngle_rad, long a_pcell, long b_pcell){

int modifiedCells = 0;
  ROS_DEBUG("[map.cpp@performSensingOperation]");

  grid_map::Index candidateIndex, rayIndex;
  grid_map::Position candidatePos, rayPos;
  double rel_y, rel_x, rel_a;
  bool hit;

  if (a_pcell < b_pcell) {
    long tmp;
    tmp = a_pcell;
    a_pcell = b_pcell;
    b_pcell = tmp;
  }
  // The robot is located in one of the focal point of the ellipse
  // We need to calculate the centre of the ellipse starting from the focal point
  long c_pcell = sqrt((a_pcell * a_pcell) - (b_pcell * b_pcell));
  grid_map::Position centerPos(pos_X_m + (c_pcell*cos(heading_rad)),
                               pos_Y_m + (c_pcell*sin(heading_rad)));
  
  
  Length length(2*a_pcell, 2*b_pcell);

  // Iterate cells over a circle ARC with radius range, center pos in nav
  // planning grid and FOV angle
  for (grid_map::EllipseIterator nav_iterator(nav_grid_, centerPos, length, heading_rad);
       !nav_iterator.isPastEnd(); ++nav_iterator) {
    
    // If cell is empty
    if (isGridValueFree(*nav_iterator)) {

      //                      cout << "[map.cpp@performSensingOperation] Cell is
      //                      free" << endl;
      nav_grid_.getPosition(*nav_iterator, candidatePos);

      // relative cell position respect to center
      // rel_x = candidatePos.x() - pos_X_m;
      // rel_y = candidatePos.y() - pos_Y_m;

      // // angle between heading and cell position
      // rel_a = std::atan2(rel_y, rel_x) - heading_rad;
      // rel_a = constrainAnglePI(rel_a);
      
      // cell is also inside FOV arc
      // if ((rel_a <= maxAngle_rad) & (rel_a >= minAngle_rad)) {
        candidateIndex = (*nav_iterator);
        hit = false;
        // trace a ray between that cell and robot cell:
        for (grid_map::LineIterator lin_iterator(nav_grid_, centerPos,
                                                 candidatePos);
             !lin_iterator.isPastEnd(); ++lin_iterator) {

          nav_grid_.getPosition(*lin_iterator, rayPos);
          rayIndex = (*lin_iterator);
          // if an obstacle is found, end
          if (isGridValueObst(*lin_iterator)) {
            hit = true;
           
            break;
          }

          if (!hit) {
            // Update the cells and the counter only if the cells is free and
            // not already visited
            if (isGridValueFree(*lin_iterator)) {
              setGridValue(Map::CellValue::VIST, (*lin_iterator)(0),
                           (*lin_iterator)(1));
              modifiedCells++;
            }

          }
        }
      // }
    }
  }
  return modifiedCells;
}

int Map::getInformationGain(double pos_X_m, double pos_Y_m, double heading_rad,
                            double FOV_rad, double range_m) {
  int freeCells = 0;
  ROS_DEBUG("[map.cpp@getInformationGain]");

  grid_map::Index candidateIndex, rayIndex;
  grid_map::Position candidatePos, rayPos;
  double rel_y, rel_x, rel_a;
  grid_map::Position centerPos(pos_X_m, pos_Y_m);
  bool hit;
  //        cout << "[map.cpp@getInformationGain]Robot in (" << pos_X_m << ","
  //        << pos_Y_m << "), with orientation: " << heading_rad << endl;
  // Iterate cells over a circle ARC with radius range, center pos in nav
  // planning grid and FOV angle
  for (grid_map::CircleIterator nav_iterator(nav_grid_, centerPos, range_m);
       !nav_iterator.isPastEnd(); ++nav_iterator) {

    // If cell is empty
    if (isGridValueFree(*nav_iterator)) {
      nav_grid_.getPosition(*nav_iterator, candidatePos);

      // relative cell position respect to center
      rel_x = candidatePos.x() - pos_X_m;
      rel_y = candidatePos.y() - pos_Y_m;

      // angle between heading and cell position
      rel_a = std::atan2(rel_y, rel_x) - heading_rad;
      rel_a = constrainAnglePI(rel_a);

      // cell is also inside FOV arc
      if (std::abs(rel_a) <= (FOV_rad / 2)) {
        candidateIndex = (*nav_iterator);
        hit = false;
        // trace a ray betwen that cell and robot cell:
        for (grid_map::LineIterator lin_iterator(nav_grid_, candidatePos,
                                                 centerPos);
             !lin_iterator.isPastEnd(); ++lin_iterator) {

          nav_grid_.getPosition(*lin_iterator, rayPos);
          rayIndex = (*lin_iterator);
          // if an obstacle is found, end
          if (isGridValueObst(*nav_iterator)) {
            hit = true;
            ROS_DEBUG("[map.cpp@getInformationGain] HIT! cell [%d, %d]- [%3.3f "
                      "m., %3.3f m.] -  Hit point: [%d, %d]- [%3.3f m., %3.3f "
                      "m.]",
                      candidateIndex(0), candidateIndex(1), candidatePos(0),
                      candidatePos(1), rayIndex(0), rayIndex(1), rayPos(0),
                      rayPos(1));
            break;
          }
        }

        // if the free cell is reached, set its value to 2 and stop the ray
        if (!hit) {
          freeCells++;
          ROS_DEBUG("[map.cpp@getInformationGain] free Cell found: [%d, %d]- "
                    "[%3.3f m., %3.3f m.] ",
                    rayIndex(0), rayIndex(1), rayPos(0), rayPos(1));
        }
      }
    }
  }
  ROS_DEBUG(
      "[map.cpp@getInformationGain] Totals free cells in nav_grid area: %d ",
      freeCells);
  //          cout << "[map.cpp@getInformationGain] Totals free cells in
  //          nav_grid area: " << freeCells << endl;
  return freeCells;
}

int Map::getRFIDReading(double pos_X_m, double pos_Y_m, double heading_rad,
                            double FOV_rad, double range_m) {
  int freeCells = 0;
  ROS_DEBUG("[map.cpp@getRFIDReading]");

  grid_map::Index candidateIndex, rayIndex;
  grid_map::Position candidatePos, rayPos;
  double rel_y, rel_x, rel_a;
  grid_map::Position centerPos(pos_X_m, pos_Y_m);
  bool hit;
  //        cout << "[map.cpp@getInformationGain]Robot in (" << pos_X_m << ","
  //        << pos_Y_m << "), with orientation: " << heading_rad << endl;
  // Iterate cells over a circle ARC with radius range, center pos in nav
  // planning grid and FOV angle
  for (grid_map::CircleIterator nav_iterator(rfid_grid_, centerPos, range_m);
       !nav_iterator.isPastEnd(); ++nav_iterator) {

    // If cell is empty
    if (isGridValueFree(*nav_iterator)) {
      nav_grid_.getPosition(*nav_iterator, candidatePos);

      // relative cell position respect to center
      rel_x = candidatePos.x() - pos_X_m;
      rel_y = candidatePos.y() - pos_Y_m;

      // angle between heading and cell position
      rel_a = std::atan2(rel_y, rel_x) - heading_rad;
      rel_a = constrainAnglePI(rel_a);

      // cell is also inside FOV arc
      if (std::abs(rel_a) <= (FOV_rad / 2)) {
        candidateIndex = (*nav_iterator);
        hit = false;
        // trace a ray between that cell and robot cell:
        for (grid_map::LineIterator lin_iterator(nav_grid_, candidatePos,
                                                 centerPos);
             !lin_iterator.isPastEnd(); ++lin_iterator) {

          nav_grid_.getPosition(*lin_iterator, rayPos);
          rayIndex = (*lin_iterator);
          // if an obstacle is found, end
          if (isGridValueObst(*nav_iterator)) {
            hit = true;
            ROS_DEBUG("[map.cpp@getInformationGain] HIT! cell [%d, %d]- [%3.3f "
                      "m., %3.3f m.] -  Hit point: [%d, %d]- [%3.3f m., %3.3f "
                      "m.]",
                      candidateIndex(0), candidateIndex(1), candidatePos(0),
                      candidatePos(1), rayIndex(0), rayIndex(1), rayPos(0),
                      rayPos(1));
            break;
          }
        }

        // if the free cell is reached, set its value to 2 and stop the ray
        if (!hit) {
          freeCells++;
          ROS_DEBUG("[map.cpp@getInformationGain] free Cell found: [%d, %d]- "
                    "[%3.3f m., %3.3f m.] ",
                    rayIndex(0), rayIndex(1), rayPos(0), rayPos(1));
        }
      }
    }
  }
  ROS_DEBUG(
      "[map.cpp@getInformationGain] Totals free cells in nav_grid area: %d ",
      freeCells);
  //          cout << "[map.cpp@getInformationGain] Totals free cells in
  //          nav_grid area: " << freeCells << endl;
  return freeCells;
}

// ATTENTION: it doesn't work
void Map::calculateInfoGainSensingTime(double pos_X_m, double pos_Y_m,
                                       double heading_rad, double FOV_rad,
                                       double range_m) {
  ROS_FATAL("[Map.cpp@calculateInfoGainSensingTime]: THIS METHOD SHOULD NEVER "
            "BE CALLED");
}

double Map::constrainAnglePI(double x) {
  // remove full turns of 2pi
  x = fmod(x + M_PI, 2 * M_PI);
  // x will be between 0 and 2pi
  if (x < 0)
    x += 2 * M_PI;
  return x - M_PI;
}

float Map::toFloat(Map::CellValue value) const {
  float floatVal = -1.0;

  switch (value) {
  case Map::CellValue::FREE:
    floatVal = 0.0;
    break;
  case Map::CellValue::OBST:
    floatVal = 1.0;
    break;
  case Map::CellValue::VIST:
    floatVal = 2.0;
    break;
  default:
    ROS_FATAL("[Map@toFloat] INVALID CASTING REQUESTED...");
    break;
  }
  return floatVal;
}

Map::CellValue Map::toCellValue(float floatVal, std::string who) const {
  Map::CellValue value;

  if (floatVal == 0.0) {
    value = Map::CellValue::FREE;
  } else if (floatVal == 1.0) {
    value = Map::CellValue::OBST;
  } else if (floatVal == 2.0) {
    value = Map::CellValue::VIST;
  } else {
    ROS_FATAL("[Map@toCellValue (%s)] INVALID CASTING REQUESTED... (%3.3f does "
              "not equal to any valid cell value)",
              who.c_str(), floatVal);
    value = Map::CellValue::FREE;
  }

  return value;
}

bool Map::isGridValueObst(grid_map::Index ind) {
  return (getGridValue(ind(0), ind(1)) == Map::CellValue::OBST);
}

bool Map::isGridValueVist(grid_map::Index ind) {
  return (getGridValue(ind(0), ind(1)) == Map::CellValue::VIST);
}

bool Map::isGridValueFree(grid_map::Index ind) {
  return (getGridValue(ind(0), ind(1)) == Map::CellValue::FREE);
}

bool Map::isPathPlanningGridValueFree(grid_map::Index ind) {
  return (getPathPlanningGridValue(ind(0), ind(1)) == Map::CellValue::FREE);
}

bool Map::isPathPlanningGridValueVist(grid_map::Index ind) {
  return (getPathPlanningGridValue(ind(0), ind(1)) == Map::CellValue::VIST);
}
bool Map::isPathPlanningGridValueObst(grid_map::Index ind) {
  return (getPathPlanningGridValue(ind(0), ind(1)) == Map::CellValue::OBST);
}

grid_map_msgs::GridMap Map::toMessagePathPlanning() {
  return toMessage(&planning_grid_, true);
}

grid_map_msgs::GridMap Map::toMessageGrid() {
  return toMessage(&nav_grid_, true);
}

grid_map_msgs::GridMap Map::toMessage(grid_map::GridMap *gm, bool full) {
  grid_map_msgs::GridMap message;
  ros::Time time = ros::Time::now();
  gm->add("elevation", (*gm)["layer"]);
  gm->setBasicLayers({"elevation"});

  if (full) {
    for (grid_map::GridMapIterator iterator(*gm); !iterator.isPastEnd();
         ++iterator) {
      if (gm->at("elevation", *iterator) == Map::CellValue::FREE)
        gm->at("elevation", *iterator) = NAN;
    }
  }
  gm->setTimestamp(time.toNSec());
  GridMapRosConverter::toMessage(*gm, message);
  return message;
}

void Map::createSecondNavigationGrid(grid_map::GridMap& gridMap) {
  // TODO: fix the method in order to dilate the edges of the wall
  // remove isolated white dots...
  cv::Mat element = cv::getStructuringElement( cv::MORPH_CROSS, cv::Size( 20, 20 ) );
  cv::Mat erosion_dst, dilate_dst, edges, tmp_img;
  cout << "Before converter" << endl;
  GridMapCvConverter::toImage<unsigned short, 1>(gridMap, "layer", CV_16UC1, 0.0, 0.3, tmp_img);
  cv::erode(tmp_img, erosion_dst, element);
  cv::dilate(erosion_dst, dilate_dst, element);
  // find edges
  cout << "Before canny" << endl;
  dilate_dst.convertTo(dilate_dst, CV_8UC1);
  cv::Canny(dilate_dst, edges, 100, 200);
  cout << "After." << endl;
  cv::dilate(edges, dilate_dst, element);
  cv::bitwise_not(dilate_dst, tmp_img);
  // Threshold.
  // Set values equal to or above 220 to 0.
  // Set values below 220 to 255.
  cv::threshold(tmp_img, tmp_img, 220, 255, cv::THRESH_BINARY_INV);
  // Mask used to flood filling.
  // Notice the size needs to be 2 pixels than the image.
  int height = tmp_img.rows + 2;
  int width = tmp_img.cols + 2;
  cv::Mat mask = cv::Mat(height, width, CV_8U , 0.0);
  // Floodfill from point (0, 0)
  cv::floodFill(tmp_img, mask, cv::Point(0,0), 255);
  // Invert floodfilled image
  cv::bitwise_not(tmp_img, tmp_img);
  cv::imwrite("/tmp/dilated.png", tmp_img);

}

}
