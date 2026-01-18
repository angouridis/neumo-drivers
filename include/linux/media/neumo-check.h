/*
 * demux.h
 *
 * The Kernel Digital TV Demux kABI defines a driver-internal interface for
 * registering low-level, hardware specific driver to a hardware independent
 * demux layer.
 *
 * Copyright (c) 2025 Deep Thought <deeptho@gmail.com>
 *
 * Copyright (c) 2002 Convergence GmbH
 *
 * based on code:
 * Copyright (c) 2000 Nokia Research Center
 *                    Tampere, FINLAND
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


/*deliberately cause a compiler error when code includes files
	from the main linuc kernel that should not be included in neumo code
*/


#ifdef _UAPI_DVBDMX_H_
#pragma message "You must never include dmx.h in neumo sources"
#endif

#ifdef _DVB_FRONTEND_H_
#pragma message "You must never include dvb_frontend.h in neumo sources"
#endif

#ifdef _DMXDEV_H_
#pragma message "You must never include dmxdev.h in neumo sources"
#endif

#ifdef _DVB_DEMUX_H_
#pragma message "You must never include dvb_demux.h in neumo sources"
#endif

#ifdef _DVB_NET_H_
#pragma message "You must never include dvb_net.h in neumo sources"
#endif
