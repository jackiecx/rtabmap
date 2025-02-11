/*
Copyright (c) 2010-2014, Mathieu Labbe - IntRoLab - Universite de Sherbrooke
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Universite de Sherbrooke nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef UTIL3D_FILTERING_H_
#define UTIL3D_FILTERING_H_

#include <rtabmap/core/RtabmapExp.h>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/pcl_base.h>

namespace rtabmap
{

namespace util3d
{

pcl::PointCloud<pcl::PointXYZ>::Ptr RTABMAP_EXP voxelize(
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
		float voxelSize);
pcl::PointCloud<pcl::PointXYZRGB>::Ptr RTABMAP_EXP voxelize(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		float voxelSize);


pcl::PointCloud<pcl::PointXYZ>::Ptr RTABMAP_EXP sampling(
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
		int samples);
pcl::PointCloud<pcl::PointXYZRGB>::Ptr RTABMAP_EXP sampling(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		int samples);


pcl::PointCloud<pcl::PointXYZ>::Ptr RTABMAP_EXP passThrough(
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
		const std::string & axis,
		float min,
		float max);
pcl::PointCloud<pcl::PointXYZRGB>::Ptr RTABMAP_EXP passThrough(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		const std::string & axis,
		float min,
		float max);


pcl::PointCloud<pcl::PointXYZ>::Ptr RTABMAP_EXP removeNaNFromPointCloud(
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud);
pcl::PointCloud<pcl::PointXYZRGB>::Ptr RTABMAP_EXP removeNaNFromPointCloud(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud);


pcl::PointCloud<pcl::PointNormal>::Ptr RTABMAP_EXP removeNaNNormalsFromPointCloud(
		const pcl::PointCloud<pcl::PointNormal>::Ptr & cloud);
pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr RTABMAP_EXP removeNaNNormalsFromPointCloud(
		const pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr & cloud);

/**
 * For convenience.
 */

pcl::IndicesPtr RTABMAP_EXP radiusFiltering(
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
		float radiusSearch,
		int minNeighborsInRadius);
pcl::IndicesPtr RTABMAP_EXP radiusFiltering(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		float radiusSearch,
		int minNeighborsInRadius);

/**
 * @brief Wrapper of the pcl::RadiusOutlierRemoval class.
 *
 * Points in the cloud which have less than a minimum of neighbors in the
 * specified radius are filtered.
 * @param cloud the input cloud.
 * @param indices the input indices of the cloud to check, if empty, all points in the cloud are checked.
 * @param radiusSearch the radius in meter.
 * @param minNeighborsInRadius the minimum of neighbors to keep the point.
 * @return the indices of the points satisfying the parameters.
 */

pcl::IndicesPtr RTABMAP_EXP radiusFiltering(
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
		const pcl::IndicesPtr & indices,
		float radiusSearch,
		int minNeighborsInRadius);
pcl::IndicesPtr RTABMAP_EXP radiusFiltering(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		const pcl::IndicesPtr & indices,
		float radiusSearch,
		int minNeighborsInRadius);

/**
 * For convenience.
 */
pcl::PointCloud<pcl::PointXYZRGB>::Ptr RTABMAP_EXP subtractFiltering(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & substractCloud,
		float radiusSearch,
		int minNeighborsInRadius = 0);

/**
 * Subtract a cloud from another one using radius filtering.
 * @param cloud the input cloud.
 * @param indices the input indices of the cloud to check, if empty, all points in the cloud are checked.
 * @param cloud the input cloud to subtract.
 * @param indices the input indices of the subtracted cloud to check, if empty, all points in the cloud are checked.
 * @param radiusSearch the radius in meter.
 * @return the indices of the points satisfying the parameters.
 */
pcl::IndicesPtr RTABMAP_EXP subtractFiltering(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		const pcl::IndicesPtr & indices,
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & substractCloud,
		const pcl::IndicesPtr & substractIndices,
		float radiusSearch,
		int minNeighborsInRadius = 0);


/**
 * For convenience.
 */

pcl::IndicesPtr RTABMAP_EXP normalFiltering(
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
		float angleMax,
		const Eigen::Vector4f & normal,
		float radiusSearch,
		const Eigen::Vector4f & viewpoint);
pcl::IndicesPtr RTABMAP_EXP normalFiltering(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		float angleMax,
		const Eigen::Vector4f & normal,
		float radiusSearch,
		const Eigen::Vector4f & viewpoint);

/**
 * @brief Given a normal and a maximum angle error, keep all points of the cloud
 * respecting this normal.
 *
 * The normals are computed using the radius search parameter (pcl::NormalEstimation class is used for this), then
 * for each normal, the corresponding point is filtered if the
 * angle (using pcl::getAngle3D()) with the normal specified by the user is larger than the maximum
 * angle specified by the user.
 * @param cloud the input cloud.
 * @param indices the input indices of the cloud to process, if empty, all points in the cloud are processed.
 * @param angleMax the maximum angle.
 * @param normal the normal to which each point's normal is compared.
 * @param radiusSearch radius parameter used for normal estimation (see pcl::NormalEstimation).
 * @param viewpoint from which viewpoint the normals should be estimated (see pcl::NormalEstimation).
 * @return the indices of the points which respect the normal constraint.
 */

pcl::IndicesPtr RTABMAP_EXP normalFiltering(
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
		const pcl::IndicesPtr & indices,
		float angleMax,
		const Eigen::Vector4f & normal,
		float radiusSearch,
		const Eigen::Vector4f & viewpoint);
pcl::IndicesPtr RTABMAP_EXP normalFiltering(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		const pcl::IndicesPtr & indices,
		float angleMax,
		const Eigen::Vector4f & normal,
		float radiusSearch,
		const Eigen::Vector4f & viewpoint);

/**
 * For convenience.
 */
std::vector<pcl::IndicesPtr> RTABMAP_EXP extractClusters(
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
		float clusterTolerance,
		int minClusterSize,
		int maxClusterSize = std::numeric_limits<int>::max(),
		int * biggestClusterIndex = 0);
std::vector<pcl::IndicesPtr> RTABMAP_EXP extractClusters(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		float clusterTolerance,
		int minClusterSize,
		int maxClusterSize = std::numeric_limits<int>::max(),
		int * biggestClusterIndex = 0);

/**
 * @brief Wrapper of the pcl::EuclideanClusterExtraction class.
 *
 * Extract all clusters from a point cloud given a maximum cluster distance tolerance.
 * @param cloud the input cloud.
 * @param indices the input indices of the cloud to process, if empty, all points in the cloud are processed.
 * @param clusterTolerance the cluster distance tolerance (see pcl::EuclideanClusterExtraction).
 * @param minClusterSize minimum size of the clusters to return (see pcl::EuclideanClusterExtraction).
 * @param maxClusterSize maximum size of the clusters to return (see pcl::EuclideanClusterExtraction).
 * @param biggestClusterIndex the index of the biggest cluster, if the clusters are empty, a negative index is set.
 * @return the indices of each cluster found.
 */
std::vector<pcl::IndicesPtr> RTABMAP_EXP extractClusters(
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
		const pcl::IndicesPtr & indices,
		float clusterTolerance,
		int minClusterSize,
		int maxClusterSize = std::numeric_limits<int>::max(),
		int * biggestClusterIndex = 0);
std::vector<pcl::IndicesPtr> RTABMAP_EXP extractClusters(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		const pcl::IndicesPtr & indices,
		float clusterTolerance,
		int minClusterSize,
		int maxClusterSize = std::numeric_limits<int>::max(),
		int * biggestClusterIndex = 0);

pcl::IndicesPtr RTABMAP_EXP extractNegativeIndices(
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
		const pcl::IndicesPtr & indices);
pcl::IndicesPtr RTABMAP_EXP extractNegativeIndices(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		const pcl::IndicesPtr & indices);

} // namespace util3d
} // namespace rtabmap

#endif /* UTIL3D_FILTERING_H_ */
