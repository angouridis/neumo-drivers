
/*
 * neumo-dvb_frontend_private.h
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

struct dtv_frontend_properties;
struct neumo_dvb_frontend_private;

extern int dvb_frontend_debug;
extern int dvb_shutdown_timeout;
extern int dvb_force_auto_inversion;
extern int dvb_override_tune_delay;
extern int dvb_powerdown_on_sleep;
extern int dvb_mfe_wait_time;

extern struct mutex frontend_mutex;
extern const struct file_operations neumo_dvb_frontend_fops;


struct neumo_dvb_frontend_private {
	/* thread/frontend values */
	void * dummy; //force a crash in case drivers use dirty tricks to access
	struct dvb_frontend* dvb_api_fe;
	struct neumo_dvb_frontend* neumo_api_fe;
	struct dvb_device* dvbdev;
	struct dvb_frontend_parameters dvb_parameters_out;
	struct neumo_dvb_extra * extra; //data which really should be in dvb_frontend but is not there for historical reasons

	struct semaphore sem;
	//struct list_head list_head;
	wait_queue_head_t wait_queue;
	struct task_struct *thread;
	unsigned long release_jiffies;
	unsigned int wakeup;
	unsigned long tune_mode_flags;
	unsigned int delay;
	unsigned int reinitialise;
	int tone;
	int voltage;

	/* swzigzag values */
	unsigned int state;
	unsigned int bending;
	int lnb_drift;
	unsigned int inversion;
	unsigned int auto_step;
	unsigned int auto_sub_step;
	unsigned int started_auto_step;
	unsigned int min_delay;
	unsigned int max_drift;
	unsigned int step_size;
	int quality;
	unsigned int check_wrapped;
	int heartbeat_interval;
#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
	struct media_pipeline pipe;
#endif
	unsigned int exit;
	int algo_status;
	enum fe_status status;
};


extern __poll_t neumo_dvb_frontend_poll_fepriv(struct neumo_dvb_frontend_private* fepriv, struct file *file,
																							 struct poll_table_struct *wait);

extern struct neumo_dvb_frontend_private* neumo_dvb_create_fepriv(struct dvb_frontend* dvb_api_fe,
																																	struct neumo_dvb_frontend* neumo_api_fe);

extern void neumo_dvb_frontend_clear_events_fepriv(struct neumo_dvb_frontend_private *fepriv);
extern void neumo_dvb_frontend_add_event_fepriv(struct neumo_dvb_frontend_private* fepriv,
																								enum fe_status status);

extern void neumo_dvb_frontend_wakeup_fepriv(	struct neumo_dvb_frontend_private* fepriv);
extern void neumo_dvb_frontend_private_free(struct neumo_dvb_frontend_private* fepriv, struct dvb_adapter* adapter);
extern void neumo_dvb_frontend_stop_fepriv(struct neumo_dvb_frontend_private* fepriv, struct dvb_adapter* adapter);

extern bool dvb_driver_has_ts_bus_ctrl(struct neumo_dvb_frontend_private* fepriv);
extern int dvb_driver_ts_bus_ctrl(struct neumo_dvb_frontend_private* fepriv, int acquire);
extern int dvb_driver_get_ts_bus_ctrl(struct neumo_dvb_frontend_private* fepriv, int acquire);
extern struct dvb_adapter* dvb_driver_get_adapter(struct neumo_dvb_frontend_private* fepriv);
extern struct kref* dvb_driver_get_refcount(struct neumo_dvb_frontend_private* fepriv);
extern int dvb_driver_get_fe_id(struct neumo_dvb_frontend_private* fepriv);


extern void dvb_driver_dvb_frontend_put(struct dvb_frontend *fe);

extern int dvb_driver_dvb_frontend_do_ioctl(struct dvb_frontend* fe, struct file *file,
																						unsigned int cmd, void *parg);

extern void dvb_driver_dvb_frontend_init(struct dvb_frontend *fe);

extern int dvb_driver_dtv_property_legacy_params_sync(struct dvb_frontend* fe,
																					 struct dvb_frontend_parameters *p);
extern void dvb_driver_dvb_frontend_restart(struct dvb_frontend* fe);

extern void dvb_driver_hw_algo(struct neumo_dvb_frontend_private *fepriv,
															 struct dvb_frontend* fe,
															 bool* re_tune, enum fe_status* status);
extern void dvb_driver_custom_algo(struct neumo_dvb_frontend_private *fepriv,
																	 struct dvb_frontend* fe,
																	 bool* re_tune, enum fe_status* status);

extern void dvb_driver_dvb_frontend_swzigzag(struct dvb_frontend *fe);

extern int dvb_driver_dvb_frontend_thread(void *data);
