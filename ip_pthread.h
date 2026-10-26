#ifndef __IP_PTHREAD_H__
#define __IP_PTHREAD_H__

/**
 * IP 处理线程函数
 * 根据对应的网段选择发送的网卡
 * 没有目的 IP 的 MAC 地址则发送 ARP 请求
 * 参数 void *arg: 待转发的数据
 */
extern void *ip_pthread(void *arg);

#endif // !__IP_PTHREAD_H__
