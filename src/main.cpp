﻿#include <stdio.h>
#include <iostream>
#include <iomanip>
#include <opencv2/opencv.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include "main.hpp"
#include "histogram/histogram.hpp"
#include "gold_standard/file_reader.hpp"
#include "gold_standard/gold_standard_statistic.hpp"
#include "svm/svm.hpp"
#include "util.hpp"
#include "data_generation/transition_generator.hpp"

using namespace sbd;

int main(int argc, char** argv) {
    bool USE_CACHED_HISTOGRAMS = false;

    if (argc != 2) {
        std::cout << "Usage: sbd data_folder" << std::endl;
        std::cout << "  data_folder: Folder for the images and the truth data. Must contain the placeholder [type], which will be replaced by 'frames' or 'truth'" << std::endl;
        std::cout << "               For local execution, just set this to '../resources/[type]/'" << std::endl;
        exit(1);
    }

    std::string dataFolder(argv[1]);

//    GoldStandardStatistic::create(dataFolder);

    std::unordered_set<sbd::GoldStandardElement> gold = readGoldStandard(dataFolder);

//    TransitionGenerator transitionGenerator(gold, getFileNames(dataFolder));
//    transitionGenerator.createRandomTransitions(10);
    
    cv::FileStorage fs;
    Features features;
    std::string histogramCachePath = "../resources/differenceHistograms.yaml";
    if (!USE_CACHED_HISTOGRAMS || !boost::filesystem::exists(histogramCachePath))
    {
        std::vector<std::string> imagePaths = getFileNames(dataFolder);
        features = buildHistogramDifferences(imagePaths, gold);
        std::cout << "Caching built histograms." << std::endl;
        fs.open(histogramCachePath, cv::FileStorage::WRITE);
        fs << "Histograms" << features.values;
        fs << "Labels" << features.classes;
    }
    else
    {
        std::cout << "Using cached histogram differences." << std::endl;
        fs.open(histogramCachePath, cv::FileStorage::READ);
        fs["Histograms"] >> features.values;
        fs["Labels"] >> features.classes;
    }
    fs.release();

    Features trainSet, testSet;
    splitTrainTestSets(features, 0.7, trainSet, testSet);
    cv::Ptr<SVMLearner> learner = trainSVM(trainSet);
    evaluate(testSet, learner);

    // wait for key, so we can read the console output
#ifdef _WIN32
    system("pause");
#else
    cv::waitKey(0);
#endif
    return 0;
}

/**
 * 1.
 * Reads the gold standard and returns it in some easy format.
 */
std::unordered_set<sbd::GoldStandardElement> readGoldStandard(std::string dataFolder) {
    printf("Reading gold standard.\n");

    std::string truthFolder = boost::replace_first_copy(dataFolder, "[type]", "truth");
    std::cout << "Reading truth from " << truthFolder << std::endl;

    FileReader fileReader;
    std::unordered_set<sbd::GoldStandardElement> goldStandard = fileReader.readDir(truthFolder.c_str(), true);

    return goldStandard;
}


/**
 * 2.
 * Read the frame file names recursively.
 */
std::vector<std::string> getFileNames(std::string dataFolder) {
    printf("Getting frame file names.\n");

    std::vector<boost::filesystem::path> imagePaths;
    std::string extension = ".jpg";
    std::string framesFolder = boost::replace_first_copy(dataFolder, "[type]", "frames");
    std::cout << "Reading frames from " << framesFolder << std::endl;

    boost::filesystem::recursive_directory_iterator rdi(framesFolder);
    boost::filesystem::recursive_directory_iterator end_rdi;
    for (; rdi != end_rdi; rdi++) {
        if (extension.compare(rdi->path().extension().string()) == 0) {
            imagePaths.push_back(rdi->path());
        }
    }

    // sort according to int number of frame
    std::sort(imagePaths.begin(), imagePaths.end(), [](boost::filesystem::path aPath, boost::filesystem::path bPath) {
        std::string a = aPath.filename().string();
        std::string b = bPath.filename().string();
        std::string aParent = aPath.parent_path().filename().string();
        std::string bParent = bPath.parent_path().filename().string();

        int parentCompare = aParent.compare(bParent);

        if (parentCompare == 0) {
            return std::stoi(a) < std::stoi(b);
        } else {
            return parentCompare < 0;
        }
    });

    std::vector<std::string> imageStrPaths;
    imageStrPaths.reserve(imagePaths.size());
    for (auto img : imagePaths) {
        imageStrPaths.push_back(img.string());
    }

    return imageStrPaths;
}

/**
 * 3.
 * Given two images, compute the differences in 8 * 8 * 8 histogram bins.
 */
Features buildHistogramDifferences(std::vector<std::string> &imagePaths, std::unordered_set<sbd::GoldStandardElement> &goldStandard) {
    printf("Building histogram differences.\n");

    Histogram histBuilder(8, false); // using color histogram and 8 bins

    std::cout << "Reading " << imagePaths.size() << " images .." << std::endl;

    cv::Mat diffs;
    cv::Mat golds;
    std::vector<std::string> frameNumbers;

    unsigned long tenPercent = imagePaths.size() / 10;
    std::cout << "0% " << std::flush;

    for (int i = 0; i < imagePaths.size() - 1; i += 1) {
        std::string imagePath1 = imagePaths[i];
        std::string imagePath2 = imagePaths[i + 1];

        cv::Mat diff = histBuilder.getDiff(imagePath1, imagePath2);
        float gold = static_cast<float>(findGold(imagePath1, imagePath2, goldStandard));
        std::string frameNumber = boost::filesystem::path(imagePath1).stem().string();

        frameNumbers.push_back(frameNumber);
        diffs.push_back(diff);
        golds.push_back(gold);

        if (diffs.rows % tenPercent == 0)
            std::cout << (diffs.rows / tenPercent * 10) << "% " << std::flush;
    }
    std::cout << std::endl;

    Features features = { golds, diffs };

    // Draw absolute changes of diffs
    std::vector<float> absChanges = histBuilder.getAbsChanges(diffs);
    histBuilder.drawAbsChanges(absChanges, golds, frameNumbers);

    writeVisualizationData(imagePaths, absChanges, golds);


    return features;
}

/**
 * 4.
 * Trains the SVM with the histogram differences and the gold standard.
 */

cv::Ptr<sbd::SVMLearner> trainSVM(Features &trainSet) {
    printf("Training SVM.\n");
    // Set up training data

    cv::Mat trainMat = trainSet.values;
    cv::Mat labelsMat = trainSet.classes;

    std::cout << "Training on " << cv::sum(labelsMat)[0] << "/" << labelsMat.size().height << " positive instances." << std::endl;

//    std::cout << trainMat << std::endl;
//    for (auto it = trainMat.begin<float>(); it != trainMat.end<float>(); it++)
//      if (*it != 0)
//        printf("%f,", *it);

    assert(trainMat.isContinuous());

    cv::Ptr<SVMLearner> svm = new SVMLearner();
    svm->train(trainMat, labelsMat);
    //svm->plotDecisionRegions();

    return svm;
}

/**
 * 5.
 * Evaluate on some held-out test set.
 */
void evaluate(Features &testSet, SVMLearner *learner) {
    printf("Evaluating on test set ...\n");

    cv::Mat testMat = testSet.values;
    cv::Mat labelsMat = testSet.classes;
    std::cout << "Evaluating on " << cv::sum(labelsMat)[0] << "/" << labelsMat.size().height << " positive instances." << std::endl;

    int tp = 0, fp = 0, tn = 0, fn = 0;

    for (int rowIndex = 0; rowIndex < testMat.rows; rowIndex++) {
        cv::Mat mTest = testMat.row(rowIndex);
        float predicted = learner->predict(mTest);
        float actual = labelsMat.at<float>(rowIndex, 0);
        if (predicted == actual) {
            (actual && ++tp) || tn++;
        } else {
            (actual && ++fn) || fp++;
        }
//        printf("Predicted %f   Actual: %f\n", predicted, actual);
    }

    float precision = static_cast<float>(tp) / (tp + fp);
    float recall    = static_cast<float>(tp) / (tp + fn);
    float f1        = 2 * precision * recall / (precision + recall);
    float accuracy  = static_cast<float>(tp + tn) / (tp + tn + fp + fn);

    std::cout.precision(2);
    printf("TP: %i FP: %i TN: %i FN: %i\n", tp, fp, tn, fn);
    std::cout << std::setw(11) << "Precision: " << precision << std::endl;
    std::cout << std::setw(11) << "Recall: "    << recall << std::endl;
    std::cout << std::setw(11) << "F1: "        << f1 << std::endl;
    std::cout << std::setw(11) << "Accuracy: "  << accuracy << std::endl;
}
