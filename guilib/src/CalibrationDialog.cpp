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

#include "rtabmap/gui/CalibrationDialog.h"
#include "ui_calibrationDialog.h"

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>

#include <rtabmap/core/CameraEvent.h>
#include <rtabmap/gui/UCv2Qt.h>

#include <rtabmap/utilite/ULogger.h>

namespace rtabmap {

#define COUNT_MIN 40

CalibrationDialog::CalibrationDialog(bool stereo, const QString & savingDirectory, bool switchImages, QWidget * parent) :
	QDialog(parent),
	stereo_(stereo),
	savingDirectory_(savingDirectory),
	processingData_(false),
	savedCalibration_(false)
{
	imagePoints_.resize(2);
	imageParams_.resize(2);
	imageSize_.resize(2);
	stereoImagePoints_.resize(2);
	models_.resize(2);

	minIrs_.resize(2);
	maxIrs_.resize(2);
	minIrs_[0] = 0x0000;
	maxIrs_[0] = 0x7fff;
	minIrs_[1] = 0x0000;
	maxIrs_[1] = 0x7fff;

	qRegisterMetaType<cv::Mat>("cv::Mat");

	ui_ = new Ui_calibrationDialog();
	ui_->setupUi(this);

	connect(ui_->pushButton_calibrate, SIGNAL(clicked()), this, SLOT(calibrate()));
	connect(ui_->pushButton_restart, SIGNAL(clicked()), this, SLOT(restart()));
	connect(ui_->pushButton_save, SIGNAL(clicked()), this, SLOT(save()));
	connect(ui_->checkBox_switchImages, SIGNAL(stateChanged(int)), this, SLOT(restart()));

	connect(ui_->spinBox_boardWidth, SIGNAL(valueChanged(int)), this, SLOT(setBoardWidth(int)));
	connect(ui_->spinBox_boardHeight, SIGNAL(valueChanged(int)), this, SLOT(setBoardHeight(int)));
	connect(ui_->doubleSpinBox_squareSize, SIGNAL(valueChanged(double)), this, SLOT(setSquareSize(double)));

	connect(ui_->buttonBox, SIGNAL(rejected()), this, SLOT(close()));

	ui_->image_view->setFocus();

	ui_->progressBar_count->setMaximum(COUNT_MIN);
	ui_->progressBar_count->setFormat("%v");
	ui_->progressBar_count_2->setMaximum(COUNT_MIN);
	ui_->progressBar_count_2->setFormat("%v");

	ui_->radioButton_raw->setChecked(true);

	ui_->checkBox_switchImages->setChecked(switchImages);

	this->setStereoMode(stereo_);
}

CalibrationDialog::~CalibrationDialog()
{
	this->unregisterFromEventsManager();
	delete ui_;
}

void CalibrationDialog::saveSettings(QSettings & settings, const QString & group) const
{
	if(!group.isEmpty())
	{
		settings.beginGroup(group);
	}
	settings.setValue("board_width", ui_->spinBox_boardWidth->value());
	settings.setValue("board_height", ui_->spinBox_boardHeight->value());
	settings.setValue("board_square_size", ui_->doubleSpinBox_squareSize->value());
	settings.setValue("geometry", this->saveGeometry());
	if(!group.isEmpty())
	{
		settings.endGroup();
	}
}

void CalibrationDialog::loadSettings(QSettings & settings, const QString & group)
{
	if(!group.isEmpty())
	{
		settings.beginGroup(group);
	}
	this->setBoardWidth(settings.value("board_width", ui_->spinBox_boardWidth->value()).toInt());
	this->setBoardHeight(settings.value("board_height", ui_->spinBox_boardHeight->value()).toInt());
	this->setSquareSize(settings.value("board_square_size", ui_->doubleSpinBox_squareSize->value()).toDouble());
	QByteArray bytes = settings.value("geometry", QByteArray()).toByteArray();
	if(!bytes.isEmpty())
	{
		this->restoreGeometry(bytes);
	}
	if(!group.isEmpty())
	{
		settings.endGroup();
	}
}

void CalibrationDialog::setSwitchedImages(bool switched)
{
	ui_->checkBox_switchImages->setChecked(switched);
}

void CalibrationDialog::setStereoMode(bool stereo)
{
	this->restart();

	stereo_ = stereo;
	ui_->progressBar_x_2->setVisible(stereo_);
	ui_->progressBar_y_2->setVisible(stereo_);
	ui_->progressBar_size_2->setVisible(stereo_);
	ui_->progressBar_skew_2->setVisible(stereo_);
	ui_->progressBar_count_2->setVisible(stereo_);
	ui_->label_right->setVisible(stereo_);
	ui_->image_view_2->setVisible(stereo_);
	ui_->label_fx_2->setVisible(stereo_);
	ui_->label_fy_2->setVisible(stereo_);
	ui_->label_cx_2->setVisible(stereo_);
	ui_->label_cy_2->setVisible(stereo_);
	ui_->label_error_2->setVisible(stereo_);
	ui_->label_baseline->setVisible(stereo_);
	ui_->label_baseline_name->setVisible(stereo_);
	ui_->lineEdit_K_2->setVisible(stereo_);
	ui_->lineEdit_D_2->setVisible(stereo_);
	ui_->lineEdit_R_2->setVisible(stereo_);
	ui_->lineEdit_P_2->setVisible(stereo_);
	ui_->radioButton_stereoRectified->setVisible(stereo_);
	ui_->checkBox_switchImages->setVisible(stereo_);
}

void CalibrationDialog::setBoardWidth(int width)
{
	if(width != ui_->spinBox_boardWidth->value())
	{
		ui_->spinBox_boardWidth->setValue(width);
		this->restart();
	}
}

void CalibrationDialog::setBoardHeight(int height)
{
	if(height != ui_->spinBox_boardHeight->value())
	{
		ui_->spinBox_boardHeight->setValue(height);
		this->restart();
	}
}

void CalibrationDialog::setSquareSize(double size)
{
	if(size != ui_->doubleSpinBox_squareSize->value())
	{
		ui_->doubleSpinBox_squareSize->setValue(size);
		this->restart();
	}
}

void CalibrationDialog::closeEvent(QCloseEvent* event)
{
	if(!savedCalibration_ && models_[0].isValid() &&
			(!stereo_ ||
					(stereoModel_.left().isValid() &&
					stereoModel_.right().isValid()&&
					(!ui_->label_baseline->isVisible() || stereoModel_.baseline() > 0.0))))
	{
		QMessageBox::StandardButton b = QMessageBox::question(this, tr("Save calibration?"),
				tr("The camera is calibrated but you didn't "
				   "save the calibration, do you want to save it?"),
				QMessageBox::Yes | QMessageBox::Ignore | QMessageBox::Cancel, QMessageBox::Yes);
		event->ignore();
		if(b == QMessageBox::Yes)
		{
			if(this->save())
			{
				event->accept();
			}
		}
		else if(b == QMessageBox::Ignore)
		{
			event->accept();
		}
	}
	else
	{
		event->accept();
	}
	if(event->isAccepted())
	{
		this->unregisterFromEventsManager();
	}
}

void CalibrationDialog::handleEvent(UEvent * event)
{
	if(!processingData_)
	{
		if(event->getClassName().compare("CameraEvent") == 0)
		{
			rtabmap::CameraEvent * e = (rtabmap::CameraEvent *)event;
			if(e->getCode() == rtabmap::CameraEvent::kCodeData)
			{
				processingData_ = true;
				QMetaObject::invokeMethod(this, "processImages",
						Q_ARG(cv::Mat, e->data().imageRaw()),
						Q_ARG(cv::Mat, e->data().depthOrRightRaw()),
						Q_ARG(QString, QString(e->cameraName().c_str())));
			}
		}
	}
}

void CalibrationDialog::processImages(const cv::Mat & imageLeft, const cv::Mat & imageRight, const QString & cameraName)
{
	processingData_ = true;
	if(cameraName_.isEmpty())
	{
		cameraName_ = "0000";
		if(!cameraName.isEmpty())
		{
			cameraName_ = cameraName;
		}
	}
	if(ui_->label_serial->text().isEmpty())
	{
		ui_->label_serial->setText(cameraName_);

	}
	std::vector<cv::Mat> inputRawImages(2);
	if(ui_->checkBox_switchImages->isChecked())
	{
		inputRawImages[0] = imageRight;
		inputRawImages[1] = imageLeft;
	}
	else
	{
		inputRawImages[0] = imageLeft;
		inputRawImages[1] = imageRight;
	}

	std::vector<cv::Mat> images(2);
	images[0] = inputRawImages[0];
	images[1] = inputRawImages[1];
	imageSize_[0] = images[0].size();
	imageSize_[1] = images[1].size();

	bool boardFound[2] = {false};
	bool boardAccepted[2] = {false};
	bool readyToCalibrate[2] = {false};

	std::vector<std::vector<cv::Point2f> > pointBuf(2);

	bool depthDetected = false;
	for(int id=0; id<(stereo_?2:1); ++id)
	{
		cv::Mat viewGray;
		if(!images[id].empty())
		{
			if(images[id].type() == CV_16UC1)
			{
				depthDetected = true;
				//assume IR image: convert to gray scaled
				const float factor = 255.0f / float((maxIrs_[id] - minIrs_[id]));
				viewGray = cv::Mat(images[id].rows, images[id].cols, CV_8UC1);
				for(int i=0; i<images[id].rows; ++i)
				{
					for(int j=0; j<images[id].cols; ++j)
					{
						viewGray.at<unsigned char>(i, j) = (unsigned char)std::min(float(std::max(images[id].at<unsigned short>(i,j) - minIrs_[id], 0)) * factor, 255.0f);
					}
				}
				cvtColor(viewGray, images[id], cv::COLOR_GRAY2BGR); // convert to show detected points in color
			}
			else if(images[id].channels() == 3)
			{
				cvtColor(images[id], viewGray, cv::COLOR_BGR2GRAY);
			}
			else
			{
				viewGray = images[id];
				cvtColor(viewGray, images[id], cv::COLOR_GRAY2BGR); // convert to show detected points in color
			}
		}
		else
		{
			UERROR("Image %d is empty!! Should not!", id);
		}

		minIrs_[id] = 0;
		maxIrs_[id] = 0x7FFF;

		//Dot it only if not yet calibrated
		if(!ui_->pushButton_save->isEnabled())
		{
			cv::Size boardSize(ui_->spinBox_boardWidth->value(), ui_->spinBox_boardHeight->value());
			if(!viewGray.empty())
			{
				int flags = CV_CALIB_CB_ADAPTIVE_THRESH | CV_CALIB_CB_NORMALIZE_IMAGE;

				if(!viewGray.empty())
				{
					int maxScale = viewGray.cols < 640?2:1;
					for( int scale = 1; scale <= maxScale; scale++ )
					{
						cv::Mat timg;
						if( scale == 1 )
							timg = viewGray;
						else
							cv::resize(viewGray, timg, cv::Size(), scale, scale, CV_INTER_CUBIC);
						boardFound[id] = cv::findChessboardCorners(timg, boardSize, pointBuf[id], flags);
						if(boardFound[id])
						{
							if( scale > 1 )
							{
								cv::Mat cornersMat(pointBuf[id]);
								cornersMat *= 1./scale;
							}
							break;
						}
					}
				}
			}

			if(boardFound[id]) // If done with success,
			{
				// improve the found corners' coordinate accuracy for chessboard
				float minSquareDistance = -1.0f;
				for(unsigned int i=0; i<pointBuf[id].size()-1; ++i)
				{
					float d = cv::norm(pointBuf[id][i] - pointBuf[id][i+1]);
					if(minSquareDistance == -1.0f || minSquareDistance > d)
					{
						minSquareDistance = d;
					}
				}
				float radius = minSquareDistance/2.0f +0.5f;
				cv::cornerSubPix( viewGray, pointBuf[id], cv::Size(radius, radius), cv::Size(-1,-1),
						cv::TermCriteria( CV_TERMCRIT_EPS + CV_TERMCRIT_ITER, 30, 0.1 ));

				// Draw the corners.
				cv::drawChessboardCorners(images[id], boardSize, cv::Mat(pointBuf[id]), boardFound[id]);

				std::vector<float> params(4,0);
				getParams(pointBuf[id], boardSize, imageSize_[id], params[0], params[1], params[2], params[3]);

				bool addSample = true;
				for(unsigned int i=0; i<imageParams_[id].size(); ++i)
				{
					if(fabs(params[0] - imageParams_[id][i].at(0)) < 0.1 && // x
						fabs(params[1] - imageParams_[id][i].at(1)) < 0.1 && // y
						fabs(params[2] - imageParams_[id][i].at(2)) < 0.05 && // size
						fabs(params[3] - imageParams_[id][i].at(3)) < 0.1) // skew
					{
						addSample = false;
					}
				}
				if(addSample)
				{
					boardAccepted[id] = true;

					imagePoints_[id].push_back(pointBuf[id]);
					imageParams_[id].push_back(params);

					UINFO("[%d] Added board, total=%d. (x=%f, y=%f, size=%f, skew=%f)", id, (int)imagePoints_[id].size(), params[0], params[1], params[2], params[3]);
				}

				// update statistics
				std::vector<float> xRange(2, imageParams_[id][0].at(0));
				std::vector<float> yRange(2, imageParams_[id][0].at(1));
				std::vector<float> sizeRange(2, imageParams_[id][0].at(2));
				std::vector<float> skewRange(2, imageParams_[id][0].at(3));
				for(unsigned int i=1; i<imageParams_[id].size(); ++i)
				{
					xRange[0] = imageParams_[id][i].at(0) < xRange[0] ? imageParams_[id][i].at(0) : xRange[0];
					xRange[1] = imageParams_[id][i].at(0) > xRange[1] ? imageParams_[id][i].at(0) : xRange[1];
					yRange[0] = imageParams_[id][i].at(1) < yRange[0] ? imageParams_[id][i].at(1) : yRange[0];
					yRange[1] = imageParams_[id][i].at(1) > yRange[1] ? imageParams_[id][i].at(1) : yRange[1];
					sizeRange[0] = imageParams_[id][i].at(2) < sizeRange[0] ? imageParams_[id][i].at(2) : sizeRange[0];
					sizeRange[1] = imageParams_[id][i].at(2) > sizeRange[1] ? imageParams_[id][i].at(2) : sizeRange[1];
					skewRange[0] = imageParams_[id][i].at(3) < skewRange[0] ? imageParams_[id][i].at(3) : skewRange[0];
					skewRange[1] = imageParams_[id][i].at(3) > skewRange[1] ? imageParams_[id][i].at(3) : skewRange[1];
				}
				//UINFO("Stats [%d]:", id);
				//UINFO("  Count = %d", (int)imagePoints_[id].size());
				//UINFO("  x =    [%f -> %f]", xRange[0], xRange[1]);
				//UINFO("  y =    [%f -> %f]", yRange[0], yRange[1]);
				//UINFO("  size = [%f -> %f]", sizeRange[0], sizeRange[1]);
				//UINFO("  skew = [%f -> %f]", skewRange[0], skewRange[1]);

				float xGood = xRange[1] - xRange[0];
				float yGood = yRange[1] - yRange[0];
				float sizeGood = sizeRange[1] - sizeRange[0];
				float skewGood = skewRange[1] - skewRange[0];

				if(id == 0)
				{
					ui_->progressBar_x->setValue(xGood*100);
					ui_->progressBar_y->setValue(yGood*100);
					ui_->progressBar_size->setValue(sizeGood*100);
					ui_->progressBar_skew->setValue(skewGood*100);
					if((int)imagePoints_[id].size() > ui_->progressBar_count->maximum())
					{
						ui_->progressBar_count->setMaximum((int)imagePoints_[id].size());
					}
					ui_->progressBar_count->setValue((int)imagePoints_[id].size());
				}
				else
				{
					ui_->progressBar_x_2->setValue(xGood*100);
					ui_->progressBar_y_2->setValue(yGood*100);
					ui_->progressBar_size_2->setValue(sizeGood*100);
					ui_->progressBar_skew_2->setValue(skewGood*100);

					if((int)imagePoints_[id].size() > ui_->progressBar_count_2->maximum())
					{
						ui_->progressBar_count_2->setMaximum((int)imagePoints_[id].size());
					}
					ui_->progressBar_count_2->setValue((int)imagePoints_[id].size());
				}

				if(imagePoints_[id].size() >= COUNT_MIN && xGood > 0.5 && yGood > 0.5 && sizeGood > 0.4 && skewGood > 0.5)
				{
					readyToCalibrate[id] = true;
				}

				//update IR values
				if(inputRawImages[id].type() == CV_16UC1)
				{
					//update min max IR if the chessboard was found
					minIrs_[id] = 0xFFFF;
					maxIrs_[id] = 0;
					for(size_t i = 0; i < pointBuf[id].size(); ++i)
					{
						const cv::Point2f &p = pointBuf[id][i];
						cv::Rect roi(std::max(0, (int)p.x - 3), std::max(0, (int)p.y - 3), 6, 6);

						roi.width = std::min(roi.width, inputRawImages[id].cols - roi.x);
						roi.height = std::min(roi.height, inputRawImages[id].rows - roi.y);

						//find minMax in the roi
						double min, max;
						cv::minMaxLoc(inputRawImages[id](roi), &min, &max);
						if(min < minIrs_[id])
						{
							minIrs_[id] = min;
						}
						if(max > maxIrs_[id])
						{
							maxIrs_[id] = max;
						}
					}
				}
			}
		}
	}
	ui_->label_baseline->setVisible(!depthDetected);
	ui_->label_baseline_name->setVisible(!depthDetected);

	if(stereo_ && ((boardAccepted[0] && boardFound[1]) || (boardAccepted[1] && boardFound[0])))
	{
		stereoImagePoints_[0].push_back(pointBuf[0]);
		stereoImagePoints_[1].push_back(pointBuf[1]);
		UINFO("Add stereo image points (size=%d)", (int)stereoImagePoints_[0].size());
	}

	if(!stereo_ && readyToCalibrate[0])
	{
		ui_->pushButton_calibrate->setEnabled(true);
	}
	else if(stereo_ && readyToCalibrate[0] && readyToCalibrate[1] && stereoImagePoints_[0].size())
	{
		ui_->pushButton_calibrate->setEnabled(true);
	}

	if(ui_->radioButton_rectified->isChecked())
	{
		if(models_[0].isValid())
		{
			images[0] = models_[0].rectifyImage(images[0]);
		}
		if(models_[1].isValid())
		{
			images[1] = models_[1].rectifyImage(images[1]);
		}
	}
	else if(ui_->radioButton_stereoRectified->isChecked() &&
			(stereoModel_.left().isValid() &&
			stereoModel_.right().isValid()&&
			(!ui_->label_baseline->isVisible() || stereoModel_.baseline() > 0.0)))
	{
		images[0] = stereoModel_.left().rectifyImage(images[0]);
		images[1] = stereoModel_.right().rectifyImage(images[1]);
	}

	if(ui_->checkBox_showHorizontalLines->isChecked())
	{
		for(int id=0; id<(stereo_?2:1); ++id)
		{
			int step = imageSize_[id].height/16;
			for(int i=step; i<imageSize_[id].height; i+=step)
			{
				cv::line(images[id], cv::Point(0, i), cv::Point(imageSize_[id].width, i), CV_RGB(0,255,0));
			}
		}
	}

	ui_->label_left->setText(tr("%1x%2").arg(images[0].cols).arg(images[0].rows));

	//show frame
	ui_->image_view->setImage(uCvMat2QImage(images[0]).mirrored(ui_->checkBox_mirror->isChecked(), false));
	if(stereo_)
	{
		ui_->label_right->setText(tr("%1x%2").arg(images[1].cols).arg(images[1].rows));
		ui_->image_view_2->setImage(uCvMat2QImage(images[1]).mirrored(ui_->checkBox_mirror->isChecked(), false));
	}
	processingData_ = false;
}

void CalibrationDialog::restart()
{
	// restart
	savedCalibration_ = false;
	imagePoints_[0].clear();
	imagePoints_[1].clear();
	imageParams_[0].clear();
	imageParams_[1].clear();
	stereoImagePoints_[0].clear();
	stereoImagePoints_[1].clear();
	models_[0] = CameraModel();
	models_[1] = CameraModel();
	stereoModel_ = StereoCameraModel();
	cameraName_.clear();
	minIrs_[0] = 0x0000;
	maxIrs_[0] = 0x7fff;
	minIrs_[1] = 0x0000;
	maxIrs_[1] = 0x7fff;

	ui_->pushButton_calibrate->setEnabled(false);
	ui_->pushButton_save->setEnabled(false);
	ui_->radioButton_raw->setChecked(true);
	ui_->radioButton_rectified->setEnabled(false);
	ui_->radioButton_stereoRectified->setEnabled(false);

	ui_->progressBar_count->reset();
	ui_->progressBar_count->setMaximum(COUNT_MIN);
	ui_->progressBar_x->reset();
	ui_->progressBar_y->reset();
	ui_->progressBar_size->reset();
	ui_->progressBar_skew->reset();

	ui_->progressBar_count_2->reset();
	ui_->progressBar_count_2->setMaximum(COUNT_MIN);
	ui_->progressBar_x_2->reset();
	ui_->progressBar_y_2->reset();
	ui_->progressBar_size_2->reset();
	ui_->progressBar_skew_2->reset();

	ui_->label_serial->clear();
	ui_->label_fx->setNum(0);
	ui_->label_fy->setNum(0);
	ui_->label_cx->setNum(0);
	ui_->label_cy->setNum(0);
	ui_->label_baseline->setNum(0);
	ui_->label_error->setNum(0);
	ui_->lineEdit_K->clear();
	ui_->lineEdit_D->clear();
	ui_->lineEdit_R->clear();
	ui_->lineEdit_P->clear();
	ui_->label_fx_2->setNum(0);
	ui_->label_fy_2->setNum(0);
	ui_->label_cx_2->setNum(0);
	ui_->label_cy_2->setNum(0);
	ui_->lineEdit_K_2->clear();
	ui_->lineEdit_D_2->clear();
	ui_->lineEdit_R_2->clear();
	ui_->lineEdit_P_2->clear();
}

void CalibrationDialog::calibrate()
{
	processingData_ = true;
	savedCalibration_ = false;

	QMessageBox mb(QMessageBox::Information,
			tr("Calibrating..."),
			tr("Operation in progress..."));
	mb.show();
	QApplication::processEvents();
	uSleep(100); // hack make sure the text in the QMessageBox is shown...
	QApplication::processEvents();

	std::vector<std::vector<cv::Point3f> > objectPoints(1);
	cv::Size boardSize(ui_->spinBox_boardWidth->value(), ui_->spinBox_boardHeight->value());
	float squareSize = ui_->doubleSpinBox_squareSize->value();
	// compute board corner positions
	for( int i = 0; i < boardSize.height; ++i )
		for( int j = 0; j < boardSize.width; ++j )
			objectPoints[0].push_back(cv::Point3f(float( j*squareSize ), float( i*squareSize ), 0));

	for(int id=0; id<(stereo_?2:1); ++id)
	{
		UINFO("Calibrating camera %d (samples=%d)", id, (int)imagePoints_[id].size());

		objectPoints.resize(imagePoints_[id].size(), objectPoints[0]);

		//calibrate
		std::vector<cv::Mat> rvecs, tvecs;
		std::vector<float> reprojErrs;
		cv::Mat K, D;
		K = cv::Mat::eye(3,3,CV_64FC1);
		UINFO("calibrate!");
		//Find intrinsic and extrinsic camera parameters
		double rms = cv::calibrateCamera(objectPoints,
				imagePoints_[id],
				imageSize_[id],
				K,
				D,
				rvecs,
				tvecs);

		UINFO("Re-projection error reported by calibrateCamera: %f", rms);

		// compute reprojection errors
		std::vector<cv::Point2f> imagePoints2;
		int i, totalPoints = 0;
		double totalErr = 0, err;
		reprojErrs.resize(objectPoints.size());

		for( i = 0; i < (int)objectPoints.size(); ++i )
		{
			cv::projectPoints( cv::Mat(objectPoints[i]), rvecs[i], tvecs[i], K, D, imagePoints2);
			err = cv::norm(cv::Mat(imagePoints_[id][i]), cv::Mat(imagePoints2), CV_L2);

			int n = (int)objectPoints[i].size();
			reprojErrs[i] = (float) std::sqrt(err*err/n);
			totalErr        += err*err;
			totalPoints     += n;
		}

		double totalAvgErr =  std::sqrt(totalErr/totalPoints);

		UINFO("avg re projection error = %f", totalAvgErr);

		cv::Mat P(3,4,CV_64FC1);
		P.at<double>(2,3) = 1;
		K.copyTo(P.colRange(0,3).rowRange(0,3));

		std::cout << "cameraMatrix = " << K << std::endl;
		std::cout << "distCoeffs = " << D << std::endl;
		std::cout << "width = " << imageSize_[id].width << std::endl;
		std::cout << "height = " << imageSize_[id].height << std::endl;

		models_[id] = CameraModel(cameraName_.toStdString(), imageSize_[id], K, D, cv::Mat::eye(3,3,CV_64FC1), P);

		if(id == 0)
		{
			ui_->label_fx->setNum(models_[id].fx());
			ui_->label_fy->setNum(models_[id].fy());
			ui_->label_cx->setNum(models_[id].cx());
			ui_->label_cy->setNum(models_[id].cy());
			ui_->label_error->setNum(totalAvgErr);

			std::stringstream strK, strD, strR, strP;
			strK << models_[id].K();
			strD << models_[id].D();
			strR << models_[id].R();
			strP << models_[id].P();
			ui_->lineEdit_K->setText(strK.str().c_str());
			ui_->lineEdit_D->setText(strD.str().c_str());
			ui_->lineEdit_R->setText(strR.str().c_str());
			ui_->lineEdit_P->setText(strP.str().c_str());
		}
		else
		{
			ui_->label_fx_2->setNum(models_[id].fx());
			ui_->label_fy_2->setNum(models_[id].fy());
			ui_->label_cx_2->setNum(models_[id].cx());
			ui_->label_cy_2->setNum(models_[id].cy());
			ui_->label_error_2->setNum(totalAvgErr);

			std::stringstream strK, strD, strR, strP;
			strK << models_[id].K();
			strD << models_[id].D();
			strR << models_[id].R();
			strP << models_[id].P();
			ui_->lineEdit_K_2->setText(strK.str().c_str());
			ui_->lineEdit_D_2->setText(strD.str().c_str());
			ui_->lineEdit_R_2->setText(strR.str().c_str());
			ui_->lineEdit_P_2->setText(strP.str().c_str());
		}
	}

	if(stereo_ && models_[0].isValid() && models_[1].isValid())
	{
		UINFO("stereo calibration (samples=%d)...", (int)stereoImagePoints_[0].size());
		cv::Size imageSize = imageSize_[0].width > imageSize_[1].width?imageSize_[0]:imageSize_[1];
		cv::Mat R, T, E, F;

		std::vector<std::vector<cv::Point3f> > objectPoints(1);
		cv::Size boardSize(ui_->spinBox_boardWidth->value(), ui_->spinBox_boardHeight->value());
		float squareSize = ui_->doubleSpinBox_squareSize->value();
		// compute board corner positions
		for( int i = 0; i < boardSize.height; ++i )
			for( int j = 0; j < boardSize.width; ++j )
				objectPoints[0].push_back(cv::Point3f(float( j*squareSize ), float( i*squareSize ), 0));
		objectPoints.resize(stereoImagePoints_[0].size(), objectPoints[0]);

		// calibrate extrinsic
#if CV_MAJOR_VERSION < 3
		double rms = cv::stereoCalibrate(
				objectPoints,
				stereoImagePoints_[0],
				stereoImagePoints_[1],
				models_[0].K(), models_[0].D(),
				models_[1].K(), models_[1].D(),
				imageSize, R, T, E, F,
				cv::TermCriteria(cv::TermCriteria::COUNT+cv::TermCriteria::EPS, 100, 1e-5),
				cv::CALIB_FIX_INTRINSIC);
#else
		double rms = cv::stereoCalibrate(
				objectPoints,
				stereoImagePoints_[0],
				stereoImagePoints_[1],
				models_[0].K(), models_[0].D(),
				models_[1].K(), models_[1].D(),
				imageSize, R, T, E, F,
				cv::CALIB_FIX_INTRINSIC,
				cv::TermCriteria(cv::TermCriteria::COUNT+cv::TermCriteria::EPS, 100, 1e-5));
#endif
		UINFO("stereo calibration... done with RMS error=%f", rms);

		double err = 0;
		int npoints = 0;
		std::vector<cv::Vec3f> lines[2];
		UINFO("Computing avg re-projection error...");
		for(unsigned int i = 0; i < stereoImagePoints_[0].size(); i++ )
		{
			int npt = (int)stereoImagePoints_[0][i].size();
			cv::Mat imgpt[2];
			for(int k = 0; k < 2; k++ )
			{
				imgpt[k] = cv::Mat(stereoImagePoints_[k][i]);
				cv::undistortPoints(imgpt[k], imgpt[k], models_[k].K(), models_[k].D(), cv::Mat(), models_[k].K());
				computeCorrespondEpilines(imgpt[k], k+1, F, lines[k]);
			}
			for(int j = 0; j < npt; j++ )
			{
				double errij = fabs(stereoImagePoints_[0][i][j].x*lines[1][j][0] +
									stereoImagePoints_[0][i][j].y*lines[1][j][1] + lines[1][j][2]) +
							   fabs(stereoImagePoints_[1][i][j].x*lines[0][j][0] +
									stereoImagePoints_[1][i][j].y*lines[0][j][1] + lines[0][j][2]);
				err += errij;
			}
			npoints += npt;
		}
		double totalAvgErr = err/(double)npoints;

		UINFO("stereo avg re projection error = %f", totalAvgErr);

		cv::Mat R1, R2, P1, P2, Q;
		cv::Rect validRoi[2];

		cv::stereoRectify(models_[0].K(), models_[0].D(),
						models_[1].K(), models_[1].D(),
						imageSize, R, T, R1, R2, P1, P2, Q,
						cv::CALIB_ZERO_DISPARITY, 0, imageSize, &validRoi[0], &validRoi[1]);

		UINFO("Valid ROI1 = %d,%d,%d,%d  ROI2 = %d,%d,%d,%d newImageSize=%d/%d",
				validRoi[0].x, validRoi[0].y, validRoi[0].width, validRoi[0].height,
				validRoi[1].x, validRoi[1].y, validRoi[1].width, validRoi[1].height,
				imageSize.width, imageSize.height);

		if(imageSize_[0].width == imageSize_[1].width)
		{
			//Stereo, keep new extrinsic projection matrix
			stereoModel_ = StereoCameraModel(
					cameraName_.toStdString(),
					imageSize_[0], models_[0].K(), models_[0].D(), R1, P1,
					imageSize_[1], models_[1].K(), models_[1].D(), R2, P2,
					R, T, E, F);
		}
		else
		{
			//Kinect
			stereoModel_ = StereoCameraModel(
					cameraName_.toStdString(),
					imageSize_[0], models_[0].K(), models_[0].D(), cv::Mat::eye(3,3,CV_64FC1), models_[0].P(),
					imageSize_[1], models_[1].K(), models_[1].D(), cv::Mat::eye(3,3,CV_64FC1), models_[1].P(),
					R, T, E, F);
		}

		std::stringstream strR1, strP1, strR2, strP2;
		strR1 << stereoModel_.left().R();
		strP1 << stereoModel_.left().P();
		strR2 << stereoModel_.right().R();
		strP2 << stereoModel_.right().P();
		ui_->lineEdit_R->setText(strR1.str().c_str());
		ui_->lineEdit_P->setText(strP1.str().c_str());
		ui_->lineEdit_R_2->setText(strR2.str().c_str());
		ui_->lineEdit_P_2->setText(strP2.str().c_str());

		ui_->label_baseline->setNum(stereoModel_.baseline());
		//ui_->label_error_stereo->setNum(totalAvgErr);
	}

	if(stereo_ &&
		stereoModel_.left().isValid() &&
		stereoModel_.right().isValid()&&
		(!ui_->label_baseline->isVisible() || stereoModel_.baseline() > 0.0))
	{
		ui_->radioButton_rectified->setEnabled(true);
		ui_->radioButton_stereoRectified->setEnabled(true);
		ui_->radioButton_stereoRectified->setChecked(true);
		ui_->pushButton_save->setEnabled(true);
	}
	else if(models_[0].isValid())
	{
		ui_->radioButton_rectified->setEnabled(true);
		ui_->radioButton_rectified->setChecked(true);
		ui_->pushButton_save->setEnabled(!stereo_);
	}

	UINFO("End calibration");
	processingData_ = false;
}

bool CalibrationDialog::save()
{
	bool saved = false;
	processingData_ = true;
	if(!stereo_)
	{
		UASSERT(models_[0].isValid());
		QString cameraName = models_[0].name().c_str();
		QString filePath = QFileDialog::getSaveFileName(this, tr("Export"), savingDirectory_+"/"+cameraName+".yaml", "*.yaml");

		if(!filePath.isEmpty())
		{
			QString name = QFileInfo(filePath).baseName();
			QString dir = QFileInfo(filePath).absoluteDir().absolutePath();
			models_[0].setName(name.toStdString());
			if(models_[0].save(dir.toStdString()))
			{
				QMessageBox::information(this, tr("Export"), tr("Calibration file saved to \"%1\".").arg(filePath));
				UINFO("Saved \"%s\"!", filePath.toStdString().c_str());
				savedCalibration_ = true;
				saved = true;
			}
			else
			{
				UERROR("Error saving \"%s\"", filePath.toStdString().c_str());
			}
		}
	}
	else
	{
		UASSERT(stereoModel_.left().isValid() &&
				stereoModel_.right().isValid()&&
				(!ui_->label_baseline->isVisible() || stereoModel_.baseline() > 0.0));
		QString cameraName = stereoModel_.name().c_str();
		QString filePath = QFileDialog::getSaveFileName(this, tr("Export"), savingDirectory_ + "/" + cameraName, "*.yaml");
		QString name = QFileInfo(filePath).baseName();
		QString dir = QFileInfo(filePath).absoluteDir().absolutePath();
		if(!name.isEmpty())
		{
			stereoModel_.setName(name.toStdString());
			std::string base = (dir+QDir::separator()+name).toStdString();
			std::string leftPath = base+"_left.yaml";
			std::string rightPath = base+"_right.yaml";
			std::string posePath = base+"_pose.yaml";
			if(stereoModel_.save(dir.toStdString(), false))
			{
				QMessageBox::information(this, tr("Export"), tr("Calibration files saved:\n  \"%1\"\n  \"%2\"\n  \"%3\".").
						arg(leftPath.c_str()).arg(rightPath.c_str()).arg(posePath.c_str()));
				UINFO("Saved \"%s\" and \"%s\"!", leftPath.c_str(), rightPath.c_str());
				savedCalibration_ = true;
				saved = true;
			}
			else
			{
				UERROR("Error saving \"%s\" and \"%s\"", leftPath.c_str(), rightPath.c_str());
			}
		}
	}
	processingData_ = false;
	return saved;
}

float CalibrationDialog::getArea(const std::vector<cv::Point2f> & corners, const cv::Size & boardSize)
{
	//Get 2d image area of the detected checkerboard.
	//The projected checkerboard is assumed to be a convex quadrilateral, and the area computed as
	//|p X q|/2; see http://mathworld.wolfram.com/Quadrilateral.html.

	cv::Point2f up_left = corners[0];
	cv::Point2f up_right = corners[boardSize.width-1];
	cv::Point2f down_right = corners[corners.size()-1];
	cv::Point2f down_left = corners[corners.size()-boardSize.width];
	cv::Point2f a = up_right - up_left;
	cv::Point2f b = down_right - up_right;
	cv::Point2f c = down_left - down_right;
	cv::Point2f p = b + c;
	cv::Point2f q = a + b;
	return std::fabs(p.x*q.y - p.y*q.x) / 2.0f;
}

float CalibrationDialog::getSkew(const std::vector<cv::Point2f> & corners, const cv::Size & boardSize)
{
   // Get skew for given checkerboard detection.
   // Scaled to [0,1], which 0 = no skew, 1 = high skew
   // Skew is proportional to the divergence of three outside corners from 90 degrees.

	cv::Point2f up_left = corners[0];
	cv::Point2f up_right = corners[boardSize.width-1];
	cv::Point2f down_right = corners[corners.size()-1];


	//  Return angle between lines ab, bc
	cv::Point2f ab = up_left - up_right;
	cv::Point2f cb = down_right - up_right;
	float angle =  std::acos(ab.dot(cb) / (cv::norm(ab) * cv::norm(cb)));

	float r = 2.0f * std::fabs((CV_PI / 2.0f) - angle);
	return r > 1.0f?1.0f:r;
}

// x -> [0, 1] (left, right)
// y -> [0, 1] (top, bottom)
// size -> [0, 1] (small -> big)
// skew -> [0, 1] (low, high)
void CalibrationDialog::getParams(const std::vector<cv::Point2f> & corners, const cv::Size & boardSize, const cv::Size & imageSize,
		float & x, float & y, float & size, float & skew)
{
	float area = getArea(corners, boardSize);
	size = std::sqrt(area / (imageSize.width * imageSize.height));
	skew = getSkew(corners, boardSize);
	float meanX = 0.0f;
	float meanY = 0.0f;
	for(unsigned int i=0; i<corners.size(); ++i)
	{
		meanX += corners[i].x;
		meanY += corners[i].y;
	}
	meanX /= corners.size();
	meanY /= corners.size();
	x = meanX / imageSize.width;
	y = meanY / imageSize.height;
}

} /* namespace rtabmap */
