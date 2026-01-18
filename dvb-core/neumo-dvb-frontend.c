// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dvb_frontend.c: DVB frontend tuning interface/thread
 *
 * Copyright (C) 1999-2001 Ralph  Metzler
 *			   Marcus Metzler
 *			   Holger Waechtler
 *				      for convergence integrated media GmbH
 *
 * Copyright (C) 2004 Andrew de Quincey (tuning thread cleanup)
 * Copyright (C) 2020-2025 Deep Thought <deeptho@gmail.com> (modifications) *
 */

/* Enables DVBv3 compatibility bits at the headers */
#define __DVB_CORE__

#define pr_fmt(fmt) "dvb_frontend: " fmt

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <linux/nospec.h>
#include <linux/list.h>
#include <linux/freezer.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/compat.h>
#include <asm/processor.h>
#include <linux/dvb/neumo-frontend.h>
#include <media/neumo-dvb-frontend.h>
#include <media/neumo-dvb-frontend-private.h>
#include <media/neumo-dvbdev.h>
#include <linux/dvb/version.h>

int dvb_frontend_debug;
int dvb_shutdown_timeout;
int dvb_force_auto_inversion;
int dvb_override_tune_delay;
int dvb_powerdown_on_sleep = 1;
int dvb_mfe_wait_time = 5;

module_param_named(frontend_debug, dvb_frontend_debug, int, 0644);
MODULE_PARM_DESC(frontend_debug, "Turn on/off frontend core debugging (default:off).");
module_param(dvb_shutdown_timeout, int, 0644);
MODULE_PARM_DESC(dvb_shutdown_timeout, "wait <shutdown_timeout> seconds after close() before suspending hardware");
module_param(dvb_force_auto_inversion, int, 0644);
MODULE_PARM_DESC(dvb_force_auto_inversion, "0: normal (default), 1: INVERSION_AUTO forced always");
module_param(dvb_override_tune_delay, int, 0644);
MODULE_PARM_DESC(dvb_override_tune_delay, "0: normal (default), >0 => delay in milliseconds to wait for lock after a tune attempt");
module_param(dvb_powerdown_on_sleep, int, 0644);
MODULE_PARM_DESC(dvb_powerdown_on_sleep, "0: do not power down, 1: turn LNB voltage off on sleep (default)");
module_param(dvb_mfe_wait_time, int, 0644);
MODULE_PARM_DESC(dvb_mfe_wait_time, "Wait up to <mfe_wait_time> seconds on open() for multi-frontend to become available (default:5 seconds)");

#define dprintk(fmt, arg...) \
	printk(KERN_DEBUG pr_fmt("%s:%d: " fmt), __func__, __LINE__, ##arg)

#define fe_dprintk(fe, fmt, arg...)																			\
	printk(KERN_DEBUG pr_fmt("%s:%d: fe%d " fmt), __func__, __LINE__, fe->dvb->num, ##arg)

#define adapter_dprintk(adapter, fmt, arg...)																			\
	printk(KERN_DEBUG pr_fmt("%s:%d: fe%d " fmt), __func__, __LINE__, adapter->num, ##arg)

struct neumo_dvb_frontend_private;

//indexed by fe; stores neumo_dvb_frontend_private*
static struct xarray neumo_fes;


struct neumo_dtv_fe_algo_state {
	atomic_t task_should_stop;
	atomic_t cur_index; //used to measure progress; should be updated from time to time
	atomic_t max_index; //when cur_inde==max_index task is done
	wait_queue_head_t wait_queue;
};


struct neumo_user_data {
	bool has_selected_api;
	bool uses_neumo_api; //which api the opener has selected (activated by ioctl)
};

static void user_data_init(void) {

};


struct neumo_dvb_extra {
	struct neumo_dvb_fe_events events;
	struct dtv_fe_spectrum spectrum;
	struct dtv_fe_constellation constellation;
	struct neumo_dtv_fe_algo_state algo_state;
	struct xarray users; //indexed by dvbdev; stores neumo_user_data
};

static void neumo_dvb_extra_init(struct neumo_dvb_extra* extra) {
	xa_init(&extra->users);
}

static void neumo_dvb_extra_free_users(struct neumo_dvb_extra* extra) {
	unsigned long index;
	void *entry;
	xa_for_each(&extra->users, index, entry){
		kfree(entry);
	}
	xa_destroy(&extra->users);
}


void neumo_dvb_frontend_private_free(struct neumo_dvb_frontend_private* fepriv, struct dvb_adapter* adapter)
{
	adapter_dprintk(adapter, "calling kfree(fepriv)\n");
	neumo_dvb_extra_free_users(fepriv->extra);
	kfree(fepriv->extra);
	kfree(fepriv);
	adapter_dprintk(adapter, "setting frontend_priv=NULL\n");
}

static void neumo_dvb_frontend_private_init(struct neumo_dvb_frontend_private *fepriv,
																						struct dvb_frontend* dvb_api_fe,
																						struct neumo_dvb_frontend* neumo_api_fe)
{
	fepriv->extra = kzalloc(sizeof(struct neumo_dvb_extra), GFP_KERNEL);
	fepriv->neumo_api_fe = neumo_api_fe;
	fepriv->dvb_api_fe = dvb_api_fe;
	struct neumo_dvb_extra* extra = fepriv->extra;
	neumo_dvb_extra_init(extra);
	sema_init(&fepriv->sem, 1);
	init_waitqueue_head(&fepriv->wait_queue);
	init_waitqueue_head(&extra->events.wait_queue);
	mutex_init(&extra->events.mtx);
	fepriv->inversion = INVERSION_OFF;
}

#define FESTATE_IDLE 1
#define FESTATE_RETUNE 2
#define FESTATE_TUNING_FAST 4
#define FESTATE_TUNING_SLOW 8
#define FESTATE_TUNED 16
#define FESTATE_ZIGZAG_FAST 32
#define FESTATE_ZIGZAG_SLOW 64
#define FESTATE_DISEQC 128
#define FESTATE_ERROR 256
#define FESTATE_SCAN_FIRST 512
#define FESTATE_SCAN_NEXT 1024
#define FESTATE_GETTING_SPECTRUM 2048
#define FESTATE_WAITFORLOCK (FESTATE_TUNING_FAST | FESTATE_TUNING_SLOW | FESTATE_ZIGZAG_FAST | FESTATE_ZIGZAG_SLOW | FESTATE_DISEQC)
#define FESTATE_SEARCHING_FAST (FESTATE_TUNING_FAST | FESTATE_ZIGZAG_FAST)
#define FESTATE_SEARCHING_SLOW (FESTATE_TUNING_SLOW | FESTATE_ZIGZAG_SLOW)
#define FESTATE_LOSTLOCK (FESTATE_ZIGZAG_FAST | FESTATE_ZIGZAG_SLOW)
#define FESTATE_SCANNING (FESTATE_SCAN_FIRST|FESTATE_SCAN_NEXT)
/*
 * FESTATE_IDLE. No tuning parameters have been supplied and the loop is idling.
 * FESTATE_RETUNE. Parameters have been supplied, but we have not yet performed the first tune.
 * FESTATE_TUNING_FAST. Tuning parameters have been supplied and fast zigzag scan is in progress.
 * FESTATE_TUNING_SLOW. Tuning parameters have been supplied. Fast zigzag failed, so we're trying again, but slower.
 * FESTATE_TUNED. The frontend has successfully locked on.
 * FESTATE_ZIGZAG_FAST. The lock has been lost, and a fast zigzag has been initiated to try and regain it.
 * FESTATE_ZIGZAG_SLOW. The lock has been lost. Fast zigzag has been failed, so we're trying again, but slower.
 * FESTATE_DISEQC. A DISEQC command has just been issued.
 * FESTATE_WAITFORLOCK. When we're waiting for a lock.
 * FESTATE_SEARCHING_FAST. When we're searching for a signal using a fast zigzag scan.
 * FESTATE_SEARCHING_SLOW. When we're searching for a signal using a slow zigzag scan.
 * FESTATE_LOSTLOCK. When the lock has been lost, and we're searching it again.
 */

DEFINE_MUTEX(frontend_mutex);


static void neumo_driver_dvb_frontend_invoke_release(struct neumo_dvb_frontend *fe,
					void (*release)(struct neumo_dvb_frontend *fe));


static void __neumo_driver_dvb_frontend_free(struct neumo_dvb_frontend *fe)
{
	struct neumo_dvb_frontend_private* fepriv = fe->frontend_priv;
	struct dvb_adapter* adapter = fe->dvb;

	adapter_dprintk(adapter, "Start freeing frontend\n");
	if (fepriv)
		dvb_device_put(fepriv->dvbdev);
	adapter_dprintk(adapter, "calling neumo_driver_dvb_frontend_invoke_release\n");
	neumo_driver_dvb_frontend_invoke_release(fe, fe->ops.release); //decreases demod refcount by 1
	neumo_dvb_frontend_private_free(fepriv, adapter);
	fe->frontend_priv = NULL;
	adapter_dprintk(adapter, "End freeing frontend\n");
}

static void neumo_driver_dvb_frontend_free(struct kref *ref)
{
	struct neumo_dvb_frontend *fe =
		container_of(ref, struct neumo_dvb_frontend, refcount);

	__neumo_driver_dvb_frontend_free(fe);
}

static void neumo_driver_dvb_frontend_put(struct neumo_dvb_frontend *fe)
{
	/* call detach before dropping the reference count */
	if (fe->ops.detach)
		fe->ops.detach(fe);
	/*
	 * Check if the frontend was registered, as otherwise
	 * kref was not initialized yet.
	 */
	if (fe->frontend_priv) {
		int cnt = kref_read(&fe->refcount);
		fe_dprintk(fe, "refcount %p=%d fe=%p\n", (void*)&fe->refcount, cnt, fe);
		kref_put(&fe->refcount, neumo_driver_dvb_frontend_free);
	}
	else {
		fe_dprintk(fe, "calling __neumo_driver_dvb_frontend_free fe=%p\n", fe);
		__neumo_driver_dvb_frontend_free(fe);
	}
}

static int neumo_driver_dtv_get_frontend(struct neumo_dvb_frontend *fe,
			    struct neumo_driver_dtv_frontend_properties *c,
			    struct dvb_frontend_parameters *p_out);


static int
neumo_driver_dtv_property_legacy_params_sync(struct neumo_dvb_frontend* fe,
																						 struct dvb_frontend_parameters *p);

/*
 * Due to DVBv3 API calls, a delivery system should be mapped into one of
 * the 4 DVBv3 delivery systems (FE_QPSK, FE_QAM, FE_OFDM or FE_ATSC),
 * otherwise, a DVBv3 call will fail.
 */
//phase out?
enum dvbv3_emulation_type {
	DVBV3_UNKNOWN,
	DVBV3_QPSK,
	DVBV3_QAM,
	DVBV3_OFDM,
	DVBV3_ATSC,
};

//phase out?
static enum dvbv3_emulation_type dvbv3_type(u32 delivery_system)
{
	switch (delivery_system) {
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
		return DVBV3_QAM;
	case SYS_DVBS:
	case SYS_AUTO:
	case SYS_DVBS2:
	case SYS_TURBO:
	case SYS_ISDBS:
	case SYS_DSS:
		return DVBV3_QPSK;
	case SYS_DVBT:
	case SYS_DVBT2:
	case SYS_ISDBT:
	case SYS_DTMB:
		return DVBV3_OFDM;
	case SYS_ATSC:
	case SYS_ATSCMH:
	case SYS_DVBC_ANNEX_B:
		return DVBV3_ATSC;
	case SYS_UNDEFINED:
	case SYS_ISDBC:
	case SYS_DVBH:
	case SYS_DAB:
	default:
		/*
		 * Doesn't know how to emulate those types and/or
		 * there's no frontend driver from this type yet
		 * with some emulation code, so, we're not sure yet how
		 * to handle them, or they're not compatible with a DVBv3 call.
		 */
		return DVBV3_UNKNOWN;
	}
}

void neumo_dvb_frontend_add_event_fepriv(struct neumo_dvb_frontend_private* fepriv,
																								enum fe_status status)
{
	struct neumo_dvb_fe_events *events = &((struct neumo_dvb_extra*)fepriv->extra)->events;
	struct dvb_frontend_event *e;
	int wp;

	mutex_lock(&events->mtx);

	wp = (events->eventw + 1) % MAX_EVENT;
	if (wp == events->eventr) {
		events->overflow = 1;
		events->eventr = (events->eventr + 1) % MAX_EVENT;
	}

	e = &events->events[events->eventw];
	e->status = status;
	e->parameters = fepriv->dvb_parameters_out;

	events->eventw = wp;

	mutex_unlock(&events->mtx);

	wake_up_interruptible(&events->wait_queue);
}

static void neumo_driver_dvb_frontend_add_event(struct neumo_dvb_frontend* neumo_fe, enum fe_status status)
{
	struct neumo_dvb_frontend_private *fepriv = neumo_fe->frontend_priv;
	struct neumo_driver_dtv_frontend_properties *c = &neumo_fe->dtv_property_cache;

	dev_dbg(neumo_fe->dvb->device, "%s:\n", __func__);

	if ((status & FE_HAS_LOCK) && neumo_fe->ops.get_frontend)
		neumo_driver_dtv_get_frontend(neumo_fe, c, &fepriv->dvb_parameters_out);

	neumo_dvb_frontend_add_event_fepriv(fepriv, status);
}

static int dvb_frontend_test_event(struct neumo_dvb_frontend_private *fepriv,
					 struct neumo_dvb_fe_events *events)
{
	int ret;

	up(&fepriv->sem);
	ret = events->eventw != events->eventr;
	down(&fepriv->sem);

	return ret;
}

static int dvb_frontend_get_event_fepriv(struct neumo_dvb_frontend_private *fepriv,
																	struct dvb_adapter* adapter,
																	struct dvb_frontend_event *event, int flags)
{
	struct neumo_dvb_fe_events *events = &((struct neumo_dvb_extra*)fepriv->extra)->events;

	if (events->overflow) {
		events->overflow = 0;
		events->eventr = events->eventw;
		adapter_dprintk(adapter, "Returning -EOVERFLOW=%d adapter=%d\n", -EOVERFLOW, adapter->num);
		return -EOVERFLOW;
	}

	if (events->eventw == events->eventr) {
		int ret;

		if (flags & O_NONBLOCK) {
			adapter_dprintk(adapter, "Returning -EWOULDBLOCK=%d\n", -EWOULDBLOCK);
			return -EWOULDBLOCK;
		}

		ret = wait_event_interruptible(events->wait_queue,
																	 dvb_frontend_test_event(fepriv, events));

		if (ret < 0) {
			adapter_dprintk(adapter, "Retuning -ret=%d\n", ret);
			return ret;
		}
	}

	mutex_lock(&events->mtx);
	*event = events->events[events->eventr];
	events->eventr = (events->eventr + 1) % MAX_EVENT;
	mutex_unlock(&events->mtx);

	return 0;
}

void neumo_dvb_frontend_clear_events_fepriv(struct neumo_dvb_frontend_private *fepriv)
{
	struct neumo_dvb_fe_events* events = &((struct neumo_dvb_extra*)fepriv->extra)->events;

	mutex_lock(&events->mtx);
	events->eventr = events->eventw;
	mutex_unlock(&events->mtx);
}

static void neumo_driver_dvb_frontend_init(struct neumo_dvb_frontend *fe)
{
	dev_dbg(fe->dvb->device,
		"%s: initialising adapter %i frontend %i (%s)...\n",
		__func__, fe->dvb->num, fe->id, fe->ops.info.name);

	if (fe->ops.init)
		fe->ops.init(fe);
	if (fe->ops.tuner_ops.init) {
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		fe->ops.tuner_ops.init(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}
}


static int neumo_dvb_frontend_is_exiting_fepriv(struct neumo_dvb_frontend_private *fepriv)
{
	if (fepriv->exit != DVB_FE_NO_EXIT)
		return 1;

	if (fepriv->dvbdev->writers == 1)
		if (time_after_eq(jiffies, fepriv->release_jiffies +
					dvb_shutdown_timeout * HZ))
			return 1;

	return 0;
}


static int neumo_driver_dvb_frontend_is_exiting(struct neumo_dvb_frontend *fe)
{
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
	return neumo_dvb_frontend_is_exiting_fepriv(fepriv);
}

int neumo_dvb_frontend_task_should_stop(struct neumo_dvb_frontend *fe)
{
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
	struct neumo_dvb_extra* extra = fepriv->extra;
	if(neumo_driver_dvb_frontend_is_exiting(fe))
		return 1;
	if(atomic_read(&extra->algo_state.task_should_stop))
		return 1;
	return 0;
}

//dvb_api does not have this
EXPORT_SYMBOL(neumo_dvb_frontend_task_should_stop);


static int dvb_frontend_should_wakeup(struct neumo_dvb_frontend *fe)
{
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;

	if (fepriv->wakeup) {
		fepriv->wakeup = 0;
		return 1;
	}
	return neumo_driver_dvb_frontend_is_exiting(fe);
}

void neumo_dvb_frontend_wakeup_fepriv(struct neumo_dvb_frontend_private *fepriv)
{
	fepriv->wakeup = 1;
	wake_up_interruptible(&fepriv->wait_queue);
}

#ifdef UNUSED
static void neumo_driver_dvb_frontend_restart(struct neumo_dvb_frontend* fe)
{
	struct neumo_dvb_frontend_private* fepriv = fe->frontend_priv;
	dprintk("calling dvb_driver_dvb_frontend_init\n");
	neumo_driver_dvb_frontend_init(fe);
	if (fe->ops.set_tone && fepriv->tone != -1) {
		fe_dprintk(fe, "calling set_tone: tone=%d\n", fepriv->tone);
		fe->ops.set_tone(fe, fepriv->tone);
	}
	if (fe->ops.set_voltage && fepriv->voltage != -1) {
		fe_dprintk(fe, "calling set_voltage: voltage=%d\n", fepriv->voltage);
		fe->ops.set_voltage(fe, fepriv->voltage);
	}
}
#endif

static void neumo_driver_hw_algo(struct neumo_dvb_frontend_private *fepriv,
																 struct neumo_dvb_frontend* fe,
																 bool* re_tune, enum fe_status* status)
{
	struct dvb_adapter* adapter = fe->dvb;
	struct neumo_dvb_extra* extra = fepriv->extra;
	if (fepriv->state & FESTATE_SCANNING) {
		adapter_dprintk(adapter, "Scan requested, FESTATE_SCANNING state=%d\n", fepriv->state);
		if (fepriv->neumo_api_fe->ops.scan)
			fe->ops.scan(fe, fepriv->state & FESTATE_SCAN_NEXT , &fepriv->delay, status);
		fepriv->state = FESTATE_IDLE;
	}	 else if (fepriv->state & FESTATE_GETTING_SPECTRUM) {
		adapter_dprintk(adapter, "starting spectrum scan\n");
		if (fe->ops.spectrum_start)
			fe->ops.spectrum_start(fe,  &extra->spectrum,
														 &fepriv->delay, status);
		adapter_dprintk(adapter, "spectrum scan ended s=0x%x\n", *status);
		fepriv->state = FESTATE_IDLE;
	} else {
		if (fepriv->state & FESTATE_RETUNE) {
			dev_dbg(fe->dvb->device, "%s: Retune requested, FESTATE_RETUNE\n", __func__);
			*re_tune = true;
			fepriv->state = FESTATE_TUNED;
		} else {
			*re_tune = false;
		}
		if (fe->ops.tune) {
			fe->ops.tune(fe, re_tune, fepriv->tune_mode_flags, &fepriv->delay, status);
		}
	}
	if ((*status != fepriv->status && !(fepriv->tune_mode_flags & FE_TUNE_MODE_ONESHOT))
			|| (fepriv->heartbeat_interval>0)) {
		neumo_driver_dvb_frontend_add_event(fe, *status);
		fepriv->status = *status;
	}
}

/*
	This thread is started when a frontend device is opened.
	It runs a work cycle in a loop, where a new cycle is started  every fepriv->heartbeat_interval
	milliseconds (but a cycle can sometimes take longer)

	For the typical case of DVBFE_ALGO_HW, the work cycle consist of
	 -sleep for fepriv->heartbeat_interval milliseconds (sleep is interrupted by an ioctl)
	 -if in IDLE state, do nothing (continue sleeping)
   -if in any of the other states, perform the following actions, select a new state, send an event
	  message to inform user space of status change (locked, no longer locked, spectrum finished)
		and then go to sleep again:
	   1. if in SCANNING state, ask driver to find the next mux, and then go into idle state
	   2. if in GETTING_SPECTRUM state, ask driver to run a spectrum scan operation, and then go into idle state
	   3. if in RETUNE state, ask driver to tune  (set frequency, symbolrate check for lock...)
        and if this succeeds, go to TUNED state
		 4. if in  TUNED state, ask the driver to read the current signal status (SNR and such) and lock
	      status, and remain ine TUNED state
	 -when the device receives an ioctl, the frontend loop is requested to abort its current operation and restart
    the work cycle. Long running driver operations (e.g., tuning which can take several seconds, or spectrum
		acquisition, which can take even linger) should periodically call
       kthread_should_stop() || neumo_dvb_frontend_task_should_stop(fe)
    to check if such a user request was received, and if so, return early. This prevents ioctls from blocking
		for a long time.
	 -when the frontend device is closed by all users, the work cycle is similarly interrupted and the thread
	  is then terminated
	 -the controlling user space connection (the only one having opened the frontend device read-write)
	  can also call DTV_STOP to explictly ask the frontend loop to go into IDLE mode.

		It is important that the frontend loop is in IDLE mode while setting new tuning parameters prior to a tune.
		This can be achieved in 2 ways:
    1) sending all tuning parameters and the DTV_TUNE command in a single ioctl
		   During an ioctl a lock is held, which means that the frontend loop can only be sleeping or waiting
		   for the lock to be released, i.e., it is not possible that driver code runs while the new parameters
		   are only partially written to the internal dtv_frontend_properties structure
		2) call DTV_STOP in a first ioctl. Then set parameters in one or more ioctls. In a last ioctl, send
		   DTVT_TUNE
		Obviously, method 2 is slower and more error prone

		If a user space ioctl asks for a tune, the code checks some parameters, and if these are out of
		range, the frontend loop goes to idle state. Similarly, if the driver code returns an error after tuning,
		because it detected some illegal user request, the loop goes to IDLE state again.

		Note that a failure to  lock (poor signal, signal which cannot be demodulated... is not considered an error,
		and the loop will enter TUNED state. There is no expectation that the driver itself will attempt to retune
		in this case. Instead the user space is expected to detect this situation and take the right action,
		which could invole sending diseqc commands, retuning ...


		From the user

 */

static int neumo_driver_dvb_frontend_thread(void *data)
{
	struct neumo_dvb_frontend_private *fepriv = data;
	struct neumo_dvb_frontend* fe = fepriv->dvbdev->priv;
	struct neumo_dvb_extra* extra = fepriv->extra;
	struct dvb_adapter* adapter = fe->dvb;
	enum fe_status status = FE_NONE;
	enum neumo_dvbfe_algo algo;
	bool re_tune = false;
	bool semheld = false;

	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	fepriv->check_wrapped = 0;
	fepriv->quality = 0;
	fepriv->delay = 3 * HZ;
	fepriv->status = 0;
	fepriv->wakeup = 0;
	fepriv->reinitialise = 0;

	dprintk("calling neumo_driver_dvb_frontend_init\n");
	neumo_driver_dvb_frontend_init(fe);

	set_freezable();
	while (1) {
		up(&fepriv->sem);			/* is locked when we enter the thread... */
restart:
		wait_event_interruptible_timeout(fepriv->wait_queue,
						 dvb_frontend_should_wakeup(fe) ||
						 kthread_should_stop() ||
						 freezing(current),
            (fepriv->heartbeat_interval > 0 ) ? (fepriv->heartbeat_interval*HZ)/1000 :
																		 fepriv->delay);

		if (kthread_should_stop() || neumo_driver_dvb_frontend_is_exiting(fe)) {
			/* got signal or quitting */
			if (!down_interruptible(&fepriv->sem))
				semheld = true;
			fepriv->exit = DVB_FE_NORMAL_EXIT;
			break;
		}

		if (try_to_freeze())
			goto restart;

		if (down_interruptible(&fepriv->sem))
			break;

		if (fepriv->reinitialise) {
			dprintk("calling neumo_driver_dvb_frontend_init\n");
			neumo_driver_dvb_frontend_init(fe);
			fepriv->reinitialise = 0;
		}
		if(fepriv->state == FESTATE_IDLE || fepriv->state == FESTATE_DISEQC)
			continue;
		/* do an iteration of the tuning loop */
		if (fe->ops.get_frontend_algo) {
			algo = fe->ops.get_frontend_algo(fe);
			switch (algo) {
			case DVBFE_ALGO_NOTUNE:
				break;
			case DVBFE_ALGO_HW:
				adapter_dprintk(adapter, "Frontend ALGO = DVBFE_ALGO_HW\n");
				neumo_driver_hw_algo(fepriv, fe, &re_tune, &status);
				atomic_set(&extra->algo_state.task_should_stop, false);
				break;
			case DVBFE_ALGO_SW:
				adapter_dprintk(adapter, "No DVBFE_ALGO_SW for neumo_dvb_frontend\n");
				break;
			case DVBFE_ALGO_CUSTOM:
				adapter_dprintk(adapter, "No DVBFE_ALGO_CUSTOM for neumo_dvb_frontend\n");
				break;
			default:
				adapter_dprintk(adapter, "UNDEFINED ALGO !\n");
				break;
			}
		}
	}
	adapter_dprintk(adapter, "Exiting frontend thread\n");
	if (dvb_powerdown_on_sleep) {
		if (fe->ops.set_voltage) {
			fe_dprintk(fe, "calling set_voltage OFF: old voltage=%d\n", fepriv->voltage);
			fe->ops.set_voltage(fe, SEC_VOLTAGE_OFF);
		}
		if (fe->ops.tuner_ops.sleep) {
			if (fe->ops.i2c_gate_ctrl)
				fe->ops.i2c_gate_ctrl(fe, 1);
			fe_dprintk(fe, "Calling tuner sleep\n");
			fe->ops.tuner_ops.sleep(fe);
			if (fe->ops.i2c_gate_ctrl) {
				fe_dprintk(fe, "Calling i2c_gate_ctrl\n");
				fe->ops.i2c_gate_ctrl(fe, 0);
			}
		}
		if (fe->ops.sleep) {
			fe_dprintk(fe, "Calling sleep adapter=%d\n", fe->dvb->num);
			fe->ops.sleep(fe);
		}
	}

	fepriv->thread = NULL;
	if (kthread_should_stop())
		fepriv->exit = DVB_FE_DEVICE_REMOVED;
	else
		fepriv->exit = DVB_FE_NO_EXIT;
	mb();

	if (semheld)
		up(&fepriv->sem);
	neumo_dvb_frontend_wakeup_fepriv(fepriv);
	return 0;
}

void neumo_dvb_frontend_stop_fepriv(struct neumo_dvb_frontend_private* fepriv, struct dvb_adapter* adapter)
{

	adapter_dprintk(adapter, "\n");

	if (fepriv->exit != DVB_FE_DEVICE_REMOVED)
		fepriv->exit = DVB_FE_NORMAL_EXIT;
	mb();

	if (!fepriv->thread)
		return;

	kthread_stop(fepriv->thread);

	sema_init(&fepriv->sem, 1);
	fepriv->state = FESTATE_IDLE;

	/* paranoia check in case a signal arrived */
	if (fepriv->thread)
		adapter_dprintk(adapter,
			 "warning: thread %p won't exit\n", fepriv->thread);
}

/*
 * Sleep for the amount of time given by add_usec parameter
 *
 * This needs to be as precise as possible, as it affects the detection of
 * the dish tone command at the satellite subsystem. The precision is improved
 * by using a scheduled msleep followed by udelay for the remainder.
 */
void dvb_frontend_sleep_until(ktime_t *waketime, u32 add_usec)
{
	s32 delta;

	*waketime = ktime_add_us(*waketime, add_usec);
	delta = ktime_us_delta(ktime_get_boottime(), *waketime);
	if (delta > 2500) {
		msleep((delta - 1500) / 1000);
		delta = ktime_us_delta(ktime_get_boottime(), *waketime);
	}
	if (delta > 0)
		udelay(delta);
}
EXPORT_SYMBOL(dvb_frontend_sleep_until);


static int neumo_dvb_frontend_start_fepriv(struct neumo_dvb_frontend_private* fepriv, struct dvb_adapter* adapter, int fe_id)
{
	int ret;
	struct task_struct *fe_thread;

	dev_dbg(adapter->device, "%s:\n", __func__);

	if (fepriv->thread) {
		if (fepriv->exit == DVB_FE_NO_EXIT)
			return 0;
		else
			neumo_dvb_frontend_stop_fepriv(fepriv, adapter);
	}

	if (signal_pending(current))
		return -EINTR;
	if (down_interruptible(&fepriv->sem))
		return -EINTR;

	fepriv->state = FESTATE_IDLE;
	fepriv->exit = DVB_FE_NO_EXIT;
	fepriv->thread = NULL;
	mb();

	if(fepriv->dvb_api_fe)  {
		fe_thread = kthread_run(dvb_driver_dvb_frontend_thread, fepriv,
														"kdvb-ad-%i-fe-%i", adapter->num, fe_id);
	} else if (fepriv->neumo_api_fe) {
		fe_thread = kthread_run(neumo_driver_dvb_frontend_thread, fepriv,
														"kdvb-ad-%i-fe-%i", adapter->num, fe_id);
	}
	if (IS_ERR(fe_thread)) {
		ret = PTR_ERR(fe_thread);
		adapter_dprintk(adapter,
			 "dvb_frontend_start: failed to start kthread (%d)\n",
			 ret);
		up(&fepriv->sem);
		return ret;
	}
	fepriv->thread = fe_thread;
	return 0;
}

static bool neumo_driver_is_sat_fe(struct neumo_dvb_frontend* fe) {
	int i;
	for (i=0; i< sizeof(fe->ops.delsys)/sizeof(fe->ops.delsys); ++i) {
		switch(fe->ops.delsys[i]) {
		case SYS_DVBS:
		case SYS_DVBS2:
		case SYS_DVBS2X:
			return true;
		}
	}
	return false;
}

static bool neumo_driver_is_symbol_rate_fe(struct neumo_dvb_frontend* fe) {
	int i;
	for (i=0; i< sizeof(fe->ops.delsys)/sizeof(fe->ops.delsys); ++i) {
		switch(fe->ops.delsys[i]) {
		case SYS_DVBS:
		case SYS_DVBS2:
		case SYS_TURBO:
		case SYS_DVBC_ANNEX_A:
		case SYS_DVBC_ANNEX_C: //symbol rate == 0 is allowed - driver should do right thing
			return true;
		}
	}
	return false;
}

static void neumo_driver_dvb_frontend_get_frequency_limits(struct neumo_dvb_frontend *fe,
								u32 *freq_min, u32 *freq_max,
								u32 *tolerance)
{
	u32 tuner_min = fe->ops.tuner_ops.info.frequency_min_hz;
	u32 tuner_max = fe->ops.tuner_ops.info.frequency_max_hz;
	u32 frontend_min = fe->ops.info.frequency_min_hz;
	u32 frontend_max = fe->ops.info.frequency_max_hz;
	fe_dprintk(fe, "fe=%p: %u %u %u %u\n", fe,
					fe->ops.tuner_ops.info.frequency_min_hz, fe->ops.tuner_ops.info.frequency_max_hz,
					fe->ops.info.frequency_min_hz, fe->ops.info.frequency_max_hz);
	*freq_min = max(frontend_min, tuner_min);

	if (frontend_max == 0)
		*freq_max = tuner_max;
	else if (tuner_max == 0)
		*freq_max = frontend_max;
	else
		*freq_max = min(frontend_max, tuner_max);

	if (*freq_min == 0 || *freq_max == 0)
		dev_warn(fe->dvb->device,
			 "DVB: adapter %i frontend %u frequency limits undefined - fix the driver\n",
			 fe->dvb->num, fe->id);

	dev_dbg(fe->dvb->device, "frequency interval: tuner: %u...%u, frontend: %u...%u",
		tuner_min, tuner_max, frontend_min, frontend_max);

	/* If the standard is for satellite, convert frequencies to kHz */
	if(neumo_driver_is_sat_fe(fe)) {
		*freq_min /= kHz;
		*freq_max /= kHz;
		fe_dprintk(fe, "This is a sat_fe returning %u %d\n", *freq_min, *freq_max);
		if (tolerance)
			*tolerance = fe->ops.info.frequency_tolerance_hz / kHz;
	} else  if (tolerance)
		*tolerance = fe->ops.info.frequency_tolerance_hz;
	fe_dprintk(fe, "Returning %u %d\n", *freq_min, *freq_max);
}

static u32 neumo_driver_neumo_dvb_frontend_get_stepsize(struct neumo_dvb_frontend *fe)
{
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 fe_step = fe->ops.info.frequency_stepsize_hz;
	u32 tuner_step = fe->ops.tuner_ops.info.frequency_step_hz;
	u32 step = max(fe_step, tuner_step);

	switch (c->delivery_system) {
	case SYS_DVBS:
	case SYS_DVBS2:
	case SYS_TURBO:
	case SYS_ISDBS:
		step /= kHz;
		break;
	default:
		break;
	}

	return step;
}

static int dvb_frontend_check_parameters(struct neumo_dvb_frontend *fe)
{
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 freq_min;
	u32 freq_max;
	bool need_nonzero_symbol_rate= c->algorithm != ALGORITHM_BLIND;
	switch(c->algorithm) {
	default:
		break;
	case ALGORITHM_WARM:
	case ALGORITHM_BLIND:
	case ALGORITHM_BLIND_BEST_GUESS:
	case ALGORITHM_COLD_BEST_GUESS:
		fe_dprintk(fe, "checking frequency\n");
		/* range check: frequency */
		neumo_driver_dvb_frontend_get_frequency_limits(fe, &freq_min, &freq_max, NULL);
		if ((freq_min && c->frequency < freq_min) ||
				(freq_max && c->frequency > freq_max)) {
			fe_dprintk(fe, "DVB: adapter %i frontend %i frequency %u out of range (%u..%u)\n",
								 fe->dvb->num, fe->id, c->frequency,
								 freq_min, freq_max);
			return -EINVAL;
		}
	}
	/* range check: symbol rate */
	if(neumo_driver_is_symbol_rate_fe(fe) && (c->symbol_rate > 0 || need_nonzero_symbol_rate)) {
		fe_dprintk(fe, "checking symbol_rate: %d range: %d %d\n", c->symbol_rate, fe->ops.info.symbol_rate_min, fe->ops.info.symbol_rate_max);
		if ((fe->ops.info.symbol_rate_min &&
				 c->symbol_rate < fe->ops.info.symbol_rate_min) ||
				(fe->ops.info.symbol_rate_max &&
					 c->symbol_rate > fe->ops.info.symbol_rate_max)) {
			dev_warn(fe->dvb->device, "DVB: adapter %i frontend %i symbol rate %u out of range (%u..%u)\n",
							 fe->dvb->num, fe->id, c->symbol_rate,
							 fe->ops.info.symbol_rate_min,
							 fe->ops.info.symbol_rate_max);
				fe_dprintk(fe, "DVB: adapter %i frontend %i symbol rate %u out of range (%u..%u)\n",
								fe->dvb->num, fe->id, c->symbol_rate,
								fe->ops.info.symbol_rate_min,
								fe->ops.info.symbol_rate_max);
				return -EINVAL;
		}
	}

	return 0;
}

static int neumo_driver_dvb_frontend_clear_cache(struct neumo_dvb_frontend *fe)
{
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
	int i;
	u32 delsys;

	delsys = c->delivery_system;
	memset(c, 0, offsetof(struct neumo_driver_dtv_frontend_properties, strength));
	c->delivery_system = delsys;

	dev_dbg(fe->dvb->device, "%s: Clearing cache for delivery system %d\n",
		__func__, c->delivery_system);

	c->transmission_mode = TRANSMISSION_MODE_AUTO;
	c->bandwidth_hz = 0;	/* AUTO */
	c->guard_interval = GUARD_INTERVAL_AUTO;
	c->hierarchy = HIERARCHY_AUTO;
	c->symbol_rate = 0;
	c->code_rate_HP = FEC_AUTO;
	c->code_rate_LP = FEC_AUTO;
	c->fec_inner = FEC_AUTO;
	c->rolloff = ROLLOFF_AUTO;
#if 1 //preserve the following, as they are not only set by DTV_TONE and DTV_VOLTAGE
	c->voltage = fepriv->voltage;
	c->sectone = fepriv->tone;
#endif
	c->pilot = PILOT_AUTO;

	c->isdbt_partial_reception = 0;
	c->isdbt_sb_mode = 0;
	c->isdbt_sb_subchannel = 0;
	c->isdbt_sb_segment_idx = 0;
	c->isdbt_sb_segment_count = 0;
	c->isdbt_layer_enabled = 7;	/* All layers (A,B,C) */
	for (i = 0; i < 3; i++) {
		c->layer[i].fec = FEC_AUTO;
		c->layer[i].modulation = QAM_AUTO;
		c->layer[i].interleaving = 0;
		c->layer[i].segment_count = 0;
	}

	c->stream_id = NO_STREAM_ID_FILTER;
	c->modcode = MODCODE_ALL;
	c->scrambling_sequence_index = 0;/* default sequence */

	switch (c->delivery_system) {
	case SYS_DVBS:
	case SYS_DVBS2:
	case SYS_TURBO:
		c->modulation = QPSK;   /* implied for DVB-S in legacy API */
		c->rolloff = ROLLOFF_35;/* implied for DVB-S */
		break;
	case SYS_ATSC:
		c->modulation = VSB_8;
		break;
	case SYS_ISDBS:
		c->symbol_rate = 28860000;
		c->rolloff = ROLLOFF_35;
		c->bandwidth_hz = c->symbol_rate / 100 * 135;
		break;
	default:
		c->modulation = QAM_AUTO;
		break;
	}

	c->lna = LNA_AUTO;
	c->pls_search_codes_len = 0;
	c->pls_search_range_start = 0;
	c->pls_search_range_end = 0;
	return 0;
}

#define _DTV_CMD(n, s, b) \
[n] = { \
	.name = #n, \
	.cmd  = n, \
	.set  = s,\
	.buffer = b \
}

struct dtv_cmds_h {
	char	*name;		/* A display name for debugging purposes */

	__u32	cmd;		/* A unique ID */

	/* Flags */
	__u32	set:1;		/* Either a set or get property */
	__u32	buffer:1;	/* Does this property use the buffer? */
	__u32	reserved:30;	/* Align */
};


struct dtv_cmds_h dtv_cmds[] = {
	_DTV_CMD(DTV_TUNE, 1, 0),
	_DTV_CMD(DTV_SCAN, 1, 0),
	_DTV_CMD(DTV_SPECTRUM, 1, 0),
	_DTV_CMD(DTV_CONSTELLATION, 1, 0),
	_DTV_CMD(DTV_BITRATE, 0, 0),
	_DTV_CMD(DTV_LOCKTIME, 0, 0),
	_DTV_CMD(DTV_HEARTBEAT, 1, 0),
	_DTV_CMD(DTV_CLEAR, 1, 0),
	/* Set */
	_DTV_CMD(DTV_OUTPUT_BBFRAMES, 1, 0),
	_DTV_CMD(DTV_FREQUENCY, 1, 0),
	_DTV_CMD(DTV_SCAN_START_FREQUENCY, 1, 0),
	_DTV_CMD(DTV_SCAN_END_FREQUENCY, 1, 0),
	_DTV_CMD(DTV_SCAN_RESOLUTION, 1, 0),
	_DTV_CMD(DTV_SEARCH_RANGE, 1, 0),
	_DTV_CMD(DTV_MAX_SYMBOL_RATE, 1, 0),


	_DTV_CMD(DTV_SCAN_FFT_SIZE, 1, 0),
	_DTV_CMD(DTV_ISI_LIST, 0, 0),
	_DTV_CMD(DTV_MATYPE_LIST, 0, 0),
	_DTV_CMD(DTV_PLS_SEARCH_RANGE, 1, 0),
	_DTV_CMD(DTV_PLS_SEARCH_LIST, 1, 0),
	_DTV_CMD(DTV_BANDWIDTH_HZ, 1, 0),
	_DTV_CMD(DTV_MODULATION, 1, 0),
	_DTV_CMD(DTV_INVERSION, 1, 0),
	_DTV_CMD(DTV_DISEQC_MASTER, 1, 1),
	_DTV_CMD(DTV_SYMBOL_RATE, 1, 0),
	_DTV_CMD(DTV_INNER_FEC, 1, 0),
	_DTV_CMD(DTV_VOLTAGE, 1, 0),
	_DTV_CMD(DTV_TONE, 1, 0),
	_DTV_CMD(DTV_PILOT, 1, 0),
	_DTV_CMD(DTV_ROLLOFF, 1, 0),
	_DTV_CMD(DTV_DELIVERY_SYSTEM, 1, 0),
	_DTV_CMD(DTV_HIERARCHY, 1, 0),
	_DTV_CMD(DTV_CODE_RATE_HP, 1, 0),
	_DTV_CMD(DTV_CODE_RATE_LP, 1, 0),
	_DTV_CMD(DTV_GUARD_INTERVAL, 1, 0),
	_DTV_CMD(DTV_TRANSMISSION_MODE, 1, 0),
	_DTV_CMD(DTV_INTERLEAVING, 1, 0),

	_DTV_CMD(DTV_ISDBT_PARTIAL_RECEPTION, 1, 0),
	_DTV_CMD(DTV_ISDBT_SOUND_BROADCASTING, 1, 0),
	_DTV_CMD(DTV_ISDBT_SB_SUBCHANNEL_ID, 1, 0),
	_DTV_CMD(DTV_ISDBT_SB_SEGMENT_IDX, 1, 0),
	_DTV_CMD(DTV_ISDBT_SB_SEGMENT_COUNT, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYER_ENABLED, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERA_FEC, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERA_MODULATION, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERA_SEGMENT_COUNT, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERA_TIME_INTERLEAVING, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERB_FEC, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERB_MODULATION, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERB_SEGMENT_COUNT, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERB_TIME_INTERLEAVING, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERC_FEC, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERC_MODULATION, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERC_SEGMENT_COUNT, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERC_TIME_INTERLEAVING, 1, 0),

	_DTV_CMD(DTV_STREAM_ID, 1, 0),
	_DTV_CMD(DTV_MODCODE, 1, 0),
	_DTV_CMD(DTV_SCRAMBLING_SEQUENCE_INDEX, 1, 0),
	_DTV_CMD(DTV_MATYPE, 0, 0),
	_DTV_CMD(DTV_ENABLE_MODCOD, 1, 0),
	_DTV_CMD(DTV_LNA, 1, 0),
	_DTV_CMD(DTV_ALGORITHM, 1, 0),

	/* Get */
	_DTV_CMD(DTV_DISEQC_SLAVE_REPLY, 0, 1),
	_DTV_CMD(DTV_API_VERSION, 0, 0),

	_DTV_CMD(DTV_ENUM_DELSYS, 0, 0),

	_DTV_CMD(DTV_ATSCMH_PARADE_ID, 1, 0),
	_DTV_CMD(DTV_ATSCMH_RS_FRAME_ENSEMBLE, 1, 0),

	_DTV_CMD(DTV_ATSCMH_FIC_VER, 0, 0),
	_DTV_CMD(DTV_ATSCMH_NOG, 0, 0),
	_DTV_CMD(DTV_ATSCMH_TNOG, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SGN, 0, 0),
	_DTV_CMD(DTV_ATSCMH_PRC, 0, 0),
	_DTV_CMD(DTV_ATSCMH_RS_FRAME_MODE, 0, 0),
	_DTV_CMD(DTV_ATSCMH_RS_CODE_MODE_PRI, 0, 0),
	_DTV_CMD(DTV_ATSCMH_RS_CODE_MODE_SEC, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SCCC_BLOCK_MODE, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SCCC_CODE_MODE_A, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SCCC_CODE_MODE_B, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SCCC_CODE_MODE_C, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SCCC_CODE_MODE_D, 0, 0),
	_DTV_CMD(DTV_RF_INPUT, 1, 0),

	/* Statistics API */
	_DTV_CMD(DTV_STAT_SIGNAL_STRENGTH, 0, 0),
	_DTV_CMD(DTV_STAT_CNR, 0, 0),
	_DTV_CMD(DTV_STAT_PRE_ERROR_BIT_COUNT, 0, 0),
	_DTV_CMD(DTV_STAT_PRE_TOTAL_BIT_COUNT, 0, 0),
	_DTV_CMD(DTV_STAT_POST_ERROR_BIT_COUNT, 0, 0),
	_DTV_CMD(DTV_STAT_POST_TOTAL_BIT_COUNT, 0, 0),
	_DTV_CMD(DTV_STAT_ERROR_BLOCK_COUNT, 0, 0),
	_DTV_CMD(DTV_STAT_TOTAL_BLOCK_COUNT, 0, 0),

};


int dtv_max_command = (sizeof(dtv_cmds) /sizeof(dtv_cmds[0]) -1);

/* Synchronise the legacy tuning parameters into the cache, so that demodulator
 * drivers can use a single set_frontend tuning function, regardless of whether
 * it's being used for the legacy or new API, reducing code and complexity.
 */
static int dtv_property_cache_sync(struct neumo_dvb_frontend *fe,
					 struct neumo_driver_dtv_frontend_properties *c,
					 const struct dvb_frontend_parameters *p)
{
	c->frequency = p->frequency;
	c->inversion = p->inversion;

	switch (dvbv3_type(c->delivery_system)) {
	case DVBV3_QPSK:
		dev_dbg(fe->dvb->device, "%s: Preparing QPSK req\n", __func__);
		c->symbol_rate = p->u.qpsk.symbol_rate;
		c->fec_inner = p->u.qpsk.fec_inner;
		break;
	case DVBV3_QAM:
		dev_dbg(fe->dvb->device, "%s: Preparing QAM req\n", __func__);
		c->symbol_rate = p->u.qam.symbol_rate;
		c->fec_inner = p->u.qam.fec_inner;
		c->modulation = p->u.qam.modulation;
		break;
	case DVBV3_OFDM:
		dev_dbg(fe->dvb->device, "%s: Preparing OFDM req\n", __func__);

		switch (p->u.ofdm.bandwidth) {
		case BANDWIDTH_10_MHZ:
			c->bandwidth_hz = 10000000;
			break;
		case BANDWIDTH_8_MHZ:
			c->bandwidth_hz = 8000000;
			break;
		case BANDWIDTH_7_MHZ:
			c->bandwidth_hz = 7000000;
			break;
		case BANDWIDTH_6_MHZ:
			c->bandwidth_hz = 6000000;
			break;
		case BANDWIDTH_5_MHZ:
			c->bandwidth_hz = 5000000;
			break;
		case BANDWIDTH_1_712_MHZ:
			c->bandwidth_hz = 1712000;
			break;
		case BANDWIDTH_AUTO:
			c->bandwidth_hz = 0;
		}

		c->code_rate_HP = p->u.ofdm.code_rate_HP;
		c->code_rate_LP = p->u.ofdm.code_rate_LP;
		c->modulation = p->u.ofdm.constellation;
		c->transmission_mode = p->u.ofdm.transmission_mode;
		c->guard_interval = p->u.ofdm.guard_interval;
		c->hierarchy = p->u.ofdm.hierarchy_information;
		break;
	case DVBV3_ATSC:
		dev_dbg(fe->dvb->device, "%s: Preparing ATSC req\n", __func__);
		c->modulation = p->u.vsb.modulation;
		if (c->delivery_system == SYS_ATSCMH)
			break;
		if ((c->modulation == VSB_8) || (c->modulation == VSB_16))
			c->delivery_system = SYS_ATSC;
		else
			c->delivery_system = SYS_DVBC_ANNEX_B;
		break;
	case DVBV3_UNKNOWN:
		dev_err(fe->dvb->device,
			"%s: doesn't know how to handle a DVBv3 call to delivery system %i\n",
			__func__, c->delivery_system);
		return -EINVAL;
	}

	return 0;
}

/* Ensure the cached values are set correctly in the frontend
 * legacy tuning structures, for the advanced tuning API.
 */
static int
neumo_driver_dtv_property_legacy_params_sync(struct neumo_dvb_frontend* fe,
																						 struct dvb_frontend_parameters *p)
{
	const struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct dvb_adapter* adapter = fe->dvb;
	p->frequency = c->frequency;
	p->inversion = c->inversion;

	switch (dvbv3_type(c->delivery_system)) {
	case DVBV3_UNKNOWN:
		adapter_dprintk(adapter, "%s: doesn't know how to handle a DVBv3 call to delivery system %i\n",
			__func__, c->delivery_system);
		return -EINVAL;
	case DVBV3_QPSK:
		adapter_dprintk(adapter, "%s: Preparing QPSK req\n", __func__);
		p->u.qpsk.symbol_rate = c->symbol_rate;
		p->u.qpsk.fec_inner = c->fec_inner;
		break;
	case DVBV3_QAM:
		adapter_dprintk(adapter, "%s: Preparing QAM req\n", __func__);
		p->u.qam.symbol_rate = c->symbol_rate;
		p->u.qam.fec_inner = c->fec_inner;
		p->u.qam.modulation = c->modulation;
		break;
	case DVBV3_OFDM:
		adapter_dprintk(adapter, "%s: Preparing OFDM req\n", __func__);
		switch (c->bandwidth_hz) {
		case 10000000:
			p->u.ofdm.bandwidth = BANDWIDTH_10_MHZ;
			break;
		case 8000000:
			p->u.ofdm.bandwidth = BANDWIDTH_8_MHZ;
			break;
		case 7000000:
			p->u.ofdm.bandwidth = BANDWIDTH_7_MHZ;
			break;
		case 6000000:
			p->u.ofdm.bandwidth = BANDWIDTH_6_MHZ;
			break;
		case 5000000:
			p->u.ofdm.bandwidth = BANDWIDTH_5_MHZ;
			break;
		case 1712000:
			p->u.ofdm.bandwidth = BANDWIDTH_1_712_MHZ;
			break;
		case 0:
		default:
			p->u.ofdm.bandwidth = BANDWIDTH_AUTO;
		}
		p->u.ofdm.code_rate_HP = c->code_rate_HP;
		p->u.ofdm.code_rate_LP = c->code_rate_LP;
		p->u.ofdm.constellation = c->modulation;
		p->u.ofdm.transmission_mode = c->transmission_mode;
		p->u.ofdm.guard_interval = c->guard_interval;
		p->u.ofdm.hierarchy_information = c->hierarchy;
		break;
	case DVBV3_ATSC:
		adapter_dprintk(adapter, "%s: Preparing VSB req\n", __func__);
		p->u.vsb.modulation = c->modulation;
		break;
	}
	return 0;
}

/**
 * neumo_driver_dtv_get_frontend - calls a callback for retrieving DTV parameters
 * @fe:		struct neumo_dvb_frontend pointer
 * @c:		struct neumo_driver_dtv_frontend_properties pointer (DVBv5 cache)
 * @p_out:	struct dvb_frontend_parameters pointer (DVBv3 FE struct)
 *
 * This routine calls either the DVBv3 or DVBv5 get_frontend call.
 * If c is not null, it will update the DVBv5 cache struct pointed by it.
 * If p_out is not null, it will update the DVBv3 params pointed by it.
 */
static int neumo_driver_dtv_get_frontend(struct neumo_dvb_frontend *fe,
					struct neumo_driver_dtv_frontend_properties *c,
					struct dvb_frontend_parameters *p_out)
{
	int r;

	if (fe->ops.get_frontend) {
		r = fe->ops.get_frontend(fe, c);
		if (unlikely(r < 0))
			return r;
		if (p_out)
			neumo_driver_dtv_property_legacy_params_sync(fe, p_out);
		return 0;
	}

	/* As everything is in cache, get_frontend fops are always supported */
	return 0;
}

static int neumo_driver_dvb_frontend_handle_ioctl(struct file *file,
						 unsigned int cmd, void *parg);

static int neumo_driver_dvb_frontend_handle_algo_ctrl_ioctl(struct file *file,
																							 unsigned int cmd, void *parg);

static int dtv_set_sat_scan(struct neumo_dvb_frontend *fe, bool scan_continue);
static int dtv_set_spectrum(struct neumo_dvb_frontend *fe, enum dtv_fe_spectrum_method method);
static int dtv_get_spectrum(struct neumo_dvb_frontend *fe, struct dtv_fe_spectrum*user);
static int dtv_set_pls_search_list(struct neumo_dvb_frontend *fe, struct dtv_pls_search_list* user);
static int dtv_get_pls_search_list(struct neumo_dvb_frontend *fe, struct dtv_pls_search_list* user);
static int dtv_set_constellation(struct neumo_dvb_frontend *fe, struct dtv_fe_constellation* constellation);
static int dtv_get_constellation(struct neumo_dvb_frontend *fe, struct dtv_fe_constellation* user);
static int dtv_get_matype_list(struct neumo_dvb_frontend *fe, struct dtv_matype_list* user);

static int neumo_driver_dtv_property_process_get(struct neumo_dvb_frontend *fe,
						const struct neumo_driver_dtv_frontend_properties *c,
						struct dtv_property *tvp,
						struct file *file)
{
	int ncaps;
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
	switch (tvp->cmd) {
	case DTV_ENUM_DELSYS:
		ncaps = 0;
		while (ncaps < MAX_DELSYS && fe->ops.delsys[ncaps]) {
			tvp->u.buffer.data[ncaps] = fe->ops.delsys[ncaps];
			ncaps++;
		}
		tvp->u.buffer.len = ncaps;
		break;
	case DTV_ALGORITHM:
		tvp->u.data = c->algorithm;
		break;
	case DTV_RF_INPUT:
		tvp->u.data = c->rf_in_valid ? c->rf_in : fe->dvb->num;
		break;
	case DTV_OUTPUT_BBFRAMES:
		tvp->u.data = c->output_bbframes;
		break;
	case DTV_FREQUENCY:
		tvp->u.data = c->frequency;
		break;
	case DTV_BITRATE:
		tvp->u.data = c->bit_rate;
		break;
	case DTV_LOCKTIME:
		tvp->u.data = c->locktime;
		//fe_dprintk(fe, "LOCK TIME: returning %d\n", 	tvp->u.data);
		break;
	case DTV_SCAN_START_FREQUENCY:
		tvp->u.data = c->scan_start_frequency;
		break;
	case DTV_SCAN_END_FREQUENCY:
		tvp->u.data = c->scan_end_frequency;
		break;
	case DTV_SCAN_RESOLUTION:
		tvp->u.data = c->scan_resolution;
		break;
	case DTV_SCAN_FFT_SIZE:
		tvp->u.data = c->scan_fft_size;
		break;
 	case DTV_SPECTRUM:
		dtv_get_spectrum(fe, &tvp->u.spectrum);
		break;
 	case DTV_CONSTELLATION:
		dtv_get_constellation(fe, &tvp->u.constellation);
		break;
 	case DTV_MATYPE_LIST:
		dtv_get_matype_list(fe, &tvp->u.matype_list);
		break;
	case DTV_HEARTBEAT:
		tvp->u.data = fepriv->heartbeat_interval;
		break;
	case DTV_SEARCH_RANGE:
		tvp->u.data = c->search_range;
		break;
	case DTV_MAX_SYMBOL_RATE:
		tvp->u.data = c->max_symbol_rate;
		break;
	case DTV_PLS_SEARCH_RANGE: {
		int i=0;
		memcpy(&tvp->u.buffer.data[i], &c->pls_search_range_start, sizeof(c->pls_search_range_start));
		i+=sizeof(c->pls_search_range_start);

		memcpy(&tvp->u.buffer.data[i], &c->pls_search_range_end, sizeof(c->pls_search_range_end));
		i+=sizeof(c->pls_search_range_end);
		tvp->u.buffer.len = i;
	}
		break;
	case DTV_PLS_SEARCH_LIST:
		dtv_get_pls_search_list(fe, &tvp->u.pls_search_codes);
		break;
	case DTV_ISI_LIST:
		tvp->u.buffer.len = (sizeof(c->isi_bitset) <  sizeof(tvp->u.buffer.data)) ?
			sizeof(c->isi_bitset) :  sizeof(tvp->u.buffer.data);
		memcpy(&tvp->u.buffer.data[0], &c->isi_bitset[0], tvp->u.buffer.len  * sizeof(__u8));
		break;
	case DTV_MODULATION:
		tvp->u.data = c->modulation;
		break;
	case DTV_BANDWIDTH_HZ:
		tvp->u.data = c->bandwidth_hz;
		break;
	case DTV_INVERSION:
		tvp->u.data = c->inversion;
		break;
	case DTV_SYMBOL_RATE:
		tvp->u.data = c->symbol_rate;
		break;
	case DTV_INNER_FEC:
		tvp->u.data = c->fec_inner;
		break;
	case DTV_PILOT:
		tvp->u.data = c->pilot;
		break;
	case DTV_ROLLOFF:
		tvp->u.data = c->rolloff;
		break;
	case DTV_DELIVERY_SYSTEM:
		tvp->u.data = c->delivery_system;
		break;
	case DTV_VOLTAGE:
		tvp->u.data = c->voltage;
		break;
	case DTV_TONE:
		tvp->u.data = c->sectone;
		break;
	case DTV_API_VERSION:
		tvp->u.data = (DVB_API_VERSION << 8) | DVB_API_VERSION_MINOR;
		break;
	case DTV_CODE_RATE_HP:
		tvp->u.data = c->code_rate_HP;
		break;
	case DTV_CODE_RATE_LP:
		tvp->u.data = c->code_rate_LP;
		break;
	case DTV_GUARD_INTERVAL:
		tvp->u.data = c->guard_interval;
		break;
	case DTV_TRANSMISSION_MODE:
		tvp->u.data = c->transmission_mode;
		break;
	case DTV_HIERARCHY:
		tvp->u.data = c->hierarchy;
		break;
	case DTV_INTERLEAVING:
		tvp->u.data = c->interleaving;
		break;

	/* ISDB-T Support here */
	case DTV_ISDBT_PARTIAL_RECEPTION:
		tvp->u.data = c->isdbt_partial_reception;
		break;
	case DTV_ISDBT_SOUND_BROADCASTING:
		tvp->u.data = c->isdbt_sb_mode;
		break;
	case DTV_ISDBT_SB_SUBCHANNEL_ID:
		tvp->u.data = c->isdbt_sb_subchannel;
		break;
	case DTV_ISDBT_SB_SEGMENT_IDX:
		tvp->u.data = c->isdbt_sb_segment_idx;
		break;
	case DTV_ISDBT_SB_SEGMENT_COUNT:
		tvp->u.data = c->isdbt_sb_segment_count;
		break;
	case DTV_ISDBT_LAYER_ENABLED:
		tvp->u.data = c->isdbt_layer_enabled;
		break;
	case DTV_ISDBT_LAYERA_FEC:
		tvp->u.data = c->layer[0].fec;
		break;
	case DTV_ISDBT_LAYERA_MODULATION:
		tvp->u.data = c->layer[0].modulation;
		break;
	case DTV_ISDBT_LAYERA_SEGMENT_COUNT:
		tvp->u.data = c->layer[0].segment_count;
		break;
	case DTV_ISDBT_LAYERA_TIME_INTERLEAVING:
		tvp->u.data = c->layer[0].interleaving;
		break;
	case DTV_ISDBT_LAYERB_FEC:
		tvp->u.data = c->layer[1].fec;
		break;
	case DTV_ISDBT_LAYERB_MODULATION:
		tvp->u.data = c->layer[1].modulation;
		break;
	case DTV_ISDBT_LAYERB_SEGMENT_COUNT:
		tvp->u.data = c->layer[1].segment_count;
		break;
	case DTV_ISDBT_LAYERB_TIME_INTERLEAVING:
		tvp->u.data = c->layer[1].interleaving;
		break;
	case DTV_ISDBT_LAYERC_FEC:
		tvp->u.data = c->layer[2].fec;
		break;
	case DTV_ISDBT_LAYERC_MODULATION:
		tvp->u.data = c->layer[2].modulation;
		break;
	case DTV_ISDBT_LAYERC_SEGMENT_COUNT:
		tvp->u.data = c->layer[2].segment_count;
		break;
	case DTV_ISDBT_LAYERC_TIME_INTERLEAVING:
		tvp->u.data = c->layer[2].interleaving;
		break;

	/* Multistream support */
	case DTV_STREAM_ID:
		tvp->u.data = c->stream_id;
		break;

	/* Modcode support */
	case DTV_MODCODE:
		tvp->u.data = c->modcode;
		break;

	/* Physical layer scrambling support */
	case DTV_SCRAMBLING_SEQUENCE_INDEX:
		tvp->u.data = c->scrambling_sequence_index;
		break;

	case DTV_MATYPE:
		tvp->u.data = c->matype_valid ? c->matype_val : -1;
		break;
#ifdef TODO
	case DTV_FRAME_LEN:
		tvp->u.data = c->frame_len;
		break;
#endif
	/* ATSC-MH */
	case DTV_ATSCMH_FIC_VER:
		tvp->u.data = fe->dtv_property_cache.atscmh_fic_ver;
		break;
	case DTV_ATSCMH_PARADE_ID:
		tvp->u.data = fe->dtv_property_cache.atscmh_parade_id;
		break;
	case DTV_ATSCMH_NOG:
		tvp->u.data = fe->dtv_property_cache.atscmh_nog;
		break;
	case DTV_ATSCMH_TNOG:
		tvp->u.data = fe->dtv_property_cache.atscmh_tnog;
		break;
	case DTV_ATSCMH_SGN:
		tvp->u.data = fe->dtv_property_cache.atscmh_sgn;
		break;
	case DTV_ATSCMH_PRC:
		tvp->u.data = fe->dtv_property_cache.atscmh_prc;
		break;
	case DTV_ATSCMH_RS_FRAME_MODE:
		tvp->u.data = fe->dtv_property_cache.atscmh_rs_frame_mode;
		break;
	case DTV_ATSCMH_RS_FRAME_ENSEMBLE:
		tvp->u.data = fe->dtv_property_cache.atscmh_rs_frame_ensemble;
		break;
	case DTV_ATSCMH_RS_CODE_MODE_PRI:
		tvp->u.data = fe->dtv_property_cache.atscmh_rs_code_mode_pri;
		break;
	case DTV_ATSCMH_RS_CODE_MODE_SEC:
		tvp->u.data = fe->dtv_property_cache.atscmh_rs_code_mode_sec;
		break;
	case DTV_ATSCMH_SCCC_BLOCK_MODE:
		tvp->u.data = fe->dtv_property_cache.atscmh_sccc_block_mode;
		break;
	case DTV_ATSCMH_SCCC_CODE_MODE_A:
		tvp->u.data = fe->dtv_property_cache.atscmh_sccc_code_mode_a;
		break;
	case DTV_ATSCMH_SCCC_CODE_MODE_B:
		tvp->u.data = fe->dtv_property_cache.atscmh_sccc_code_mode_b;
		break;
	case DTV_ATSCMH_SCCC_CODE_MODE_C:
		tvp->u.data = fe->dtv_property_cache.atscmh_sccc_code_mode_c;
		break;
	case DTV_ATSCMH_SCCC_CODE_MODE_D:
		tvp->u.data = fe->dtv_property_cache.atscmh_sccc_code_mode_d;
		break;

	case DTV_LNA:
		tvp->u.data = c->lna;
		break;

	/* Fill quality measures */
	case DTV_STAT_SIGNAL_STRENGTH:
		tvp->u.st = c->strength;
		break;
	case DTV_STAT_CNR:
		tvp->u.st = c->cnr;
		break;
	case DTV_STAT_PRE_ERROR_BIT_COUNT:
		tvp->u.st = c->pre_bit_error;
		break;
	case DTV_STAT_PRE_TOTAL_BIT_COUNT:
		tvp->u.st = c->pre_bit_count;
		break;
	case DTV_STAT_POST_ERROR_BIT_COUNT:
		tvp->u.st = c->post_bit_error;
		break;
	case DTV_STAT_POST_TOTAL_BIT_COUNT:
		tvp->u.st = c->post_bit_count;
		break;
	case DTV_STAT_ERROR_BLOCK_COUNT:
		tvp->u.st = c->block_error;
		break;
	case DTV_STAT_TOTAL_BLOCK_COUNT:
		tvp->u.st = c->block_count;
		break;
	default:
		dev_dbg(fe->dvb->device,
			"%s: FE property %d doesn't exist\n",
			__func__, tvp->cmd);
		return -EINVAL;
	}

	if (!dtv_cmds[tvp->cmd].buffer)
		dev_dbg(fe->dvb->device,
						"%s: GET cmd 0x%08x (%s) = 0x%08x\n",
			__func__, tvp->cmd, dtv_cmds[tvp->cmd].name,
						tvp->u.data);
	else
		dev_dbg(fe->dvb->device,
			"%s: GET cmd 0x%08x (%s) len %d: %*ph\n",
						__func__,
						tvp->cmd, dtv_cmds[tvp->cmd].name,
			tvp->u.buffer.len,
						tvp->u.buffer.len, tvp->u.buffer.data);

	return 0;
}

static int neumo_driver_dtv_set_frontend(struct neumo_dvb_frontend *fe);
static int neumo_driver_dtv_set_sec_configured(struct neumo_dvb_frontend *fe);

static bool is_dvbv3_delsys(u32 delsys)
{
	return (delsys == SYS_DVBT) || (delsys == SYS_DVBC_ANNEX_A) ||
				 (delsys == SYS_DVBS) || (delsys == SYS_ATSC);
}

/**
 * neumo_driver_emulate_delivery_system - emulate a DVBv5 delivery system with a DVBv3 type
 * @fe:			struct frontend;
 * @delsys:			DVBv5 type that will be used for emulation
 *
 * Provides emulation for delivery systems that are compatible with the old
 * DVBv3 call. Among its usages, it provices support for ISDB-T, and allows
 * using a DVB-S2 only frontend just like it were a DVB-S, if the frontend
 * parameters are compatible with DVB-S spec.
 */
static int neumo_driver_emulate_delivery_system(struct neumo_dvb_frontend *fe, u32 delsys)
{
	int i;
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;

	c->delivery_system = delsys;
	if(delsys == SYS_AUTO)
		return 0;
	/*
	 * If the call is for ISDB-T, put it into full-seg, auto mode, TV
	 */
	if (c->delivery_system == SYS_ISDBT) {
		dev_dbg(fe->dvb->device,
			"%s: Using defaults for SYS_ISDBT\n",
			__func__);

		if (!c->bandwidth_hz)
			c->bandwidth_hz = 6000000;

		c->isdbt_partial_reception = 0;
		c->isdbt_sb_mode = 0;
		c->isdbt_sb_subchannel = 0;
		c->isdbt_sb_segment_idx = 0;
		c->isdbt_sb_segment_count = 0;
		c->isdbt_layer_enabled = 7;
		for (i = 0; i < 3; i++) {
			c->layer[i].fec = FEC_AUTO;
			c->layer[i].modulation = QAM_AUTO;
			c->layer[i].interleaving = 0;
			c->layer[i].segment_count = 0;
		}
	}
	dev_dbg(fe->dvb->device, "%s: change delivery system on cache to %d\n",
		__func__, c->delivery_system);

	return 0;
}

/**
 * dvbv5_set_delivery_system - Sets the delivery system for a DVBv5 API call
 * @fe:			frontend struct
 * @desired_system:	delivery system requested by the user
 *
 * A DVBv5 call know what's the desired system it wants. So, set it.
 *
 * There are, however, a few known issues with early DVBv5 applications that
 * are also handled by this logic:
 *
 * 1) Some early apps use SYS_UNDEFINED as the desired delivery system.
 *    This is an API violation, but, as we don't want to break userspace,
 *    convert it to the first supported delivery system.
 * 2) Some apps might be using a DVBv5 call in a wrong way, passing, for
 *    example, SYS_DVBT instead of SYS_ISDBT. This is because early usage of
 *    ISDB-T provided backward compat with DVB-T.
 */
static int dvbv5_set_delivery_system(struct neumo_dvb_frontend *fe,
						 u32 desired_system)
{
	int ncaps;
	u32 delsys = SYS_UNDEFINED;
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	//	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	enum dvbv3_emulation_type type;
	fe_dprintk(fe, "adapter=%d desired_system=%d\n", fe->dvb->num, desired_system);
	/*
	 * It was reported that some old DVBv5 applications were
	 * filling delivery_system with SYS_UNDEFINED. If this happens,
	 * assume that the application wants to use the first supported
	 * delivery system.
	 */
	if (desired_system == SYS_UNDEFINED)
		desired_system = fe->ops.delsys[0];
	fe_dprintk(fe, "desired_system=%d\n", desired_system);
	/*
	 * This is a DVBv5 call. So, it likely knows the supported
	 * delivery systems. So, check if the desired delivery system is
	 * supported
	 */
	ncaps = 0;

	while (ncaps < MAX_DELSYS && fe->ops.delsys[ncaps]) {
		fe_dprintk(fe, "trying delsys [%d]=%d <> %d\n", ncaps, fe->ops.delsys[ncaps], desired_system);
		if (fe->ops.delsys[ncaps] == desired_system) {
			c->delivery_system = desired_system;
			fe_dprintk(fe, "found delsys [%d]=%d <> %d\n", ncaps, fe->ops.delsys[ncaps], desired_system);
			dev_dbg(fe->dvb->device,
							"%s: Changing delivery system to %d\n",
								__func__, desired_system);
			return 0;
		}
		ncaps++;
	}

	/*
	 * The requested delivery system isn't supported. Maybe userspace
		 * is requesting a DVBv3 compatible delivery system.
		 *
		 * The emulation only works if the desired system is one of the
		 * delivery systems supported by DVBv3 API
			 */
	if (!is_dvbv3_delsys(desired_system)) {
		dev_dbg(fe->dvb->device,
						"%s: Delivery system %d not supported.\n",
						__func__, desired_system);
		return -EINVAL;
	}

	type = dvbv3_type(desired_system);
	fe_dprintk(fe, "c->delivery_system=%d\n", c->delivery_system);
	/*
	* Get the last non-DVBv3 delivery system that has the same type
	* of the desired system
	*/
	ncaps = 0;
	while (ncaps < MAX_DELSYS && fe->ops.delsys[ncaps]) {
		if (dvbv3_type(fe->ops.delsys[ncaps]) == type)
			delsys = fe->ops.delsys[ncaps];
		ncaps++;
	}

	/* There's nothing compatible with the desired delivery system */
	if (delsys == SYS_UNDEFINED) {
		dev_dbg(fe->dvb->device,
						"%s: Delivery system %d not supported on emulation mode.\n",
						__func__, desired_system);
		fe_dprintk(fe, "delsys=SYS_UNDEFINED");
		return -EINVAL;
	}

	dev_dbg(fe->dvb->device,
					"%s: Using delivery system %d emulated as if it were %d\n",
					__func__, delsys, desired_system);

	return neumo_driver_emulate_delivery_system(fe, desired_system);
}

/**
 * dvbv3_set_delivery_system - Sets the delivery system for a DVBv3 API call
 * @fe:	frontend struct
 *
 * A DVBv3 call doesn't know what's the desired system it wants. It also
 * doesn't allow to switch between different types. Due to that, userspace
 * should use DVBv5 instead.
 * However, in order to avoid breaking userspace API, limited backward
 * compatibility support is provided.
 *
 * There are some delivery systems that are incompatible with DVBv3 calls.
 *
 * This routine should work fine for frontends that support just one delivery
 * system.
 *
 * For frontends that support multiple frontends:
 * 1) It defaults to use the first supported delivery system. There's an
 *    userspace application that allows changing it at runtime;
 *
 * 2) If the current delivery system is not compatible with DVBv3, it gets
 *    the first one that it is compatible.
 *
 * NOTE: in order for this to work with applications like Kaffeine that
 *	uses a DVBv5 call for DVB-S2 and a DVBv3 call to go back to
 *	DVB-S, drivers that support both DVB-S and DVB-S2 should have the
 *	SYS_DVBS entry before the SYS_DVBS2, otherwise it won't switch back
 *	to DVB-S.
 */
static int dvbv3_set_delivery_system(struct neumo_dvb_frontend *fe)
{
	int ncaps;
	u32 delsys = SYS_UNDEFINED;
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;

	/* If not set yet, defaults to the first supported delivery system */
	if (c->delivery_system == SYS_UNDEFINED)
		c->delivery_system = fe->ops.delsys[0];

	/*
	 * Trivial case: just use the current one, if it already a DVBv3
	 * delivery system
	 */
	if (is_dvbv3_delsys(c->delivery_system)) {
		dev_dbg(fe->dvb->device,
			"%s: Using delivery system to %d\n",
			__func__, c->delivery_system);
		return 0;
	}

	/*
	 * Seek for the first delivery system that it is compatible with a
	 * DVBv3 standard
	 */
	ncaps = 0;
	while (ncaps < MAX_DELSYS && fe->ops.delsys[ncaps]) {
		if (dvbv3_type(fe->ops.delsys[ncaps]) != DVBV3_UNKNOWN) {
			delsys = fe->ops.delsys[ncaps];
			break;
		}
		ncaps++;
	}
	if (delsys == SYS_UNDEFINED) {
		dev_dbg(fe->dvb->device,
			"%s: Couldn't find a delivery system that works with FE_SET_FRONTEND\n",
			__func__);
		return -EINVAL;
	}
	return neumo_driver_emulate_delivery_system(fe, delsys);
}

static int neumo_driver_dtv_property_process_set_int(struct neumo_dvb_frontend *fe,
						struct file *file,
						u32 cmd, u32 data)
{
	int r = 0;
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;

	/* Allow the frontend to validate incoming properties */
	if (fe->ops.set_property) {
		r = fe->ops.set_property(fe, cmd, data);
		if (r < 0)
			return r;
	}

	switch(cmd) {
	case DTV_ALGORITHM:
		c->algorithm = data;
		break;
	case DTV_OUTPUT_BBFRAMES:
		c->output_bbframes = data;
		break;
	case DTV_FREQUENCY:
		c->frequency = data;
		break;
	case DTV_SCAN_START_FREQUENCY:
		c->scan_start_frequency = data;
		break;
	case DTV_SCAN_END_FREQUENCY:
		c->scan_end_frequency = data;
		break;
	case DTV_SCAN_RESOLUTION:
		c->scan_resolution = data;
		break;
	case DTV_SCAN_FFT_SIZE:
		c->scan_fft_size = data;
		break;

	case DTV_SEARCH_RANGE:
		c->search_range = data;
		break;
	case DTV_MAX_SYMBOL_RATE:
		c->max_symbol_rate = data;
		break;
	case DTV_MODULATION:
		c->modulation = data;
		break;
	case DTV_BANDWIDTH_HZ:
		c->bandwidth_hz = data;
		break;
	case DTV_INVERSION:
		c->inversion = data;
		break;
	case DTV_SYMBOL_RATE:
		c->symbol_rate = data;
		break;
	case DTV_INNER_FEC:
		c->fec_inner = data;
		break;
	case DTV_PILOT:
		c->pilot = data;
		break;
	case DTV_ROLLOFF:
		c->rolloff = data;
		break;
	case DTV_DELIVERY_SYSTEM:
		r = dvbv5_set_delivery_system(fe, data);
		break;
	case DTV_VOLTAGE:
		c->voltage = data;
		r = neumo_driver_dvb_frontend_handle_ioctl(file, FE_SET_VOLTAGE,
								(void *)c->voltage);
		fe_dprintk(fe, "DTV_VOLTAGE: %d r=%d\n", c->voltage, r);
		break;
	case DTV_TONE:
		c->sectone = data;
		fe_dprintk(fe, "DTV_TONE: %d\n", c->sectone);
		r = neumo_driver_dvb_frontend_handle_ioctl(file, FE_SET_TONE,
								(void *)c->sectone);
		break;
	case DTV_CODE_RATE_HP:
		c->code_rate_HP = data;
		break;
	case DTV_CODE_RATE_LP:
		c->code_rate_LP = data;
		break;
	case DTV_GUARD_INTERVAL:
		c->guard_interval = data;
		break;
	case DTV_TRANSMISSION_MODE:
		c->transmission_mode = data;
		break;
	case DTV_HIERARCHY:
		c->hierarchy = data;
		break;
	case DTV_INTERLEAVING:
		c->interleaving = data;
		break;

	/* ISDB-T Support here */
	case DTV_ISDBT_PARTIAL_RECEPTION:
		c->isdbt_partial_reception = data;
		break;
	case DTV_ISDBT_SOUND_BROADCASTING:
		c->isdbt_sb_mode = data;
		break;
	case DTV_ISDBT_SB_SUBCHANNEL_ID:
		c->isdbt_sb_subchannel = data;
		break;
	case DTV_ISDBT_SB_SEGMENT_IDX:
		c->isdbt_sb_segment_idx = data;
		break;
	case DTV_ISDBT_SB_SEGMENT_COUNT:
		c->isdbt_sb_segment_count = data;
		break;
	case DTV_ISDBT_LAYER_ENABLED:
		c->isdbt_layer_enabled = data;
		break;
	case DTV_ISDBT_LAYERA_FEC:
		c->layer[0].fec = data;
		break;
	case DTV_ISDBT_LAYERA_MODULATION:
		c->layer[0].modulation = data;
		break;
	case DTV_ISDBT_LAYERA_SEGMENT_COUNT:
		c->layer[0].segment_count = data;
		break;
	case DTV_ISDBT_LAYERA_TIME_INTERLEAVING:
		c->layer[0].interleaving = data;
		break;
	case DTV_ISDBT_LAYERB_FEC:
		c->layer[1].fec = data;
		break;
	case DTV_ISDBT_LAYERB_MODULATION:
		c->layer[1].modulation = data;
		break;
	case DTV_ISDBT_LAYERB_SEGMENT_COUNT:
		c->layer[1].segment_count = data;
		break;
	case DTV_ISDBT_LAYERB_TIME_INTERLEAVING:
		c->layer[1].interleaving = data;
		break;
	case DTV_ISDBT_LAYERC_FEC:
		c->layer[2].fec = data;
		break;
	case DTV_ISDBT_LAYERC_MODULATION:
		c->layer[2].modulation = data;
		break;
	case DTV_ISDBT_LAYERC_SEGMENT_COUNT:
		c->layer[2].segment_count = data;
		break;
	case DTV_ISDBT_LAYERC_TIME_INTERLEAVING:
		c->layer[2].interleaving = data;
		break;

	/* Multistream support */
	case DTV_STREAM_ID:
		c->stream_id = data;
		break;

    /* Modcode support */
	case DTV_MODCODE:
		c->modcode = data;
		break;

	/* Physical layer scrambling support */
	case DTV_SCRAMBLING_SEQUENCE_INDEX:
		c->scrambling_sequence_index = data;
		break;
#ifdef TODO
	case DTV_ENABLE_MODCOD:
		c->enable_modcod = data;
		break;
#endif
	/* ATSC-MH */
	case DTV_ATSCMH_PARADE_ID:
		fe->dtv_property_cache.atscmh_parade_id = data;
		break;
	case DTV_ATSCMH_RS_FRAME_ENSEMBLE:
		fe->dtv_property_cache.atscmh_rs_frame_ensemble = data;
		break;

	case DTV_LNA:
		c->lna = data;
		if (fe->ops.set_lna)
			r = fe->ops.set_lna(fe);
		if (r < 0)
			c->lna = LNA_AUTO;
		break;
	/* neumo api if interval>0 tune loop will run every heart_beat interval milliseconds and event will be
		 sent even if status does not change*/
	case DTV_HEARTBEAT:
		fepriv->heartbeat_interval = data;
		if(fepriv->heartbeat_interval<500)
			fepriv->heartbeat_interval = 500;
		break;
	default:
		return -EINVAL;
	}

	return r;
}

/**
 * neumo_driver_dtv_property_process_set -  Sets a single DTV property
 * @fe:		Pointer to &struct neumo_dvb_frontend
 * @file:	Pointer to &struct file
 * @cmd:	Digital TV command
 * @data:	An unsigned 32-bits number
 *
 * This routine assigns the property
 * value to the corresponding member of
 * &struct neumo_driver_dtv_frontend_properties
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
static int neumo_driver_dtv_property_process_set(struct neumo_dvb_frontend *fe,
																		struct file *file,
																		u32 cmd, struct dtv_property *tvp)
{
	int r = 0;
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;


	if (!cmd || cmd > dtv_max_command) {
		dev_warn(fe->dvb->device, "%s: SET cmd 0x%08x undefined\n",
						 __func__, cmd);
		return -EINVAL;
	}
	/** Dump DTV command name*/
	dev_dbg(fe->dvb->device,
					"%s: SET cmd 0x%08x (%s) to 0x%08x\n",
					__func__, cmd, dtv_cmds[cmd].name, tvp->u.data);

	switch(cmd) {
	case DTV_CLEAR:
		fe_dprintk(fe, "cmd=DTV_CLEAR");
		/*
		 * Reset a cache of data specific to the frontend here. This does
		 * not effect hardware.
		 */
		neumo_driver_dvb_frontend_clear_cache(fe);
		break;
	case DTV_TUNE:
		/*
		 * Use the cached Digital TV properties to tune the
		 * frontend
		 */
		dev_dbg(fe->dvb->device,
			"%s: Setting the frontend from property cache\n",
			__func__);
		r = neumo_driver_dtv_set_frontend(fe);
		fe_dprintk(fe, "cmd=DTV_TUNE r=%d adapter=%d", r, fe->dvb->num);
		break;
	case DTV_SET_SEC_CONFIGURED:
		/*
		 * inform driver that frontend has configured lnb or other secondary equipment
		 */
		dev_dbg(fe->dvb->device,
			"%s: Setting the frontend from property cache\n",
			__func__);

		r = neumo_driver_dtv_set_sec_configured(fe);
		fe_dprintk(fe, "cmd=NEUMO_DRIVER_DTV_SET_SEC_CONFIGURED r=%d adapter=%d", r, fe->dvb->num);
		break;
	case DTV_SCAN:
		/*
		 * Use the cached Digital TV properties to scan the
		 * frontend
		 */
		fe_dprintk(fe, "cmd=DTV_SCAN");
		fe_dprintk(fe, "calling sat scan\n");
		dev_dbg(fe->dvb->device,
			"%s: Setting the frontend from property cache\n",
			__func__);
		r = dtv_set_sat_scan(fe, tvp->u.data);
		fe_dprintk(fe, "called sat scan r=%d\n", r);
		break;
	case DTV_SPECTRUM:
		/*
		 * Use the cached Digital TV properties to scan the
		 * frontend
		 */
		fe_dprintk(fe, "cmd=DTV_SPECTRUM");
		fe_dprintk(fe, "get spectrum scan called\n");
		dev_dbg(fe->dvb->device,
			"%s: Setting the frontend from property cache\n",
			__func__);
		r = dtv_set_spectrum(fe, tvp->u.data);
		break;
	case DTV_CONSTELLATION:
		/*
		 * Use the cached Digital TV properties to scan the
		 * frontend
		 */
		fe_dprintk(fe, "cmd=DTV_CONSTELLATION");
		dev_dbg(fe->dvb->device,
			"%s: Setting the frontend from property cache\n",
			__func__);
		r = dtv_set_constellation(fe, &tvp->u.constellation);
		break;
	case DTV_PLS_SEARCH_RANGE:
		fe_dprintk(fe, "cmd=PLS_SEARCH_RANGE");
		if(tvp->u.buffer.len == sizeof(c->pls_search_range_start) + sizeof(c->pls_search_range_start)) {
			int i= 0;
			memcpy(&c->pls_search_range_start, &tvp->u.buffer.data[i], sizeof(c->pls_search_range_start));
			i+=sizeof(c->pls_search_range_start);
			memcpy(&c->pls_search_range_end, &tvp->u.buffer.data[i], sizeof(c->pls_search_range_start));
			i+=sizeof(c->pls_search_range_start);
		}
		break;
	case DTV_PLS_SEARCH_LIST: {
		fe_dprintk(fe, "cmd=PLS_SEARCH_LIST");
		r = dtv_set_pls_search_list(fe, &tvp->u.pls_search_codes);
	}
		break;
	default:
		r = neumo_driver_dtv_property_process_set_int(fe, file, cmd, tvp->u.data);
		fe_dprintk(fe, "adapter=%d: cmd=%d data=%d ret=%d", fe->dvb->num, cmd, tvp->u.data, r);
	}

	return r;
}

static int init_dtv_fe_spectrum_scan(struct dtv_fe_spectrum* s, struct neumo_driver_dtv_frontend_properties *p,
																		 enum dtv_fe_spectrum_method method)
{
	switch(method) {
	case SPECTRUM_METHOD_SWEEP:
	case SPECTRUM_METHOD_FFT:
		s->spectrum_method = method;
		break;
	default:
		return -EINVAL;
		break;
	}

	return 0;
}



static inline void stop_task(struct neumo_dvb_frontend_private* fepriv) {
	if(fepriv->neumo_api_fe) {
		if(fepriv->neumo_api_fe->ops.stop_task) {
			fepriv->neumo_api_fe->ops.stop_task(fepriv->neumo_api_fe);
		}
	} else {
		BUG_ON(!fepriv->dvb_api_fe);
	}
}

/*
	This handles all ioctls which can access data without locking the fe_priv structure
 */
static int neumo_driver_dvb_frontend_handle_algo_ctrl_ioctl(struct file *file,
																							 unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct neumo_dvb_frontend *fe = dvbdev->priv;
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
	struct neumo_dvb_extra* extra = fepriv->extra;

	struct dtv_algo_ctrl* p  = parg;
	int err = -ENOTSUPP;
	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	switch (p->cmd) {
	case DTV_STOP:
		//this will request the task to stop processing
		fe_dprintk(fe, "entering DTV_STOP: stop_task\n");
		atomic_set(&extra->algo_state.task_should_stop, true);
		if (down_interruptible(&fepriv->sem))
			return -ERESTARTSYS;
		adapter_dprintk(fe->dvb, "DTV_STOP starts stop_task\n");
		if (fe->ops.stop_task)
			fe->ops.stop_task(fe);

		fepriv->state = FESTATE_IDLE;

		atomic_set(&extra->algo_state.task_should_stop, false);

		neumo_dvb_frontend_clear_events_fepriv(fepriv);
		neumo_driver_dvb_frontend_add_event(fe, FE_IDLE);

		up(&fepriv->sem);
		return 0;
		break;
#if 0
	case DTV_ALGO_GET_PROGRESS: {
		struct dtv_algo_ctrl* p  = parg;
		p->u.progress.cur_index = atomic_read(&fe->algo_state.cur_index);
		p->u.progress.max_index = atomic_read(&fe->algo_state.max_index);
	}
		return 0;
		break;
	case DTV_ALGO_WAIT_FOR_PROGRESS: {
		struct dtv_algo_ctrl* p  = parg;
		int ret;

		if (file->f_flags & O_NONBLOCK)
			return -EWOULDBLOCK;

		ret = wait_event_interruptible(fe->algo_state.wait_queue,
																	 dvb_frontend_algo_test_progress(&p->u.progress, &fe->algo_state));
		return ret;
	}
		break;
#endif

	default:
		return -ENOTSUPP;
	} /* switch */

	return err;
}

//main entry point; semaphore not held yet
static int neumo_driver_dvb_frontend_do_ioctl(struct neumo_dvb_frontend* fe,
																					 struct file *file, unsigned int cmd, void *parg) {
	struct neumo_dvb_frontend_private* fepriv = fe->frontend_priv;
	struct dvb_adapter* adapter = fe->dvb;
	adapter_dprintk(adapter, "%s: (%d)\n", __func__, _IOC_NR(cmd));
	if((file->f_flags & O_ACCMODE) != O_RDONLY)  {
		if(cmd == DTV_STOP) {
			static struct dtv_algo_ctrl algo_ctrl = {.cmd= DTV_STOP};
			fe_dprintk(fe, "BAD CALL: DTV_STOP insted of FE_ALGO_CTRL\n");
			parg = &algo_ctrl;
			//	cmd = FE_ALGO_CTRL;
		}

		if(cmd == FE_ALGO_CTRL) {
			fe_dprintk(fe, "algo_ctrl requested\n");
			if ((file->f_flags & O_ACCMODE) == O_RDONLY
					&& (_IOC_DIR(cmd) != _IOC_READ
							|| cmd == DTV_STOP)) {
				return -EPERM;
			}
			return neumo_driver_dvb_frontend_handle_algo_ctrl_ioctl(file, cmd, parg);

		}
		if(cmd == FE_GET_EVENT) {
			return dvb_frontend_get_event_fepriv(fepriv, adapter, parg, file->f_flags);
		}
	}

	if (down_interruptible(&fepriv->sem))
		return -ERESTARTSYS;

	if (fepriv->exit != DVB_FE_NO_EXIT) {
		up(&fepriv->sem);
		return -ENODEV;
	}

	/*
	 * If the frontend is opened in read-only mode, only the ioctls
	 * that don't interfere with the tune logic should be accepted.
	 * That allows an external application to monitor the DVB QoS and
	 * statistics parameters.
	 *
	 * That matches all _IOR() ioctls, except for two special cases:
	 *   - FE_GET_EVENT is part of the tuning logic on a DVB application (there is only one event queue)
	 *   - FE_DISEQC_RECV_SLAVE_REPLY is part of DiSEqC 2.0
	 *     setup
	 * So, those two ioctls should also return -EPERM, as otherwise
	 * reading from them would interfere with a DVB tune application
	 */
	if ((file->f_flags & O_ACCMODE) == O_RDONLY
			&& (_IOC_DIR(cmd) != _IOC_READ
		|| cmd == FE_GET_EVENT
		|| cmd == FE_DISEQC_RECV_SLAVE_REPLY)) {
		up(&fepriv->sem);
		return -EPERM;
	}

	int err = neumo_driver_dvb_frontend_handle_ioctl(file, cmd, parg);

	up(&fepriv->sem);
	return err;
}

static struct neumo_user_data* get_user_data(	struct neumo_dvb_frontend_private* fepriv,
																							struct dvb_device *dvbdev)
{
	struct neumo_user_data* user_data = xa_load(&fepriv->extra->users, (intptr_t) dvbdev);
	if(!user_data) {
		user_data =kzalloc(sizeof(struct neumo_dvb_extra), GFP_KERNEL);;
		user_data_init();
	}
	xa_store(&fepriv->extra->users, (intptr_t) dvbdev, user_data, GFP_KERNEL);
	return user_data;
}

static void neumo_dvb_free_user_data(struct neumo_dvb_extra* extra, struct dvb_device* dvbdev) {
	unsigned long index;
	void *entry;
	struct neumo_user_data* user_data = xa_load(&extra->users, (intptr_t) dvbdev);
	if(user_data) {
		kfree(user_data);
		xa_erase(&extra->users, (intptr_t) dvbdev);
	}
	xa_destroy(&extra->users);
}

//main entry point; semaphore not held yet
static int dvb_frontend_do_ioctl(struct file *file, unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	void* fe_ = dvbdev->priv; //do not know type yet
	struct neumo_dvb_frontend_private* fepriv = xa_load(&neumo_fes, (intptr_t) fe_);
	struct dvb_adapter* adapter;
	if(fepriv->dvb_api_fe) {
		adapter = dvb_driver_get_adapter(fepriv);
	} else {
		BUG_ON(!fepriv->neumo_api_fe);
		struct neumo_dvb_frontend* fe = fepriv->neumo_api_fe;
		adapter = fe->dvb;
	}
	struct neumo_user_data* user_data = get_user_data(fepriv, dvbdev);

	if(cmd == FE_SELECT_API) {
		struct dvb_select_api* api = parg;
		int requested= api->api_type;
		if(user_data->has_selected_api) {
			if(api->api_type == API_TYPE_DVB_API || api->api_type == API_TYPE_NEUMO) {
				user_data->uses_neumo_api = (api->api_type == API_TYPE_NEUMO);
			} else
				api->api_type = user_data->uses_neumo_api ? API_TYPE_NEUMO : API_TYPE_DVB_API;
			api->api_version = DTV_API_VERSION;
			adapter_dprintk(adapter, "switching API; requested=%d selected=%d vers=%d\n", requested,
											api->api_type, api->api_version);
		}
		return 0;
	}

	if(user_data->uses_neumo_api) {
		struct neumo_dvb_frontend* fe = fe_;
		return neumo_driver_dvb_frontend_do_ioctl(fe, file, cmd, parg);
	} else  {
		struct dvb_frontend* fe = fe_;
		return dvb_driver_dvb_frontend_do_ioctl(fe, file, cmd, parg);
	}
	return -1;
}

static long neumo_dvb_frontend_ioctl(struct file *file, unsigned int cmd,
																		 unsigned long arg)
{
	struct dvb_device *dvbdev = file->private_data;

	if (!dvbdev)
		return -ENODEV;
	return dvb_usercopy(file, cmd, arg, dvb_frontend_do_ioctl);
}

#ifdef CONFIG_COMPAT
struct compat_dtv_property {
	__u32 cmd;
	__u32 reserved[3];
	union {
		__u32 data;
		struct dtv_fe_stats st;
		struct {
			__u8 data[32];
			__u32 len;
			__u32 reserved1[3];
			compat_uptr_t reserved2;
		} buffer;
	} u;
	int result;
} __attribute__ ((packed));

struct compat_dtv_properties {
	__u32 num;
	compat_uptr_t props;
};

#define COMPAT_FE_SET_PROPERTY		 _IOW('o', 82, struct compat_dtv_properties)
#define COMPAT_FE_GET_PROPERTY		 _IOR('o', 83, struct compat_dtv_properties)

static int neumo_driver_dvb_frontend_handle_compat_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct neumo_dvb_frontend *fe = dvbdev->priv;
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
	int i, err = 0;
	if (cmd == COMPAT_FE_SET_PROPERTY) {
		struct compat_dtv_properties prop, *tvps = NULL;
		struct compat_dtv_property *tvp = NULL;

		if (copy_from_user(&prop, compat_ptr(arg), sizeof(prop)))
			return -EFAULT;

		tvps = &prop;

		/*
		 * Put an arbitrary limit on the number of messages that can
		 * be sent at once
		 */
		if (!tvps->num || (tvps->num > DTV_IOCTL_MAX_MSGS))
			return -EINVAL;

		tvp = memdup_user(compat_ptr(tvps->props), tvps->num * sizeof(*tvp));
		if (IS_ERR(tvp))
			return PTR_ERR(tvp);

		for (i = 0; i < tvps->num; i++) {
			err = neumo_driver_dtv_property_process_set_int(fe, file,
																				 (tvp + i)->cmd,
																				 (tvp + i)->u.data);
			if (err < 0) {
				kfree(tvp);
				return err;
			}
		}
		kfree(tvp);
	} else if (cmd == COMPAT_FE_GET_PROPERTY) {
		struct compat_dtv_properties prop, *tvps = NULL;
		struct compat_dtv_property *tvp = NULL;
		struct neumo_driver_dtv_frontend_properties getp = fe->dtv_property_cache;

		if (copy_from_user(&prop, compat_ptr(arg), sizeof(prop)))
			return -EFAULT;

		tvps = &prop;

		/*
		 * Put an arbitrary limit on the number of messages that can
		 * be sent at once
		 */
		if (!tvps->num || (tvps->num > DTV_IOCTL_MAX_MSGS))
			return -EINVAL;

		tvp = memdup_user(compat_ptr(tvps->props), tvps->num * sizeof(*tvp));
		if (IS_ERR(tvp))
			return PTR_ERR(tvp);

		/*
		 * Let's use our own copy of property cache, in order to
		 * avoid mangling with DTV zigzag logic, as drivers might
		 * return crap, if they don't check if the data is available
		 * before updating the properties cache.
		 */
		if (fepriv->state != FESTATE_IDLE && fepriv->state != FESTATE_DISEQC) {
			err = neumo_driver_dtv_get_frontend(fe, &getp, NULL);
			if (err < 0) {
				kfree(tvp);
				return err;
			}
		}
		for (i = 0; i < tvps->num; i++) {
			err = neumo_driver_dtv_property_process_get(
					fe, &getp, (struct dtv_property *)(tvp + i), file);
			if (err < 0) {
				kfree(tvp);
				return err;
			}
		}

		if (copy_to_user((void __user *)compat_ptr(tvps->props), tvp,
				 tvps->num * sizeof(struct compat_dtv_property))) {
			kfree(tvp);
			return -EFAULT;
		}
		kfree(tvp);
	}

	return err;
}

static long neumo_dvb_frontend_compat_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	struct dvb_device *dvbdev = file->private_data;
	void* fe_ = dvbdev->priv; //do not know type yet
	struct neumo_dvb_frontend_private* fepriv = xa_load(&neumo_fes, (intptr_t) fe_);

	int err;

	if (cmd == COMPAT_FE_SET_PROPERTY || cmd == COMPAT_FE_GET_PROPERTY) {
		if (down_interruptible(&fepriv->sem))
			return -ERESTARTSYS;

		err = neumo_driver_dvb_frontend_handle_compat_ioctl(file, cmd, arg);

		up(&fepriv->sem);
		return err;
	}

	return neumo_dvb_frontend_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static int neumo_driver_dtv_set_frontend(struct neumo_dvb_frontend *fe)
{
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct neumo_dvb_frontend_tune_settings fetunesettings;
	u32 rolloff = 0;

	if (dvb_frontend_check_parameters(fe) < 0) {
		fepriv->state = FESTATE_IDLE;
		return -EINVAL;
	}

	/*
	 * Initialize output parameters to match the values given by
	 * the user. FE_SET_FRONTEND triggers an initial frontend event
	 * with status = 0, which copies output parameters to userspace.
	 */
	neumo_driver_dtv_property_legacy_params_sync(fe, &fepriv->dvb_parameters_out);

	/*
	 * Be sure that the bandwidth will be filled for all
	 * non-satellite systems, as tuners need to know what
	 * low pass/Nyquist half filter should be applied, in
	 * order to avoid inter-channel noise.
	 *
	 * ISDB-T and DVB-T/T2 already sets bandwidth.
	 * ATSC and DVB-C don't set, so, the core should fill it.
	 *
	 * On DVB-C Annex A and C, the bandwidth is a function of
	 * the roll-off and symbol rate. Annex B defines different
	 * roll-off factors depending on the modulation. Fortunately,
	 * Annex B is only used with 6MHz, so there's no need to
	 * calculate it.
	 *
	 * While not officially supported, a side effect of handling it at
	 * the cache level is that a program could retrieve the bandwidth
	 * via DTV_BANDWIDTH_HZ, which may be useful for test programs.
	 */
	switch (c->delivery_system) {
	case SYS_ATSC:
	case SYS_DVBC_ANNEX_B:
		c->bandwidth_hz = 6000000;
		break;
	case SYS_DVBC_ANNEX_A:
		rolloff = 115;
		break;
	case SYS_DVBC_ANNEX_C:
		rolloff = 113;
		break;
	case SYS_DSS:
		rolloff = 120;
		break;
	case SYS_DVBS:
	case SYS_TURBO:
	case SYS_ISDBS:
		rolloff = 135;
		break;
	case SYS_DVBS2:
		switch (c->rolloff) {
		case ROLLOFF_20:
			rolloff = 120;
			break;
		case ROLLOFF_25:
			rolloff = 125;
			break;
		default:
		case ROLLOFF_35:
			rolloff = 135;
		}
		break;
	default:
		break;
	}
	if (rolloff)
		c->bandwidth_hz = mult_frac(c->symbol_rate, rolloff, 100);

	/* force auto frequency inversion if requested */
	if (dvb_force_auto_inversion)
		c->inversion = INVERSION_AUTO;

	/*
	 * without hierarchical coding code_rate_LP is irrelevant,
	 * so we tolerate the otherwise invalid FEC_NONE setting
	 */
	if (c->hierarchy == HIERARCHY_NONE && c->code_rate_LP == FEC_NONE)
		c->code_rate_LP = FEC_AUTO;

	/* get frontend-specific tuning settings */
	memset(&fetunesettings, 0, sizeof(struct neumo_dvb_frontend_tune_settings));
	if (fe->ops.get_tune_settings && (fe->ops.get_tune_settings(fe, &fetunesettings) == 0)) {
		fepriv->min_delay = (fetunesettings.min_delay_ms * HZ) / 1000;
		fepriv->max_drift = fetunesettings.max_drift;
		fepriv->step_size = fetunesettings.step_size;
	} else {
		/* default values */
		switch (c->delivery_system) {
		case SYS_DVBS:
		case SYS_DVBS2:
		case SYS_ISDBS:
		case SYS_TURBO:
		case SYS_DVBC_ANNEX_A:
		case SYS_DVBC_ANNEX_C:
			fepriv->min_delay = HZ / 20;
			fepriv->step_size = c->symbol_rate / 16000;
			fepriv->max_drift = c->symbol_rate / 2000;
			break;
		case SYS_DVBT:
		case SYS_DVBT2:
		case SYS_ISDBT:
		case SYS_DTMB:
			fepriv->min_delay = HZ / 20;
			fepriv->step_size = neumo_driver_neumo_dvb_frontend_get_stepsize(fe) * 2;
			fepriv->max_drift = (neumo_driver_neumo_dvb_frontend_get_stepsize(fe) * 2) + 1;
			break;
		default:
			/*
			 * FIXME: This sounds wrong! if frequency_stepsize is
			 * defined by the frontend, why not use it???
			 */
			fepriv->min_delay = HZ / 20;
			fepriv->step_size = 0; /* no zigzag */
			fepriv->max_drift = 0;
			break;
		}
	}
	if (dvb_override_tune_delay > 0)
		fepriv->min_delay = (dvb_override_tune_delay * HZ) / 1000;

	fepriv->state = FESTATE_RETUNE;

	/* Request the search algorithm to search */
	fepriv->algo_status |= DVBFE_ALGO_SEARCH_AGAIN;

	neumo_dvb_frontend_clear_events_fepriv(fepriv);
	neumo_dvb_frontend_add_event_fepriv(fepriv, 0);
	neumo_dvb_frontend_wakeup_fepriv(fepriv);
	fepriv->status = 0;

	return 0;
}

static int neumo_driver_dtv_set_sec_configured(struct neumo_dvb_frontend *fe)
{
	if (fe->ops.set_sec_ready) {
		fe_dprintk(fe, "calling set_sec_ready: num=%d\n", fe->dvb->num);
		return fe->ops.set_sec_ready(fe);
	} else {
		fe_dprintk(fe, "not calling set_sec_ready: num=%d\n", fe->dvb->num);
	}
	return 0; //deliberately do not output error when not supported
}

static int dtv_set_sat_scan(struct neumo_dvb_frontend *fe, bool scan_continue)
{
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
 	/* Request the search algorithm to search */
	fepriv->state = scan_continue ? FESTATE_SCAN_NEXT : FESTATE_SCAN_FIRST;
	neumo_dvb_frontend_clear_events_fepriv(fepriv);
	neumo_driver_dvb_frontend_add_event(fe, 0);
	neumo_dvb_frontend_wakeup_fepriv(fepriv);
	fepriv->status = 0;

	return 0;
}

static int dtv_get_pls_search_list(struct neumo_dvb_frontend *fe, struct dtv_pls_search_list* user)
{
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	int len = c->pls_search_codes_len;
	if(user->num_codes < len)
		len = user->num_codes;
	if(user->codes == NULL || len <=0)
		return -EFAULT;
	if (copy_to_user(user->codes, &c->pls_search_codes, len*sizeof(user->codes[0])))
		return -EFAULT;
	return 0;
}

static int dtv_set_pls_search_list(struct neumo_dvb_frontend *fe, struct dtv_pls_search_list* user)
{
	int i;
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	c->pls_search_codes_len = sizeof(c->pls_search_codes)/sizeof(c->pls_search_codes[0]);
	if(user->num_codes < c->pls_search_codes_len)
		c->pls_search_codes_len = user->num_codes;
	if(c->pls_search_codes_len==0 || user->codes == NULL) {
		fe_dprintk(fe, "ERROR: len=%d/%d codes=%p", c->pls_search_codes_len, user->num_codes, user->codes);
		return -EFAULT;
	}
	if (copy_from_user(&c->pls_search_codes, user->codes, c->pls_search_codes_len*sizeof(user->codes[0]))) {
		fe_dprintk(fe, "ERROR: len=%d/%d codes=%p", c->pls_search_codes_len, user->num_codes, user->codes);
		return -EFAULT;
	}
	fe_dprintk(fe, "PLS: %d codes:\n", c->pls_search_codes_len);
	for(i=0;i< c->pls_search_codes_len;++i)
		fe_dprintk(fe, "code=0x%x\n", c->pls_search_codes[i]);

	return 0;
}

static int dtv_set_spectrum(struct neumo_dvb_frontend *fe, enum dtv_fe_spectrum_method method)
{
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct neumo_dvb_extra* extra = fepriv->extra;
	int ret;
	if (!fe->ops.spectrum_get || !fe->ops.spectrum_start)
		return  -ENOTSUPP;
	ret = init_dtv_fe_spectrum_scan(&extra->spectrum, c, method);
	if(ret<0)
		return ret;
 	/* Request the search algorithm to search */
	fepriv->state = FESTATE_GETTING_SPECTRUM;
	neumo_dvb_frontend_clear_events_fepriv(fepriv);
	neumo_driver_dvb_frontend_add_event(fe, 0);
	neumo_dvb_frontend_wakeup_fepriv(fepriv);
	fepriv->status = 0;

	return 0;
}

static int dtv_get_spectrum(struct neumo_dvb_frontend *fe, struct dtv_fe_spectrum* user)
{
	int err = 0;
	fe_dprintk(fe, "spectrum retrieved user: n=%d\n", user->num_freq);

	if(fe->ops.spectrum_get)
		fe->ops.spectrum_get(fe, user);

	return err;
}

static int dtv_set_constellation(struct neumo_dvb_frontend *fe, struct dtv_fe_constellation* constellation)
{
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	if (!fe->ops.constellation_get)
		return  -ENOTSUPP;
	c->constellation.num_samples = constellation->num_samples;
	c->constellation.method = constellation->method;
	c->constellation.constel_select = constellation->constel_select;
	c->constellation.samples = NULL;
	fe_dprintk(fe, "SET constellation: num_samples=%d constel_select=%d\n", constellation->num_samples, constellation->constel_select);
	return 0;
}

static int dtv_get_constellation(struct neumo_dvb_frontend *fe, struct dtv_fe_constellation* user)
{
	int err = 0;

	if(fe->ops.constellation_get) {
		fe->ops.constellation_get(fe, user);
		//fe_dprintk(fe, "constellation retrieved user->num_samples=%d\n", user->num_samples);
	}
	else if(user) {
		user->num_samples = 0;
		//fe_dprintk(fe, "constellation retrieved user->num_samples=%d\n", user->num_samples);
	}
	return err;
}

static int dtv_get_matype_list(struct neumo_dvb_frontend *fe, struct dtv_matype_list* user)
{
	//fe_dprintk(fe, "constellation retrieved user->num_samples=%d\n", user->num_samples);
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;

	if(!user)
		return -1;

	if(user->num_entries > c->num_matypes)
		user->num_entries = c->num_matypes;

	if(copy_to_user((void __user *)user->matypes, c->matypes,
									user->num_entries * sizeof(user->matypes[0])))
		return -EFAULT;

	return 0;
}

static int neumo_driver_dvb_get_property(struct neumo_dvb_frontend *fe, struct file *file,
					struct dtv_properties *tvps)
{
	struct neumo_dvb_frontend_private* fepriv = fe->frontend_priv;
	struct dtv_property *tvp = NULL;
	struct neumo_driver_dtv_frontend_properties getp;
	int i, err;

	memcpy(&getp, &fe->dtv_property_cache, sizeof(getp));

	dev_dbg(fe->dvb->device, "%s: properties.num = %d\n",
		__func__, tvps->num);
	dev_dbg(fe->dvb->device, "%s: properties.props = %p\n",
		__func__, tvps->props);
#if 0
	fe_dprintk(fe, "num_props=%d\n", tvps->num);
#endif
	/*
	 * Put an arbitrary limit on the number of messages that can
	 * be sent at once
	 */
	if (!tvps->num || tvps->num > DTV_IOCTL_MAX_MSGS)
		return -EINVAL;
#if 0
	fe_dprintk(fe, "num_props=%d\n", tvps->num);
#endif
	tvp = memdup_user((void __user *)tvps->props, tvps->num * sizeof(*tvp));
	if (IS_ERR(tvp))
		return PTR_ERR(tvp);
#if 0
	fe_dprintk(fe, "num_props=%d\n", tvps->num);
#endif
	/*
	 * Let's use our own copy of property cache, in order to
	 * avoid mangling with DTV zigzag logic, as drivers might
	 * return crap, if they don't check if the data is available
	 * before updating the properties cache.
	 */
	if (fepriv->state != FESTATE_IDLE && fepriv->state != FESTATE_DISEQC) {
		if(tvps->num > 1 || (tvp[0].cmd != DTV_SPECTRUM || tvp[0].cmd != DTV_CONSTELLATION)) {
			err = neumo_driver_dtv_get_frontend(fe, &getp, NULL);
			if(err<0) {
				fe_dprintk(fe, "FAILED: prop=%d err=%d\n", tvp[i].cmd, err);
			}
			if (err < 0)
				goto out;
		}
	}
	for (i = 0; i < tvps->num; i++) {
		err = neumo_driver_dtv_property_process_get(fe, &getp,
								 tvp + i, file);
		if(err<0) {
			fe_dprintk(fe, "FAILED: prop=%d err=%d\n", tvp[i].cmd, err);
		}
		if (err < 0)
			goto out;
	}

	if (copy_to_user((void __user *)tvps->props, tvp,
			 tvps->num * sizeof(struct dtv_property))) {
		err = -EFAULT;
		goto out;
	}

	err = 0;
out:
	kfree(tvp);
	return err;
}

static int neumo_driver_dvb_get_frontend(struct neumo_dvb_frontend *fe,
																				 struct dvb_frontend_parameters *p_out)
{
	struct neumo_driver_dtv_frontend_properties getp;

	/*
	 * Let's use our own copy of property cache, in order to
	 * avoid mangling with DTV zigzag logic, as drivers might
	 * return crap, if they don't check if the data is available
	 * before updating the properties cache.
	 */
	memcpy(&getp, &fe->dtv_property_cache, sizeof(getp));

	return neumo_driver_dtv_get_frontend(fe, &getp, p_out);
}

static int neumo_driver_dvb_get_dvb_api_property(struct neumo_dvb_frontend *fe, struct file *file,
					struct dvb_api_dtv_properties* dvb_api_tvps)
{
	struct neumo_dvb_frontend_private* fepriv = fe->frontend_priv;
	struct dvb_api_dtv_property* dvb_api_tvp = NULL;
	struct neumo_driver_dtv_frontend_properties getp;
	int i, err;

	memcpy(&getp, &fe->dtv_property_cache, sizeof(getp));

	dev_dbg(fe->dvb->device, "%s: properties.num = %d\n",
		__func__, dvb_api_tvps->num);
	dev_dbg(fe->dvb->device, "%s: properties.props = %p\n",
		__func__, dvb_api_tvps->props);
#if 0
	fe_dprintk(fe, "num_props=%d\n", tvps->num);
#endif
	/*
	 * Put an arbitrary limit on the number of messages that can
	 * be sent at once
	 */
	if (!dvb_api_tvps->num || dvb_api_tvps->num > DTV_IOCTL_MAX_MSGS)
		return -EINVAL;
#if 0
	fe_dprintk(fe, "num_props=%d\n", dvb_api_tvps->num);
#endif
	dvb_api_tvp = memdup_user((void __user *)dvb_api_tvps->props, dvb_api_tvps->num * sizeof(*dvb_api_tvp));
	if (IS_ERR(dvb_api_tvp))
		return PTR_ERR(dvb_api_tvp);
#if 0
	fe_dprintk(fe, "num_props=%d\n", dvb_api_tvps->num);
#endif
	/*
	 * Let's use our own copy of property cache, in order to
	 * avoid mangling with DTV zigzag logic, as drivers might
	 * return crap, if they don't check if the data is available
	 * before updating the properties cache.
	 */
	if (fepriv->state != FESTATE_IDLE && fepriv->state != FESTATE_DISEQC) {
		if(dvb_api_tvps->num > 1) {
			err = neumo_driver_dtv_get_frontend(fe, &getp, NULL);
			if(err<0) {
				fe_dprintk(fe, "FAILED: prop=%d err=%d\n", dvb_api_tvp[i].cmd, err);
			}
			if (err < 0)
				goto out;
		}
	}
	for (i = 0; i < dvb_api_tvps->num; i++) {
		struct dtv_property temp;
		struct dvb_api_dtv_property* x = &dvb_api_tvp[i];
		err = neumo_driver_dtv_property_process_get(fe, &getp,
								 &temp, file);
		x->cmd = temp.cmd;
		memcpy(&x->u, &temp.u, sizeof(x->u));
		x->result = temp.result;
		if(err<0) {
			fe_dprintk(fe, "FAILED: prop=%d err=%d\n", dvb_api_tvp[i].cmd, err);
		}
		if (err < 0)
			goto out;
	}

	if (copy_to_user((void __user *)dvb_api_tvps->props, dvb_api_tvp,
			 dvb_api_tvps->num * sizeof(struct dvb_api_dtv_property))) {
		err = -EFAULT;
		goto out;
	}

	err = 0;
out:
	kfree(dvb_api_tvp);
	return err;
}

static int neumo_driver_dvb_frontend_handle_ioctl(struct file *file, unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct neumo_dvb_frontend *fe = dvbdev->priv;
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dvb_adapter* adapter = fe->dvb;
	struct neumo_driver_dtv_frontend_properties *c = &fe->dtv_property_cache;
	int i, err = -ENOTSUPP;

	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	switch (cmd) {
    case FE_READ_TEMP:
		if (fe->ops.read_temp) {
				err = fe->ops.read_temp(fe, parg);
		}
		break;

	case FE_ECP3FW_READ:
		//printk("FE_ECP3FW_READ *****************");
		if (fe->ops.spi_read) {
			struct ecp3_info *info = parg;
			fe->ops.spi_read(fe, info);
		}
		err = 0;
		break;
	case FE_ECP3FW_WRITE:
		//printk("FE_ECP3FW_WRITE *****************");
		if (fe->ops.spi_write) {
			struct ecp3_info *info = parg;
			fe->ops.spi_write(fe, info);

		}
		err = 0;
		break;

	case FE_24CXX_READ:
		//printk("FE_24CXX_READ *****************");
		if (fe->ops.mcu_read) {
			struct mcu24cxx_info *info = parg;
			fe->ops.mcu_read(fe, info);
		}
		err = 0;
		break;
	case FE_24CXX_WRITE:
		//printk("FE_24CXX_WRITE *****************");
		if (fe->ops.mcu_write) {
			struct mcu24cxx_info *info = parg;
			fe->ops.mcu_write(fe, info);

		}
		err = 0;
		break;
	case FE_REGI2C_READ:
		if (fe->ops.reg_i2cread) {
			struct usbi2c_access *info = parg;
			fe->ops.reg_i2cread(fe, info);
		}
		err = 0;
		break;
	case FE_REGI2C_WRITE:
		if (fe->ops.reg_i2cwrite) {
			struct usbi2c_access *info = parg;
			fe->ops.reg_i2cwrite(fe, info);
		}
		err = 0;
		break;
	case FE_EEPROM_READ:
		if (fe->ops.eeprom_read) {
			struct eeprom_info *info = parg;
			fe->ops.eeprom_read(fe, info);
		}
		err = 0;
		break;
	case FE_EEPROM_WRITE:
		if (fe->ops.eeprom_write) {
			struct eeprom_info *info = parg;
			fe->ops.eeprom_write(fe, info);
		}
		err = 0;
		break;

	case FE_SET_PROPERTY: {
		struct dtv_properties *tvps = parg;
		struct dtv_property *tvp = NULL;

		dev_dbg(fe->dvb->device, "%s: properties.num = %d\n",
			__func__, tvps->num);
		dev_dbg(fe->dvb->device, "%s: properties.props = %p\n",
			__func__, tvps->props);

		/*
		 * Put an arbitrary limit on the number of messages that can
		 * be sent at once
		 */
		if (!tvps->num || (tvps->num > DTV_IOCTL_MAX_MSGS))
			return -EINVAL;

		tvp = memdup_user((void __user *)tvps->props, tvps->num * sizeof(*tvp));
		if (IS_ERR(tvp))
			return PTR_ERR(tvp);

		for (i = 0; i < tvps->num; i++) {
			err = neumo_driver_dtv_property_process_set(fe, file,
																		 (tvp + i)->cmd,
																		 (tvp + i));
			if (err < 0) {
				fe_dprintk(fe, "FE_SET_PROPERTY %d failed\n",  (tvp + i)->cmd);
				kfree(tvp);
				return err;
			}
		}
		kfree(tvp);
		err = 0;
		break;
	}

	case DVB_API_FE_GET_PROPERTY:
		err = neumo_driver_dvb_get_dvb_api_property(fe, file, parg);
		break;

	case DVB_API_FE_SET_PROPERTY: {
		struct dtv_properties *tvps = parg;
		struct dtv_property *tvp = NULL;

		dev_dbg(fe->dvb->device, "%s: properties.num = %d\n",
			__func__, tvps->num);
		dev_dbg(fe->dvb->device, "%s: properties.props = %p\n",
			__func__, tvps->props);

		/*
		 * Put an arbitrary limit on the number of messages that can
		 * be sent at once
		 */
		if (!tvps->num || (tvps->num > DTV_IOCTL_MAX_MSGS))
			return -EINVAL;

		tvp = memdup_user((void __user *)tvps->props, tvps->num * sizeof(*tvp));
		if (IS_ERR(tvp))
			return PTR_ERR(tvp);

		for (i = 0; i < tvps->num; i++) {
			err = neumo_driver_dtv_property_process_set(fe, file,
																		 (tvp + i)->cmd,
																		 (tvp + i));
			if (err < 0) {
				fe_dprintk(fe, "FE_SET_PROPERTY %d failed\n",  (tvp + i)->cmd);
				kfree(tvp);
				return err;
			}
		}
		kfree(tvp);
		err = 0;
		break;
	}
	case FE_GET_PROPERTY:
		err = neumo_driver_dvb_get_property(fe, file, parg);
		break;


	case FE_GET_EXTENDED_INFO: {
		struct dvb_frontend_extended_info *info = parg;
		memset(info, 0, sizeof(*info));
		fe_dprintk(fe, "FE_GET_EXTENDED_INFO: default_rf_input=%d %d => %d\n", info->default_rf_input,
						fe->ops.info.default_rf_input,
						(fe->ops.info.supports_neumo && fe->ops.info.default_rf_input >=0) ?
						fe->ops.info.default_rf_input : fe->dvb->num);
		info->supports_neumo = fe->ops.info.supports_neumo;
		info->supports_bbframes = fe->ops.info.supports_bbframes;
		info->default_rf_input = (fe->ops.info.supports_neumo && fe->ops.info.default_rf_input >=0) ?
			fe->ops.info.default_rf_input : 0;
		if (fe->ops.info.num_rf_inputs > 0 ) {
			int i;
			int n= fe->ops.info.num_rf_inputs;
			if (n > sizeof(info->rf_inputs)/sizeof(info->rf_inputs[0]))
				n =  sizeof(info->rf_inputs)/sizeof(info->rf_inputs[0]);
			for(i=0; i < n; ++i)
					info->rf_inputs[i] = fe->ops.info.rf_inputs[i];
			info->num_rf_inputs = n;
		} else {
			info->rf_inputs[0] = 	info->default_rf_input;
			info->num_rf_inputs = 1;
		}
		//fe->dvb->proposed_mac u8[6]
		//fe->dvb->device
		fe_dprintk(fe, "dev->id=%d dev->parent->id=%d\n", fe->dvb->device->id, fe->dvb->device->parent ? fe->dvb->device->parent->id :-1);
		//id = device_instance
		//bus = Type of bus device is on.
		if(fe->ops.info.adapter_mac_address) {
			info->adapter_mac_address = fe->ops.info.adapter_mac_address;
			fe_dprintk(fe, "set MAC from info.adapter_mac_address: %16llxx num=%d\n", info->adapter_mac_address, fe->dvb->num);
		} else {
			uint64_t proposed_mac;
			memcpy(&proposed_mac, fe->dvb->proposed_mac, sizeof(proposed_mac));
			fe_dprintk(fe, "set MAC from proposed mac: %016llx num=%d\n", proposed_mac, fe->dvb->num);
			info->adapter_mac_address =  proposed_mac ? proposed_mac : (0x2L | ((((uint64_t)fe->dvb->num) << 8) <<32));
		}
		fe_dprintk(fe, "MAC: 0x%llx", info->adapter_mac_address);
		info->card_mac_address = fe->ops.info.card_mac_address ? fe->ops.info.card_mac_address:
			fe->dvb->num; //best we can do; each adapter will appear as different card
		strscpy(info->card_address, fe->ops.info.card_address, sizeof(info->card_address));
		strscpy(info->card_name, fe->ops.info.name, sizeof(info->card_name));
		strscpy(info->card_short_name, fe->ops.info.card_short_name, sizeof(info->card_short_name));
		if(fe->ops.info.adapter_name[0]!=0) {
			strscpy(info->adapter_name, fe->ops.info.adapter_name, sizeof(info->adapter_name));
		} else {
			snprintf(info->adapter_name, sizeof(info->adapter_name), "A%d %s",
							 fe->dvb->num,  info->card_short_name);
		}
		info->symbol_rate_min = fe->ops.info.symbol_rate_min;
		info->symbol_rate_max = fe->ops.info.symbol_rate_max;
		info->symbol_rate_tolerance = fe->ops.info.symbol_rate_tolerance;
		info->caps = fe->ops.info.caps;
		info->extended_caps = fe->ops.info.extended_caps;
		info->frequency_stepsize = neumo_driver_neumo_dvb_frontend_get_stepsize(fe);
		neumo_driver_dvb_frontend_get_frequency_limits(fe, &info->frequency_min,
							&info->frequency_max,
							&info->frequency_tolerance);

		/* Set CAN_INVERSION_AUTO bit on in other than oneshot mode */
		if (!(fepriv->tune_mode_flags & FE_TUNE_MODE_ONESHOT))
			info->caps |= FE_CAN_INVERSION_AUTO;
		err = 0;
		break;
	}

	case FE_READ_STATUS: {
		enum fe_status *status = parg;

		/* if retune was requested but hasn't occurred yet, prevent
		 * that user get signal state from previous tuning */
		if (fepriv->state == FESTATE_RETUNE ||
				fepriv->state == FESTATE_ERROR) {
			fe_dprintk(fe, "FE_READ_STATUS retune=%d error=%d\n", fepriv->state == FESTATE_RETUNE ,
							fepriv->state == FESTATE_ERROR);
			err = 0;
			*status = 0;
			break;
		}

		if (fe->ops.read_status) {
			err = fe->ops.read_status(fe, status);
		}
		break;
	}

	case FE_SET_RF_INPUT: {
		struct fe_rf_input_control* rf_input = (struct fe_rf_input_control* )parg;
		fe_dprintk(fe, "FE_SET_RF_INPUT owner=%d config_id=%d rf_in=%d adapter=%d\n",  rf_input->owner,  rf_input->config_id, rf_input->rf_in,
						fe->dvb->num);
		if (fe->ops.set_rf_input)
			err = fe->ops.set_rf_input(fe, rf_input);
		else
			err = FE_RESERVATION_NOT_SUPPORTED;
	}
		break;

	case FE_DISEQC_RESET_OVERLOAD:
		if (fe->ops.diseqc_reset_overload) {
			err = fe->ops.diseqc_reset_overload(fe);
			if(fepriv->state == FESTATE_IDLE)
				fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_DISEQC_SEND_MASTER_CMD: {
		struct dvb_diseqc_master_cmd *cmd = parg;
		if (cmd->msg_len > sizeof(cmd->msg)) {
			err = -EINVAL;
			break;
		}
		if (fe->ops.diseqc_send_long_master_cmd) {
			struct dvb_diseqc_long_master_cmd long_cmd;
			long_cmd.msg_len = cmd->msg_len;
			memcpy(&long_cmd.msg[0], &cmd->msg[0], sizeof(cmd->msg[0])*cmd->msg_len);
			err = fe->ops.diseqc_send_long_master_cmd(fe, &long_cmd);
			fe_dprintk(fe, "sending master_cmd done err=%d\n", err);
			if(fepriv->state == FESTATE_IDLE)
				fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		} else if (fe->ops.diseqc_send_master_cmd) {
			err = fe->ops.diseqc_send_master_cmd(fe, cmd);
			fe_dprintk(fe, "sending long master_cmd done err=%d\n", err);
			if(fepriv->state == FESTATE_IDLE)
				fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
	}
		break;

	case FE_DISEQC_SEND_LONG_MASTER_CMD:
		if (fe->ops.diseqc_send_long_master_cmd) {
			struct dvb_diseqc_long_master_cmd *cmd = parg;

			if (cmd->msg_len > sizeof(cmd->msg)) {
				err = -EINVAL;
				break;
			}
			err = fe->ops.diseqc_send_long_master_cmd(fe, cmd);
			fe_dprintk(fe, "sending master_cmd done err=%d\n", err);
			if(fepriv->state == FESTATE_IDLE)
				fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_DISEQC_SEND_BURST:
		if (fe->ops.diseqc_send_burst) {
			err = fe->ops.diseqc_send_burst(fe,
						(enum fe_sec_mini_cmd)parg);
			if(fepriv->state == FESTATE_IDLE)
				fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_SET_TONE:
		if (fe->ops.set_tone) {
			fe_dprintk(fe, "FE_SET_TONE %d\n", (enum fe_sec_tone_mode)parg);
			err = fe->ops.set_tone(fe,
								 (enum fe_sec_tone_mode)parg);
			fepriv->tone = (enum fe_sec_tone_mode)parg;
			if(fepriv->state == FESTATE_IDLE)
				fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
			c->sectone = fepriv->tone;
		}
		break;

	case FE_SET_VOLTAGE:
		if (fe->ops.set_voltage) {
			fe_dprintk(fe, "FE_SET_VOLTAGE %d\n", (enum fe_sec_voltage)parg);
			err = fe->ops.set_voltage(fe,
							(enum fe_sec_voltage)parg);
			fepriv->voltage = (enum fe_sec_voltage)parg;
			if(fepriv->state == FESTATE_IDLE)
				fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_DISEQC_RECV_SLAVE_REPLY:
		if (fe->ops.diseqc_recv_slave_reply)
			err = fe->ops.diseqc_recv_slave_reply(fe, parg);
		break;

	case FE_ENABLE_HIGH_LNB_VOLTAGE:
		if (fe->ops.enable_high_lnb_voltage)
			err = fe->ops.enable_high_lnb_voltage(fe, (long)parg);
		break;

	case FE_SET_FRONTEND_TUNE_MODE:
		fepriv->tune_mode_flags = (unsigned long)parg;
		err = 0;
		break;

		/* DEPRECATED dish control ioctls */

	case FE_DISHNETWORK_SEND_LEGACY_CMD:
		if (fe->ops.dishnetwork_send_legacy_command) {
			err = fe->ops.dishnetwork_send_legacy_command(fe,
							 (unsigned long)parg);
			if(fepriv->state == FESTATE_IDLE)
				fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		} else if (fe->ops.set_voltage) {
			/*
			 * NOTE: This is a fallback condition.  Some frontends
			 * (stv0299 for instance) take longer than 8msec to
			 * respond to a set_voltage command.  Those switches
			 * need custom routines to switch properly.  For all
			 * other frontends, the following should work ok.
			 * Dish network legacy switches (as used by Dish500)
			 * are controlled by sending 9-bit command words
			 * spaced 8msec apart.
			 * the actual command word is switch/port dependent
			 * so it is up to the userspace application to send
			 * the right command.
			 * The command must always start with a '0' after
			 * initialization, so parg is 8 bits and does not
			 * include the initialization or start bit
			 */
			unsigned long swcmd = ((unsigned long)parg) << 1;
			ktime_t nexttime;
			ktime_t tv[10];
			int i;
			u8 last = 1;

			if (dvb_frontend_debug)
				fe_dprintk(fe, "switch command: 0x%04lx\n",
					swcmd);
			nexttime = ktime_get_boottime();
			if (dvb_frontend_debug)
				tv[0] = nexttime;
			/* before sending a command, initialize by sending
			 * a 32ms 18V to the switch
			 */
			fe->ops.set_voltage(fe, SEC_VOLTAGE_18);
			dvb_frontend_sleep_until(&nexttime, 32000);

			for (i = 0; i < 9; i++) {
				if (dvb_frontend_debug)
					tv[i + 1] = ktime_get_boottime();
				if ((swcmd & 0x01) != last) {
					/* set voltage to (last ? 13V : 18V) */
					fe->ops.set_voltage(fe, (last) ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18);
					last = (last) ? 0 : 1;
				}
				swcmd = swcmd >> 1;
				if (i != 8)
					dvb_frontend_sleep_until(&nexttime, 8000);
			}
			if (dvb_frontend_debug) {
				fe_dprintk(fe, "(adapter %d): switch delay (should be 32k followed by all 8k)\n",
					fe->dvb->num);
				for (i = 1; i < 10; i++)
					pr_info("%d: %d\n", i,
						(int)ktime_us_delta(tv[i], tv[i - 1]));
			}
			err = 0;
			if(fepriv->state == FESTATE_IDLE)
				fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	/* DEPRECATED statistics ioctls */

	case FE_READ_BER:
		if (fe->ops.read_ber) {
				err = fe->ops.read_ber(fe, parg);
		}
		break;

	case FE_READ_SIGNAL_STRENGTH:
		if (fe->ops.read_signal_strength) {
				err = fe->ops.read_signal_strength(fe, parg);
		}
		break;

	case FE_READ_SNR:
		if (fe->ops.read_snr) {
				err = fe->ops.read_snr(fe, parg);
		}
		break;

	case FE_READ_UNCORRECTED_BLOCKS:
		if (fe->ops.read_ucblocks) {
				err = fe->ops.read_ucblocks(fe, parg);
		}
		break;

	/* DEPRECATED DVBv3 ioctls */

	case FE_SET_FRONTEND:
		err = dvbv3_set_delivery_system(fe);
		if (err)
			break;

		err = dtv_property_cache_sync(fe, c, parg);
		if (err)
			break;
		err = neumo_driver_dtv_set_frontend(fe);
		break;

	case FE_GET_EVENT: //already handled in neumo_driver_dvb_frontend_handle_ioctl above
		err = dvb_frontend_get_event_fepriv(fepriv, adapter, parg, file->f_flags);
		break;

	case FE_GET_FRONTEND:
		err = neumo_driver_dvb_get_frontend(fe, parg);
		break;

	default:
		fe_dprintk(fe, "unsupported ioctl %d\n", cmd);
		return -ENOTSUPP;
	} /* switch */

	return err;
}

__poll_t neumo_dvb_frontend_poll_fepriv(struct neumo_dvb_frontend_private* fepriv, struct file *file, struct poll_table_struct *wait)
{
	struct neumo_dvb_extra* extra = fepriv->extra;
	struct neumo_dvb_fe_events* events = &extra->events;

	poll_wait(file, &events->wait_queue, wait);

	if (events->eventw != events->eventr)
		return (EPOLLIN | EPOLLRDNORM | EPOLLPRI);

	return 0;
}

static __poll_t neumo_dvb_frontend_poll(struct file *file, struct poll_table_struct *wait)
{
	struct dvb_device* dvbdev = file->private_data;
	void* fe_ = dvbdev->priv; //do not know type yet
	struct neumo_dvb_frontend_private* fepriv = xa_load(&neumo_fes, (intptr_t) fe_);
	return neumo_dvb_frontend_poll_fepriv(fepriv, file, wait);
}

static int neumo_dvb_frontend_open_mfe(	struct neumo_dvb_frontend_private* fepriv,
																				struct dvb_adapter* adapter, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;

	if (fepriv->exit == DVB_FE_DEVICE_REMOVED)
		return -ENODEV;

	if (adapter->mfe_shared == 2) {
		mutex_lock(&adapter->mfe_lock);
		if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
			if (adapter->mfe_dvbdev &&
			    !adapter->mfe_dvbdev->writers) {
				mutex_unlock(&adapter->mfe_lock);
				return -EBUSY;
			}
			adapter->mfe_dvbdev = dvbdev;
		}
	} else if (adapter->mfe_shared) {
		mutex_lock(&adapter->mfe_lock);

		if (!adapter->mfe_dvbdev)
			adapter->mfe_dvbdev = dvbdev;

		else if (adapter->mfe_dvbdev != dvbdev) {
			struct dvb_device* mfedev = adapter->mfe_dvbdev;
			struct neumo_dvb_frontend* mfe = mfedev->priv;
			struct neumo_dvb_frontend_private* mfepriv = mfe->frontend_priv;
			int mferetry = (dvb_mfe_wait_time << 1);

			mutex_unlock(&adapter->mfe_lock);
			while (mferetry-- && (mfedev->users != -1 ||
								mfepriv->thread)) {
				if (msleep_interruptible(500)) {
					if (signal_pending(current))
						return -EINTR;
				}
			}

			mutex_lock(&adapter->mfe_lock);
			if (adapter->mfe_dvbdev != dvbdev) {
				mfedev = adapter->mfe_dvbdev;
				mfe = mfedev->priv;
				mfepriv = mfe->frontend_priv;
				if (mfedev->users != -1 ||
						mfepriv->thread) {
					mutex_unlock(&adapter->mfe_lock);
					return -EBUSY;
				}
				adapter->mfe_dvbdev = dvbdev;
			}
		}
	}
	return 0;
}

/*returns negative when bus control was executed with an error
	returns when bus_ctrl was exexcuted without an error
	returns 1 if there is no bus_ctrl
*/
inline static int ts_bus_ctrl(struct neumo_dvb_frontend_private* fepriv, struct dvb_device *dvbdev, int acquire) {
	if(fepriv->dvb_api_fe) {
		if(dvbdev->users == -1 && dvb_driver_has_ts_bus_ctrl(fepriv))
			return dvb_driver_ts_bus_ctrl(fepriv, acquire);
		else return 1;
	}
	struct neumo_dvb_frontend* fe = fepriv->neumo_api_fe;
	BUG_ON(!fe);
	if (dvbdev->users == -1  && fe->ops.ts_bus_ctrl) {
		return fe->ops.ts_bus_ctrl(fe, acquire);
	}
	return 1;
}


static int neumo_dvb_frontend_open_fepriv(struct neumo_dvb_frontend_private *fepriv,
																					struct dvb_adapter *adapter,
																					struct inode *inode, struct file *file, int fe_id,
																					struct kref* fe_refcount)
{
	struct dvb_device *dvbdev = file->private_data;
	struct neumo_dvb_fe_events* events = &((struct neumo_dvb_extra*)fepriv->extra)->events;
	int ret = neumo_dvb_frontend_open_mfe(fepriv, adapter, file);
	if(ret < 0)
		return ret;

	int ret1= ts_bus_ctrl(fepriv, dvbdev, 1);
	if (ret1<=0) {
		if ((ret = ret1) < 0) {
			if (adapter->mfe_shared)
				mutex_unlock(&adapter->mfe_lock);
			return ret;
		}

		/* If we took control of the bus, we need to force
			 reinitialization.  This is because many ts_bus_ctrl()
			 functions strobe the RESET pin on the demod, and if the
			 frontend thread already exists then the dvb_init() routine
			 won't get called (which is what usually does initial
			 register configuration). */
		fepriv->reinitialise = 1;
	}

	if ((ret = dvb_generic_open(inode, file)) < 0)
		goto err1;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		/* normal tune mode when opened R/W */
		fepriv->tune_mode_flags &= ~ FE_TUNE_MODE_ONESHOT;
		fepriv->tone = -1;
		fepriv->voltage = -1;
		fepriv->heartbeat_interval = 0;

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
		mutex_lock(&adapter->mdev_lock);
		if (adapter->mdev) {
			mutex_lock(&adapter->mdev->graph_mutex);
			if (adapter->mdev->enable_source)
				ret = adapter->mdev->enable_source(
								 dvbdev->entity,
								 &fepriv->pipe);
			mutex_unlock(&adapter->mdev->graph_mutex);
			if (ret) {
				mutex_unlock(&adapter->mdev_lock);
				dev_err(adapter->device,
					"Tuner is busy. Error %d\n", ret);
				goto err2;
			}
		}
		mutex_unlock(&adapter->mdev_lock);
#endif
		ret = neumo_dvb_frontend_start_fepriv(fepriv, adapter, fe_id);
		if (ret)
			goto err3;

		/*  empty event queue */
		events->eventr = events->eventw = 0;
	}


	kref_get(fe_refcount);

	if (adapter->mfe_shared)
		mutex_unlock(&adapter->mfe_lock);
	return ret;

err3:
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	mutex_lock(&adapter->mdev_lock);
	if (adapter->mdev) {
		mutex_lock(&adapter->mdev->graph_mutex);
		if (adapter->mdev->disable_source)
			adapter->mdev->disable_source(dvbdev->entity);
		mutex_unlock(&adapter->mdev->graph_mutex);
	}
	mutex_unlock(&adapter->mdev_lock);
err2:
#endif
	dvb_generic_release(inode, file);
err1:
	ts_bus_ctrl(fepriv, dvbdev, 0);
	if (adapter->mfe_shared)
		mutex_unlock(&adapter->mfe_lock);
	return ret;
}

static int neumo_dvb_frontend_open(struct inode *inode, struct file *file)
{
	struct dvb_device* dvbdev = file->private_data;
	void* fe_ = dvbdev->priv; //do not know type yet
	struct neumo_dvb_frontend_private* fepriv = xa_load(&neumo_fes, (intptr_t) fe_);

	if(fepriv->dvb_api_fe) {
		struct kref* fe_refcount = dvb_driver_get_refcount(fepriv);
		int fe_id = dvb_driver_get_fe_id(fepriv);
		struct dvb_adapter* adapter = dvb_driver_get_adapter(fepriv);
		return neumo_dvb_frontend_open_fepriv(fepriv, adapter, inode, file, fe_id, fe_refcount);
	} else {
		BUG_ON(!fepriv->neumo_api_fe);
		struct neumo_dvb_frontend* fe = fepriv->neumo_api_fe;
		return neumo_dvb_frontend_open_fepriv(fepriv, fe->dvb, inode, file, fe->id, &fe->refcount);
	}

}


static int neumo_dvb_frontend_release_fepriv(struct neumo_dvb_frontend_private* fepriv,
																			struct dvb_adapter* adapter, struct inode* inode, struct file* file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct neumo_dvb_extra* extra = fepriv->extra;
	int ret;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		fepriv->release_jiffies = jiffies;
		mb();
	}
	adapter_dprintk(adapter, "Calling dvb_generic_release\n");
	ret = dvb_generic_release(inode, file);
	adapter_dprintk(adapter, "called dvb_generic_release dvbdev->users=%d\n", dvbdev->users);

	if (dvbdev->users == -1) {

		adapter_dprintk(adapter, "Calling stop_task\n");
		atomic_set(&extra->algo_state.task_should_stop, true);
		if (down_interruptible(&fepriv->sem))
				return -ERESTARTSYS;
		stop_task(fepriv);
		fepriv->state = FESTATE_IDLE;
		atomic_set(&extra->algo_state.task_should_stop, false);
		neumo_dvb_frontend_clear_events_fepriv(fepriv);
		neumo_dvb_frontend_add_event_fepriv(fepriv, FE_IDLE);
		up(&fepriv->sem);
		adapter_dprintk(adapter, "Waking up wait queue\n");


		wake_up(&fepriv->wait_queue);
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
		mutex_lock(&adapter->mdev_lock);
		if (adapter->mdev) {
			mutex_lock(&adapter->mdev->graph_mutex);
			if (adapter->mdev->disable_source)
				adapter->mdev->disable_source(dvbdev->entity);
			mutex_unlock(&adapter->mdev->graph_mutex);
		}
		mutex_unlock(&adapter->mdev_lock);
#endif
		adapter_dprintk(adapter, "fepriv->exit=%d DVB_FE_NO_EXIT=%d\n", fepriv->exit, DVB_FE_NO_EXIT);
		if (fepriv->exit != DVB_FE_NO_EXIT) {
			adapter_dprintk(adapter, "waking up dvbdev->wait_queue\n");
			wake_up(&dvbdev->wait_queue);
		}
		ts_bus_ctrl(fepriv, dvbdev, 0);
	}
	adapter_dprintk(adapter, "Calling dvb_frontend_put\n");
	return ret;
}

//OK
static int neumo_dvb_frontend_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	void* fe_ = dvbdev->priv; //do not know type yet
	struct neumo_dvb_frontend_private* fepriv = xa_load(&neumo_fes, (intptr_t) fe_);
	int ret;
	neumo_dvb_free_user_data(fepriv->extra, dvbdev);
	if(fepriv->dvb_api_fe) {
		struct dvb_adapter* adapter = dvb_driver_get_adapter(fepriv);
		ret= neumo_dvb_frontend_release_fepriv(fepriv, adapter, inode, file);
		dvb_driver_dvb_frontend_put(fepriv->dvb_api_fe);
		adapter_dprintk(adapter, "Calling dvb_frontend_put done\n");
	} else {
		BUG_ON(!fepriv->neumo_api_fe);
		struct dvb_adapter* adapter = fepriv->neumo_api_fe->dvb;
		ret= neumo_dvb_frontend_release_fepriv(fepriv, adapter, inode, file);
		neumo_driver_dvb_frontend_put(fepriv->neumo_api_fe);
		adapter_dprintk(adapter, "Calling dvb_frontend_put done\n");
	}
	return ret;
}

//OK
const struct file_operations neumo_dvb_frontend_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= neumo_dvb_frontend_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= neumo_dvb_frontend_compat_ioctl,
#endif
	.poll		= neumo_dvb_frontend_poll,
	.open		= neumo_dvb_frontend_open,
	.release	= neumo_dvb_frontend_release,
	.llseek		= noop_llseek,
};


//dvb_frontend_suspend does not exist in neumo api
//dvb_frontend_resume does not exist in neumo api


struct neumo_dvb_frontend_private* neumo_dvb_create_fepriv(struct dvb_frontend* dvb_api_fe,
																													 struct neumo_dvb_frontend* neumo_api_fe)
{
	BUG_ON(!((!!dvb_api_fe) ^ (!!neumo_api_fe)));
	struct neumo_dvb_frontend_private* fepriv = kzalloc(sizeof(struct neumo_dvb_frontend_private), GFP_KERNEL);

	if (!fepriv) {
		return NULL;
	}
	fepriv->dvb_api_fe = dvb_api_fe;
	fepriv->neumo_api_fe = neumo_api_fe;
	struct neumo_dvb_extra* extra = (struct neumo_dvb_extra*)&fepriv->extra;
	init_waitqueue_head(&extra->algo_state.wait_queue);

	neumo_dvb_frontend_private_init(fepriv, dvb_api_fe, neumo_api_fe);
	xa_store(&neumo_fes, dvb_api_fe ? (intptr_t) dvb_api_fe : (intptr_t) neumo_api_fe, fepriv, GFP_KERNEL);
	return fepriv;
}

int neumo_dvb_register_frontend(struct dvb_adapter *adapter, struct neumo_dvb_frontend* fe)
{
	const struct dvb_device dvbdev_template = {
		.users = ~0,
		.writers = 1,
		.readers = (~0) - 1,
		.fops = &neumo_dvb_frontend_fops,
#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
		.name = fe->ops.info.name,
#endif
	};
	fe->dvb = adapter;

	if (mutex_lock_interruptible(&frontend_mutex))
		return -ERESTARTSYS;
	fe->frontend_priv = neumo_dvb_create_fepriv(NULL, fe);
	struct neumo_dvb_frontend_private* fepriv = fe->frontend_priv;

	if (!fepriv) {
		mutex_unlock(&frontend_mutex);
		return -ENOMEM;
	}


	kref_init(&fe->refcount);

	/*
	 * After initialization, there need to be two references: one
	 * for dvb_unregister_frontend(), and another one for
	 * dvb_frontend_detach().
	 */
	kref_get(&fe->refcount);


	dvb_register_device(adapter, &fepriv->dvbdev, &dvbdev_template,
					fe, DVB_DEVICE_FRONTEND, 0);

	/*
	 * Initialize the cache to the proper values according with the
	 * first supported delivery system (ops->delsys[0])
	 */

	fe->dtv_property_cache.delivery_system = fe->ops.delsys[0];
	neumo_driver_dvb_frontend_clear_cache(fe);

	mutex_unlock(&frontend_mutex);
	return 0;
}

EXPORT_SYMBOL(neumo_dvb_register_frontend);

int neumo_dvb_unregister_frontend(struct neumo_dvb_frontend *fe)
{
	struct neumo_dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dvb_adapter* adapter = fe->dvb;
	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	mutex_lock(&frontend_mutex);
	neumo_dvb_frontend_stop_fepriv(fepriv, adapter);
	if(fepriv) {
		xa_erase(&neumo_fes, (intptr_t)fe);
	}
	if(fepriv && fepriv->dvbdev)
		dvb_remove_device(fepriv->dvbdev);
	/* fe is invalid now */
	mutex_unlock(&frontend_mutex);
	neumo_driver_dvb_frontend_put(fe);
	return 0;
}
EXPORT_SYMBOL(neumo_dvb_unregister_frontend);

static void neumo_driver_dvb_frontend_invoke_release(struct neumo_dvb_frontend *fe,
					void (*release)(struct neumo_dvb_frontend *fe))
{
	if (release) {
		release(fe);
#ifdef CONFIG_MEDIA_ATTACH
		fe_dprintk(fe, "fe=%p DETACH release=%p\n", fe, release);
		dvb_detach(release);
#endif
	}
}

void neumo_dvb_frontend_detach(struct neumo_dvb_frontend *fe)
{
	neumo_driver_dvb_frontend_invoke_release(fe, fe->ops.release_sec);
	neumo_driver_dvb_frontend_invoke_release(fe, fe->ops.tuner_ops.release);
	neumo_driver_dvb_frontend_invoke_release(fe, fe->ops.analog_ops.release);
	neumo_driver_dvb_frontend_put(fe);
}
EXPORT_SYMBOL(neumo_dvb_frontend_detach);


extern int neumo_dvb_frontend_module_init(void);
int neumo_dvb_frontend_module_init(void)
{
	xa_init(&neumo_fes);
	return 0;
}

extern void neumo_dvb_frontend_module_exit(void);
void neumo_dvb_frontend_module_exit(void)
{
	dprintk("module exit\n");
	xa_destroy(&neumo_fes);
	return;
}

MODULE_LICENSE("GPL");
MODULE_VERSION("0.9");



//check for incorrect include files
#include "linux/media/neumo-check.h"
