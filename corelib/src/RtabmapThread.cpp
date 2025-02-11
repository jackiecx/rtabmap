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

#include "rtabmap/core/Rtabmap.h"
#include "rtabmap/core/RtabmapThread.h"
#include "rtabmap/core/RtabmapEvent.h"
#include "rtabmap/core/Camera.h"
#include "rtabmap/core/CameraEvent.h"
#include "rtabmap/core/ParamEvent.h"
#include "rtabmap/core/OdometryEvent.h"
#include "rtabmap/core/UserDataEvent.h"
#include "rtabmap/core/Memory.h"

#include <rtabmap/utilite/ULogger.h>
#include <rtabmap/utilite/UEventsManager.h>
#include <rtabmap/utilite/UStl.h>
#include <rtabmap/utilite/UTimer.h>
#include <rtabmap/utilite/UConversion.h>

namespace rtabmap {

RtabmapThread::RtabmapThread(Rtabmap * rtabmap) :
		_dataBufferMaxSize(Parameters::defaultRtabmapImageBufferSize()),
		_rate(Parameters::defaultRtabmapDetectionRate()),
		_createIntermediateNodes(Parameters::defaultRtabmapCreateIntermediateNodes()),
		_frameRateTimer(new UTimer()),
		_rtabmap(rtabmap),
		_paused(false),
		lastPose_(Transform::getIdentity()),
		_rotVariance(0),
		_transVariance(0)

{
	UASSERT(rtabmap != 0);
}

RtabmapThread::~RtabmapThread()
{
	UEventsManager::removeHandler(this);

	// Stop the thread first
	join(true);

	delete _frameRateTimer;
	delete _rtabmap;
}

void RtabmapThread::pushNewState(State newState, const ParametersMap & parameters)
{
	ULOGGER_DEBUG("to %d", newState);

	_stateMutex.lock();
	{
		_state.push(newState);
		_stateParam.push(parameters);
	}
	_stateMutex.unlock();

	_dataAdded.release();
}

void RtabmapThread::clearBufferedData()
{
	_dataMutex.lock();
	{
		_dataBuffer.clear();
		lastPose_.setIdentity();
		_rotVariance = 0;
		_transVariance = 0;
	}
	_dataMutex.unlock();

	_userDataMutex.lock();
	{
		_userData = cv::Mat();
	}
	_userDataMutex.unlock();
}

void RtabmapThread::setDetectorRate(float rate)
{
	UASSERT(rate >= 0.0f);
	_rate = rate;
}

void RtabmapThread::setDataBufferSize(unsigned int size)
{
	_dataBufferMaxSize = size;
}

void RtabmapThread::createIntermediateNodes(bool enabled)
{
	enabled = _createIntermediateNodes;
}

void RtabmapThread::publishMap(bool optimized, bool full, bool graphOnly) const
{
	std::map<int, Signature> signatures;
	std::map<int, Transform> poses;
	std::multimap<int, Link> constraints;
	std::map<int, int> mapIds;
	std::map<int, double> stamps;
	std::map<int, std::string> labels;
	std::map<int, std::vector<unsigned char> > userDatas;

	if(graphOnly)
	{
		_rtabmap->getGraph(poses,
				constraints,
				optimized,
				full,
				&signatures);
	}
	else
	{
		_rtabmap->get3DMap(
				signatures,
				poses,
				constraints,
				optimized,
				full);
	}

	this->post(new RtabmapEvent3DMap(
			signatures,
			poses,
			constraints));
}

void RtabmapThread::mainLoopKill()
{
	this->clearBufferedData();

	// this will post the newData semaphore
	_dataAdded.release();
}

void RtabmapThread::mainLoop()
{
	State state = kStateDetecting;
	ParametersMap parameters;

	_stateMutex.lock();
	{
		if(!_state.empty() && !_stateParam.empty())
		{
			state = _state.top();
			_state.pop();
			parameters = _stateParam.top();
			_stateParam.pop();
		}
	}
	_stateMutex.unlock();

	int id = 0;
	cv::Mat userData;
	switch(state)
	{
	case kStateDetecting:
		this->process();
		break;
	case kStateInit:
		UASSERT(!parameters.at("RtabmapThread/DatabasePath").empty());
		Parameters::parse(parameters, Parameters::kRtabmapImageBufferSize(), _dataBufferMaxSize);
		Parameters::parse(parameters, Parameters::kRtabmapDetectionRate(), _rate);
		Parameters::parse(parameters, Parameters::kRtabmapCreateIntermediateNodes(), _createIntermediateNodes);
		UASSERT(_dataBufferMaxSize >= 0);
		UASSERT(_rate >= 0.0f);
		_rtabmap->init(parameters, parameters.at("RtabmapThread/DatabasePath"));
		break;
	case kStateChangingParameters:
		Parameters::parse(parameters, Parameters::kRtabmapImageBufferSize(), _dataBufferMaxSize);
		Parameters::parse(parameters, Parameters::kRtabmapDetectionRate(), _rate);
		Parameters::parse(parameters, Parameters::kRtabmapCreateIntermediateNodes(), _createIntermediateNodes);
		UASSERT(_dataBufferMaxSize >= 0);
		UASSERT(_rate >= 0.0f);
		_rtabmap->parseParameters(parameters);
		break;
	case kStateReseting:
		_rtabmap->resetMemory();
		this->clearBufferedData();
		break;
	case kStateClose:
		if(_dataBuffer.size())
		{
			UWARN("Closing... %d data still buffered! They will be cleared.", (int)_dataBuffer.size());
			this->clearBufferedData();
		}
		_rtabmap->close();
		break;
	case kStateDumpingMemory:
		_rtabmap->dumpData();
		break;
	case kStateDumpingPrediction:
		_rtabmap->dumpPrediction();
		break;
	case kStateExportingDOTGraph:
		_rtabmap->generateDOTGraph(
				parameters.at("path"),
				atoi(parameters.at("id").c_str()),
				atoi(parameters.at("margin").c_str()));
		break;
	case kStateExportingPoses:
		_rtabmap->exportPoses(
				parameters.at("path"),
				uStr2Bool(parameters.at("optimized")),
				uStr2Bool(parameters.at("global")),
				atoi(parameters.at("type").c_str()));
		break;
	case kStateCleanDataBuffer:
		this->clearBufferedData();
		break;
	case kStatePublishingMap:
		this->publishMap(
				uStr2Bool(parameters.at("optimized")),
				uStr2Bool(parameters.at("global")),
				uStr2Bool(parameters.at("graph_only")));
		break;
	case kStateTriggeringMap:
		_rtabmap->triggerNewMap();
		break;
	case kStateAddingUserData:
		_userDataMutex.lock();
		{
			userData = _userData;
			_userData = cv::Mat();
		}
		_userDataMutex.unlock();
		_rtabmap->setUserData(0, userData);
		break;
	case kStateSettingGoal:
		id = atoi(parameters.at("id").c_str());
		if(id == 0 && !parameters.at("label").empty() && _rtabmap->getMemory())
		{
			id = _rtabmap->getMemory()->getSignatureIdByLabel(parameters.at("label"));
		}
		if(id <= 0 || !_rtabmap->computePath(id, true))
		{
			UERROR("Failed to set a goal to location=%d.", id);
		}
		this->post(new RtabmapGlobalPathEvent(id, _rtabmap->getPath()));
		break;
	case kStateCancellingGoal:
		_rtabmap->clearPath();
		break;
	case kStateLabelling:
		if(!_rtabmap->labelLocation(atoi(parameters.at("id").c_str()), parameters.at("label").c_str()))
		{
			this->post(new RtabmapLabelErrorEvent(atoi(parameters.at("id").c_str()), parameters.at("label").c_str()));
		}
		break;
	default:
		UFATAL("Invalid state !?!?");
		break;
	}
}


void RtabmapThread::handleEvent(UEvent* event)
{
	if(this->isRunning() && event->getClassName().compare("CameraEvent") == 0)
	{
		UDEBUG("CameraEvent");
		CameraEvent * e = (CameraEvent*)event;
		if(e->getCode() == CameraEvent::kCodeData)
		{
			this->addData(OdometryEvent(e->data(), Transform(), 1, 1));
		}
	}
	else if(event->getClassName().compare("OdometryEvent") == 0)
	{
		UDEBUG("OdometryEvent");
		OdometryEvent * e = (OdometryEvent*)event;
		if(!e->pose().isNull())
		{
			this->addData(*e);
		}
		else
		{
			lastPose_.setNull();
		}
	}
	else if(event->getClassName().compare("UserDataEvent") == 0)
	{
		if(!_paused)
		{
			UDEBUG("UserDataEvent");
			bool updated = false;
			UserDataEvent * e = (UserDataEvent*)event;
			_userDataMutex.lock();
			if(!e->data().empty())
			{
				updated = !_userData.empty();
				_userData = e->data();
			}
			_userDataMutex.unlock();
			if(updated)
			{
				UWARN("New user data received before the last one was processed... replacing "
					"user data with this new one. Note that UserDataEvent should be used only "
					"if the rate of UserDataEvent is lower than RTAB-Map's detection rate (%f Hz).", _rate);
			}
			else
			{
				pushNewState(kStateAddingUserData);
			}
		}
	}
	else if(event->getClassName().compare("RtabmapEventCmd") == 0)
	{
		RtabmapEventCmd * rtabmapEvent = (RtabmapEventCmd*)event;
		RtabmapEventCmd::Cmd cmd = rtabmapEvent->getCmd();
		if(cmd == RtabmapEventCmd::kCmdInit)
		{
			ULOGGER_DEBUG("CMD_INIT");
			ParametersMap parameters = ((RtabmapEventCmd*)event)->getParameters();
			UASSERT(rtabmapEvent->value1().isStr());
			UASSERT(parameters.insert(ParametersPair("RtabmapThread/DatabasePath", rtabmapEvent->value1().toStr())).second);
			pushNewState(kStateInit, parameters);
		}
		else if(cmd == RtabmapEventCmd::kCmdClose)
		{
			ULOGGER_DEBUG("CMD_CLOSE");
			pushNewState(kStateClose);
		}
		else if(cmd == RtabmapEventCmd::kCmdResetMemory)
		{
			ULOGGER_DEBUG("CMD_RESET_MEMORY");
			pushNewState(kStateReseting);
		}
		else if(cmd == RtabmapEventCmd::kCmdDumpMemory)
		{
			ULOGGER_DEBUG("CMD_DUMP_MEMORY");
			pushNewState(kStateDumpingMemory);
		}
		else if(cmd == RtabmapEventCmd::kCmdDumpPrediction)
		{
			ULOGGER_DEBUG("CMD_DUMP_PREDICTION");
			pushNewState(kStateDumpingPrediction);
		}
		else if(cmd == RtabmapEventCmd::kCmdGenerateDOTGraph)
		{
			ULOGGER_DEBUG("CMD_GENERATE_DOT_GRAPH");
			UASSERT(rtabmapEvent->value1().isBool());
			UASSERT(rtabmapEvent->value2().isStr());
			UASSERT(rtabmapEvent->value1().toBool() || rtabmapEvent->value3().isInt() || rtabmapEvent->value3().isUInt());
			UASSERT(rtabmapEvent->value1().toBool() || rtabmapEvent->value4().isInt() || rtabmapEvent->value4().isUInt());
			ParametersMap param;
			param.insert(ParametersPair("path", rtabmapEvent->value2().toStr()));
			param.insert(ParametersPair("id", !rtabmapEvent->value1().toBool()?rtabmapEvent->value3().toStr():"0"));
			param.insert(ParametersPair("margin", !rtabmapEvent->value1().toBool()?rtabmapEvent->value4().toStr():"0"));
			pushNewState(kStateExportingDOTGraph, param);
		}
		else if(cmd == RtabmapEventCmd::kCmdExportPoses)
		{
			ULOGGER_DEBUG("CMD_EXPORT_POSES");
			UASSERT(rtabmapEvent->value1().isBool());
			UASSERT(rtabmapEvent->value2().isBool());
			UASSERT(rtabmapEvent->value3().isStr());
			UASSERT(rtabmapEvent->value4().isUndef() || rtabmapEvent->value4().isInt() || rtabmapEvent->value4().isUInt());
			ParametersMap param;
			param.insert(ParametersPair("global", rtabmapEvent->value1().toStr()));
			param.insert(ParametersPair("optimized", rtabmapEvent->value1().toStr()));
			param.insert(ParametersPair("path", rtabmapEvent->value3().toStr()));
			param.insert(ParametersPair("type", rtabmapEvent->value4().isInt()?rtabmapEvent->value4().toStr():"0"));
			pushNewState(kStateExportingPoses, param);

		}
		else if(cmd == RtabmapEventCmd::kCmdCleanDataBuffer)
		{
			ULOGGER_DEBUG("CMD_CLEAN_DATA_BUFFER");
			pushNewState(kStateCleanDataBuffer);
		}
		else if(cmd == RtabmapEventCmd::kCmdPublish3DMap)
		{
			ULOGGER_DEBUG("CMD_PUBLISH_MAP");
			UASSERT(rtabmapEvent->value1().isBool());
			UASSERT(rtabmapEvent->value2().isBool());
			UASSERT(rtabmapEvent->value3().isBool());
			ParametersMap param;
			param.insert(ParametersPair("global", rtabmapEvent->value1().toStr()));
			param.insert(ParametersPair("optimized", rtabmapEvent->value2().toStr()));
			param.insert(ParametersPair("graph_only", rtabmapEvent->value3().toStr()));
			pushNewState(kStatePublishingMap, param);
		}
		else if(cmd == RtabmapEventCmd::kCmdTriggerNewMap)
		{
			ULOGGER_DEBUG("CMD_TRIGGER_NEW_MAP");
			pushNewState(kStateTriggeringMap);
		}
		else if(cmd == RtabmapEventCmd::kCmdPause)
		{
			ULOGGER_DEBUG("CMD_PAUSE");
			_paused = !_paused;
		}
		else if(cmd == RtabmapEventCmd::kCmdGoal)
		{
			ULOGGER_DEBUG("CMD_GOAL");
			UASSERT(rtabmapEvent->value1().isStr() || rtabmapEvent->value1().isInt() || rtabmapEvent->value1().isUInt());
			ParametersMap param;
			param.insert(ParametersPair("label", rtabmapEvent->value1().isStr()?rtabmapEvent->value1().toStr():""));
			param.insert(ParametersPair("id", !rtabmapEvent->value1().isStr()?rtabmapEvent->value1().toStr():"0"));
			pushNewState(kStateSettingGoal, param);
		}
		else if(cmd == RtabmapEventCmd::kCmdCancelGoal)
		{
			ULOGGER_DEBUG("CMD_CANCEL_GOAL");
			pushNewState(kStateCancellingGoal);
		}
		else if(cmd == RtabmapEventCmd::kCmdLabel)
		{
			ULOGGER_DEBUG("CMD_LABEL");
			UASSERT(rtabmapEvent->value1().isStr());
			UASSERT(rtabmapEvent->value2().isUndef() || rtabmapEvent->value2().isInt() || rtabmapEvent->value2().isUInt());
			ParametersMap param;
			param.insert(ParametersPair("label", rtabmapEvent->value1().toStr()));
			param.insert(ParametersPair("id", rtabmapEvent->value2().isUndef()?"0":rtabmapEvent->value2().toStr()));
			pushNewState(kStateLabelling, param);
		}
		else
		{
			UWARN("Cmd %d unknown!", cmd);
		}
	}
	else if(event->getClassName().compare("ParamEvent") == 0)
	{
		ULOGGER_DEBUG("changing parameters");
		pushNewState(kStateChangingParameters, ((ParamEvent*)event)->getParameters());
	}
}

//============================================================
// MAIN LOOP
//============================================================
void RtabmapThread::process()
{
	OdometryEvent data;
	if(_state.empty() && getData(data))
	{
		if(_rtabmap->getMemory())
		{
			if(_rtabmap->process(data.data(), data.pose(), data.covariance()))
			{
				Statistics stats = _rtabmap->getStatistics();
				stats.addStatistic(Statistics::kMemoryImages_buffered(), (float)_dataBuffer.size());
				ULOGGER_DEBUG("posting statistics_ event...");
				this->post(new RtabmapEvent(stats));
			}
		}
		else
		{
			UERROR("RTAB-Map is not initialized! Ignoring received data...");
		}
	}
}

void RtabmapThread::addData(const OdometryEvent & odomEvent)
{
	if(!_paused)
	{
		bool ignoreFrame = false;
		if(_rate>0.0f)
		{
			if(_frameRateTimer->getElapsedTime() < 1.0f/_rate)
			{
				ignoreFrame = true;
			}
		}
		if(_dataBufferMaxSize > 0 && !lastPose_.isIdentity() && odomEvent.pose().isIdentity())
		{
			UWARN("Odometry is reset (identity pose detected). Increment map id!");
			pushNewState(kStateTriggeringMap);
			_rotVariance = 0;
			_transVariance = 0;
		}

		if(ignoreFrame && !_createIntermediateNodes)
		{
			return;
		}
		else if(!ignoreFrame)
		{
			_frameRateTimer->start();
		}

		lastPose_ = odomEvent.pose();
		double maxRotVar = odomEvent.rotVariance();
		double maxTransVar = odomEvent.transVariance();
		if(maxRotVar > _rotVariance)
		{
			_rotVariance = maxRotVar;
		}
		if(maxTransVar > _transVariance)
		{
			_transVariance = maxTransVar;
		}

		bool notify = true;
		_dataMutex.lock();
		{
			if(_rotVariance <= 0)
			{
				_rotVariance = 1.0;
			}
			if(_transVariance <= 0)
			{
				_transVariance = 1.0;
			}
			if(ignoreFrame)
			{
				// remove data from the frame, keeping only constraints
				SensorData tmp(
						cv::Mat(),
						odomEvent.data().id(),
						odomEvent.data().stamp(),
						odomEvent.data().userDataRaw());
				_dataBuffer.push_back(OdometryEvent(tmp, odomEvent.pose(), _rotVariance, _transVariance));
			}
			else
			{
				_dataBuffer.push_back(OdometryEvent(odomEvent.data(), odomEvent.pose(), _rotVariance, _transVariance));
			}
			UDEBUG("Added data %d", odomEvent.data().id());

			_rotVariance = 0;
			_transVariance = 0;
			while(_dataBufferMaxSize > 0 && _dataBuffer.size() > _dataBufferMaxSize)
			{
				ULOGGER_WARN("Data buffer is full, the oldest data is removed to add the new one.");
				_dataBuffer.pop_front();
				notify = false;
			}
		}
		_dataMutex.unlock();

		if(notify)
		{
			_dataAdded.release();
		}
	}
}

bool RtabmapThread::getData(OdometryEvent & data)
{
	ULOGGER_DEBUG("");

	ULOGGER_INFO("waiting for data");
	_dataAdded.acquire();
	ULOGGER_INFO("wake-up");

	bool dataFilled = false;
	_dataMutex.lock();
	{
		if(!_dataBuffer.empty())
		{
			data = _dataBuffer.front();
			_dataBuffer.pop_front();
			dataFilled = true;
		}
	}
	_dataMutex.unlock();
	return dataFilled;
}

} /* namespace rtabmap */
