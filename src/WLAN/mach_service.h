/*
 * mach_service.h — wland's Mach service thread.
 *
 * Owns the receive right for org.nextbsd.wlan and runs the MIG demux for
 * wlan.defs. Same shape as ipconfigd's mach_service.c and configd's serve loop.
 */
#ifndef _WLAND_MACH_SERVICE_H_
#define _WLAND_MACH_SERVICE_H_

int	mach_service_start(void);
void	mach_service_join(void);

#endif /* _WLAND_MACH_SERVICE_H_ */
