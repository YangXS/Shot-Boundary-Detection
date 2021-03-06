#include "util.hpp"
#include <numeric>
#include <iostream>
#include <fstream>
#include <boost/filesystem.hpp>
#include <src/gold_standard/gold_standard_element.hpp>
#include <numeric>

using namespace sbd;

// split the feature vector in train and test set by trainSetRatio (e.g. 0.7 for 70% trainingset and 30% test)
void sbd::splitTrainTestSets(Features &input, float trainSetRatio, Features &trainSet, Features &testSet)
{
    int cols = input.values.cols;
    int rows = input.values.rows;
    assert(cols > 0);
    cv::Mat trainValues = cv::Mat(0, cols, CV_32FC1);
    cv::Mat testValues  = cv::Mat(0, cols, CV_32FC1);
    cv::Mat trainLabels = cv::Mat(0, 1, CV_32FC1);
    cv::Mat testLabels  = cv::Mat(0, 1, CV_32FC1);

    // create an array containing the random row order, so we can shuffle the train/test set order
    std::vector<int> randomRows(rows);
    std::iota(randomRows.begin(), randomRows.end(), 0); // Fill with 0, 1, ..., n-1
    srand(1599);
    std::random_shuffle(randomRows.begin(), randomRows.end());

    for (int i = 0; i < rows; i++) {
        float currentRatio = i / static_cast<float>(rows);
        int randomRow = randomRows[i];
        if (currentRatio < trainSetRatio) {
            trainValues.push_back(input.values.row(randomRow));
            trainLabels.push_back(input.classes.row(randomRow));
        }
        else {
            testValues.push_back(input.values.row(randomRow));
            testLabels.push_back(input.classes.row(randomRow));
        }
    }

    trainSet.values = trainValues;
    trainSet.classes = trainLabels;
    testSet.values = testValues;
    testSet.classes = testLabels;
}


// For the two given files find out the gold standard, i.e. whether it is a CUT or not.
bool sbd::findGold(std::string path1, std::string path2, std::unordered_set<sbd::GoldStandardElement> &golds) {
    std::string videoName1 = boost::filesystem::path(path1).parent_path().stem().string();
    std::string videoName2 = boost::filesystem::path(path2).parent_path().stem().string();
    std::string frameNr1 = boost::filesystem::path(path1).stem().string();
    std::string frameNr2 = boost::filesystem::path(path2).stem().string();

    /*if (std::stoi(frameNr1) + 1 != std::stoi(frameNr2)) {
        return true;
    }*/

    GoldStandardElement gold(videoName1, videoName2, "", std::stoi(frameNr1), std::stoi(frameNr2));

    //    std::cout << videoName1 << "-" << videoName2 << "-" << frameNr1 << "-" << frameNr2 << std::endl;
    //    for (int i = 0; i < gold.size(); i++) {
    //        if (videoName1 == gold[i].name &&
    //                videoName2 == gold[i].name &&
    //                frameNr1 == std::to_string(gold[i].startFrame) &&
    //                frameNr2 == std::to_string(gold[i].endFrame))
    //            return true;
    //    }
    //    return false;

    return golds.find(gold) != golds.end();
}


void sbd::writeVisualizationData(std::vector<std::string> &imagePaths, std::vector<float> diffs, cv::Mat& gold, std::vector<float> predictions) {
    std::string filepath = "../resources/d3/data/visData.tsv";

    std::ofstream fout(filepath);

    if (!fout) {
        printf("Could not open visData file\n");
        return;
    }

    fout << "idx\tframe1\tframe2\tabsDiff\tprediction\tgold" << std::endl;
    
    for (int i = 0; i < gold.rows; ++i) {
        float diffVal = diffs[i];
        float goldVal = gold.at<float>(i, 0);
        float predVal = predictions[i];

        // get the name of the folder, that contains the current images
        std::string videoFolder = boost::filesystem::path(imagePaths[i]).parent_path().filename().string();
        std::string frame1 = videoFolder + "/" + boost::filesystem::path(imagePaths[i]).filename().string();
        std::string frame2 = videoFolder + "/" + boost::filesystem::path(imagePaths[i + 1]).filename().string();

        fout << (i+1) << "\t"
            << frame1 << "\t"
            << frame2 << "\t"
            << diffVal << "\t"
            << predVal << "\t"
            << goldVal 
            << std::endl;
    }

    fout.close();
}
