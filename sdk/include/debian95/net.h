/* Debian95 — networking: PCI, RTL8139 NIC, and a small IPv4 stack with a
 * default-deny firewall. */
#ifndef DEBIAN95_NET_H
#define DEBIAN95_NET_H

#include <debian95/types.h>

/* PCI */
typedef struct { u8 bus, slot, func, irq; u16 io_base; } PciDev;
int pci_find(u16 vendor, u16 device, PciDev *out);

/* RTL8139 */
int  rtl8139_init(void);
int  rtl8139_present(void);
void rtl8139_mac(u8 *out);
int  rtl8139_send(const void *data, u16 len);
int  rtl8139_poll(void *out, u16 maxlen);

/* IPv4 stack */
int  net_init(void);                 /* 0 if a NIC is present + configured */
void net_poll(void);                 /* service one received frame (from the desktop) */
int  net_arp(u32 ip);                /* resolve ip -> MAC (blocking, cached); 0 = ok */
int  net_ping(u32 ip);               /* ICMP echo; 0 = reply received, -1 = timeout */
int  net_dhcp(void);                 /* obtain IP/gw/dns via DHCP; 0 = ok */
int  net_dns(const char *host, u32 *out);   /* resolve a hostname (A record); 0 = ok */
int  net_http_get(const char *host, const char *path, char *out, int outmax); /* TCP HTTP/1.0 GET; returns bytes or -1 */
void net_listen(u16 port, const char *html_body);   /* start the built-in HTTP server */
void net_unlisten(void);                             /* stop it */
void net_status(char *line, int n);  /* one-line status */
void net_ip_to_str(u32 ip, char *out);
u32  net_local_ip(void);
u32  net_gateway_ip(void);
u32  net_dns_ip(void);

/* firewall */
void net_firewall_set(int on);       /* default-deny on/off */
int  net_firewall_on(void);
u32  net_firewall_drops(void);

#define IP4(a,b,c,d) (((u32)(a)<<24)|((u32)(b)<<16)|((u32)(c)<<8)|(u32)(d))

#endif /* DEBIAN95_NET_H */
