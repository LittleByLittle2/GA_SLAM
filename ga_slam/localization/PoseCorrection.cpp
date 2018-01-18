#include "ga_slam/localization/PoseCorrection.hpp"

// GA SLAM
#include "ga_slam/TypeDefs.hpp"
#include "ga_slam/mapping/Map.hpp"
#include "ga_slam/processing/ImageProcessing.hpp"

// Eigen
#include <Eigen/Core>
#include <Eigen/Geometry>

// PCL
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>

// OpenCV
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>

// STL
#include <mutex>
#include <cmath>

namespace ga_slam {

void PoseCorrection::configure(
        double traversedDistanceThreshold,
        double minSlopeThreshold,
        double slopeSumThresholdMultiplier,
        double globalMapLength,
        double globalMapResolution) {
    traversedDistanceThreshold_ = traversedDistanceThreshold;
    minSlopeThreshold_ = minSlopeThreshold;
    slopeSumThresholdMultiplier_ = slopeSumThresholdMultiplier;

    globalMap_.setParameters(globalMapLength, globalMapLength,
            globalMapResolution);
}

void PoseCorrection::createGlobalMap(const Cloud::ConstPtr& cloud) {
    std::lock_guard<std::mutex> guard(globalMapMutex_);

    auto& meanData = globalMap_.getMeanZ();
    auto& varianceData = globalMap_.getVarianceZ();

    size_t cloudIndex = 0;
    size_t mapIndex;

    for (const auto& point : cloud->points) {
        cloudIndex++;

        if (!globalMap_.getIndexFromPosition(point.x, point.y, mapIndex))
            continue;

        float& mean = meanData(mapIndex);
        float& variance = varianceData(mapIndex);
        const float& pointVariance = 1.;

        if (!std::isfinite(mean)) {
            mean = point.z;
            variance = pointVariance;
        } else {
            const double innovation = point.z - mean;
            const double gain = variance / (variance + pointVariance);
            mean = mean + (gain * innovation);
            variance = variance * (1. - gain);
        }
    }

    globalMap_.setValid(true);
    globalMap_.setTimestamp(cloud->header.stamp);
}

bool PoseCorrection::distanceCriterionFulfilled(const Pose& pose) const {
    const Eigen::Vector3d currentXYZ = pose.translation();
    const Eigen::Vector3d lastXYZ = lastCorrectedPose_.translation();
    const Eigen::Vector2d deltaXY = currentXYZ.head(2) - lastXYZ.head(2);

    return deltaXY.norm() >= traversedDistanceThreshold_;
}

bool PoseCorrection::featureCriterionFulfilled(const Map& localMap) const {
    Image image;
    ImageProcessing::convertMapToImage(localMap, image);
    ImageProcessing::calculateGradientImage(image, image);
    cv::threshold(image, image, minSlopeThreshold_, 0., cv::THRESH_TOZERO);

    const double slopeSum = cv::sum(image)[0];
    const double resolution = localMap.getParameters().resolution;
    const double slopeSumThreshold = slopeSumThresholdMultiplier_ / resolution;

    return slopeSum >= slopeSumThreshold;
}

Pose PoseCorrection::matchMaps(const Pose& pose, const Map& localMap) {
    auto correctedPose = pose;

    lastCorrectedPose_= correctedPose;

    return correctedPose;
}

}  // namespace ga_slam
