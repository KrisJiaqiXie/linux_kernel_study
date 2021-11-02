/* vi: set sw=4 ts=4: */
/*
 * ll_types.c
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */
#include <arpa/inet.h>
#include <linux/if_arp.h>

#include "libbb.h"
#include "rt_names.h"

const char* ll_type_n2a(int type, char *buf, int len)
{
#define __PF(f,n) { ARPHRD_##f, #n },
static const struct {
	int type;
	const char *name;
} arphrd_names[] = {
{ 0, "generic" },
__PF(ETHER,ether)
__PF(EETHER,eether)
__PF(AX25,ax25)
__PF(PRONET,pronet)
__PF(CHAOS,chaos)
#ifdef ARPHRD_IEEE802_TR
__PF(IEEE802,ieee802)
#else
__PF(IEEE802,tr)
#endif
__PF(ARCNET,arcnet)
__PF(APPLETLK,atalk)
__PF(DLCI,dlci)
#ifdef ARPHRD_ATM
__PF(ATM,atm)
#endif
__PF(METRICOM,metricom)
#ifdef ARPHRD_IEEE1394
__PF(IEEE1394,ieee1394)
#endif

__PF(SLIP,slip)
__PF(CSLIP,cslip)
__PF(SLIP6,slip6)
__PF(CSLIP6,cslip6)
__PF(RSRVD,rsrvd)
__PF(ADAPT,adapt)
__PF(ROSE,rose)
__PF(X25,x25)
#ifdef ARPHRD_HWX25
__PF(HWX25,hwx25)
#endif
__PF(PPP,ppp)
__PF(HDLC,hdlc)
__PF(LAPB,lapb)
#ifdef ARPHRD_DDCMP
__PF(DDCMP,ddcmp)
__PF(RAWHDLC,rawhdlc)
#endif

__PF(TUNNEL,ipip)
__PF(TUNNEL6,tunnel6)
__PF(FRAD,frad)
__PF(SKIP,skip)
__PF(LOOPBACK,loopback)
__PF(LOCALTLK,ltalk)
__PF(FDDI,fddi)
__PF(BIF,bif)
__PF(SIT,sit)
__PF(IPDDP,ip/ddp)
__PF(IPGRE,gre)
__PF(PIMREG,pimreg)
__PF(HIPPI,hippi)
__PF(ASH,ash)
__PF(ECONET,econet)
__PF(IRDA,irda)
__PF(FCPP,fcpp)
__PF(FCAL,fcal)
__PF(FCPL,fcpl)
__PF(FCFABRIC,fcfb0)
__PF(FCFABRIC+1,fcfb1)
__PF(FCFABRIC+2,fcfb2)
__PF(FCFABRIC+3,fcfb3)
__PF(FCFABRIC+4,fcfb4)
__PF(FCFABRIC+5,fcfb5)
__PF(FCFABRIC+6,fcfb6)
__PF(FCFABRIC+7,fcfb7)
__PF(FCFABRIC+8,fcfb8)
__PF(FCFABRIC+9,fcfb9)
__PF(FCFABRIC+10,fcfb10)
__PF(FCFABRIC+11,fcfb11)
__PF(FCFABRIC+12,fcfb12)
#ifdef ARPHRD_IEEE802_TR
__PF(IEEE802_TR,tr)
#endif
#ifdef ARPHRD_IEEE80211
__PF(IEEE80211,ieee802.11)
#endif
#ifdef ARPHRD_VOID
__PF(VOID,void)
#endif
};
#undef __PF

	int i;
	for (i = 0; i < ARRAY_SIZE(arphrd_names); i++) {
		 if (arphrd_names[i].type == type)
			return arphrd_names[i].name;
	}
	snprintf(buf, len, "[%d]", type);
	return buf;
}
