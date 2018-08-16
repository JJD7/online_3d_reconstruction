/* STEPS:
*
* 1. extract 2D features
* 2. find feature correspondence
* 3. convert corresponding features to 3D using disparity image information
* 4. find transformation between corresponding 3D points using estimateAffine3D: Output 3D affine transformation matrix  3 x 4
* 5. use decomposeProjectionMatrix to get rotation or Euler angles
*
*
*
* */

#include "pose.h"

Pose::Pose(int argc, char* argv[])
{
	currentDateTimeStr = currentDateTime();
	cout << "currentDateTime=" << currentDateTimeStr << "\n\n";

#if 0
	cv::setBreakOnError(true);
#endif
	if (parseCmdArgs(argc, argv) == -1) return;
	
	if (downsample)
	{
		//read cloud
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb = read_PLY_File(read_PLY_filename0);
		
		//downsample cloud
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb_filtered = downsamplePtCloud(cloudrgb);
		
		string writePath = "downsampled_" + read_PLY_filename0;
		save_pt_cloud_to_PLY_File(cloudrgb_filtered, writePath);
		
		pcl::PolygonMesh mesh;
		visualize_pt_cloud(true, cloudrgb_filtered, false, mesh, "Downsampled Point Cloud");
		
		cout << "Cya!" << endl;
		return;
	}
	
	if (smooth_surface)
	{
		void smoothPtCloud();
		return;
	}
	
	if(mesh_surface)
	{
		void meshSurface();
		return;
	}
	
	if (visualize)
	{
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb = read_PLY_File(read_PLY_filename0);
		pcl::PolygonMesh mesh;
		visualize_pt_cloud(true, cloudrgb, false, mesh, read_PLY_filename0);
		cout << "Cya!" << endl;
		return;
	}
	
	if (align_point_cloud)
	{
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_in = read_PLY_File(read_PLY_filename0);
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_out = read_PLY_File(read_PLY_filename1);
		
		pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 tf_icp = runICPalignment(cloud_in, cloud_out);
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_Fitted(new pcl::PointCloud<pcl::PointXYZRGB> ());
		transformPtCloud(cloud_in, cloud_Fitted, tf_icp);
		
		string writePath = "ICP_aligned_" + read_PLY_filename0;
		save_pt_cloud_to_PLY_File(cloud_Fitted, writePath);
		
		pcl::PolygonMesh mesh;
		visualize_pt_cloud(true, cloud_Fitted, false, mesh, writePath);
		
		return;
	}
	
	readCalibFile();
	readPoseFile();
	readImages();
	
	int64 app_start_time = getTickCount();
	int64 t = getTickCount();
	
	findFeatures();
	cout << "Finding features, time: " << ((getTickCount() - t) / getTickFrequency()) << " sec" << endl;

	vector<pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4> t_matVec;
	vector<pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4> t_FMVec;
	
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_hexPos_MAVLink(new pcl::PointCloud<pcl::PointXYZRGB> ());
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_hexPos_FM(new pcl::PointCloud<pcl::PointXYZRGB> ());
	cloud_hexPos_MAVLink->is_dense = true;
	cloud_hexPos_FM->is_dense = true;
	ofstream hexPosMAVLinkfile, hexPosFMfile, hexPosFMFittedfile;
	hexPosMAVLinkfile.open(hexPosMAVLinkfilename, ios::out);
	hexPosFMfile.open(hexPosFMfilename, ios::out);
	
	for (int i = 0; i < img_numbers.size(); i++)
	{
		//SEARCH PROCESS: get NSECS from images_times_data and search for corresponding or nearby entry in pose_data and heading_data
		int pose_index = data_index_finder(img_numbers[i]);
		
		pcl::PointXYZRGB hexPosMAVLink;
		hexPosMAVLink.x = pose_data[pose_index][tx_ind];
		hexPosMAVLink.y = pose_data[pose_index][ty_ind];
		hexPosMAVLink.z = pose_data[pose_index][tz_ind];
		uint32_t rgbMAVLink = (uint32_t)255;
		hexPosMAVLink.rgb = *reinterpret_cast<float*>(&rgbMAVLink);
		hexPosMAVLinkVec.push_back(hexPosMAVLink);
		hexPosMAVLinkfile << hexPosMAVLink.x << "," << hexPosMAVLink.y << "," << hexPosMAVLink.z << endl;
		cloud_hexPos_MAVLink->points.push_back(hexPosMAVLink);
		
		pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 t_mat = generateTmat(pose_data[pose_index]);
		t_matVec.push_back(t_mat);
		
		if (i == 0)
		{
			t_FMVec.push_back(t_mat);
			continue;
		}
		
		//Feature Matching Alignment
		//generate point clouds of matched keypoints and estimate rigid body transform between them
		pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 T_SVD_matched_pts = generate_tf_of_Matched_Keypoints_Point_Cloud(i, t_FMVec, t_mat);
		
		t_FMVec.push_back(T_SVD_matched_pts * t_mat);
		
		pcl::PointXYZRGB hexPosFM;// = pcl::transformPoint(hexPosMAVLink, T_SVD_matched_pts);
		hexPosFM.x = static_cast<float> (T_SVD_matched_pts (0, 0) * hexPosMAVLink.x + T_SVD_matched_pts (0, 1) * hexPosMAVLink.y + T_SVD_matched_pts (0, 2) * hexPosMAVLink.z + T_SVD_matched_pts (0, 3));
		hexPosFM.y = static_cast<float> (T_SVD_matched_pts (1, 0) * hexPosMAVLink.x + T_SVD_matched_pts (1, 1) * hexPosMAVLink.y + T_SVD_matched_pts (1, 2) * hexPosMAVLink.z + T_SVD_matched_pts (1, 3));
		hexPosFM.z = static_cast<float> (T_SVD_matched_pts (2, 0) * hexPosMAVLink.x + T_SVD_matched_pts (2, 1) * hexPosMAVLink.y + T_SVD_matched_pts (2, 2) * hexPosMAVLink.z + T_SVD_matched_pts (2, 3));
		uint32_t rgbFM = (uint32_t)255 << 8;	//green
		hexPosFM.rgb = *reinterpret_cast<float*>(&rgbFM);
		hexPosFMVec.push_back(hexPosFM);
		hexPosFMfile << hexPosFM.x << "," << hexPosFM.y << "," << hexPosFM.z << endl;
		cloud_hexPos_FM->points.push_back(hexPosFM);
	}
	cout << "Completed calculating feature matched transformations." << endl;
	hexPosMAVLinkfile.close();
	hexPosFMfile.close();
	
	//transforming the camera positions using ICP
	pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 tf_icp = runICPalignment(cloud_hexPos_FM, cloud_hexPos_MAVLink);
	
	cout << "Creating point cloud." << endl;
	//pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb_MAVLink(new pcl::PointCloud<pcl::PointXYZRGB> ());
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb_FeatureMatched(new pcl::PointCloud<pcl::PointXYZRGB> ());
	//cloudrgb_MAVLink->is_dense = true;
	cloudrgb_FeatureMatched->is_dense = true;
	
	for (int i = 0; i < img_numbers.size(); i++)
	{
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb(new pcl::PointCloud<pcl::PointXYZRGB> ());
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr transformed_cloudrgb( new pcl::PointCloud<pcl::PointXYZRGB>() );
		
		if(jump_pixels == 1)
			createPtCloud(i, cloudrgb);
		else
			createFeaturePtCloud(i, cloudrgb);
		//cout << "Created point cloud " << i << endl;
		
		pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 tf = tf_icp * t_FMVec[i];
		
		transformPtCloud(cloudrgb, transformed_cloudrgb, tf);
		
		//generating the bigger point cloud
		if (i == 0)
			copyPointCloud(*transformed_cloudrgb,*cloudrgb_FeatureMatched);
		else
			cloudrgb_FeatureMatched->insert(cloudrgb_FeatureMatched->end(),transformed_cloudrgb->begin(),transformed_cloudrgb->end());
		cout << "Transformed and added." << endl;
	}
	
	cout << "Finding 3D transformation, time: " << ((getTickCount() - t) / getTickFrequency()) << " sec" << endl;
	cout << "Finished Pose Estimation, total time: " << ((getTickCount() - app_start_time) / getTickFrequency()) << " sec" << endl;
	
	cout << "Saving point clouds..." << endl;
	//read_PLY_filename0 = "cloudrgb_MAVLink_" + currentDateTimeStr + ".ply";
	//save_pt_cloud_to_PLY_File(cloudrgb_MAVLink, read_PLY_filename0);
	
	read_PLY_filename1 = "cloudrgb_FeatureMatched_" + currentDateTimeStr + ".ply";
	save_pt_cloud_to_PLY_File(cloudrgb_FeatureMatched, read_PLY_filename1);
	
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_hexPos_Fitted(new pcl::PointCloud<pcl::PointXYZRGB> ());
	transformPtCloud(cloud_hexPos_FM, cloud_hexPos_Fitted, tf_icp);
	hexPosFMFittedfile.open(hexPosFMFittedfilename, ios::out);
	for (int i = 0; i < cloud_hexPos_Fitted->points.size(); i++)
	{
		uint32_t rgbFitted = (uint32_t)255 << 16;	//blue
		cloud_hexPos_Fitted->points[i].rgb = *reinterpret_cast<float*>(&rgbFitted);
		hexPosFMFittedVec.push_back(cloud_hexPos_Fitted->points[i]);
		hexPosFMFittedfile << cloud_hexPos_Fitted->points[i].x << "," << cloud_hexPos_Fitted->points[i].y << "," << cloud_hexPos_Fitted->points[i].z << endl;
	}
	hexPosFMFittedfile.close();
	cloud_hexPos_Fitted->insert(cloud_hexPos_Fitted->end(),cloud_hexPos_FM->begin(),cloud_hexPos_FM->end());
	cloud_hexPos_Fitted->insert(cloud_hexPos_Fitted->end(),cloud_hexPos_MAVLink->begin(),cloud_hexPos_MAVLink->end());
	
	//rectifying Feature Matched Pt Cloud
	//cout << "rectifying Feature Matched Pt Cloud using ICP result..." << endl;
	//pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb_FM_Fitted(new pcl::PointCloud<pcl::PointXYZRGB> ());
	//transformPtCloud(cloudrgb_FeatureMatched, cloudrgb_FM_Fitted, tf_icp);
	
	//downsampling
	cout << "downsampling..." << endl;
	//pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb_MAVLink_downsamp = downsamplePtCloud(cloudrgb_MAVLink);
	//read_PLY_filename0 = "downsampled_" + read_PLY_filename0;
	//save_pt_cloud_to_PLY_File(cloudrgb_MAVLink_downsamp, read_PLY_filename0);
	
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb_FeatureMatched_downsamp = downsamplePtCloud(cloudrgb_FeatureMatched);
	read_PLY_filename1 = "downsampled_rectified_" + read_PLY_filename1;
	save_pt_cloud_to_PLY_File(cloudrgb_FeatureMatched_downsamp, read_PLY_filename1);
	
	if(preview)
	{
		displayCamPositions = true;
		pcl::PolygonMesh mesh;
		visualize_pt_cloud(true, cloud_hexPos_Fitted, false, mesh, "hexPos_Fitted red:MAVLink green:FeatureMatched blue:FM_Fitted");
		visualize_pt_cloud(true, cloudrgb_FeatureMatched_downsamp, false, mesh, "cloudrgb_FM_Fitted_downsampled");
	}
	
}

pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 Pose::generate_tf_of_Matched_Keypoints_Point_Cloud
(int img_index, vector<pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4> &t_FMVec, 
pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 t_mat_MAVLink)
{
	cout << "matching img " << img_index << " with_img/matches";
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_current (new pcl::PointCloud<pcl::PointXYZRGB> ());
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_prior (new pcl::PointCloud<pcl::PointXYZRGB> ());
	cloud_current->is_dense = true;
	cloud_prior->is_dense = true;
	bool first_match = true;
	
	Mat disp_img_src;
	if(use_segment_labels)
		disp_img_src = double_disparity_images[img_index];
	else
		disp_img_src = disparity_images[img_index];
	//cout << "Read source disparity image." << endl;
	
	vector<KeyPoint> keypoints_src = features[img_index].keypoints;
	cuda::GpuMat descriptor_src(features[img_index].descriptors);
	//cout << "Read source keypoints." << endl;
	
	for (int dst_index = img_index-1; dst_index >= max(img_index - range_width,0); dst_index--)
	{
		//reference https://stackoverflow.com/questions/44988087/opencv-feature-matching-match-descriptors-to-knn-filtered-keypoints
		//reference https://github.com/opencv/opencv/issues/6130
		//reference http://study.marearts.com/2014/07/opencv-study-orb-gpu-feature-extraction.html
		//reference https://docs.opencv.org/3.1.0/d6/d1d/group__cudafeatures2d.html
		
		
		//cout << "image " << img_index << " to " << dst_index << endl;
		vector<vector<DMatch>> matches;
		cuda::GpuMat descriptor_dst(features[dst_index].descriptors);
		matcher->knnMatch(descriptor_src, descriptor_dst, matches, 2);
		vector<DMatch> good_matches;
		for(int k = 0; k < matches.size(); k++)
		{
			if(matches[k][0].distance < 0.5 * matches[k][1].distance && matches[k][0].distance < 40)
			{
				//cout << matches[k][0].distance << "/" << matches[k][1].distance << " " << 
				//matches[k][0].imgIdx << "/" << matches[k][1].imgIdx << " " << 
				//matches[k][0].queryIdx << "/" << matches[k][1].queryIdx << " " << 
				//matches[k][0].trainIdx << "/" << matches[k][1].trainIdx << endl;
				good_matches.push_back(matches[k][0]);
			}
		}
		if(good_matches.size() < 100)	//less number of matches.. don't bother working on this one. good matches are around 500-600
			continue;
		
		cout << " " << dst_index << "/" << good_matches.size();
		
		Mat disp_img_dst;
		if(use_segment_labels)
			disp_img_dst = double_disparity_images[dst_index];
		else
			disp_img_dst = disparity_images[dst_index];
		//cout << "Read destination disparity image." << endl;
		
		vector<KeyPoint> keypoints_dst = features[dst_index].keypoints;
		//cout << "Read destination keypoints." << endl;
		//using sequential matched points to estimate the rigid body transformation between matched 3D points
		for (int match_index = 0; match_index < good_matches.size(); match_index++)
		{
			DMatch match = good_matches[match_index];
			
			//define 3d points for all keypoints
			vector<Point3d> keypoints3D_src, keypoints3D_dst;
			vector<int> keypoints3D_2D_index_src, keypoints3D_2D_index_dst;
			
			//cout << "Converting 2D matches to 3D matches... match.trainIdx " << match.trainIdx << " match.queryIdx " << match.queryIdx << endl;
			int trainIdx = match.trainIdx;
			int queryIdx = match.queryIdx;
			
			//*3. convert corresponding features to 3D using disparity image information
			//cout << "keypoints_src[queryIdx].pt.y " << keypoints_src[queryIdx].pt.y << " keypoints_src[queryIdx].pt.x " << keypoints_src[queryIdx].pt.x << endl;
			double disp_val_src, disp_val_dst;
			if(use_segment_labels)
			{
				disp_val_src = disp_img_src.at<double>(keypoints_src[queryIdx].pt.y, keypoints_src[queryIdx].pt.x);
				disp_val_dst = disp_img_dst.at<double>(keypoints_dst[trainIdx].pt.y, keypoints_dst[trainIdx].pt.x);
			}
			else
			{
				disp_val_src = (double)disp_img_src.at<char>(keypoints_src[queryIdx].pt.y, keypoints_src[queryIdx].pt.x);
				disp_val_dst = (double)disp_img_dst.at<char>(keypoints_dst[trainIdx].pt.y, keypoints_dst[trainIdx].pt.x);
			}
			//cout << "Read disparity value." << endl;
		
			cv::Mat_<double> vec_src(4, 1);
			cv::Mat_<double> vec_dst(4, 1);

			if (disp_val_src > minDisparity && disp_val_dst > minDisparity && keypoints_src[queryIdx].pt.x >= cols_start_aft_cutout && keypoints_dst[trainIdx].pt.x >= cols_start_aft_cutout)
			{
				double xs = keypoints_src[queryIdx].pt.x;
				double ys = keypoints_src[queryIdx].pt.y;
				
				vec_src(0) = xs; vec_src(1) = ys; vec_src(2) = disp_val_src; vec_src(3) = 1;
				vec_src = Q * vec_src;
				vec_src /= vec_src(3);
				
				Point3d src_3D_pt = Point3d(vec_src(0), vec_src(1), vec_src(2));

				double xd = keypoints_dst[trainIdx].pt.x;
				double yd = keypoints_dst[trainIdx].pt.y;

				vec_dst(0) = xd; vec_dst(1) = yd; vec_dst(2) = disp_val_dst; vec_dst(3) = 1;
				vec_dst = Q * vec_dst;
				vec_dst /= vec_dst(3);

				Point3d dst_3D_pt = Point3d(vec_dst(0), vec_dst(1), vec_dst(2));
				
				keypoints3D_src.push_back(src_3D_pt);
				keypoints3D_2D_index_src.push_back(queryIdx);

				keypoints3D_dst.push_back(dst_3D_pt);
				keypoints3D_2D_index_dst.push_back(trainIdx);
			}
			
			pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_current_temp (new pcl::PointCloud<pcl::PointXYZRGB> ());
			pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_prior_temp (new pcl::PointCloud<pcl::PointXYZRGB> ());
			cloud_current_temp->is_dense = true;
			cloud_prior_temp->is_dense = true;
			
			for (int i = 0; i < keypoints3D_src.size(); ++i)
			{
				pcl::PointXYZRGB pt_3d_src, pt_3d_dst;
				
				pt_3d_src.x = keypoints3D_src[i].x;
				pt_3d_src.y = keypoints3D_src[i].y;
				pt_3d_src.z = keypoints3D_src[i].z;
				
				pt_3d_dst.x = keypoints3D_dst[i].x;
				pt_3d_dst.y = keypoints3D_dst[i].y;
				pt_3d_dst.z = keypoints3D_dst[i].z;
				
				cloud_current_temp->points.push_back(pt_3d_src);
				cloud_prior_temp->points.push_back(pt_3d_dst);
			}
			//cout << "cloud_current_temp->size() " << cloud_current_temp->size() << endl;
			//cout << "cloud_prior_temp->size() " << cloud_prior_temp->size() << endl;
			pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 t_FM = t_FMVec[dst_index];
			//cout << "t_FMVec[" << dst_index << "]\n" << t_FM << endl;
			
			pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_current_t_temp (new pcl::PointCloud<pcl::PointXYZRGB> ());
			pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_prior_t_temp (new pcl::PointCloud<pcl::PointXYZRGB> ());
			
			pcl::transformPointCloud(*cloud_current_temp, *cloud_current_t_temp, t_mat_MAVLink);
			//cout << "cloud_current_temp transformed." << endl;
			pcl::transformPointCloud(*cloud_prior_temp, *cloud_prior_t_temp, t_FM);
			//cout << "cloud_prior_temp transformed." << endl;
			
			if (first_match)
			{
				copyPointCloud(*cloud_current_t_temp,*cloud_current);
				copyPointCloud(*cloud_prior_t_temp,*cloud_prior);
				first_match = false;
				//cout << "clouds copied!" << endl;
			}
			else
			{
				cloud_current->insert(cloud_current->end(),cloud_current_t_temp->begin(),cloud_current_t_temp->end());
				cloud_prior->insert(cloud_prior->end(),cloud_prior_t_temp->begin(),cloud_prior_t_temp->end());
				//cout << "clouds inserted!" << endl;
			}
		}
		
	}
	
	cout << endl;
	
	//cout << "cloud_current->size() " << cloud_current->size() << endl;
	//cout << "cloud_prior->size() " << cloud_prior->size() << endl;
	
	//cout << "Finding Rigid Body Transformation..." << endl;
	
	pcl::registration::TransformationEstimationSVD<pcl::PointXYZRGB, pcl::PointXYZRGB> te2;
	pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 T_SVD_matched_pts;
	
	te2.estimateRigidTransformation(*cloud_current, *cloud_prior, T_SVD_matched_pts);
	//cout << "computed transformation between MATCHED KEYPOINTS T_SVD2 is\n" << T_SVD_matched_pts << endl;
	//log_file << "computed transformation between MATCHED KEYPOINTS T_SVD2 is\n" << T_SVD_matched_pts << endl;
	
	return T_SVD_matched_pts;
}
