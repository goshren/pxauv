/************************************************************************************
					文件名：Database.c
					最后一次修改时间：2025/7/3
					修改内容：
*************************************************************************************/

#include "Database.h"

/************************************************************************************
 									全局变量
*************************************************************************************/
sqlite3 *g_database = NULL;


/************************************************************************************
 									指令
*************************************************************************************/
/*	GPS	*/
const char Sql_createGPSTable[180] = {"create table GPS(time char, systemFlag char, isValid int, satelliteNum int, longitudeDirection char, longitude float, latitudeDirection char, latitude float);"}; 

/*	MainCabin	*/
const char Sql_createMainCabinTable[180] = {"create table MainCabin(time char, temperature float, humidity float, pressure float, isLeak01 char, isLeak02 char, deviceState char, releaser1State char, releaser2State char);"};

/*	CTD	*/
const char Sql_createCTDTable[180] = {"create table CTD(time char, temperature float, conductivity float, pressure float, depth float, salinity float, soundVelocity float, density float);"};

/*	DVL*/
const char Sql_createDVLTable[180] = {"create table DVL(time char, pitch float, roll float, heading float, transducerEntryDepth float, speedX float, speedY float, speedZ float, buttomDistance float);"};

/*	USBL*/
const char Sql_createUSBLTable[64] = {"create table USBL(time char, recvdata char);"};

/*	DTU*/
const char Sql_createDTUTable[64] = {"create table DTU(time char, recv char);"};

/*	Sonar*/
const char Sql_createSonarTable[64] = {"create table Sonar(time char, bearing float, distance float);"};

/*	ConnectHost	*/
const char Sql_createTCPRecvTable[64] = {"create table TCP(time char, recv char);"};

/*******************************************************************
 * 函数原型:int Database_init(sqlite3 *db)
 * 函数简介:初始化sqlite3，主要进行创建数据库，并创建表。
 * 函数参数:db:数据库指针
 * 函数返回值: 成功返回数据库指针，失败返回 NULL
 *****************************************************************/
sqlite3 *Database_init(sqlite3 *db)
{
    time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
	char time[32] = {0};
    sprintf(time, "%d-%02d-%02d--%02d:%02d:%02d.db",nowtime->tm_year + 1900,nowtime->tm_mon + 1,nowtime->tm_mday,\
    											nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);
	char filename[64] = {"../database/"};
	strcat(filename,time);

	if(sqlite3_open(filename, &db) != 0)
	{
		fprintf(stderr, "database init error:open sqlite3 database failed: %s\n", sqlite3_errmsg(db));
		return NULL;
	}

    char *errmsg = NULL;
	do
	{
		if(sqlite3_exec(db, Sql_createGPSTable, NULL, NULL, &errmsg) != SQLITE_OK)
		{
			fprintf(stderr, "database init error:create GPS table error: %s\n", errmsg);
			break;
		}
		else if(sqlite3_exec(db, Sql_createMainCabinTable, NULL, NULL, &errmsg) != SQLITE_OK)
		{
			fprintf(stderr, "database init error:create MainCabin table error: %s\n", errmsg);
			break;
		}
		else if(sqlite3_exec(db, Sql_createCTDTable, NULL, NULL, &errmsg) != SQLITE_OK)
		{
			fprintf(stderr, "database init error:create CTD table error: %s\n", errmsg);
			break;
		}
		else if(sqlite3_exec(db, Sql_createDVLTable, NULL, NULL, &errmsg) != SQLITE_OK)
		{
			fprintf(stderr, "database init error:create DVL table error: %s\n", errmsg);
			break;
		}
		else if(sqlite3_exec(db, Sql_createUSBLTable, NULL, NULL, &errmsg) != SQLITE_OK)
		{
			fprintf(stderr, "database init error:create USBL table error: %s\n", errmsg);
			break;
		}
		else if(sqlite3_exec(db, Sql_createDTUTable, NULL, NULL, &errmsg) != SQLITE_OK)
		{
			fprintf(stderr, "database init error:create DTU table error: %s\n", errmsg);
			break;
		}
		else if(sqlite3_exec(db, Sql_createSonarTable, NULL, NULL, &errmsg) != SQLITE_OK)
		{
			fprintf(stderr, "database init error:create Sonar table error: %s\n", errmsg);
			break;
		}
		else if(sqlite3_exec(db, Sql_createTCPRecvTable, NULL, NULL, &errmsg) != SQLITE_OK)
		{
			fprintf(stderr, "database init error:create TCP table error: %s\n", errmsg);
			break;
		}

		return db;
	}while(0);
	
	sqlite3_free(errmsg);
	return NULL;
}

/*******************************************************************
 * 函数原型:int Database_insertGPSData(sqlite3 *db, gpsDataPack_t *psensor)
 * 函数简介:保存GPS采集的数据到数据库
 * 函数参数:db:数据库指针，psensor:GPS数据结构体指针
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int Database_insertGPSData(sqlite3 *db, gpsDataPack_t *psensor)
{
	if(db == NULL)
	{
		fprintf(stderr, "database insert gps data error: db NULL\n");
		return -1;
	}
	else if(psensor == NULL)
	{
		fprintf(stderr, "database insert gps data error: psensor NULL\n");
		return -1;
	}
	else if(strlen(psensor->systemFlag) == 0)
	{
		fprintf(stderr, "database insert gps data error: nodata\n");
		return -1;
	}

	char *Sql_insert = NULL;
	if((Sql_insert = (char*)malloc(sizeof(char) * 256)) == NULL)
	{
		fprintf(stderr, "database insert gps data error: malloc failed\n");
		return -1;
	}

	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
	char time[32] = {0};
    sprintf(time, "%02d:%02d:%02d",nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);

	sprintf(Sql_insert,"insert into GPS values('%s','%s',%d,%d,'%c',%f,'%c',%f);", time, psensor->systemFlag, psensor->isValid, psensor->satelliteNum, \
			psensor->longitudeDirection, psensor->longitude, psensor->latitudeDirection, psensor->latitude);

	char *errmsg = NULL;
	if(sqlite3_exec(db, Sql_insert, NULL, NULL, &errmsg) != SQLITE_OK)
	{
		fprintf(stderr, "database insert gps data error:%s", errmsg);
		free(Sql_insert);
		sqlite3_free(errmsg);
		return -1;
	}

	free(Sql_insert);
	sqlite3_free(errmsg);
	return 0;
}

/*******************************************************************
 * 函数原型:int Database_insertMainCabinData(sqlite3 *db, maincabinDataPack_t *psensor)
 * 函数简介:保存主控舱采集的数据到数据库
 * 函数参数:db:数据库指针，psensor:主控舱数据结构体指针
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int Database_insertMainCabinData(sqlite3 *db, maincabinDataPack_t *psensor)
{
	if(db == NULL)
	{
		fprintf(stderr, "database insert maincabin data error: db NULL\n");
		return -1;
	}
	else if(psensor == NULL)
	{
		fprintf(stderr, "database insert maincabin data error: psensor NULL\n");
		return -1;
	}

	char *Sql_insert = NULL;
	if((Sql_insert = (char*)malloc(sizeof(char) * 256)) == NULL)
	{
		fprintf(stderr, "database insert maincabin data error: malloc failed\n");
		return -1;
	}

	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
	char time[32] = {0};
    sprintf(time, "%02d:%02d:%02d",nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);

	sprintf(Sql_insert,"insert into MainCabin values('%s',%f,%f,%f,'%s','%s','%s','%s','%s');", time, psensor->temperature, psensor->humidity, psensor->pressure, \
			psensor->isLeak01, psensor->isLeak02, psensor->deviceState, psensor->releaser1State, psensor->releaser1State);

	char *errmsg = NULL;
	if(sqlite3_exec(db, Sql_insert, NULL, NULL, &errmsg) != SQLITE_OK)
	{
		fprintf(stderr, "database insert maincabin data error:%s", errmsg);
		free(Sql_insert);
		sqlite3_free(errmsg);
		return -1;
	}

	free(Sql_insert);
	sqlite3_free(errmsg);
	return 0;
}

/*******************************************************************
 * 函数原型:int Database_insertCTDData(sqlite3 *db, ctdDataPack_t *psensor)
 * 函数简介:保存CTD采集的数据到数据库
 * 函数参数:db:数据库指针，psensor:CTD数据结构体指针
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int Database_insertCTDData(sqlite3 *db, ctdDataPack_t *psensor)
{
	if(db == NULL)
	{
		fprintf(stderr, "database insert ctd data error: db NULL\n");
		return -1;
	}
	else if(psensor == NULL)
	{
		fprintf(stderr, "database insert ctd data error: psensor NULL\n");
		return -1;
	}

	char *Sql_insert = NULL;
	if((Sql_insert = (char*)malloc(sizeof(char) * 256)) == NULL)
	{
		fprintf(stderr, "database insert ctd data error: malloc failed\n");
		return -1;
	}

	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
	char time[32] = {0};
    sprintf(time, "%02d:%02d:%02d",nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);

	sprintf(Sql_insert,"insert into CTD values('%s',%6.2f,%6.2f,%6.2f,%6.2f,%6.2f,%6.2f,%6.2f);", time, psensor->temperature, psensor->conductivity, psensor->pressure, \
			psensor->depth, psensor->salinity, psensor->soundVelocity, psensor->density);

	char *errmsg = NULL;
	if(sqlite3_exec(db, Sql_insert, NULL, NULL, &errmsg) != SQLITE_OK)
	{
		fprintf(stderr, "database insert ctd data error:%s", errmsg);
		free(Sql_insert);
		sqlite3_free(errmsg);
		return -1;
	}

	free(Sql_insert);
	sqlite3_free(errmsg);
	return 0;
}

/*******************************************************************
 * 函数原型:int Database_insertDVLData(sqlite3 *db, dvlDataPack_t *psensor)
 * 函数简介:保存DVL采集的数据到数据库
 * 函数参数:db:数据库指针，psensor:DVL数据结构体指针
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int Database_insertDVLData(sqlite3 *db, dvlDataPack_t *psensor)
{
	if(db == NULL)
	{
		fprintf(stderr, "database insert dvl data error: db NULL\n");
		return -1;
	}
	else if(psensor == NULL)
	{
		fprintf(stderr, "database insert dvl data error: psensor NULL\n");
		return -1;
	}

	char *Sql_insert = NULL;
	if((Sql_insert = (char*)malloc(sizeof(char) * 256)) == NULL)
	{
		fprintf(stderr, "database insert dvl data error: malloc failed\n");
		return -1;
	}

	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
	char time[32] = {0};
    sprintf(time, "%02d:%02d:%02d",nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);

	sprintf(Sql_insert,"insert into DVL values('%s',%6.2f,%6.2f,%6.2f,%6.2f,%6.2f,%6.2f,%6.2f,%6.2f);", time, psensor->pitch, psensor->roll, psensor->heading, \
			psensor->transducerEntryDepth, psensor->speedX, psensor->speedY, psensor->speedZ, psensor->buttomDistance);

	char *errmsg = NULL;
	if(sqlite3_exec(db, Sql_insert, NULL, NULL, &errmsg) != SQLITE_OK)
	{
		fprintf(stderr, "database insert dvl data error:%s", errmsg);
		free(Sql_insert);
		sqlite3_free(errmsg);
		return -1;
	}

	free(Sql_insert);
	sqlite3_free(errmsg);
	return 0;
}

/*******************************************************************
 * 函数原型:int Database_insertUSBLData(sqlite3 *db, usblDataPack_t *psensor)
 * 函数简介:保存USBL接收的的数据到数据库
 * 函数参数:db:数据库指针，psensor:USBL数据结构体指针
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int Database_insertUSBLData(sqlite3 *db, usblDataPack_t *psensor)
{
	if(db == NULL)
	{
		fprintf(stderr, "database insert usbl data error: db NULL\n");
		return -1;
	}
	else if(psensor == NULL)
	{
		fprintf(stderr, "database insert usbl data error: psensor NULL\n");
		return -1;
	}

	char *Sql_insert = NULL;
	if((Sql_insert = (char*)malloc(sizeof(char) * 256)) == NULL)
	{
		fprintf(stderr, "database insert usbl data error: malloc failed\n");
		return -1;
	}

	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
	char time[32] = {0};
    sprintf(time, "%02d:%02d:%02d",nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);

	sprintf(Sql_insert,"insert into USBL values('%s','%s');", time, psensor->recvdata);

	char *errmsg = NULL;
	if(sqlite3_exec(db, Sql_insert, NULL, NULL, &errmsg) != SQLITE_OK)
	{
		fprintf(stderr, "database insert usbl data error:%s", errmsg);
		free(Sql_insert);
		sqlite3_free(errmsg);
		return -1;
	}

	free(Sql_insert);
	sqlite3_free(errmsg);
	return 0;
}

/*******************************************************************
 * 函数原型:int Database_insertDTURecvData(sqlite3 *db, unsigned char *dtuRecvData)
 * 函数简介:保存DTU接收的的数据到数据库
 * 函数参数:db:数据库指针，dtuRecvData:DTU接收数据的数组
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int Database_insertDTURecvData(sqlite3 *db, unsigned char *dtuRecvData)
{
	if(db == NULL)
	{
		fprintf(stderr, "database insert dtu data error: db NULL\n");
		return -1;
	}
	else if(dtuRecvData == NULL)
	{
		fprintf(stderr, "database insert dtu data error: psensor NULL\n");
		return -1;
	}

	char *Sql_insert = NULL;
	if((Sql_insert = (char*)malloc(sizeof(char) * 256)) == NULL)
	{
		fprintf(stderr, "database insert dtu data error: malloc failed\n");
		return -1;
	}

	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
	char time[32] = {0};
    sprintf(time, "%02d:%02d:%02d",nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);
	sprintf(Sql_insert,"insert into DTU values('%s','%s');", time, dtuRecvData);

	char *errmsg = NULL;
	if(sqlite3_exec(db, Sql_insert, NULL, NULL, &errmsg) != SQLITE_OK)
	{
		fprintf(stderr, "database insert dtu data error:%s", errmsg);
		free(Sql_insert);
		sqlite3_free(errmsg);
		return -1;
	}

	free(Sql_insert);
	sqlite3_free(errmsg);
	return 0;
}

/*******************************************************************
 * 函数原型:int Database_insertSonarData(sqlite3 *db, sonarDataPack_p psensor)
 * 函数简介:保存Sonar接收的的数据到数据库
 * 函数参数:db:数据库指针，psensor:Sonar数据结构体指针
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int Database_insertSonarData(sqlite3 *db, sonarDataPack_t *psensor)
{
	if(db == NULL)
	{
		fprintf(stderr, "database insert sonar data error: db NULL\n");
		return -1;
	}
	else if(psensor == NULL)
	{
		fprintf(stderr, "database insert sonar data error: psensor NULL\n");
		return -1;
	}

	char *Sql_insert = NULL;
	if((Sql_insert = (char*)malloc(sizeof(char) * 256)) == NULL)
	{
		fprintf(stderr, "database insert sonar data error: malloc failed\n");
		return -1;
	}

	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
	char time[32] = {0};
    sprintf(time, "%02d:%02d:%02d",nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);
	sprintf(Sql_insert,"insert into Sonar values('%s',%05.1f,%04.1f);", time, psensor->obstaclesBearing, psensor->obstaclesDistance);

	char *errmsg = NULL;
	if(sqlite3_exec(db, Sql_insert, NULL, NULL, &errmsg) != SQLITE_OK)
	{
		fprintf(stderr, "database insert sonar data error:%s", errmsg);
		free(Sql_insert);
		sqlite3_free(errmsg);
		return -1;
	}

	free(Sql_insert);
	sqlite3_free(errmsg);
	return 0;
}
/*******************************************************************
 * 函数原型:int Database_insertTCPRecvData(sqlite3 *db, char *tcpRecvData)
 * 函数简介:保存TCP接收的的数据到数据库
 * 函数参数:db:数据库指针，tcpRecvData:TCP接收数据的数组
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int Database_insertTCPRecvData(sqlite3 *db, char *tcpRecvData)
{
	if(db == NULL)
	{
		fprintf(stderr, "database insert tcp data error: db NULL\n");
		return -1;
	}
	else if(tcpRecvData == NULL)
	{
		fprintf(stderr, "database insert tcp data error: psensor NULL\n");
		return -1;
	}

	char *Sql_insert = NULL;
	if((Sql_insert = (char*)malloc(sizeof(char) * 256)) == NULL)
	{
		fprintf(stderr, "database insert tcp data error: malloc failed\n");
		return -1;
	}

	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
	char time[32] = {0};
    sprintf(time, "%02d:%02d:%02d",nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);
	sprintf(Sql_insert,"insert into TCP values('%s','%s');", time, tcpRecvData);

	char *errmsg = NULL;
	if(sqlite3_exec(db, Sql_insert, NULL, NULL, &errmsg) != SQLITE_OK)
	{
		fprintf(stderr, "database insert tcp data error:%s", errmsg);
		free(Sql_insert);
		sqlite3_free(errmsg);
		return -1;
	}

	free(Sql_insert);
	sqlite3_free(errmsg);
	return 0;
}

