/************************************************************************************
					文件名：GPS.c
					最后一次修改时间：2024/11/12
					修改内容：
*************************************************************************************/


#include "GPS.h"
#include "../../sys/SerialPort/SerialPort.h"

/************************************************************************************
 									全局变量(其他文件可使用)
*************************************************************************************/
/*	保存解析后的数据	*/
gpsDataPack_t g_gps_DataPack = {
    .systemFlag = {0},
    .longitudeDirection = 'x',
    .latitudeDirection = 'x',
    .satelliteNum = 0,
    .isValid = 0,
    .longitude = 0,
    .latitude = 0
};

/************************************************************************************
 									全局变量(仅可本文件使用)
*************************************************************************************/
/*	设备路径	*/
static char *g_gps_DeviceName = "/dev/gnss";

/*	设备的文件描述符	*/
static int g_gps_fd = -1;

/*	读取原始数据之后 存放的数组	*/
static char g_gps_readbuf[1024] = {0};

/*	读写锁	*/
static pthread_rwlock_t g_gps_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/*	旧的串口配置	*/
static struct termios g_gps_oldSerialPortConfig = {0};

 /*******************************************************************
 * 函数原型:int GPS_getFD(void)
 * 函数简介:返回文件描述符
 * 函数参数:无
 * 函数返回值: 文件描述符数值
 *****************************************************************/
int GPS_getFD(void)
{
    return g_gps_fd;
}

 /*******************************************************************
 * 函数原型:int GPS_Init(void)
 * 函数简介:打开GPS串口设备，并且保存旧的配置
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int GPS_Init(void)
{
	/*	0.入口检查	*/
	if(g_gps_DeviceName == NULL || g_gps_fd > 0)
	{
		return -1;
	}

	g_gps_fd = -1;

	/*	1.打开串口	*/
    g_gps_fd = SerialPort_open(g_gps_DeviceName, &g_gps_oldSerialPortConfig);
	if(g_gps_fd < 0)
	{
		return -1;
	}

	/*	2.配置串口	*/
	if(SerialPort_configBaseParams(g_gps_fd, 9600,  1, 8, 'N') < 0)
	{
        close(g_gps_fd);  // 关闭已打开的文件描述符
        g_gps_fd = -1;
		return -1;
	}

	return 0;
}

/*******************************************************************
 * 函数原型:ssize_t GPS_ReadRawData(void)
 * 函数简介:GPS从串口读取数据
 * 函数参数:无
 * 函数返回值: 成功返回读取到的字节个数，失败就返回-1
 *****************************************************************/ 
ssize_t GPS_ReadRawData(void)
{
    /* 0.*入口检查	*/
    if(g_gps_fd < 0 || g_gps_readbuf  == NULL || sizeof(g_gps_readbuf) < 512)
	{
        return -1;
    }

    pthread_rwlock_wrlock(&g_gps_rwlock);
    memset(g_gps_readbuf, 0, sizeof(g_gps_readbuf));

    ssize_t nread = 0;
    char c = 0;
    int i = 0;
    int start = 0;

    while(1)
    {
        nread = read(g_gps_fd, &c, 1);
        if(nread == 1)
        {
            if(c == '$')
            {
                start = 1;
            }
                
            if(start)
            {
                g_gps_readbuf[i++] = c;
            }
            if((c == '\n') && start == 1)
            {
                if(strstr(g_gps_readbuf,"$GNGGA") == NULL)
                {
                    i = 0;
                    start = 0;
                    memset(g_gps_readbuf, 0, sizeof(g_gps_readbuf));
                    continue;
                }
                else
                {
                    g_gps_readbuf[i] = '\0';
                    break;
                }
            }
        }
        else
        {
            break;
        }            
    }
    
    if(g_gps_readbuf[0] != '$' || strncmp(g_gps_readbuf+3,"GGA",3) != 0)
    {
        printf("GPS_ReadRawData:gps data invalid\n");
        memset(g_gps_readbuf, 0, sizeof(g_gps_readbuf));
        strcpy(g_gps_readbuf, "invalid");
        tcflush(g_gps_fd, TCIFLUSH);
        pthread_rwlock_unlock(&g_gps_rwlock);
        return -1;
    }
    
    tcflush(g_gps_fd, TCIFLUSH);
    pthread_rwlock_unlock(&g_gps_rwlock);
    return strlen(g_gps_readbuf);
}

 /*******************************************************************
 * 函数原型:int GPS_ParseData(void)
 * 函数简介:解析GPS数据
 * 函数参数:gpsRawData:原始数据数组
 * 函数返回值:成功返回0，失败返回-1 
 *****************************************************************/
int GPS_ParseData(void)
{
    if(g_gps_readbuf == NULL)
	{
		return -1;
	}
	else if(strncmp(g_gps_readbuf, "invalid", 7) == 0)
	{
		return -1;
	}

    char systemFlag[8] = {'\0'}, utctime[16] = {'\0'}, Lat[16] = {'\0'}, LatDirect[2] = {'\0'};
	char Lng[16] = {'\0'}, LngDirect[2] = {'\0'}, fs[2] = {'\0'}, svNum[4] = {'\0'};
	float fLat = 0.0f, fLng = 0.0f;

    pthread_rwlock_wrlock(&g_gps_rwlock);
    /*  判断是否有信号  */
    /* eg. $GPGGA,082559.00,4005.22599,N,11632.58234,E,1,04,3.08,14.6,M,-5.6,M,,*76"<CR><LF>*/
    if(strstr(g_gps_readbuf, ",,,,,") != NULL)
	{
        sscanf(g_gps_readbuf, "$%[^,],%[^,],%[^,],%[^,],%[^,],%[^,],%[^,],%[^,]" \
            , systemFlag, utctime, Lat, LatDirect, Lng, LngDirect, fs, svNum);

        memcpy(g_gps_DataPack.systemFlag, systemFlag, sizeof(systemFlag));
        g_gps_DataPack.latitudeDirection = 'x';
        g_gps_DataPack.longitudeDirection = 'x';
        g_gps_DataPack.isValid = atoi(fs);
        g_gps_DataPack.latitude = fLat;
        g_gps_DataPack.longitude = fLng;
        g_gps_DataPack.satelliteNum = atoi(svNum);
	}
    else
    {
        sscanf(g_gps_readbuf, "$%[^,],%[^,],%[^,],%[^,],%[^,],%[^,],%[^,],%[^,]" \
            , systemFlag, utctime, Lat, LatDirect, Lng, LngDirect, fs, svNum);

        /* 纬度格式: ddmm.mmmm */
        sscanf(&Lat[2], "%f", &fLat);
        fLat /= 60;
        fLat += (Lat[0] - '0')*10 + (Lat[1] - '0');

        /* 经度格式: dddmm.mmmm */
        sscanf(&Lng[3], "%f", &fLng);
        fLng /=  60;
        fLng += (Lng[0] - '0')*100 + (Lng[1] - '0')*10 + (Lng[2] - '0');

        memcpy(g_gps_DataPack.systemFlag, systemFlag, sizeof(systemFlag));
        g_gps_DataPack.latitudeDirection = LatDirect[0];
        g_gps_DataPack.longitudeDirection = LngDirect[0];
        g_gps_DataPack.isValid = atoi(fs);
        g_gps_DataPack.latitude = fLat;
        g_gps_DataPack.longitude = fLng;
        g_gps_DataPack.satelliteNum = atoi(svNum);
    }
    pthread_rwlock_unlock(&g_gps_rwlock);

    return 0;
}

/*******************************************************************
* 函数原型:char *GPS_DataPackageProcessing(void)
* 函数简介:将GPS的数据进行按格式打包
* 函数参数:无
* 函数返回值: 成功返回打包好的数据地址，失败返回NULL
*****************************************************************/
char *GPS_DataPackageProcessing(void)
{
	static char gpsSendDataBuf[64] = {0};

    sprintf(gpsSendDataBuf, "/%s/%d/%d/%c/%f/%c/%f/", g_gps_DataPack.systemFlag, g_gps_DataPack.isValid, \
        g_gps_DataPack.satelliteNum, g_gps_DataPack.longitudeDirection, g_gps_DataPack.longitude, \
        g_gps_DataPack.latitudeDirection, g_gps_DataPack.latitude);

    return gpsSendDataBuf;
}

/*******************************************************************
 * 函数原型:float GPS_getLongitudeValue(void)
 * 函数简介:获得经度数值。
 * 函数参数:无
 * 函数返回值: 返回经度数值。
 *****************************************************************/
float GPS_getLongitudeValue(void)
{
    return g_gps_DataPack.longitude;
}

/*******************************************************************
 * 函数原型:float GPS_getLatitudeValue(void)
 * 函数简介:获得纬度数值。
 * 函数参数:无
 * 函数返回值: 返回纬度数值。
 *****************************************************************/
float GPS_getLatitudeValue(void)
{
    return g_gps_DataPack.latitude;
}

/*******************************************************************
* 函数原型:void GPS_printSensorData(gpsDataPack_p psensorData)
* 函数简介:打印传感器采集的保存在数据结构体中的数据
* 函数参数:g_pdvlDataPack指向的DVL数据结构体
* 函数返回值: 无
*****************************************************************/ 
void GPS_PrintAllData(void)
{
	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
    printf("TIME: %d-%02d-%02d %02d:%02d:%02d\n",nowtime->tm_year + 1900,nowtime->tm_mon + 1,nowtime->tm_mday,\
    											nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);
	
	printf("************GPS数据信息**************\n");
	printf("系统标识符:%s\n", g_gps_DataPack.systemFlag);
    printf("数据有效:%d\n", g_gps_DataPack.isValid);
    printf("定位卫星数:%d\n", g_gps_DataPack.satelliteNum);
	printf("经度方向:%c\n", g_gps_DataPack.longitudeDirection);
    printf("经度:%f\n", g_gps_DataPack.longitude);
	printf("纬度方向:%c\n", g_gps_DataPack.latitudeDirection);
	printf("纬度:%f\n", g_gps_DataPack.latitude);
	printf("************************************\n");
}

