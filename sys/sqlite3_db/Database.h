/************************************************************************************
					文件名：Database.h
					最后一次修改时间：2024/11/26
					修改内容：
*************************************************************************************/


#ifndef __DATABASE_H__
#define __DATABASE_H__ 


/************************************************************************************
 								包含头文件
*************************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sqlite3.h>


#include "../../drivers/gps/GPS.h"
#include "../../drivers/maincabin/MainCabin.h"
#include "../../drivers/ctd/CTD.h"
#include "../../drivers/dvl/DVL.h"
#include "../../drivers/dtu/DTU.h"
#include "../../drivers/usbl/USBL.h"
#include "../../drivers/sonar/Sonar.h"


/************************************************************************************
								数据类型
*************************************************************************************/
sqlite3 *g_database;                        	//数据库指针


/************************************************************************************
 								函数原型
*************************************************************************************/
/*	数据库初始化	*/
sqlite3 *Database_init(sqlite3 *db);

/*	GPS*/
int Database_insertGPSData(sqlite3 *db, gpsDataPack_t *psensor);

/*	MainCabin*/
int Database_insertMainCabinData(sqlite3 *db, maincabinDataPack_t *psensor);

/*	CTD	*/
int Database_insertCTDData(sqlite3 *db, ctdDataPack_t *psensor);

/*	DVL	*/
int Database_insertDVLData(sqlite3 *db, dvlDataPack_t *psensor);

/*	USBL	*/
int Database_insertUSBLData(sqlite3 *db, usblDataPack_t *psensor);

/*	DTU	*/
int Database_insertDTURecvData(sqlite3 *db, unsigned char *dtuRecvData);

/*	Sonar	*/
int Database_insertSonarData(sqlite3 *db, sonarDataPack_t *psensor);

/*	ConnectHost*/
int Database_insertTCPRecvData(sqlite3 *db, char *tcpRecvData);

#endif

