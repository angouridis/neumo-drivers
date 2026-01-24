/*
 * neumo-dvb_demux_private.h
 *
 * The Digital TV Frontend kABI defines a driver-internal interface for
 * registering low-level, hardware specific driver to a hardware independent
 * frontend layer.
 *
 * Copyright (C) 2001 convergence integrated media GmbH
 * Copyright (C) 2004 convergence GmbH
 * Copyright (C) 2024-2026 Deep Thought <deeptho@gmail.com> changes for neumodvb
 *
 * Written by Ralph Metzler
 * Overhauled by Holger Waechtler
 * Kernel I2C stuff by Michael Hunold <hunold@convergence.de>
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

 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#pragma once

extern int dvb_demux_tscheck;
extern int dvb_demux_dtdebug;
extern int dvb_demux_speedcheck;
extern int dvb_demux_feed_err_pkts;
