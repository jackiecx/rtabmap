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

#include "DBDriverSqlite3.h"

#include "rtabmap/core/Signature.h"
#include "VisualWord.h"
#include "rtabmap/core/VWDictionary.h"
#include "rtabmap/core/util3d.h"
#include "rtabmap/core/Compression.h"
#include "DatabaseSchema_sql.h"
#include <set>

#include "rtabmap/utilite/UtiLite.h"

namespace rtabmap {

DBDriverSqlite3::DBDriverSqlite3(const ParametersMap & parameters) :
	DBDriver(parameters),
	_ppDb(0),
	_version("0.0.0"),
	_dbInMemory(Parameters::defaultDbSqlite3InMemory()),
	_cacheSize(Parameters::defaultDbSqlite3CacheSize()),
	_journalMode(Parameters::defaultDbSqlite3JournalMode()),
	_synchronous(Parameters::defaultDbSqlite3Synchronous()),
	_tempStore(Parameters::defaultDbSqlite3TempStore())
{
	ULOGGER_DEBUG("treadSafe=%d", sqlite3_threadsafe());
	this->parseParameters(parameters);
}

DBDriverSqlite3::~DBDriverSqlite3()
{
	this->closeConnection();
}

void DBDriverSqlite3::parseParameters(const ParametersMap & parameters)
{
	ParametersMap::const_iterator iter;
	if((iter=parameters.find(Parameters::kDbSqlite3CacheSize())) != parameters.end())
	{
		this->setCacheSize(std::atoi((*iter).second.c_str()));
	}
	if((iter=parameters.find(Parameters::kDbSqlite3JournalMode())) != parameters.end())
	{
		this->setJournalMode(std::atoi((*iter).second.c_str()));
	}
	if((iter=parameters.find(Parameters::kDbSqlite3Synchronous())) != parameters.end())
	{
		this->setSynchronous(std::atoi((*iter).second.c_str()));
	}
	if((iter=parameters.find(Parameters::kDbSqlite3TempStore())) != parameters.end())
	{
		this->setTempStore(std::atoi((*iter).second.c_str()));
	}
	if((iter=parameters.find(Parameters::kDbSqlite3InMemory())) != parameters.end())
	{
		this->setDbInMemory(uStr2Bool((*iter).second.c_str()));
	}
	DBDriver::parseParameters(parameters);
}

void DBDriverSqlite3::setCacheSize(unsigned int cacheSize)
{
	if(this->isConnected())
	{
		_cacheSize = cacheSize;
		std::string query = "PRAGMA cache_size = ";
		query += uNumber2Str(_cacheSize) + ";";
		this->executeNoResultQuery(query.c_str());
	}
}

void DBDriverSqlite3::setJournalMode(int journalMode)
{
	if(journalMode >= 0 && journalMode < 5)
	{
		_journalMode = journalMode;
		if(this->isConnected())
		{
			switch(_journalMode)
			{
			case 4:
				this->executeNoResultQuery("PRAGMA journal_mode = OFF;");
				break;
			case 3:
				this->executeNoResultQuery("PRAGMA journal_mode = MEMORY;");
				break;
			case 2:
				this->executeNoResultQuery("PRAGMA journal_mode = PERSIST;");
				break;
			case 1:
				this->executeNoResultQuery("PRAGMA journal_mode = TRUNCATE;");
				break;
			case 0:
			default:
				this->executeNoResultQuery("PRAGMA journal_mode = DELETE;");
				break;
			}
		}
	}
	else
	{
		ULOGGER_ERROR("Wrong journal mode (%d)", journalMode);
	}
}

void DBDriverSqlite3::setSynchronous(int synchronous)
{
	if(synchronous >= 0 && synchronous < 3)
	{
		_synchronous = synchronous;
		if(this->isConnected())
		{
			switch(_synchronous)
			{
			case 0:
				this->executeNoResultQuery("PRAGMA synchronous = OFF;");
				break;
			case 1:
				this->executeNoResultQuery("PRAGMA synchronous = NORMAL;");
				break;
			case 2:
			default:
				this->executeNoResultQuery("PRAGMA synchronous = FULL;");
				break;
			}
		}
	}
	else
	{
		ULOGGER_ERROR("Wrong synchronous value (%d)", synchronous);
	}
}

void DBDriverSqlite3::setTempStore(int tempStore)
{
	if(tempStore >= 0 && tempStore < 3)
	{
		_tempStore = tempStore;
		if(this->isConnected())
		{
			switch(_tempStore)
			{
			case 2:
				this->executeNoResultQuery("PRAGMA temp_store = MEMORY;");
				break;
			case 1:
				this->executeNoResultQuery("PRAGMA temp_store = FILE;");
				break;
			case 0:
			default:
				this->executeNoResultQuery("PRAGMA temp_store = DEFAULT;");
				break;
			}
		}
	}
	else
	{
		ULOGGER_ERROR("Wrong tempStore value (%d)", tempStore);
	}
}

void DBDriverSqlite3::setDbInMemory(bool dbInMemory)
{
	if(dbInMemory != _dbInMemory)
	{
		if(this->isConnected())
		{
			// Hard reset...
			join(true);
			this->emptyTrashes();
			this->closeConnection();
			_dbInMemory = dbInMemory;
			this->openConnection(this->getUrl());
		}
		else
		{
			_dbInMemory = dbInMemory;
		}
	}
}

/*
** This function is used to load the contents of a database file on disk
** into the "main" database of open database connection pInMemory, or
** to save the current contents of the database opened by pInMemory into
** a database file on disk. pInMemory is probably an in-memory database,
** but this function will also work fine if it is not.
**
** Parameter zFilename points to a nul-terminated string containing the
** name of the database file on disk to load from or save to. If parameter
** isSave is non-zero, then the contents of the file zFilename are
** overwritten with the contents of the database opened by pInMemory. If
** parameter isSave is zero, then the contents of the database opened by
** pInMemory are replaced by data loaded from the file zFilename.
**
** If the operation is successful, SQLITE_OK is returned. Otherwise, if
** an error occurs, an SQLite error code is returned.
*/
int DBDriverSqlite3::loadOrSaveDb(sqlite3 *pInMemory, const std::string & fileName, int isSave) const
{
  int rc;                   /* Function return code */
  sqlite3 *pFile = 0;           /* Database connection opened on zFilename */
  sqlite3_backup *pBackup = 0;  /* Backup object used to copy data */
  sqlite3 *pTo = 0;             /* Database to copy to (pFile or pInMemory) */
  sqlite3 *pFrom = 0;           /* Database to copy from (pFile or pInMemory) */

  /* Open the database file identified by zFilename. Exit early if this fails
  ** for any reason. */
  rc = sqlite3_open(fileName.c_str(), &pFile);
  if( rc==SQLITE_OK ){

    /* If this is a 'load' operation (isSave==0), then data is copied
    ** from the database file just opened to database pInMemory.
    ** Otherwise, if this is a 'save' operation (isSave==1), then data
    ** is copied from pInMemory to pFile.  Set the variables pFrom and
    ** pTo accordingly. */
    pFrom = (isSave ? pInMemory : pFile);
    pTo   = (isSave ? pFile     : pInMemory);

    /* Set up the backup procedure to copy from the "main" database of
    ** connection pFile to the main database of connection pInMemory.
    ** If something goes wrong, pBackup will be set to NULL and an error
    ** code and  message left in connection pTo.
    **
    ** If the backup object is successfully created, call backup_step()
    ** to copy data from pFile to pInMemory. Then call backup_finish()
    ** to release resources associated with the pBackup object.  If an
    ** error occurred, then  an error code and message will be left in
    ** connection pTo. If no error occurred, then the error code belonging
    ** to pTo is set to SQLITE_OK.
    */
    pBackup = sqlite3_backup_init(pTo, "main", pFrom, "main");
    if( pBackup ){
      (void)sqlite3_backup_step(pBackup, -1);
      (void)sqlite3_backup_finish(pBackup);
    }
    rc = sqlite3_errcode(pTo);
  }

  /* Close the database connection opened on database file zFilename
  ** and return the result of this function. */
  (void)sqlite3_close(pFile);
  return rc;
}

bool DBDriverSqlite3::getDatabaseVersionQuery(std::string & version) const
{
	version = "0.0.0";
	if(_ppDb)
	{
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;

		query << "SELECT version FROM Admin;";

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		if(rc == SQLITE_OK)
		{
			// Process the result if one
			rc = sqlite3_step(ppStmt);
			if(rc == SQLITE_ROW)
			{
				version = reinterpret_cast<const char*>(sqlite3_column_text(ppStmt, 0));
				rc = sqlite3_step(ppStmt);
			}
			UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

			// Finalize (delete) the statement
			rc = sqlite3_finalize(ppStmt);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}
		//else
		//{
			// old version detected
		//}
		return true;
	}
	return false;
}


bool DBDriverSqlite3::connectDatabaseQuery(const std::string & url, bool overwritten)
{
	this->disconnectDatabaseQuery();
	// Open a database connection
	_ppDb = 0;

	if(url.empty())
	{
		UERROR("url is empty...");
		return false;
	}

	int rc = SQLITE_OK;
	bool dbFileExist = UFile::exists(url.c_str());
	if(dbFileExist && overwritten)
	{
		UINFO("Deleting database %s...", url.c_str());
		UASSERT(UFile::erase(url.c_str()) == 0);
		dbFileExist = false;
	}

	if(_dbInMemory)
	{
		ULOGGER_INFO("Using database \"%s\" in the memory.", url.c_str());
		rc = sqlite3_open_v2(":memory:", &_ppDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
	}
	else
	{
		ULOGGER_INFO("Using database \"%s\" from the hard drive.", url.c_str());
		rc = sqlite3_open_v2(url.c_str(), &_ppDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
	}
	if(rc != SQLITE_OK)
	{
		UFATAL("DB error : %s (path=\"%s\")", sqlite3_errmsg(_ppDb), url.c_str());
		_ppDb = 0;
		return false;
	}

	if(_dbInMemory && dbFileExist)
	{
		UTimer timer;
		timer.start();
		ULOGGER_DEBUG("Loading DB ...");
		rc = loadOrSaveDb(_ppDb, url, 0); // Load memory from file
		ULOGGER_INFO("Loading DB time = %fs, (%s)", timer.ticks(), url.c_str());
		if(rc != SQLITE_OK)
		{
			UFATAL("DB error 2 : %s", sqlite3_errmsg(_ppDb));
			sqlite3_close(_ppDb);
			_ppDb = 0;
			return false;
		}
	}

	if(!dbFileExist)
	{
		ULOGGER_INFO("Database \"%s\" doesn't exist, creating a new one...", url.c_str());
		// Create the database
		std::string schema = DATABASESCHEMA_SQL;
		schema = uHex2Str(schema);
		this->executeNoResultQuery(schema.c_str());
	}
	UASSERT(this->getDatabaseVersionQuery(_version)); // must be true!
	UINFO("Database version = %s", _version.c_str());

	//Set database optimizations
	this->setCacheSize(_cacheSize); // this will call the SQL
	this->setJournalMode(_journalMode); // this will call the SQL
	this->setSynchronous(_synchronous); // this will call the SQL
	this->setTempStore(_tempStore); // this will call the SQL

	return true;
}
void DBDriverSqlite3::disconnectDatabaseQuery()
{
	UDEBUG("");
	if(_ppDb)
	{
		int rc = SQLITE_OK;
		// make sure that all statements are finalized
		sqlite3_stmt * pStmt;
		while( (pStmt = sqlite3_next_stmt(_ppDb, 0))!=0 )
		{
			rc = sqlite3_finalize(pStmt);
			if(rc != SQLITE_OK)
			{
				UERROR("");
			}
		}

		if(_dbInMemory)
		{
			UTimer timer;
			timer.start();
			UINFO("Saving database to %s ...",  this->getUrl().c_str());
			rc = loadOrSaveDb(_ppDb, this->getUrl(), 1); // Save memory to file
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
			ULOGGER_DEBUG("Saving DB time = %fs", timer.ticks());
		}

		// Then close (delete) the database connection
		UINFO("Disconnecting database %s...", this->getUrl().c_str());
		sqlite3_close(_ppDb);
		_ppDb = 0;
	}
}

bool DBDriverSqlite3::isConnectedQuery() const
{
	return _ppDb != 0;
}

// In bytes
void DBDriverSqlite3::executeNoResultQuery(const std::string & sql) const
{
	if(_ppDb)
	{
		UTimer timer;
		timer.start();
		int rc;
		rc = sqlite3_exec(_ppDb, sql.c_str(), 0, 0, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s, the query is %s", sqlite3_errmsg(_ppDb), sql.c_str()).c_str());
		UDEBUG("Time=%fs", timer.ticks());
	}
}

long DBDriverSqlite3::getMemoryUsedQuery() const
{
	if(_dbInMemory)
	{
		return sqlite3_memory_used();
	}
	else
	{
		return UFile::length(this->getUrl());
	}
}

void DBDriverSqlite3::loadNodeDataQuery(std::list<Signature *> & signatures) const
{
	UDEBUG("load data for %d signatures", (int)signatures.size());
	if(_ppDb)
	{
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;

		if(uStrNumCmp(_version, "0.10.1") >= 0)
		{
			query << "SELECT image, depth, calibration, scan_max_pts, scan, user_data "
				  << "FROM Data "
				  << "WHERE id = ?"
				  <<";";
		}
		else if(uStrNumCmp(_version, "0.10.0") >= 0)
		{
			query << "SELECT Data.image, Data.depth, Data.calibration, Data.scan_max_pts, Data.scan, Node.user_data "
				  << "FROM Data "
				  << "INNER JOIN Node "
				  << "ON Data.id = Node.id "
				  << "WHERE Data.id = ?"
				  <<";";
		}
		else if(uStrNumCmp(_version, "0.8.11") >= 0)
		{
			query << "SELECT Image.data, "
					 "Depth.data, Depth.local_transform, Depth.fx, Depth.fy, Depth.cx, Depth.cy, Depth.data2d_max_pts, Depth.data2d, Node.user_data "
				  << "FROM Image "
				  << "INNER JOIN Node "
				  << "on Image.id = Node.id "
				  << "LEFT OUTER JOIN Depth " // returns all images even if there are no metric data
				  << "ON Image.id = Depth.id "
				  << "WHERE Image.id = ?"
				  <<";";
		}
		else if(uStrNumCmp(_version, "0.8.8") >= 0)
		{
			query << "SELECT Image.data, "
					 "Depth.data, Depth.local_transform, Depth.fx, Depth.fy, Depth.cx, Depth.cy, Depth.data2d, Node.user_data "
				  << "FROM Image "
				  << "INNER JOIN Node "
				  << "on Image.id = Node.id "
				  << "LEFT OUTER JOIN Depth " // returns all images even if there are no metric data
				  << "ON Image.id = Depth.id "
				  << "WHERE Image.id = ?"
				  <<";";
		}
		else if(uStrNumCmp(_version, "0.7.0") >= 0)
		{
			query << "SELECT Image.data, "
					 "Depth.data, Depth.local_transform, Depth.fx, Depth.fy, Depth.cx, Depth.cy, Depth.data2d "
				  << "FROM Image "
				  << "LEFT OUTER JOIN Depth " // returns all images even if there are no metric data
				  << "ON Image.id = Depth.id "
				  << "WHERE Image.id = ?"
				  <<";";
		}
		else
		{
			query << "SELECT Image.data, "
					 "Depth.data, Depth.local_transform, Depth.constant, Depth.data2d "
				  << "FROM Image "
				  << "LEFT OUTER JOIN Depth " // returns all images even if there are no metric data
				  << "ON Image.id = Depth.id "
				  << "WHERE Image.id = ?"
				  <<";";
		}

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		const void * data = 0;
		int dataSize = 0;
		int index = 0;

		for(std::list<Signature*>::iterator iter = signatures.begin(); iter!=signatures.end(); ++iter)
		{
			UASSERT(*iter != 0);

			ULOGGER_DEBUG("Loading data for %d...", (*iter)->id());
			// bind id
			rc = sqlite3_bind_int(ppStmt, 1, (*iter)->id());
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

			// Process the result if one
			rc = sqlite3_step(ppStmt);
			if(rc == SQLITE_ROW)
			{
				index = 0;

				cv::Mat imageCompressed;
				cv::Mat depthOrRightCompressed;
				std::vector<CameraModel> models;
				StereoCameraModel stereoModel;
				Transform localTransform = Transform::getIdentity();
				cv::Mat scanCompressed;
				cv::Mat userDataCompressed;

				data = sqlite3_column_blob(ppStmt, index);
				dataSize = sqlite3_column_bytes(ppStmt, index++);

				//Create the image
				if(dataSize>4 && data)
				{
					imageCompressed = cv::Mat(1, dataSize, CV_8UC1, (void *)data).clone();
				}

				data = sqlite3_column_blob(ppStmt, index);
				dataSize = sqlite3_column_bytes(ppStmt, index++);

				//Create the depth image
				if(dataSize>4 && data)
				{
					depthOrRightCompressed = cv::Mat(1, dataSize, CV_8UC1, (void *)data).clone();
				}

				if(uStrNumCmp(_version, "0.10.0") < 0)
				{
					data = sqlite3_column_blob(ppStmt, index); // local transform
					dataSize = sqlite3_column_bytes(ppStmt, index++);
					if((unsigned int)dataSize == localTransform.size()*sizeof(float) && data)
					{
						memcpy(localTransform.data(), data, dataSize);
					}
				}

				// calibration
				if(uStrNumCmp(_version, "0.10.0") >= 0)
				{
					data = sqlite3_column_blob(ppStmt, index);
					dataSize = sqlite3_column_bytes(ppStmt, index++);
					// multi-cameras [fx,fy,cx,cy,local_transform, ... ,fx,fy,cx,cy,local_transform] (4+12)*float * numCameras
					// stereo [fx, fy, cx, cy, baseline, local_transform] (5+12)*float
					if(dataSize > 0 && data)
					{
						float * dataFloat = (float*)data;
						if((unsigned int)dataSize % (4+localTransform.size())*sizeof(float) == 0)
						{
							int cameraCount = dataSize / ((4+localTransform.size())*sizeof(float));
							UDEBUG("Loading calibration for %d cameras (%d bytes)", cameraCount, dataSize);
							int max = cameraCount*(4+localTransform.size());
							for(int i=0; i<max; i+=4+localTransform.size())
							{
								memcpy(localTransform.data(), dataFloat+i+4, localTransform.size()*sizeof(float));
								models.push_back(CameraModel(
										(double)dataFloat[i],
										(double)dataFloat[i+1],
										(double)dataFloat[i+2],
										(double)dataFloat[i+3],
										localTransform));
							}
						}
						else if((unsigned int)dataSize == (5+localTransform.size())*sizeof(float))
						{
							UDEBUG("Loading calibration of a stereo camera");
							memcpy(localTransform.data(), dataFloat+5, localTransform.size()*sizeof(float));
							stereoModel = StereoCameraModel(
									dataFloat[0],  // fx
									dataFloat[1],  // fy
									dataFloat[2],  // cx
									dataFloat[3],  // cy
									dataFloat[4], // baseline
									localTransform);
						}
						else
						{
							UFATAL("Wrong format of the Data.calibration field (size=%d bytes)", dataSize);
						}
					}

				}
				else if(uStrNumCmp(_version, "0.7.0") >= 0)
				{
					double fx = sqlite3_column_double(ppStmt, index++);
					double fyOrBaseline = sqlite3_column_double(ppStmt, index++);
					double cx = sqlite3_column_double(ppStmt, index++);
					double cy = sqlite3_column_double(ppStmt, index++);
					if(fyOrBaseline < 1.0)
					{
						//it is a baseline
						stereoModel = StereoCameraModel(fx,fx,cx,cy,fyOrBaseline, localTransform);
					}
					else
					{
						models.push_back(CameraModel(fx, fyOrBaseline, cx, cy, localTransform));
					}
				}
				else
				{
					float depthConstant = sqlite3_column_double(ppStmt, index++);
					float fx = 1.0f/depthConstant;
					float fy = 1.0f/depthConstant;
					float cx = 0.0f;
					float cy = 0.0f;
					models.push_back(CameraModel(fx, fy, cx, cy, localTransform));
				}

				int laserScanMaxPts = 0;
				if(uStrNumCmp(_version, "0.8.11") >= 0)
				{
					laserScanMaxPts = sqlite3_column_int(ppStmt, index++);
				}

				data = sqlite3_column_blob(ppStmt, index);
				dataSize = sqlite3_column_bytes(ppStmt, index++);
				//Create the laserScan
				if(dataSize>4 && data)
				{
					scanCompressed = cv::Mat(1, dataSize, CV_8UC1, (void *)data).clone(); // depth2d
				}

				if(uStrNumCmp(_version, "0.8.8") >= 0)
				{
					data = sqlite3_column_blob(ppStmt, index);
					dataSize = sqlite3_column_bytes(ppStmt, index++);
					//Create the userData
					if(dataSize>4 && data)
					{
						if(uStrNumCmp(_version, "0.10.1") >= 0)
						{
							userDataCompressed = cv::Mat(1, dataSize, CV_8UC1, (void *)data).clone(); // userData
						}
						else
						{
							// compress data (set uncompressed data to signed to make difference with compressed type)
							userDataCompressed = compressData2(cv::Mat(1, dataSize, CV_8SC1, (void *)data));
						}
					}
				}

				if(models.size())
				{
					(*iter)->sensorData() = SensorData(
							scanCompressed,
							laserScanMaxPts,
							imageCompressed,
							depthOrRightCompressed,
							models,
							(*iter)->id(),
							0,
							userDataCompressed);
				}
				else
				{
					(*iter)->sensorData() = SensorData(
							scanCompressed,
							laserScanMaxPts,
							imageCompressed,
							depthOrRightCompressed,
							stereoModel,
							(*iter)->id(),
							0,
							userDataCompressed);
				}

				rc = sqlite3_step(ppStmt); // next result...
			}
			UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

			//reset
			rc = sqlite3_reset(ppStmt);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		ULOGGER_DEBUG("Time=%fs", timer.ticks());
	}
}

bool DBDriverSqlite3::getNodeInfoQuery(int signatureId,
		Transform & pose,
		int & mapId,
		int & weight,
		std::string & label,
		double & stamp) const
{
	bool found = false;
	if(_ppDb && signatureId)
	{
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;

		if(uStrNumCmp(_version, "0.8.5") >= 0)
		{
			query << "SELECT pose, map_id, weight, label, stamp "
					 "FROM Node "
					 "WHERE id = " << signatureId <<
					 ";";
		}
		else
		{
			query << "SELECT pose, map_id, weight "
					 "FROM Node "
					 "WHERE id = " << signatureId <<
					 ";";
		}

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		const void * data = 0;
		int dataSize = 0;

		// Process the result if one
		rc = sqlite3_step(ppStmt);
		if(rc == SQLITE_ROW)
		{
			found = true;
			int index = 0;
			data = sqlite3_column_blob(ppStmt, index); // pose
			dataSize = sqlite3_column_bytes(ppStmt, index++);
			if((unsigned int)dataSize == pose.size()*sizeof(float) && data)
			{
				memcpy(pose.data(), data, dataSize);
			}

			mapId = sqlite3_column_int(ppStmt, index++); // map id
			weight = sqlite3_column_int(ppStmt, index++); // weight

			if(uStrNumCmp(_version, "0.8.5") >= 0)
			{
				const unsigned char * p = sqlite3_column_text(ppStmt, index++);
				if(p)
				{
					label = reinterpret_cast<const char*>(p); // label
				}
				stamp = sqlite3_column_double(ppStmt, index++); // stamp
			}

			rc = sqlite3_step(ppStmt); // next result...
		}
		UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	}
	return found;
}


void DBDriverSqlite3::getAllNodeIdsQuery(std::set<int> & ids, bool ignoreChildren) const
{
	if(_ppDb)
	{
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;

		if(!ignoreChildren)
		{
			query << "SELECT id "
				  << "FROM Node "
				  << "ORDER BY id";
		}
		else
		{
			query << "SELECT id "
				  << "FROM Node "
				  << "INNER JOIN Link "
				  << "ON id = to_id " // use to_id tp ignore all children (which don't have link pointing on them)
				  << "ORDER BY id";
		}

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());


		// Process the result if one
		rc = sqlite3_step(ppStmt);
		while(rc == SQLITE_ROW)
		{
			ids.insert(ids.end(), sqlite3_column_int(ppStmt, 0)); // Signature Id
			rc = sqlite3_step(ppStmt);
		}
		UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		ULOGGER_DEBUG("Time=%f ids=%d", timer.ticks(), (int)ids.size());
	}
}

void DBDriverSqlite3::getAllLinksQuery(std::multimap<int, Link> & links, bool ignoreNullLinks) const
{
	links.clear();
	if(_ppDb)
	{
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;

		if(uStrNumCmp(_version, "0.8.4") >= 0)
		{
			query << "SELECT from_id, to_id, type, transform, rot_variance, trans_variance FROM Link ORDER BY from_id, to_id";
		}
		else if(uStrNumCmp(_version, "0.7.4") >= 0)
		{
			query << "SELECT from_id, to_id, type, transform, variance FROM Link ORDER BY from_id, to_id";
		}
		else
		{
			query << "SELECT from_id, to_id, type, transform FROM Link ORDER BY from_id, to_id";
		}

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		int fromId = -1;
		int toId = -1;
		int type = Link::kUndef;
		float rotVariance = 1.0f;
		float transVariance = 1.0f;
		const void * data = 0;
		int dataSize = 0;

		// Process the result if one
		rc = sqlite3_step(ppStmt);
		while(rc == SQLITE_ROW)
		{
			int index = 0;

			fromId = sqlite3_column_int(ppStmt, index++);
			toId = sqlite3_column_int(ppStmt, index++);
			type = sqlite3_column_int(ppStmt, index++);

			data = sqlite3_column_blob(ppStmt, index);
			dataSize = sqlite3_column_bytes(ppStmt, index++);

			Transform transform;
			if((unsigned int)dataSize == transform.size()*sizeof(float) && data)
			{
				memcpy(transform.data(), data, dataSize);
			}
			else if(dataSize)
			{
				UERROR("Error while loading link transform from %d to %d! Setting to null...", fromId, toId);
			}

			if(!ignoreNullLinks || !transform.isNull())
			{
				if(uStrNumCmp(_version, "0.8.4") >= 0)
				{
					rotVariance = sqlite3_column_double(ppStmt, index++);
					transVariance = sqlite3_column_double(ppStmt, index++);
					links.insert(links.end(), std::make_pair(fromId, Link(fromId, toId, (Link::Type)type, transform, rotVariance, transVariance)));
				}
				else if(uStrNumCmp(_version, "0.7.4") >= 0)
				{
					rotVariance = transVariance = sqlite3_column_double(ppStmt, index++);
					links.insert(links.end(), std::make_pair(fromId, Link(fromId, toId, (Link::Type)type, transform, rotVariance, transVariance)));
				}
				else
				{
					// neighbor is 0, loop closures are 1 and 2 (child)
					links.insert(links.end(), std::make_pair(fromId, Link(fromId, toId, type==0?Link::kNeighbor:Link::kGlobalClosure, transform, rotVariance, transVariance)));
				}
			}

			rc = sqlite3_step(ppStmt);
		}

		UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	}
}

void DBDriverSqlite3::getLastIdQuery(const std::string & tableName, int & id) const
{
	if(_ppDb)
	{
		UDEBUG("get last id from table \"%s\"", tableName.c_str());
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;

		query << "SELECT max(id) "
			  << "FROM " << tableName
			  << ";";

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());


		// Process the result if one
		rc = sqlite3_step(ppStmt);
		if(rc == SQLITE_ROW)
		{
			id = sqlite3_column_int(ppStmt, 0); // Signature Id
			rc = sqlite3_step(ppStmt);
			UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}
		else
		{
			ULOGGER_ERROR("No result !?! from the DB");
		}

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		ULOGGER_DEBUG("Time=%fs", timer.ticks());
	}
}

void DBDriverSqlite3::getInvertedIndexNiQuery(int nodeId, int & ni) const
{
	ni = 0;
	if(_ppDb)
	{
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;

		// Create a new entry in table Signature
		query << "SELECT count(word_id) "
			  << "FROM Map_Node_Word "
			  << "WHERE node_id=" << nodeId << ";";

		//query.append("COMMIT;");

		//ULOGGER_DEBUG("DBDriverSqlite3::getSurfNi() Execute query : %s", query.toStdString().c_str());

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());


		// Process the result if one
		rc = sqlite3_step(ppStmt);
		if(rc == SQLITE_ROW)
		{
			ni = sqlite3_column_int(ppStmt, 0);
			rc = sqlite3_step(ppStmt);
			UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}
		else
		{
			ULOGGER_ERROR("No result !?! from the DB, node=%d",nodeId);
		}


		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		ULOGGER_DEBUG("Time=%fs", timer.ticks());
	}
}

void DBDriverSqlite3::getNodeIdByLabelQuery(const std::string & label, int & id) const
{
	if(_ppDb && !label.empty() && uStrNumCmp(_version, "0.8.5") >= 0)
	{
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;
		query << "SELECT id FROM Node WHERE label='" << label <<"'";

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Process the result if one
		rc = sqlite3_step(ppStmt);
		if(rc == SQLITE_ROW)
		{
			id = sqlite3_column_int(ppStmt, 0);
			rc = sqlite3_step(ppStmt);
		}
		UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		ULOGGER_DEBUG("Time=%f", timer.ticks());
	}
}

void DBDriverSqlite3::getAllLabelsQuery(std::map<int, std::string> & labels) const
{
	if(_ppDb && uStrNumCmp(_version, "0.8.5") >= 0)
	{
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;
		query << "SELECT id,label FROM Node WHERE label IS NOT NULL";

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Process the result if one
		rc = sqlite3_step(ppStmt);
		while(rc == SQLITE_ROW)
		{
			int index = 0;
			int id = sqlite3_column_int(ppStmt, index++);
			const unsigned char * p = sqlite3_column_text(ppStmt, index++);
			if(p)
			{
				std::string label = reinterpret_cast<const char*>(p);
				if(!label.empty())
				{
					labels.insert(std::make_pair(id, label));
				}
			}
			rc = sqlite3_step(ppStmt);
		}
		UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		ULOGGER_DEBUG("Time=%f", timer.ticks());
	}
}

void DBDriverSqlite3::getWeightQuery(int nodeId, int & weight) const
{
	weight = 0;
	if(_ppDb)
	{
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;

		query << "SELECT weight FROM node WHERE id =  "
			  << nodeId
			  << ";";

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());


		// Process the result if one
		rc = sqlite3_step(ppStmt);
		if(rc == SQLITE_ROW)
		{
			weight= sqlite3_column_int(ppStmt, 0); // weight
			rc = sqlite3_step(ppStmt);
		}
		UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	}
}

//may be slower than the previous version but don't have a limit of words that can be loaded at the same time
void DBDriverSqlite3::loadSignaturesQuery(const std::list<int> & ids, std::list<Signature *> & nodes) const
{
	ULOGGER_DEBUG("count=%d", (int)ids.size());
	if(_ppDb && ids.size())
	{
		std::string type;
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;
		unsigned int loaded = 0;

		// Load nodes information
		if(uStrNumCmp(_version, "0.8.5") >= 0)
		{
			query << "SELECT id, map_id, weight, pose, stamp, label "
				  << "FROM Node "
				  << "WHERE id=?;";
		}
		else
		{
			query << "SELECT id, map_id, weight, pose "
				  << "FROM Node "
				  << "WHERE id=?;";
		}

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		for(std::list<int>::const_iterator iter=ids.begin(); iter!=ids.end(); ++iter)
		{
			//ULOGGER_DEBUG("Loading node %d...", *iter);
			// bind id
			rc = sqlite3_bind_int(ppStmt, 1, *iter);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

			int id = 0;
			int mapId = 0;
			double stamp = 0.0;
			int weight = 0;
			Transform pose;
			const void * data = 0;
			int dataSize = 0;
			std::string label;

			// Process the result if one
			rc = sqlite3_step(ppStmt);
			if(rc == SQLITE_ROW)
			{
				int index = 0;
				id = sqlite3_column_int(ppStmt, index++); // Signature Id
				mapId = sqlite3_column_int(ppStmt, index++); // Map Id
				weight = sqlite3_column_int(ppStmt, index++); // weight

				data = sqlite3_column_blob(ppStmt, index); // pose
				dataSize = sqlite3_column_bytes(ppStmt, index++);
				if((unsigned int)dataSize == pose.size()*sizeof(float) && data)
				{
					memcpy(pose.data(), data, dataSize);
				}

				if(uStrNumCmp(_version, "0.8.5") >= 0)
				{
					stamp = sqlite3_column_double(ppStmt, index++); // stamp
					const unsigned char * p = sqlite3_column_text(ppStmt, index++); // label
					if(p)
					{
						label = reinterpret_cast<const char*>(p);
					}
				}

				rc = sqlite3_step(ppStmt);
			}
			UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

			// create the node
			if(id)
			{
				ULOGGER_DEBUG("Creating %d (map=%d, pose=%s)", *iter, mapId, pose.prettyPrint().c_str());
				Signature * s = new Signature(
						id,
						mapId,
						weight,
						stamp,
						label,
						pose);
				s->setSaved(true);
				nodes.push_back(s);
				++loaded;
			}
			else
			{
				UERROR("Signature %d not found in database!", *iter);
			}

			//reset
			rc = sqlite3_reset(ppStmt);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		ULOGGER_DEBUG("Time=%fs", timer.ticks());

		// Prepare the query... Get the map from signature and visual words
		std::stringstream query2;
		query2 << "SELECT word_id, pos_x, pos_y, size, dir, response, depth_x, depth_y, depth_z "
				 "FROM Map_Node_Word "
				 "WHERE node_id = ? ";

		query2 << " ORDER BY word_id"; // Needed for fast insertion below
		query2 << ";";

		rc = sqlite3_prepare_v2(_ppDb, query2.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		for(std::list<Signature*>::const_iterator iter=nodes.begin(); iter!=nodes.end(); ++iter)
		{
			//ULOGGER_DEBUG("Loading words of %d...", (*iter)->id());
			// bind id
			rc = sqlite3_bind_int(ppStmt, 1, (*iter)->id());
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

			int visualWordId = 0;
			cv::KeyPoint kpt;
			std::multimap<int, cv::KeyPoint> visualWords;
			std::multimap<int, pcl::PointXYZ> visualWords3;
			pcl::PointXYZ depth(0,0,0);

			// Process the result if one
			rc = sqlite3_step(ppStmt);
			while(rc == SQLITE_ROW)
			{
				int index = 0;
				visualWordId = sqlite3_column_int(ppStmt, index++);
				kpt.pt.x = sqlite3_column_double(ppStmt, index++);
				kpt.pt.y = sqlite3_column_double(ppStmt, index++);
				kpt.size = sqlite3_column_int(ppStmt, index++);
				kpt.angle = sqlite3_column_double(ppStmt, index++);
				kpt.response = sqlite3_column_double(ppStmt, index++);
				depth.x = sqlite3_column_double(ppStmt, index++);
				depth.y = sqlite3_column_double(ppStmt, index++);
				depth.z = sqlite3_column_double(ppStmt, index++);
				visualWords.insert(visualWords.end(), std::make_pair(visualWordId, kpt));
				visualWords3.insert(visualWords3.end(), std::make_pair(visualWordId, depth));
				rc = sqlite3_step(ppStmt);
			}
			UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

			if(visualWords.size()==0)
			{
				UDEBUG("Empty signature detected! (id=%d)", (*iter)->id());
			}
			else
			{
				(*iter)->setWords(visualWords);
				(*iter)->setWords3(visualWords3);
				ULOGGER_DEBUG("Add %d keypoints and %d 3d points to node %d", visualWords.size(), visualWords3.size(), (*iter)->id());
			}

			//reset
			rc = sqlite3_reset(ppStmt);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		ULOGGER_DEBUG("Time=%fs", timer.ticks());

		this->loadLinksQuery(nodes);
		for(std::list<Signature*>::iterator iter = nodes.begin(); iter!=nodes.end(); ++iter)
		{
			(*iter)->setModified(false);
		}
		ULOGGER_DEBUG("Time load links=%fs", timer.ticks());

		if(ids.size() != loaded)
		{
			UERROR("Some signatures not found in database");
		}
	}
}

void DBDriverSqlite3::loadLastNodesQuery(std::list<Signature *> & nodes) const
{
	ULOGGER_DEBUG("");
	if(_ppDb)
	{
		std::string type;
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::string query;
		std::list<int> ids;

		query = "SELECT n.id "
				 "FROM Node AS n "
				 "WHERE n.time_enter >= (SELECT MAX(time_enter) FROM Statistics) "
				 "ORDER BY n.id;";

		rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Process the result if one
		rc = sqlite3_step(ppStmt);
		while(rc == SQLITE_ROW)
		{
			ids.push_back(sqlite3_column_int(ppStmt, 0)); 	// Signature id
			rc = sqlite3_step(ppStmt); // next result...
		}

		UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		ULOGGER_DEBUG("Loading %d signatures...", ids.size());
		this->loadSignaturesQuery(ids, nodes);
		ULOGGER_DEBUG("loaded=%d, Time=%fs", nodes.size(), timer.ticks());
	}
}

void DBDriverSqlite3::loadQuery(VWDictionary * dictionary) const
{
	ULOGGER_DEBUG("");
	if(_ppDb && dictionary)
	{
		std::string type;
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::string query;
		std::list<VisualWord *> visualWords;

		// Get the visual words
		query = "SELECT id, descriptor_size, descriptor "
				"FROM Word "
				"WHERE time_enter >= (SELECT MAX(time_enter) FROM Statistics) "
				"ORDER BY id;";

		rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Process the result if one
		int id = 0;
		int descriptorSize = 0;
		const void * descriptor = 0;
		int dRealSize = 0;
		rc = sqlite3_step(ppStmt);
		int count = 0;
		while(rc == SQLITE_ROW)
		{
			int index=0;
			id = sqlite3_column_int(ppStmt, index++); 			// VisualWord Id
			if(id>0)
			{
				descriptorSize = sqlite3_column_int(ppStmt, index++); // VisualWord descriptor size
				descriptor = sqlite3_column_blob(ppStmt, index); 	// VisualWord descriptor array
				dRealSize = sqlite3_column_bytes(ppStmt, index++);

				cv::Mat d;
				if(dRealSize == descriptorSize)
				{
					// CV_8U binary descriptors
					d = cv::Mat(1, descriptorSize, CV_8U);
				}
				else if(dRealSize/int(sizeof(float)) == descriptorSize)
				{
					// CV_32F
					d = cv::Mat(1, descriptorSize, CV_32F);
				}
				else
				{
					UFATAL("Saved buffer size (%d bytes) is not the same as descriptor size (%d)", dRealSize, descriptorSize);
				}

				memcpy(d.data, descriptor, dRealSize);
				VisualWord * vw = new VisualWord(id, d);
				vw->setSaved(true);
				dictionary->addWord(vw);
			}
			else
			{
				ULOGGER_ERROR("Wrong word id ?!? (%d)", id);
			}
			if(++count % 5000 == 0)
			{
				ULOGGER_DEBUG("Loaded %d words...", count);
			}
			rc = sqlite3_step(ppStmt); // next result...
		}
		UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Get Last word id
		getLastWordId(id);
		dictionary->setLastWordId(id);

		ULOGGER_DEBUG("Time=%fs", timer.ticks());
	}
}

//may be slower than the previous version but don't have a limit of words that can be loaded at the same time
void DBDriverSqlite3::loadWordsQuery(const std::set<int> & wordIds, std::list<VisualWord *> & vws) const
{
	ULOGGER_DEBUG("size=%d", wordIds.size());
	if(_ppDb && wordIds.size())
	{
		std::string type;
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;
		std::set<int> loaded;

		// Get the map from signature and visual words
		query << "SELECT vw.descriptor_size, vw.descriptor "
				 "FROM Word as vw "
				 "WHERE vw.id = ?;";

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		int descriptorSize;
		const void * descriptor;
		int dRealSize;
		for(std::set<int>::const_iterator iter=wordIds.begin(); iter!=wordIds.end(); ++iter)
		{
			// bind id
			rc = sqlite3_bind_int(ppStmt, 1, *iter);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

			// Process the result if one
			rc = sqlite3_step(ppStmt);
			if(rc == SQLITE_ROW)
			{
				int index = 0;
				descriptorSize = sqlite3_column_int(ppStmt, index++); // VisualWord descriptor size
				descriptor = sqlite3_column_blob(ppStmt, index); 	// VisualWord descriptor array
				dRealSize = sqlite3_column_bytes(ppStmt, index++);

				cv::Mat d;
				if(dRealSize == descriptorSize)
				{
					// CV_8U binary descriptors
					d = cv::Mat(1, descriptorSize, CV_8U);
				}
				else if(dRealSize/int(sizeof(float)) == descriptorSize)
				{
					// CV_32F
					d = cv::Mat(1, descriptorSize, CV_32F);
				}
				else
				{
					UFATAL("Saved buffer size (%d bytes) is not the same as descriptor size (%d)", dRealSize, descriptorSize);
				}

				memcpy(d.data, descriptor, dRealSize);
				VisualWord * vw = new VisualWord(*iter, d);
				if(vw)
				{
					vw->setSaved(true);
				}
				vws.push_back(vw);
				loaded.insert(loaded.end(), *iter);

				rc = sqlite3_step(ppStmt);
			}

			UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

			rc = sqlite3_reset(ppStmt);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		ULOGGER_DEBUG("Time=%fs", timer.ticks());

		if(wordIds.size() != loaded.size())
		{
			for(std::set<int>::const_iterator iter = wordIds.begin(); iter!=wordIds.end(); ++iter)
			{
				if(loaded.find(*iter) == loaded.end())
				{
					UDEBUG("Not found word %d", *iter);
				}
			}
			UERROR("Query (%d) doesn't match loaded words (%d)", wordIds.size(), loaded.size());
		}
	}
}

void DBDriverSqlite3::loadLinksQuery(
		int signatureId,
		std::map<int, Link> & neighbors,
		Link::Type typeIn) const
{
	neighbors.clear();
	if(_ppDb)
	{
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;

		if(uStrNumCmp(_version, "0.8.4") >= 0)
		{
			query << "SELECT to_id, type, transform, rot_variance, trans_variance FROM Link ";
		}
		else if(uStrNumCmp(_version, "0.7.4") >= 0)
		{
			query << "SELECT to_id, type, transform, variance FROM Link ";
		}
		else
		{
			query << "SELECT to_id, type, transform FROM Link ";
		}
		query << "WHERE from_id = " << signatureId;
		if(typeIn != Link::kUndef)
		{
			if(uStrNumCmp(_version, "0.7.4") >= 0)
			{
				query << " AND type = " << typeIn;
			}
			else if(typeIn == Link::kNeighbor)
			{
				query << " AND type = 0";
			}
			else if(typeIn > Link::kNeighbor)
			{
				query << " AND type > 0";
			}
		}
		query << " ORDER BY to_id";

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		int toId = -1;
		int type = Link::kUndef;
		float rotVariance = 1.0f;
		float transVariance = 1.0f;
		const void * data = 0;
		int dataSize = 0;

		// Process the result if one
		rc = sqlite3_step(ppStmt);
		while(rc == SQLITE_ROW)
		{
			int index = 0;

			toId = sqlite3_column_int(ppStmt, index++);
			type = sqlite3_column_int(ppStmt, index++);

			data = sqlite3_column_blob(ppStmt, index);
			dataSize = sqlite3_column_bytes(ppStmt, index++);

			Transform transform;
			if((unsigned int)dataSize == transform.size()*sizeof(float) && data)
			{
				memcpy(transform.data(), data, dataSize);
			}
			else if(dataSize)
			{
				UERROR("Error while loading link transform from %d to %d! Setting to null...", signatureId, toId);
			}

			if(uStrNumCmp(_version, "0.8.4") >= 0)
			{
				rotVariance = sqlite3_column_double(ppStmt, index++);
				transVariance = sqlite3_column_double(ppStmt, index++);
				neighbors.insert(neighbors.end(), std::make_pair(toId, Link(signatureId, toId, (Link::Type)type, transform, rotVariance, transVariance)));
			}
			else if(uStrNumCmp(_version, "0.7.4") >= 0)
			{
				rotVariance = transVariance = sqlite3_column_double(ppStmt, index++);
				neighbors.insert(neighbors.end(), std::make_pair(toId, Link(signatureId, toId, (Link::Type)type, transform, rotVariance, transVariance)));
			}
			else
			{
				// neighbor is 0, loop closures are 1 and 2 (child)
				neighbors.insert(neighbors.end(), std::make_pair(toId, Link(signatureId, toId, type==0?Link::kNeighbor:Link::kGlobalClosure, transform, rotVariance, transVariance)));
			}

			rc = sqlite3_step(ppStmt);
		}

		UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		if(neighbors.size() == 0)
		{
			//UERROR("No neighbors loaded from signature %d", signatureId);
		}
	}
}

void DBDriverSqlite3::loadLinksQuery(std::list<Signature *> & signatures) const
{
	if(_ppDb)
	{
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::stringstream query;
		int totalLinksLoaded = 0;

		if(uStrNumCmp(_version, "0.8.4") >= 0)
		{
			query << "SELECT to_id, type, rot_variance, trans_variance, transform FROM Link "
				  << "WHERE from_id = ? "
				  << "ORDER BY to_id";
		}
		else if(uStrNumCmp(_version, "0.7.4") >= 0)
		{
			query << "SELECT to_id, type, variance, transform FROM Link "
				  << "WHERE from_id = ? "
				  << "ORDER BY to_id";
		}
		else
		{
			query << "SELECT to_id, type, transform FROM Link "
				  << "WHERE from_id = ? "
				  << "ORDER BY to_id";
		}

		rc = sqlite3_prepare_v2(_ppDb, query.str().c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		for(std::list<Signature*>::iterator iter=signatures.begin(); iter!=signatures.end(); ++iter)
		{
			// bind id
			rc = sqlite3_bind_int(ppStmt, 1, (*iter)->id());
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

			int toId = -1;
			int linkType = -1;
			float rotVariance = 1.0f;
			float transVariance = 1.0f;
			std::list<Link> links;
			const void * data = 0;
			int dataSize = 0;

			// Process the result if one
			rc = sqlite3_step(ppStmt);
			while(rc == SQLITE_ROW)
			{
				int index = 0;

				toId = sqlite3_column_int(ppStmt, index++);
				linkType = sqlite3_column_int(ppStmt, index++);
				if(uStrNumCmp(_version, "0.8.4") >= 0)
				{
					rotVariance = sqlite3_column_double(ppStmt, index++);
					transVariance = sqlite3_column_double(ppStmt, index++);
				}
				else if(uStrNumCmp(_version, "0.7.4") >= 0)
				{
					rotVariance = transVariance = sqlite3_column_double(ppStmt, index++);
				}

				//transform
				data = sqlite3_column_blob(ppStmt, index);
				dataSize = sqlite3_column_bytes(ppStmt, index++);
				Transform transform;
				if((unsigned int)dataSize == transform.size()*sizeof(float) && data)
				{
					memcpy(transform.data(), data, dataSize);
				}
				else if(dataSize)
				{
					UERROR("Error while loading link transform from %d to %d! Setting to null...", (*iter)->id(), toId);
				}

				if(linkType >= 0 && linkType != Link::kUndef)
				{
					if(uStrNumCmp(_version, "0.7.4") >= 0)
					{
						links.push_back(Link((*iter)->id(), toId, (Link::Type)linkType, transform, rotVariance, transVariance));
					}
					else // neighbor is 0, loop closures are 1 and 2 (child)
					{
						links.push_back(Link((*iter)->id(), toId, linkType == 0?Link::kNeighbor:Link::kGlobalClosure, transform, rotVariance, transVariance));
					}
				}
				else
				{
					UFATAL("Not supported link type %d ! (fromId=%d, toId=%d)",
							linkType, (*iter)->id(), toId);
				}

				++totalLinksLoaded;
				rc = sqlite3_step(ppStmt);
			}
			UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

			// add links
			(*iter)->addLinks(links);

			//reset
			rc = sqlite3_reset(ppStmt);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
			UDEBUG("time=%fs, node=%d, links.size=%d", timer.ticks(), (*iter)->id(), links.size());
		}

		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	}
}


void DBDriverSqlite3::updateQuery(const std::list<Signature *> & nodes, bool updateTimestamp) const
{
	UDEBUG("nodes = %d", nodes.size());
	if(_ppDb && nodes.size())
	{
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		Signature * s = 0;

		std::string query;
		if(uStrNumCmp(_version, "0.8.5") >= 0)
		{
			if(updateTimestamp)
			{
				query = "UPDATE Node SET weight=?, label=?, time_enter = DATETIME('NOW') WHERE id=?;";
			}
			else
			{
				query = "UPDATE Node SET weight=?, label=? WHERE id=?;";
			}
		}
		else
		{
			if(updateTimestamp)
			{
				query = "UPDATE Node SET weight=?, time_enter = DATETIME('NOW') WHERE id=?;";
			}
			else
			{
				query = "UPDATE Node SET weight=? WHERE id=?;";
			}
		}
		rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		for(std::list<Signature *>::const_iterator i=nodes.begin(); i!=nodes.end(); ++i)
		{
			s = *i;
			int index = 1;
			if(s)
			{
				rc = sqlite3_bind_int(ppStmt, index++, s->getWeight());
				UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

				if(uStrNumCmp(_version, "0.8.5") >= 0)
				{
					if(s->getLabel().empty())
					{
						rc = sqlite3_bind_null(ppStmt, index++);
						UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
					}
					else
					{
						rc = sqlite3_bind_text(ppStmt, index++, s->getLabel().c_str(), -1, SQLITE_STATIC);
						UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
					}
				}

				rc = sqlite3_bind_int(ppStmt, index++, s->id());
				UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

				//step
				rc=sqlite3_step(ppStmt);
				UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

				rc = sqlite3_reset(ppStmt);
				UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
			}
		}
		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		ULOGGER_DEBUG("Update Node table, Time=%fs", timer.ticks());

		// Update links part1
		query = "DELETE FROM Link WHERE from_id=?;";
		rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		for(std::list<Signature *>::const_iterator j=nodes.begin(); j!=nodes.end(); ++j)
		{
			if((*j)->isLinksModified())
			{
				rc = sqlite3_bind_int(ppStmt, 1, (*j)->id());
				UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

				rc=sqlite3_step(ppStmt);
				UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

				rc = sqlite3_reset(ppStmt);
				UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
			}
		}
		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		// Update links part2
		query = queryStepLink();
		rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		for(std::list<Signature *>::const_iterator j=nodes.begin(); j!=nodes.end(); ++j)
		{
			if((*j)->isLinksModified())
			{
				// Save links
				const std::map<int, Link> & links = (*j)->getLinks();
				for(std::map<int, Link>::const_iterator i=links.begin(); i!=links.end(); ++i)
				{
					stepLink(ppStmt, i->second);
				}
			}
		}
		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		ULOGGER_DEBUG("Update Neighbors Time=%fs", timer.ticks());

		// Update word references
		query = queryStepWordsChanged();
		rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		for(std::list<Signature *>::const_iterator j=nodes.begin(); j!=nodes.end(); ++j)
		{
			if((*j)->getWordsChanged().size())
			{
				const std::map<int, int> & wordsChanged = (*j)->getWordsChanged();
				for(std::map<int, int>::const_iterator iter=wordsChanged.begin(); iter!=wordsChanged.end(); ++iter)
				{
					stepWordsChanged(ppStmt, (*j)->id(), iter->first, iter->second);
				}
			}
		}
		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		ULOGGER_DEBUG("signatures update=%fs", timer.ticks());
	}
}

void DBDriverSqlite3::updateQuery(const std::list<VisualWord *> & words, bool updateTimestamp) const
{
	if(_ppDb && words.size() && updateTimestamp)
	{
		// Only timestamp update is done here, so don't enter this if at all if false
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		VisualWord * w = 0;

		std::string query = "UPDATE Word SET time_enter = DATETIME('NOW') WHERE id=?;";
		rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		for(std::list<VisualWord *>::const_iterator i=words.begin(); i!=words.end(); ++i)
		{
			w = *i;
			int index = 1;
			if(w)
			{
				rc = sqlite3_bind_int(ppStmt, index++, w->id());
				UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

				//step
				rc=sqlite3_step(ppStmt);
				UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

				rc = sqlite3_reset(ppStmt);
				UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
			}
		}
		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		ULOGGER_DEBUG("Update Word table, Time=%fs", timer.ticks());
	}
}

void DBDriverSqlite3::saveQuery(const std::list<Signature *> & signatures) const
{
	UDEBUG("");
	if(_ppDb && signatures.size())
	{
		std::string type;
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;

		// Signature table
		std::string query = queryStepNode();
		rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		for(std::list<Signature *>::const_iterator i=signatures.begin(); i!=signatures.end(); ++i)
		{
			stepNode(ppStmt, *i);
		}
		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		UDEBUG("Time=%fs", timer.ticks());

		// Create new entries in table Link
		query = queryStepLink();
		rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		for(std::list<Signature *>::const_iterator jter=signatures.begin(); jter!=signatures.end(); ++jter)
		{
			// Save links
			const std::map<int, Link> & links = (*jter)->getLinks();
			for(std::map<int, Link>::const_iterator i=links.begin(); i!=links.end(); ++i)
			{
				stepLink(ppStmt, i->second);
			}
		}
		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		UDEBUG("Time=%fs", timer.ticks());


		// Create new entries in table Map_Word_Node
		query = queryStepKeypoint();
		rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		for(std::list<Signature *>::const_iterator i=signatures.begin(); i!=signatures.end(); ++i)
		{
			UASSERT((*i)->getWords3().empty() || (*i)->getWords().size() == (*i)->getWords3().size());
			if((*i)->getWords3().size())
			{
				std::multimap<int, cv::KeyPoint>::const_iterator w=(*i)->getWords().begin();
				std::multimap<int, pcl::PointXYZ>::const_iterator p=(*i)->getWords3().begin();
				for(; w!=(*i)->getWords().end(); ++w, ++p)
				{
					UASSERT(w->first == p->first); // must be same id!
					stepKeypoint(ppStmt, (*i)->id(), w->first, w->second, p->second);
				}
			}
			else
			{
				for(std::multimap<int, cv::KeyPoint>::const_iterator w=(*i)->getWords().begin(); w!=(*i)->getWords().end(); ++w)
				{
					stepKeypoint(ppStmt, (*i)->id(), w->first, w->second, pcl::PointXYZ(0,0,0));
				}
			}
		}
		// Finalize (delete) the statement
		rc = sqlite3_finalize(ppStmt);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		UDEBUG("Time=%fs", timer.ticks());

		if(uStrNumCmp(_version, "0.10.0") >= 0)
		{
			// Add SensorData
			query = queryStepSensorData();
			rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
			UDEBUG("Saving %d images", signatures.size());

			for(std::list<Signature *>::const_iterator i=signatures.begin(); i!=signatures.end(); ++i)
			{
				if(!(*i)->sensorData().imageCompressed().empty())
				{
					UASSERT((*i)->id() == (*i)->sensorData().id());
					stepSensorData(ppStmt, (*i)->sensorData());
				}
			}

			// Finalize (delete) the statement
			rc = sqlite3_finalize(ppStmt);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
			UDEBUG("Time=%fs", timer.ticks());
		}
		else
		{
			// Add images
			query = queryStepImage();
			rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
			UDEBUG("Saving %d images", signatures.size());

			for(std::list<Signature *>::const_iterator i=signatures.begin(); i!=signatures.end(); ++i)
			{
				if(!(*i)->sensorData().imageCompressed().empty())
				{
					stepImage(ppStmt, (*i)->id(), (*i)->sensorData().imageCompressed());
				}
			}

			// Finalize (delete) the statement
			rc = sqlite3_finalize(ppStmt);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
			UDEBUG("Time=%fs", timer.ticks());

			// Add depths
			query = queryStepDepth();
			rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
			for(std::list<Signature *>::const_iterator i=signatures.begin(); i!=signatures.end(); ++i)
			{
				//metric
				if(!(*i)->sensorData().depthOrRightCompressed().empty() || !(*i)->sensorData().laserScanCompressed().empty())
				{
					UASSERT((*i)->id() == (*i)->sensorData().id());
					stepDepth(ppStmt, (*i)->sensorData());
				}
			}
			// Finalize (delete) the statement
			rc = sqlite3_finalize(ppStmt);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}

		UDEBUG("Time=%fs", timer.ticks());
	}
}

void DBDriverSqlite3::saveQuery(const std::list<VisualWord *> & words) const
{
	UDEBUG("visualWords size=%d", words.size());
	if(_ppDb)
	{
		std::string type;
		UTimer timer;
		timer.start();
		int rc = SQLITE_OK;
		sqlite3_stmt * ppStmt = 0;
		std::string query;

		// Create new entries in table Map_SS_VW
		if(words.size()>0)
		{
			query = std::string("INSERT INTO Word(id, descriptor_size, descriptor) VALUES(?,?,?);");
			rc = sqlite3_prepare_v2(_ppDb, query.c_str(), -1, &ppStmt, 0);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
			for(std::list<VisualWord *>::const_iterator iter=words.begin(); iter!=words.end(); ++iter)
			{
				const VisualWord * w = *iter;
				if(w && !w->isSaved())
				{
					rc = sqlite3_bind_int(ppStmt, 1, w->id());
					UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
					rc = sqlite3_bind_int(ppStmt, 2, w->getDescriptor().cols);
					UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
					UASSERT(w->getDescriptor().type() == CV_32F || w->getDescriptor().type() == CV_8U);
					if(w->getDescriptor().type() == CV_32F)
					{
						// CV_32F
						rc = sqlite3_bind_blob(ppStmt, 3, w->getDescriptor().data, w->getDescriptor().cols*sizeof(float), SQLITE_STATIC);
					}
					else
					{
						// CV_8U
						rc = sqlite3_bind_blob(ppStmt, 3, w->getDescriptor().data, w->getDescriptor().cols*sizeof(char), SQLITE_STATIC);
					}
					UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

					//execute query
					rc=sqlite3_step(ppStmt);
					UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

					rc = sqlite3_reset(ppStmt);
					UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
				}
			}
			// Finalize (delete) the statement
			rc = sqlite3_finalize(ppStmt);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}

		UDEBUG("Time=%fs", timer.ticks());
	}
}

std::string DBDriverSqlite3::queryStepNode() const
{
	if(uStrNumCmp(_version, "0.10.1") >= 0)
	{
		return "INSERT INTO Node(id, map_id, weight, pose, stamp, label) VALUES(?,?,?,?,?,?);";
	}
	else if(uStrNumCmp(_version, "0.8.8") >= 0)
	{
		return "INSERT INTO Node(id, map_id, weight, pose, stamp, label, user_data) VALUES(?,?,?,?,?,?,?);";
	}
	else if(uStrNumCmp(_version, "0.8.5") >= 0)
	{
		return "INSERT INTO Node(id, map_id, weight, pose, stamp, label) VALUES(?,?,?,?,?,?);";
	}
	return "INSERT INTO Node(id, map_id, weight, pose) VALUES(?,?,?,?);";
}
void DBDriverSqlite3::stepNode(sqlite3_stmt * ppStmt, const Signature * s) const
{
	UDEBUG("Save node %d", s->id());
	if(!ppStmt || !s)
	{
		UFATAL("");
	}
	int rc = SQLITE_OK;

	int index = 1;
	rc = sqlite3_bind_int(ppStmt, index++, s->id());
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_int(ppStmt, index++, s->mapId());
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_int(ppStmt, index++, s->getWeight());
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_blob(ppStmt, index++, s->getPose().data(), s->getPose().size()*sizeof(float), SQLITE_STATIC);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	if(uStrNumCmp(_version, "0.8.5") >= 0)
	{
		rc = sqlite3_bind_double(ppStmt, index++, s->getStamp());
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

		if(s->getLabel().empty())
		{
			rc = sqlite3_bind_null(ppStmt, index++);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}
		else
		{
			rc = sqlite3_bind_text(ppStmt, index++, s->getLabel().c_str(), -1, SQLITE_STATIC);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}
	}

	if(uStrNumCmp(_version, "0.10.1") >= 0)
	{
		// ignore user_data
	}
	else if(uStrNumCmp(_version, "0.8.8") >= 0)
	{
		if(s->sensorData().userDataCompressed().empty())
		{
			rc = sqlite3_bind_null(ppStmt, index++);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}
		else
		{
			rc = sqlite3_bind_blob(ppStmt, index++, s->sensorData().userDataCompressed().data, (int)s->sensorData().userDataCompressed().cols, SQLITE_STATIC);
			UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		}
	}

	//step
	rc=sqlite3_step(ppStmt);
	UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	rc = sqlite3_reset(ppStmt);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
}

std::string DBDriverSqlite3::queryStepImage() const
{
	UASSERT(uStrNumCmp(_version, "0.10.0") < 0);
	return "INSERT INTO Image(id, data) VALUES(?,?);";
}
void DBDriverSqlite3::stepImage(sqlite3_stmt * ppStmt,
		int id,
		const cv::Mat & imageBytes) const
{
	UASSERT(uStrNumCmp(_version, "0.10.0") < 0);
	UDEBUG("Save image %d (size=%d)", id, (int)imageBytes.cols);
	if(!ppStmt)
	{
		UFATAL("");
	}

	int rc = SQLITE_OK;
	int index = 1;

	rc = sqlite3_bind_int(ppStmt, index++, id);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	if(!imageBytes.empty())
	{
		rc = sqlite3_bind_blob(ppStmt, index++, imageBytes.data, (int)imageBytes.cols, SQLITE_STATIC);
	}
	else
	{
		rc = sqlite3_bind_zeroblob(ppStmt, index++, 4);
	}
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	//step
	rc=sqlite3_step(ppStmt);
	UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	rc = sqlite3_reset(ppStmt);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
}

std::string DBDriverSqlite3::queryStepDepth() const
{
	UASSERT(uStrNumCmp(_version, "0.10.0") < 0);
	if(uStrNumCmp(_version, "0.8.11") >= 0)
	{
		return "INSERT INTO Depth(id, data, fx, fy, cx, cy, local_transform, data2d, data2d_max_pts) VALUES(?,?,?,?,?,?,?,?,?);";
	}
	else if(uStrNumCmp(_version, "0.7.0") >= 0)
	{
		return "INSERT INTO Depth(id, data, fx, fy, cx, cy, local_transform, data2d) VALUES(?,?,?,?,?,?,?,?);";
	}
	else
	{
		return "INSERT INTO Depth(id, data, constant, local_transform, data2d) VALUES(?,?,?,?,?);";
	}
}
void DBDriverSqlite3::stepDepth(sqlite3_stmt * ppStmt, const SensorData & sensorData) const
{
	UASSERT(uStrNumCmp(_version, "0.10.0") < 0);
	UDEBUG("Save depth %d (size=%d) depth2d = %d",
			sensorData.id(),
			(int)sensorData.depthOrRightCompressed().cols,
			(int)sensorData.laserScanCompressed().cols);
	if(!ppStmt)
	{
		UFATAL("");
	}

	int rc = SQLITE_OK;
	int index = 1;

	rc = sqlite3_bind_int(ppStmt, index++, sensorData.id());
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	if(!sensorData.depthOrRightCompressed().empty())
	{
		rc = sqlite3_bind_blob(ppStmt, index++, sensorData.depthOrRightCompressed().data, (int)sensorData.depthOrRightCompressed().cols, SQLITE_STATIC);
	}
	else
	{
		rc = sqlite3_bind_zeroblob(ppStmt, index++, 4);
	}
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	float fx=0, fyOrBaseline=0, cx=0, cy=0;
	Transform localTransform = Transform::getIdentity();
	if(sensorData.cameraModels().size())
	{
		UASSERT_MSG(sensorData.cameraModels().size() == 1,
				uFormat("Database version %s doesn't support multi-camera!", _version.c_str()).c_str());

		fx = sensorData.cameraModels()[0].fx();
		fyOrBaseline = sensorData.cameraModels()[0].fy();
		cx = sensorData.cameraModels()[0].cx();
		cy = sensorData.cameraModels()[0].cy();
		localTransform = sensorData.cameraModels()[0].localTransform();
	}
	else if(sensorData.stereoCameraModel().isValid())
	{
		fx = sensorData.stereoCameraModel().left().fx();
		fyOrBaseline = sensorData.stereoCameraModel().baseline();
		cx = sensorData.stereoCameraModel().left().cx();
		cy = sensorData.stereoCameraModel().left().cy();
		localTransform = sensorData.stereoCameraModel().left().localTransform();
	}

	if(uStrNumCmp(_version, "0.7.0") >= 0)
	{
		rc = sqlite3_bind_double(ppStmt, index++, fx);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		rc = sqlite3_bind_double(ppStmt, index++, fyOrBaseline);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		rc = sqlite3_bind_double(ppStmt, index++, cx);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		rc = sqlite3_bind_double(ppStmt, index++, cy);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	}
	else
	{
		rc = sqlite3_bind_double(ppStmt, index++, 1.0f/fx);
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	}

	rc = sqlite3_bind_blob(ppStmt, index++, localTransform.data(), localTransform.size()*sizeof(float), SQLITE_STATIC);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	if(!sensorData.laserScanCompressed().empty())
	{
		rc = sqlite3_bind_blob(ppStmt, index++, sensorData.laserScanCompressed().data, (int)sensorData.laserScanCompressed().cols, SQLITE_STATIC);
	}
	else
	{
		rc = sqlite3_bind_zeroblob(ppStmt, index++, 4);
	}
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	if(uStrNumCmp(_version, "0.8.11") >= 0)
	{
		rc = sqlite3_bind_int(ppStmt, index++, sensorData.laserScanMaxPts());
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	}

	//step
	rc=sqlite3_step(ppStmt);
	UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	rc = sqlite3_reset(ppStmt);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
}

std::string DBDriverSqlite3::queryStepSensorData() const
{
	UASSERT(uStrNumCmp(_version, "0.10.0") >= 0);
	if(uStrNumCmp(_version, "0.10.1") >= 0)
	{
		return "INSERT INTO Data(id, image, depth, calibration, scan_max_pts, scan, user_data) VALUES(?,?,?,?,?,?,?);";
	}
	else
	{
		return "INSERT INTO Data(id, image, depth, calibration, scan_max_pts, scan) VALUES(?,?,?,?,?,?);";
	}
}
void DBDriverSqlite3::stepSensorData(sqlite3_stmt * ppStmt,
		const SensorData & sensorData) const
{
	UASSERT(uStrNumCmp(_version, "0.10.0") >= 0);
	UDEBUG("Save sensor data %d (image=%d depth=%d) depth2d = %d",
			sensorData.id(),
			(int)sensorData.imageCompressed().cols,
			(int)sensorData.depthOrRightCompressed().cols,
			(int)sensorData.laserScanCompressed().cols);
	if(!ppStmt)
	{
		UFATAL("");
	}

	int rc = SQLITE_OK;
	int index = 1;

	// id
	rc = sqlite3_bind_int(ppStmt, index++, sensorData.id());
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	// image
	if(!sensorData.imageCompressed().empty())
	{
		rc = sqlite3_bind_blob(ppStmt, index++, sensorData.imageCompressed().data, (int)sensorData.imageCompressed().cols, SQLITE_STATIC);
	}
	else
	{
		rc = sqlite3_bind_zeroblob(ppStmt, index++, 4);
	}
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	// depth or right image
	if(!sensorData.depthOrRightCompressed().empty())
	{
		rc = sqlite3_bind_blob(ppStmt, index++, sensorData.depthOrRightCompressed().data, (int)sensorData.depthOrRightCompressed().cols, SQLITE_STATIC);
	}
	else
	{
		rc = sqlite3_bind_zeroblob(ppStmt, index++, 4);
	}
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	// calibration
	std::vector<float> calibration;
	// multi-cameras [fx,fy,cx,cy,local_transform, ... ,fx,fy,cx,cy,local_transform] (4+12)*float * numCameras
	// stereo [fx, fy, cx, cy, baseline, local_transform] (5+12)*float
	if(sensorData.cameraModels().size())
	{
		calibration.resize(sensorData.cameraModels().size() * (4+Transform().size()));
		for(unsigned int i=0; i<sensorData.cameraModels().size(); ++i)
		{
			const Transform & localTransform = sensorData.cameraModels()[i].localTransform();
			calibration[i*(4+localTransform.size())] = sensorData.cameraModels()[i].fx();
			calibration[i*(4+localTransform.size())+1] = sensorData.cameraModels()[i].fy();
			calibration[i*(4+localTransform.size())+2] = sensorData.cameraModels()[i].cx();
			calibration[i*(4+localTransform.size())+3] = sensorData.cameraModels()[i].cy();
			memcpy(calibration.data()+i*(4+localTransform.size())+4, localTransform.data(), localTransform.size()*sizeof(float));
		}
	}
	else if(sensorData.stereoCameraModel().isValid())
	{
		const Transform & localTransform = sensorData.stereoCameraModel().left().localTransform();
		calibration.resize(5+localTransform.size());
		calibration[0] = sensorData.stereoCameraModel().left().fx();
		calibration[1] = sensorData.stereoCameraModel().left().fy();
		calibration[2] = sensorData.stereoCameraModel().left().cx();
		calibration[3] = sensorData.stereoCameraModel().left().cy();
		calibration[4] = sensorData.stereoCameraModel().baseline();
		memcpy(calibration.data()+5, localTransform.data(), localTransform.size()*sizeof(float));
	}

	if(calibration.size())
	{
		rc = sqlite3_bind_blob(ppStmt, index++, calibration.data(), calibration.size()*sizeof(float), SQLITE_STATIC);
	}
	else
	{
		rc = sqlite3_bind_null(ppStmt, index++);
	}
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	// scan_max_pts
	rc = sqlite3_bind_int(ppStmt, index++, sensorData.laserScanMaxPts());
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	// scan
	if(!sensorData.laserScanCompressed().empty())
	{
		rc = sqlite3_bind_blob(ppStmt, index++, sensorData.laserScanCompressed().data, (int)sensorData.laserScanCompressed().cols, SQLITE_STATIC);
	}
	else
	{
		rc = sqlite3_bind_zeroblob(ppStmt, index++, 4);
	}
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	if(uStrNumCmp(_version, "0.10.1") >= 0)
	{
		// user_data
		if(!sensorData.userDataCompressed().empty())
		{
			rc = sqlite3_bind_blob(ppStmt, index++, sensorData.userDataCompressed().data, (int)sensorData.userDataCompressed().cols, SQLITE_STATIC);
		}
		else
		{
			rc = sqlite3_bind_zeroblob(ppStmt, index++, 4);
		}
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	}

	//step
	rc=sqlite3_step(ppStmt);
	UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	rc = sqlite3_reset(ppStmt);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
}

std::string DBDriverSqlite3::queryStepLink() const
{
	if(uStrNumCmp(_version, "0.8.4") >= 0)
	{
		return "INSERT INTO Link(from_id, to_id, type, rot_variance, trans_variance, transform) VALUES(?,?,?,?,?,?);";
	}
	else if(uStrNumCmp(_version, "0.7.4") >= 0)
	{
		return "INSERT INTO Link(from_id, to_id, type, variance, transform) VALUES(?,?,?,?,?);";
	}
	else
	{
		return "INSERT INTO Link(from_id, to_id, type, transform) VALUES(?,?,?,?);";
	}
}
void DBDriverSqlite3::stepLink(
		sqlite3_stmt * ppStmt,
		const Link & link) const
{
	if(!ppStmt)
	{
		UFATAL("");
	}
	UDEBUG("Save link from %d to %d, type=%d", link.from(), link.to(), link.type());

	// Don't save virtual links
	if(link.type()==Link::kVirtualClosure)
	{
		UDEBUG("Virtual link ignored....");
		return;
	}

	int rc = SQLITE_OK;
	int index = 1;
	rc = sqlite3_bind_int(ppStmt, index++, link.from());
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_int(ppStmt, index++, link.to());
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_int(ppStmt, index++, link.type());
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	if(uStrNumCmp(_version, "0.8.4") >= 0)
	{
		rc = sqlite3_bind_double(ppStmt, index++, link.rotVariance());
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
		rc = sqlite3_bind_double(ppStmt, index++, link.transVariance());
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	}
	else if(uStrNumCmp(_version, "0.7.4") >= 0)
	{
		rc = sqlite3_bind_double(ppStmt, index++, link.rotVariance()<link.transVariance()?link.rotVariance():link.transVariance());
		UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	}

	rc = sqlite3_bind_blob(ppStmt, index++, link.transform().data(), link.transform().size()*sizeof(float), SQLITE_STATIC);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	rc=sqlite3_step(ppStmt);
	UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	rc=sqlite3_reset(ppStmt);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
}

std::string DBDriverSqlite3::queryStepWordsChanged() const
{
	return "UPDATE Map_Node_Word SET word_id = ? WHERE word_id = ? AND node_id = ?;";
}
void DBDriverSqlite3::stepWordsChanged(sqlite3_stmt * ppStmt, int nodeId, int oldWordId, int newWordId) const
{
	if(!ppStmt)
	{
		UFATAL("");
	}
	int rc = SQLITE_OK;
	int index = 1;
	rc = sqlite3_bind_int(ppStmt, index++, newWordId);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_int(ppStmt, index++, oldWordId);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_int(ppStmt, index++, nodeId);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	rc=sqlite3_step(ppStmt);
	UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	rc=sqlite3_reset(ppStmt);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
}

std::string DBDriverSqlite3::queryStepKeypoint() const
{
	return "INSERT INTO Map_Node_Word(node_id, word_id, pos_x, pos_y, size, dir, response, depth_x, depth_y, depth_z) VALUES(?,?,?,?,?,?,?,?,?,?);";
}
void DBDriverSqlite3::stepKeypoint(sqlite3_stmt * ppStmt, int nodeId, int wordId, const cv::KeyPoint & kp, const pcl::PointXYZ & pt) const
{
	if(!ppStmt)
	{
		UFATAL("");
	}
	int rc = SQLITE_OK;
	int index = 1;
	rc = sqlite3_bind_int(ppStmt, index++, nodeId);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_int(ppStmt, index++, wordId);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_double(ppStmt, index++, kp.pt.x);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_double(ppStmt, index++, kp.pt.y);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_int(ppStmt, index++, kp.size);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_double(ppStmt, index++, kp.angle);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_double(ppStmt, index++, kp.response);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_double(ppStmt, index++, pt.x);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_double(ppStmt, index++, pt.y);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
	rc = sqlite3_bind_double(ppStmt, index++, pt.z);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	rc=sqlite3_step(ppStmt);
	UASSERT_MSG(rc == SQLITE_DONE, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());

	rc = sqlite3_reset(ppStmt);
	UASSERT_MSG(rc == SQLITE_OK, uFormat("DB error: %s", sqlite3_errmsg(_ppDb)).c_str());
}

} // namespace rtabmap
