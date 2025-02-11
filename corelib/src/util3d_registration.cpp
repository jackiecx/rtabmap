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

#include "rtabmap/core/util3d_registration.h"

#include "rtabmap/core/util3d_transforms.h"
#include "rtabmap/core/util3d_filtering.h"
#include "rtabmap/core/util3d.h"

#include <pcl/registration/icp.h>
#include <pcl/registration/transformation_estimation_2D.h>
#include <pcl/sample_consensus/sac_model_registration.h>
#include <pcl/sample_consensus/ransac.h>
#include <rtabmap/utilite/ULogger.h>

namespace rtabmap
{

namespace util3d
{

// Get transform from cloud2 to cloud1
Transform transformFromXYZCorrespondences(
		const pcl::PointCloud<pcl::PointXYZ>::ConstPtr & cloud1,
		const pcl::PointCloud<pcl::PointXYZ>::ConstPtr & cloud2,
		double inlierThreshold,
		int iterations,
		bool refineModel,
		double refineModelSigma,
		int refineModelIterations,
		std::vector<int> * inliersOut,
		double * varianceOut)
{
	//NOTE: this method is a mix of two methods:
	//  - getRemainingCorrespondences() in pcl/registration/impl/correspondence_rejection_sample_consensus.hpp
	//  - refineModel() in pcl/sample_consensus/sac.h

	if(varianceOut)
	{
		*varianceOut = 1.0;
	}
	Transform transform;
	if(cloud1->size() >=3 && cloud1->size() == cloud2->size())
	{
		// RANSAC
		UDEBUG("iterations=%d inlierThreshold=%f", iterations, inlierThreshold);
		std::vector<int> source_indices (cloud2->size());
		std::vector<int> target_indices (cloud1->size());

		// Copy the query-match indices
		for (int i = 0; i < (int)cloud1->size(); ++i)
		{
			source_indices[i] = i;
			target_indices[i] = i;
		}

		// From the set of correspondences found, attempt to remove outliers
		// Create the registration model
		pcl::SampleConsensusModelRegistration<pcl::PointXYZ>::Ptr model;
		model.reset(new pcl::SampleConsensusModelRegistration<pcl::PointXYZ>(cloud2, source_indices));
		// Pass the target_indices
		model->setInputTarget (cloud1, target_indices);
		// Create a RANSAC model
		pcl::RandomSampleConsensus<pcl::PointXYZ> sac (model, inlierThreshold);
		sac.setMaxIterations(iterations);

		// Compute the set of inliers
		if(sac.computeModel())
		{
			std::vector<int> inliers;
			Eigen::VectorXf model_coefficients;

			sac.getInliers(inliers);
			sac.getModelCoefficients (model_coefficients);

			if (refineModel)
			{
				double inlier_distance_threshold_sqr = inlierThreshold * inlierThreshold;
				double error_threshold = inlierThreshold;
				double sigma_sqr = refineModelSigma * refineModelSigma;
				int refine_iterations = 0;
				bool inlier_changed = false, oscillating = false;
				std::vector<int> new_inliers, prev_inliers = inliers;
				std::vector<size_t> inliers_sizes;
				Eigen::VectorXf new_model_coefficients = model_coefficients;
				do
				{
					// Optimize the model coefficients
					model->optimizeModelCoefficients (prev_inliers, new_model_coefficients, new_model_coefficients);
					inliers_sizes.push_back (prev_inliers.size ());

					// Select the new inliers based on the optimized coefficients and new threshold
					model->selectWithinDistance (new_model_coefficients, error_threshold, new_inliers);
					UDEBUG("RANSAC refineModel: Number of inliers found (before/after): %d/%d, with an error threshold of %f.",
							(int)prev_inliers.size (), (int)new_inliers.size (), error_threshold);

					if (new_inliers.empty ())
					{
						++refine_iterations;
						if (refine_iterations >= refineModelIterations)
						{
							break;
						}
						continue;
					}

					// Estimate the variance and the new threshold
					double variance = model->computeVariance ();
					error_threshold = sqrt (std::min (inlier_distance_threshold_sqr, sigma_sqr * variance));

					UDEBUG ("RANSAC refineModel: New estimated error threshold: %f (variance=%f) on iteration %d out of %d.",
						  error_threshold, variance, refine_iterations, refineModelIterations);
					inlier_changed = false;
					std::swap (prev_inliers, new_inliers);

					// If the number of inliers changed, then we are still optimizing
					if (new_inliers.size () != prev_inliers.size ())
					{
						// Check if the number of inliers is oscillating in between two values
						if (inliers_sizes.size () >= 4)
						{
							if (inliers_sizes[inliers_sizes.size () - 1] == inliers_sizes[inliers_sizes.size () - 3] &&
							inliers_sizes[inliers_sizes.size () - 2] == inliers_sizes[inliers_sizes.size () - 4])
							{
								oscillating = true;
								break;
							}
						}
						inlier_changed = true;
						continue;
					}

					// Check the values of the inlier set
					for (size_t i = 0; i < prev_inliers.size (); ++i)
					{
						// If the value of the inliers changed, then we are still optimizing
						if (prev_inliers[i] != new_inliers[i])
						{
							inlier_changed = true;
							break;
						}
					}
				}
				while (inlier_changed && ++refine_iterations < refineModelIterations);

				// If the new set of inliers is empty, we didn't do a good job refining
				if (new_inliers.empty ())
				{
					UWARN ("RANSAC refineModel: Refinement failed: got an empty set of inliers!");
				}

				if (oscillating)
				{
					UDEBUG("RANSAC refineModel: Detected oscillations in the model refinement.");
				}

				std::swap (inliers, new_inliers);
				model_coefficients = new_model_coefficients;
			}

			if (inliers.size() >= 3)
			{
				if(inliersOut)
				{
					*inliersOut = inliers;
				}
				if(varianceOut)
				{
					*varianceOut = model->computeVariance();
				}

				// get best transformation
				Eigen::Matrix4f bestTransformation;
				bestTransformation.row (0) = model_coefficients.segment<4>(0);
				bestTransformation.row (1) = model_coefficients.segment<4>(4);
				bestTransformation.row (2) = model_coefficients.segment<4>(8);
				bestTransformation.row (3) = model_coefficients.segment<4>(12);

				transform = Transform::fromEigen4f(bestTransformation);
				UDEBUG("RANSAC inliers=%d/%d tf=%s", (int)inliers.size(), (int)cloud1->size(), transform.prettyPrint().c_str());

				return transform.inverse(); // inverse to get actual pose transform (not correspondences transform)
			}
			else
			{
				UDEBUG("RANSAC: Model with inliers < 3");
			}
		}
		else
		{
			UDEBUG("RANSAC: Failed to find model");
		}
	}
	else
	{
		UDEBUG("Not enough points to compute the transform");
	}
	return Transform();
}

void computeVarianceAndCorrespondences(
		const pcl::PointCloud<pcl::PointNormal>::ConstPtr & cloudA,
		const pcl::PointCloud<pcl::PointNormal>::ConstPtr & cloudB,
		double maxCorrespondenceDistance,
		double & variance,
		int & correspondencesOut)
{
	variance = 1;
	correspondencesOut = 0;
	pcl::registration::CorrespondenceEstimation<pcl::PointNormal, pcl::PointNormal>::Ptr est;
	est.reset(new pcl::registration::CorrespondenceEstimation<pcl::PointNormal, pcl::PointNormal>);
	est->setInputTarget(cloudA);
	est->setInputSource(cloudB);
	pcl::Correspondences correspondences;
	est->determineCorrespondences(correspondences, maxCorrespondenceDistance);

	if(correspondences.size()>=3)
	{
		std::vector<double> distances(correspondences.size());
		for(unsigned int i=0; i<correspondences.size(); ++i)
		{
			distances[i] = correspondences[i].distance;
		}

		//variance
		std::sort(distances.begin (), distances.end ());
		double median_error_sqr = distances[distances.size () >> 1];
		variance = (2.1981 * median_error_sqr);
	}

	correspondencesOut = (int)correspondences.size();
}

void computeVarianceAndCorrespondences(
		const pcl::PointCloud<pcl::PointXYZ>::ConstPtr & cloudA,
		const pcl::PointCloud<pcl::PointXYZ>::ConstPtr & cloudB,
		double maxCorrespondenceDistance,
		double & variance,
		int & correspondencesOut)
{
	variance = 1;
	correspondencesOut = 0;
	pcl::registration::CorrespondenceEstimation<pcl::PointXYZ, pcl::PointXYZ>::Ptr est;
	est.reset(new pcl::registration::CorrespondenceEstimation<pcl::PointXYZ, pcl::PointXYZ>);
	est->setInputTarget(cloudA);
	est->setInputSource(cloudB);
	pcl::Correspondences correspondences;
	est->determineCorrespondences(correspondences, maxCorrespondenceDistance);

	if(correspondences.size()>=3)
	{
		std::vector<double> distances(correspondences.size());
		for(unsigned int i=0; i<correspondences.size(); ++i)
		{
			distances[i] = correspondences[i].distance;
		}

		//variance
		std::sort(distances.begin (), distances.end ());
		double median_error_sqr = distances[distances.size () >> 1];
		variance = (2.1981 * median_error_sqr);
	}

	correspondencesOut = (int)correspondences.size();
}

// return transform from source to target (All points must be finite!!!)
Transform icp(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr & cloud_source,
			  const pcl::PointCloud<pcl::PointXYZ>::ConstPtr & cloud_target,
			  double maxCorrespondenceDistance,
			  int maximumIterations,
			  bool & hasConverged,
			  pcl::PointCloud<pcl::PointXYZ> & cloud_source_registered)
{
	pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> icp;
	// Set the input source and target
	icp.setInputTarget (cloud_target);
	icp.setInputSource (cloud_source);

	// Set the max correspondence distance to 5cm (e.g., correspondences with higher distances will be ignored)
	icp.setMaxCorrespondenceDistance (maxCorrespondenceDistance);
	// Set the maximum number of iterations (criterion 1)
	icp.setMaximumIterations (maximumIterations);
	// Set the transformation epsilon (criterion 2)
	//icp.setTransformationEpsilon (transformationEpsilon);
	// Set the euclidean distance difference epsilon (criterion 3)
	//icp.setEuclideanFitnessEpsilon (1);
	//icp.setRANSACOutlierRejectionThreshold(maxCorrespondenceDistance);

	// Perform the alignment
	icp.align (cloud_source_registered);
	hasConverged = icp.hasConverged();
	return Transform::fromEigen4f(icp.getFinalTransformation());
}

// return transform from source to target (All points/normals must be finite!!!)
Transform icpPointToPlane(
		const pcl::PointCloud<pcl::PointNormal>::ConstPtr & cloud_source,
		const pcl::PointCloud<pcl::PointNormal>::ConstPtr & cloud_target,
		double maxCorrespondenceDistance,
		int maximumIterations,
		bool & hasConverged,
		pcl::PointCloud<pcl::PointNormal> & cloud_source_registered)
{
	pcl::IterativeClosestPoint<pcl::PointNormal, pcl::PointNormal> icp;
	// Set the input source and target
	icp.setInputTarget (cloud_target);
	icp.setInputSource (cloud_source);

	pcl::registration::TransformationEstimationPointToPlaneLLS<pcl::PointNormal, pcl::PointNormal>::Ptr est;
	est.reset(new pcl::registration::TransformationEstimationPointToPlaneLLS<pcl::PointNormal, pcl::PointNormal>);
	icp.setTransformationEstimation(est);

	// Set the max correspondence distance to 5cm (e.g., correspondences with higher distances will be ignored)
	icp.setMaxCorrespondenceDistance (maxCorrespondenceDistance);
	// Set the maximum number of iterations (criterion 1)
	icp.setMaximumIterations (maximumIterations);
	// Set the transformation epsilon (criterion 2)
	//icp.setTransformationEpsilon (transformationEpsilon);
	// Set the euclidean distance difference epsilon (criterion 3)
	//icp.setEuclideanFitnessEpsilon (1);
	//icp.setRANSACOutlierRejectionThreshold(maxCorrespondenceDistance);

	// Perform the alignment
	icp.align (cloud_source_registered);
	hasConverged = icp.hasConverged();
	return Transform::fromEigen4f(icp.getFinalTransformation());
}

// return transform from source to target (All points must be finite!!!)
Transform icp2D(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr & cloud_source,
			  const pcl::PointCloud<pcl::PointXYZ>::ConstPtr & cloud_target,
			  double maxCorrespondenceDistance,
			  int maximumIterations,
			  bool & hasConverged,
			  pcl::PointCloud<pcl::PointXYZ> & cloud_source_registered)
{
	pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> icp;
	// Set the input source and target
	icp.setInputTarget (cloud_target);
	icp.setInputSource (cloud_source);

	pcl::registration::TransformationEstimation2D<pcl::PointXYZ, pcl::PointXYZ>::Ptr est;
	est.reset(new pcl::registration::TransformationEstimation2D<pcl::PointXYZ, pcl::PointXYZ>);
	icp.setTransformationEstimation(est);

	// Set the max correspondence distance to 5cm (e.g., correspondences with higher distances will be ignored)
	icp.setMaxCorrespondenceDistance (maxCorrespondenceDistance);
	// Set the maximum number of iterations (criterion 1)
	icp.setMaximumIterations (maximumIterations);
	// Set the transformation epsilon (criterion 2)
	//icp.setTransformationEpsilon (transformationEpsilon);
	// Set the euclidean distance difference epsilon (criterion 3)
	//icp.setEuclideanFitnessEpsilon (1);
	//icp.setRANSACOutlierRejectionThreshold(maxCorrespondenceDistance);

	// Perform the alignment
	icp.align (cloud_source_registered);
	hasConverged = icp.hasConverged();
	return Transform::fromEigen4f(icp.getFinalTransformation());
}


// If "voxel" > 0, "samples" is ignored
pcl::PointCloud<pcl::PointXYZ>::Ptr getICPReadyCloud(
		const cv::Mat & depth,
		float fx,
		float fy,
		float cx,
		float cy,
		int decimation,
		double maxDepth,
		float voxel,
		int samples,
		const Transform & transform)
{
	UASSERT(!depth.empty() && (depth.type() == CV_16UC1 || depth.type() == CV_32FC1));
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud;
	cloud = cloudFromDepth(
			depth,
			cx,
			cy,
			fx,
			fy,
			decimation);

	if(cloud->size())
	{
		if(maxDepth>0.0)
		{
			cloud = passThrough(cloud, "z", 0, maxDepth);
		}

		if(cloud->size())
		{
			if(voxel>0)
			{
				cloud = voxelize(cloud, voxel);
			}
			else if(samples>0 && (int)cloud->size() > samples)
			{
				cloud = sampling(cloud, samples);
			}

			if(cloud->size())
			{
				if(!transform.isNull() && !transform.isIdentity())
				{
					cloud = transformPointCloud(cloud, transform);
				}
			}
		}
	}

	return cloud;
}


}

}
