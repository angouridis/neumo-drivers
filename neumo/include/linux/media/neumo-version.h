/*
 * neumo-version.h
 *
 * The Kernel Digital TV Demux kABI defines a driver-internal interface for
 * registering low-level, hardware specific driver to a hardware independent
 * demux layer.
 *
 * Copyright (c) 2025-2026 Deep Thought <deeptho@gmail.com>
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
#pragma once
void neumo_git_versions(const char ** neumo, const char ** rev, const char ** tag, const char ** branch);
