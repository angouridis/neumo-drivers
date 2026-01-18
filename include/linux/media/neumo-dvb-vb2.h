/* SPDX-License-Identifier: LGPL-2.1+ WITH Linux-syscall-note */
/*
 * neumo-dvb_vb2.h
 *
 * Copyright (C) 2026 Deep Thought <deeptho@gmail.com>
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

//#include "uapi/dvb_api_dmx.h"

#define _UAPI_DVBDMX_H_ // prevent inclusion from dmx.h from dvbapi
#include <media/dvb_vb2.h>
#undef  _UAPI_DVBDMX_H_
