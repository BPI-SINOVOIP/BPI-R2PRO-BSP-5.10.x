#ifndef __WIFI_H__
#define __WIFI_H__

#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <net/if.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if_arp.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <paths.h>
#include <sys/wait.h>

#include "wl.h"

#ifdef SYSLOG_DEBUG
#define pr_debug(fmt, ...)              syslog(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)               syslog(LOG_INFO, fmt, ##__VA_ARGS__)
#define pr_warning(fmt, ...)    syslog(LOG_WARNING, fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)                syslog(LOG_ERR, fmt, ##__VA_ARGS__)
#else
#define pr_debug        printf
#define pr_info printf
#define pr_warning printf
#define pr_err printf
#endif

#define DEFAULT_IFNAME				"wlan0"
#define JOIN_AP_RETRIES				3
#define MAX_SAME_AP_NUM				5
#define MAX_SCAN_NUM				5
#define TCP_CONNECT_RETRIES			3

#define TCPKA_INTERVAL 				180
#define TCPKA_RETRY_INTERVAL		4
#define TCPKA_RETRY_COUNT 			15
#define ETHERNET_HEADER_LEN			14
#define IPV4_HEADER_FIXED_LEN		20
#define TCP_HEADER_FIXED_LEN		20
#define TCP_OPTIONS_LEN				12
#define WOWL_PATTERN_MATCH_OFFSET	\
			(ETHERNET_HEADER_LEN + IPV4_HEADER_FIXED_LEN + TCP_HEADER_FIXED_LEN + TCP_OPTIONS_LEN)
#define CUSTOM_LISTEN_INTERVAL		1000

#define SOL_TCP			6
#define TCP_EXT_INFO	37

struct tcp_ext_info {
	uint16_t ip_id;
	uint16_t dummy;
	uint32_t snd_nxt;
	uint32_t rcv_nxt;
	uint32_t window;
	uint32_t tsval;
	uint32_t tsecr;
};

extern const char tcpka_payload[];
extern const char wowl_pattern[];

typedef struct
{
	uint32_t ssid_len;
    char ssid[32];
    uint8_t bssid[6];
    uint32_t security;
    uint16_t channel;
    int16_t rssi;
} wifi_ap_info_t;

/*
 * ����   : wifiģ���ʼ��.
 * ����   : ��
 * ����ֵ : ��
 */
int WIFI_Init(void);

/*
 * ����   : wifiģ���˳�.
 * ����   : ��
 * ����ֵ : ��
 */
void WIFI_Deinit(void);

/*
 * ����   : WIFI�Ͽ�.
 * ����   : ��
 * ����ֵ : ��
 */
void WIFI_Disconnect(void);

/*
 * ����   : ����ָ������[��̬/��̬/OPEN].
 * ����   : 
 *          ssid : Ŀ��SSID.
            pass : ����.
			useip: 0/��̬  1/��̬  2/OPEN
 * ����ֵ : ��
 */
int WIFI_Connect(char *ssid, char *pass, int  useip);

/*
 * ����   : ��ȡ��������״̬.
 * ����   : ��
 * ����ֵ : ��
 */
int WIFI_GetStatus(void);

/*
 * ����   : ��ȡWIFI�����ŵ�.
 * ����   : ��
 * ����ֵ : [0-13]/WIFI�����ŵ�.
 */
int WIFI_GetChannel(void);

/*
 * ����   : ��ȡWIFI���ѷ�ʽ.
 * ����   : ��
 * ����ֵ : WIFI����ԭ����.
 */
int WIFI_GetWakupReason(void);
/*
 * ����   : ��ȡWIFI�ź�ǿ��.
 * ����   : ��
 * ����ֵ : WIFI�ź�ǿ��.
 */
int WIFI_GetSignal(void);

/*
 * ����   : ����WIFI��̬IP��Ϣ.
 * ����   : 
 			ipAddr      :    IP��ַ��Ϣ.
			netmask     :    ��������.
			gateway     :    ����.

 * ����ֵ :   0/�ɹ�
 *          ��0/ʧ��
 */
int WIFI_SetNetInfo(char *ipAddr, char *netmask, char *gateway, char *dns);

/*
 * ����   : ��ȡWIFI������ʽ.
 * ����   : ��
 * ����ֵ : 0/����  1/PIR��������  2/WIFI����  3/����
 */
int WIFI_StartUpMode(void);

/*
 * ����   : ������������SSID����.
 * ����   : 
            ssid         : Ŀ��SSID.
			channel_num  : ɨ��ͨ����,0/ȫ�ŵ�ɨ�� [1-12]/���ŵ�ɨ��.
			interval     : ɨ������.
 * ����ֵ :   0/�ɹ�
 *          ��0/ʧ��
 */
int WIFI_SetWakeupBySsid(char *ssid, char channelNum, unsigned short interval); 
/*
 * ����   : �������ߺ����SSID����.
 * ����   : ��
 * ����ֵ :   0/�ɹ�
 *          ��0/ʧ��
 */
int WIFI_ClearWakeupSsid(void);

/*
 * ����   : ����WIFI������ʽ.
 * ����   : 
 *            WakeupFlag : 0/������
 *						   1/������
 * ����ֵ : ��
 */
int WIFI_SetQuickStartWay(int WakeupFlag);

/*
 * ����   : ɨ���ܱ�����.
 * ����   : ��
 * ����ֵ : ��
 */
int WIFI_ClientScan(char *ssid);

/*
 * ����   : ��ȡɨ����.
 * ����   : 
 *	 		   pstResults : ɨ����.
 *             num        : AP����.
 * ����ֵ :   0/�ɹ�
 *          ��0/ʧ��
 */
int WIFI_GetClientScanResults(wifi_ap_info_t *pstResults, int num);

/*
 * ����   : �������߱���״̬.
 * ����   : ��
 * ����ֵ :   0/�ɹ�
 *          ��0/ʧ��
 */
int WIFI_Suspend(int sock);
int WIFI_Resume(void);

int rk_build_tcpka_param(wl_tcpka_param_t* param, char* ifname, int sock);
void rk_system_return(const char cmdline[], char recv_buff[], int len);
int rk_system(const char *cmd);
int rk_obtain_ip_from_vendor(char * ifname);
int rk_obtain_ip_from_udhcpc(char* ifname);

#endif /* __WIFI_H__ */
