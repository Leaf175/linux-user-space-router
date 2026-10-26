#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h> //htons
#include <sys/ioctl.h> //ioctl
#include <net/if.h> //struct ifreq
#include <netinet/ether.h> //ETH_P_ALL
#include <netpacket/packet.h>
#include "get_interface.h"
#include "ip_pthread.h"
#include "arp_link.h"
#include "main.h"

int netmask_num = 3; //默认 3 个 255 子网掩码的长度

//ARP 请求数据包
unsigned char arp_buf[42] ={
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, //目标 MAC 地址，广播地址
    0,0,0,0,0,0, //源 MAC 地址，待填充
    0x08,0x06,  //ARP，[12]-->[13]
    0x00,0x01, //硬件类型 [14]-->[15]
    0x08,0x00, // 协议类型，IP [16]-->[17]
    0x06, //硬件地址长度 [18]
    0x04, //协议地址长度 [19]
    0x00,0x01, //ARP 请求 [20]-->[21]
    0,0,0,0,0,0, //源 MAC 地址，待填充 [22]-->[27]
    0,0,0,0,  //源 IP 地址，待填充 [28]-->[31]
    0,0,0,0,0,0, //目标 MAC 地址，待填充 [32]-->[37]
    0,0,0,0  //目标 IP 地址，待填充 [38]-->[41]
};

/**
 * 根据目的 IP 地址查找对应的网段
 * 参数 unsigned char *ip：目的 ip
 * 返回值：网段的编号
 */
int find_network_segment(unsigned char *ip)
{
    int i = 0, j = get_interface_num(); //获取已注册的网络接口总数
    for(i = 0; i < j; i++)
    {
        //比较目的 ip 与网络接口 ip 的前 netmask_num(3) 字节(即比较网段)
        if(memcpy(net_interface[i].ip, ip, netmask_num) == 0)
            break; //如果找到匹配网段，退出循环
    }
    return i; //返回匹配索引，也是网卡数组中匹配的下标
}

/**
 * 根据网卡名称将对应的数据转发出去
 * 参数：
 * char *eth_name: 网络接口名称(如 ens33)
 * unsigned char *msg_data: 转发的数据
 * unsigned long msg_data_len: 数据的长度
 */
int send_msg(char *eth_name, unsigned char *msg_data, unsigned long msg_data_len)
{
    struct ifreq ethreq; //用于 ioctl 获取接口索引的结构体
    strncpy(ethreq.ifr_name, eth_name, IFNAMSIZ); //复制接口名到 ifreq 中
    //通过 SIOCGIFINDEX ioctl 获取接口的网络索引
    if(-1 == ioctl(raw_sock_fd, SIOCGIFINDEX, &ethreq))
    {
        perror("send_msg-ioctl");
        close(raw_sock_fd);
        _exit(-1);
    }
    struct sockaddr_ll sll; //套接字地址结构，用于 PF_PACKET
    bzero(&sll, sizeof(sll));
    sll.sll_ifindex = ethreq.ifr_ifindex; //设置接口索引
    //通过原始套接字往指定的网络接口转发数据包
    int len = sendto(raw_sock_fd, msg_data, msg_data_len, 0, (struct sockaddr *)&sll, sizeof(sll));
    return len; //返回实际发送字节数
}

/**
 * 组建 ARP 请求报文
 * 参数:
 * unsigned char *ip: 目的 ip
 * int network_num: 网段编号
 */
void create_arp_msg(unsigned char *ip, int network_num)
{
    memcpy(arp_buf + 38, ip, 4); //填充目的 IP 地址(偏移 38)
    memcpy(arp_buf + 28, net_interface[network_num].ip, 4); //填充源 IP 地址(本机路由器对应网卡的 ip)
    memcpy(arp_buf + 6, net_interface[network_num].mac, 6); //填充源 MAC 地址(本机路由器对应网卡的 mac)
    memcpy(arp_buf + 22, net_interface[network_num].mac, 6); //填充发送方 MAC 地址(本机路由器对应网卡的 mac))
    //注意：目标 MAC 仍然为 0，因为 ARP 请求中目标 MAC 为广播
}

/**
 * IP 处理线程函数
 * 根据对应的网段选择发送的网卡
 * 没有目的 IP 的 MAC 地址则发送 ARP 请求
 * 参数 void *arg: 待转发的数据
 */
void *ip_pthread(void *arg)
{
    RECV_DATA *msg_data = (RECV_DATA *)arg;
    int network_num = find_network_segment((msg_data->data) + 30);//获取目的 ip 对应的网卡出口
    //若未找到匹配网段(返回值等于网络接口总数)，说明无路由，放弃转发
    if(network_num == get_interface_num())
    {
        return NULL;
    }
    //若目的 IP 为广播地址(如: x.x.x.255)，不转发，避免环路
    if(msg_data->data[33] == 255)
    {
        return NULL;
    }
    //若出口为本地环回地址(lo)，不转发，防止死循环
    if(strcmp(net_interface[network_num].name, "lo") == 0)
    {
        return NULL;
    }
    //在 ARP 缓存链表中查找目标 IP 对应的 MAC 地址
    ARP_LINK *pb = find_arp_from_ip(arp_head, (msg_data->data) + 30);
    //未找到，需发送 ARP 请求
    if(pb == NULL)
    {
        //发送 arp 请求
        int j;
        for(j = 0; j < 3; j++) //连续发送 3 次 arp 请求包，未找到则放弃转发
        {
            create_arp_msg((msg_data->data) + 30, network_num);
            //发送 arp 请求
            int ret = send_msg(net_interface[network_num].name, arp_buf, sizeof(arp_buf));
            if(ret < 0)
            {
                perror("send_msg");
            }
            //重新查询 ARP 缓存，若已经收到响应则跳出
            pb = find_arp_from_ip(arp_head, (msg_data->data) + 30);
            if(pb != NULL)
            {
                break; //找到 MAC 地址，退出重试
            }
        }
        //3 次重试后仍然未获取 MAC，放弃转发
        if(pb == NULL)
        {
            //打印目的 IP 地址
            printf("%d.%d.%d.%d arp error\n", 
                msg_data->data[30], msg_data->data[31], msg_data->data[32], msg_data->data[33]);
            goto IP_PTHREAD_END;
        }
        else
        {
            //打印目的 IP 地址
            printf("%d.%d.%d.%d arp ok\n", 
                msg_data->data[30], msg_data->data[31], msg_data->data[32], msg_data->data[33]);
        }
    }
    //已获取目标 MAC，开始转发 IP 包
    memcpy((msg_data->data), pb->mac, 6);//目的 MAC 设置为目标设备 MAC
    memcpy((msg_data->data) + 6, net_interface[network_num].mac, 6);//源 MAC 设置为本机网络接口的 MAC 地址
    int ret = send_msg(net_interface[network_num].name, msg_data->data, msg_data->data_len); //路由器找到对应的网段的网口将数据转发出去
    if(ret < 0)
    {
        printf("msg_data.data_len=%d\n", (int)msg_data->data_len);
        perror("send_msg");
    }
IP_PTHREAD_END:
    return NULL;
}
