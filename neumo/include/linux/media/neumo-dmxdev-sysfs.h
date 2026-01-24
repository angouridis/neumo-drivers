/*
 *
 * Copyright (c) <2025>, Deep Thought <deeptho@gmail.com>, all rights reserved
 * Author(s): deeptho@gmail.com
 *
 */
#ifndef __NEUMO_DMXDEV_SYSFS_H
#define __NEUMO_DMXDEV_SYSFS_H
struct neumo_dmxdev;

void dvb_dmxdev_remove_sysfs(struct neumo_dmxdev* dmxdev);
int dvb_dmxdev_make_sysfs(struct neumo_dmxdev* dmxdev);

#endif //__NEUMO_DVB_DEMUX_SYSFS_H
