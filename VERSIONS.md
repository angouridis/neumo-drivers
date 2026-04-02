# Changes in release-1.9

* unicable_mode incorrect set in stid135_select_rf_in_legacy_
* Detect incorrect values of rf_in
* stid135: Improve lock detection code.
* Improved logging.
* Reduce log verbosity
* Properly log neumo driver versions.
* Properly handle drivers not supporting set_rf_input
* Avoid large on stack allocation
* Code cleanup and improved debug messages
* Experimental fix for null pointer dereference in dvb_dmxdev_add_pid
* Fix for DMX_OUT_DEMUX_TAP
* Update README
* dvb_demux: improved logging of cc and crc errors.
* Bug: incorrect resetting of embedded t2mi stream, causing startup problems if demux is opened too early on 16.0 Bosnia/Herzegovina t2mi mux
* Bug: incorrect decision on whether to respect bbframes_auto
* Bug: t2mi packets processed twice in case of input errors and input errors not properly handled.
* Avoid dprintk in routines called by dma code. Use dprintk_nice instead.
* Bug: TBS5927 and other cards based on stv6120 no longer working.
* Avoid calling FE_STiD135_GetDemodLock twice. Hopelly fixes some cases where QPSK lock is locked at tune.
* Only call pls_search_list if there is no fec_lock
* fix default plp bug
* Code cleanup
* Bug: regression due to 49da7a826c4d698 causes problems for t2mi mux 1606V@5.0W by adding data twice.

# Changes in release-1.8

* BUG: when user specifies isi=-1 on a multistream, a corrupt mix of all streams is produced; also an incorrect isi=-1 is returned. The new code picks an arbitrary stream
* Run isi-scan even when currently selected isi is invalid.
* Incorrect setting of mis_mode (bad code without effect).
* In case bbframes_mode is on, also allow demux users to directly access bbframes embedded in pid 270.
* Implement receiving matypes from demux.

* Incorrect log message.
* stid135: call get_signal_info even when error state has been set.
* stid135: BUG: isi_scan not called when user requests non-existing ISI.
* BUG: bbframes processing: out of buffer writing after receiving incorrect data.
* bbframe processing in dvb_demux: Avoid clearing isi list on crc error.


# Changes in release-1.7

* Merged tbs code up to c6af36f453d22ef8b38d2bb6
* Reduce log verbosity
* Ignore bbframes_auto for non transport streams.
* Auto detect plp_id in t2mi streams, which is useful as usually only one stream is present.
* Recover t2mi processing after stream error occurs; this fixes the problem that some t2mi streams output corrupt data if demux is started before frontend is tuned.
* Remove plp filtering.
* Bug; confusion between supports.bbframes and supports.neumo
* Bug: frontend thread keeps running after frontend device is closed, leading to i2c xfer errors.
* Show adapter_no in debug messages.
* stid135: Bug: allow retrying SET_RF_INPUT when it fails for a master request
* stid135:  Replace ChipWaitOrAbort by msleep.
* stid135: fix race in turning off adapters.
* stid135: fix deadlocksl
* stid135: avoid detecting wrong isi when in bbframes mode
* Bug: Remove double free in embedded_stream_release_.
* Fixes for newer kernels: remove_new updates
* m88rs6060: make autoclock work.
* Allow bbframes mode also for non-multi-streams
* Prevent fe monitoring from running after close. Improved debugging.

# Changes in release-1.6
* New demux api to support internal demuxing of bbframes streams (stid135-based cards only) and t2mi streams
  (all cards).
* Updated neumodvb api version to 1.6
* Documented neumodvb api version 1.6
* New /sys/module/dvb_core/demuxX/demux and /sys/module/dvb_core/demuxX/dmxdev entries to show the
  internal state of demuxing.
* Bug: incorrect unlocking during sleep.
* Bug: pls_code set to 0 when blind-tuning from positioner_dialog.
* Remove some of the confusing mess due to isi/pls_code/mode specification in stream_id in legacy applications.

# Changes in release-1.5
* Added a new demux interface allowing internal demuxing of bbrames; Extended stid135 driver to make
  use of this demux interface.

  As a result it is now possible to demux several or all streams from a multistream transponder using a
  single demodulutor (but no user programs currently support this). This involves using some new ioctls
  on /dev/dvb/adapterX/demuxY. In this is case there is no need to specify the desired stream_id (ISI) when tuning,
  but instead ask the frontend to output bbframes and pass the desired stream to the new ioctl
  DMX_SET_BBFRAMES_STREAM on /dev/dvb/adapterX/demuxY. By Opening the demux multiple times, additional streams
  can de bemuxed simultaenously.

  For backward compatibility it is still allowed to pass a stream_id to the frontend. In this case the
  frontend will not embedded multistreams in bbframes and demuxing proceeds as before.

* Added a driver option bbframes_auto to support legacy applications like tvheadend. When activated,
  this option will instruct the stid135 driver to encapsulate all multistreams into an embedded bbframes
  stream. The demuxer will automatically pick one of the streams and demux that for the legacy application

This should be set by adding the line

  ``options stid135 bbframes_auto=1''
  in  /etc/modprobe.d/stid135.conf and rebooting, or by runnning the following command as root (no need to reboot):

  `` echo 1 > /sys/module/stid135/parameters/bbframes_auto''


* One use of bbframes_auto is to provide a workaround for the non-working multistream 12606V@5.0W Streams a4 and 5 cannot
  be decoded properly due to what is probably a hardware bug. By asking the chip to output bbframes, the buggy bbframe
  decoding is skipped by the chip and instead done in the new demux software driver. As a result, both streams then work
  in user programs such as tvheadend.

# Changes in release-1.4.1
* Fix deadlock introduced by e8c6a729c905f7464bd7

# Changes in release-1.4
* Revert "Experimental workaround for 12606V@5.0W to make stream 4 work" as it caused problems on some multistreams
* Bug: tbs6916: not locking one of the chips when releasing rf_in
* Incorporated latest changes from TBS

# Changes in release-1.3
* Document neumo api
* Updated installation instructions
* Better handling of cards that do not support FE_SET_RF_INPUT
* Add FE_DISEQC_SEND_LONG_MASTER_CMD to support unicable programming
* Integrated changes from tbsdtv
* Changes for kernel 6.10
* Fix compilation warnings
* stid135: Experimental workaround for 12606V@5.0W to make stream 4 work
* stid135: Skip dummy frames when detecting modulation, so that correct modulation is reported
* stid135: Added blindscan_alwasy configuration via sysfs
* stid135: Fix register reading.
* stid135: Implemented unicable mode
* stid135: Erroneous mutex lock detection
* stid135: Bug: stdi35_select_rf_in_ called without locking
* stud135: Detect deadlock condition and work around it.
* m88rs6060: skip dummy frames when detecting modulation, so that correct modulation is reported
* stv091x: implemented recv_slave_reply (untested)
* ms88rs6060: fix driver no longer working.

# Changes in release-1.2
* Added sysfs entries in /sys/modules/stid135/
* Allow setting the default rf input for each demod, for use with tvheadend. This allows more than one slave
  tuner to use the same rf input connector on the card.
* Incorporated changes from the official tbs drivers up to July 11 2024.
* Improved determination of card mac addresses and generating fake mac addresses should the card not have one set.
* Support for the tbs6916 card.
* Bug: stid135 based cards sends out 21kHz tone and diseqc base frequency instead of 22kHz and this causes some
  diseqc switcches to sometimes ignore commands.
* Provide additional ioctls to support "syncrhonized" calls, where one adapter can wait on another adapter
  to finish setting voltages, tones and sending diseqc commands
* Protection against drivers which blindly assume name to be of size 128.
* Bug: fe_read_status should not return -1 on timeout.
* stv091x: fix isi scan.
* Bug: tuning loop runs to early when diseqc is sent but no tuning info is present yet.

# Changes in release-1.1.1

## Kernel support
* Update for linux 6.2 - 6.6 kernels
* compiles also on ubuntu 23.0

## Changes

* Integrated latest changes from tbs linux_media
* Avoid out of bounds array indexing
* Improved voltage setting and related internal state management
* si2157 double free bug
* si2158 integrates some tns code


# Changes in release-1.0.0

##Kernel support
* Update for linux 6 kernel
* compiles 6.1

## Various bugs and improvements
* Improved logging.
* Avoid null pointer dereference when short_name not sent for 2nd frontend
* tbsecp3: set default mode to 1
* Do not send diseqc in mode=0; remove dead code
* Experimental fix for cards without short name; short name for tbs6908
* Bug: incorrect tone and voltage returned when tone and voltage are set via ioctl
* Bug: bbframe mode not reset in some cases
* Set rf_in=0 for non neumodvb cards
* Different way to initialize rf_inputs.
* rename tuner_active to rf_in_selected.
* Improved debug logging: add demod number; use numbering starting at 0
* Enable bbframes for GSE
* ts_nosync (code not yet active
* wideband support (untested)
* matype not set in some cases.
* select_min_isi sometimes fails, resulting in no tune
* Bug: after pls_seacrch fails, restore default pls
* Redefine FE_HAS_TIMING_LOCK to be different from FE_OUT_OF_RESOURCES, which confuses dvblast
* cx231xx correct MAC extraction after power cycle
* BUG: incorrect reporting of FE_OUT_OF_RESOURCES when lock fails
* Temporary fix to avoid crash on module unload
* Bug: when auto-selecting a stream_id, ensure that selected stream_id is returned to driver, rather than -1
* Introduce state_sleep; release mutex during sleep, to avoid delays in parallel operation of multiple demods
* Introduce base_lock, base_unlock, base_trylock
* Increase number of events in event queue
* Bug: slow closing of frontend because of needless locking
* Disallow turning off voltage when tuner is still in use
* Defaults for rf_inputs property; More stable faking of missing mac_address
* Possible module unload bug for tbs6590
* Incorrect processing of DVB_STOP ioctl. Fix includes workaround for bad user space code containing the same bug
* Bug: interrupting fft spectrum sometimes causes crash
* Bug: need to lock mutex during spectrum scan sweep.
* Adapter name retrieval; add short names for adapters.
* Add FE_SET_RF_INPUT ioctl; add rf_inputs fields to extended frontend info;
* Add card_short_name
* Bug: memory access beyond end of array causing kernel crash
* Do not apply center frequency shift for low symbol rates. Improves locking of low symbol rate muxes?
* Bug: voltage remains on after frontend is released
* Bug: crash on module onload (chip_proc_close)
* Always use "local" value for faked mac addresses.
* distinghuish between mac address of card and adapter
* Suppress some compiler warnings
* Expose default rf_in for each dvb adapter

## stv091x based cards
* Fixes for tbs6983 (UNTESTED; may break other stv091x cards)
* Bug: tone not set on high band with stv091x
* Bug: stv091x needs mutex protection when called from dvb_frontend;
* tbs5927 - set correct frequency limits
* tbs5927: add card_address
* tbs5927 add "usb" to bus address
* set_voltage call accidentally removed from tbs5927
* Proper mac address and card name for tbs5927

## stid135 based cards
* tbs6903x: Reduce overclocking to avoid i2c nack errors at startup
* tbs6903x_V2 has only 2 rf inputs.
* tbs6903x: allow both tuners to connect to both rf inputs
* Allow exposing registers of multiple stid135 cards in /proc
* Updated spectral scan algorithm (algo 5)
* debug logging
* Bug: force frame and continous mode not reset
* Add more error detection
* Add dummy plf detection
* simplify fe_stid135_get_signal_info
* stid135 Bug: Add code which ran in get_frontend into read_status because get_frontend is no longer called
* Remove stid135-fe.h; make some functions static
* Improved debug logging: add demod number; use numbering starting at 0
* stid135: improved lock status reporting
* stid135: deadlock in spectrum_scan
* In stid135_fft, assume all functions are called with base_lock held.
* stid135: correctly handle low rolloff factors indicated by alternating matype
* stid135: disable get_frontend
* Handle out of llr condition
* stid135: allow writing registers
* Overclock i2c bus on stid135 based cards.
* Rename spectrum scan related enum; allow rf_input selection on stid135;
* stid135: report that only 4 out of 8 tuners support fft scan.
* Bug: stid135 22kHz still on when device not in use
* Bug: incorrect docs for stid135-fe module parameter
* Bug: accidentally disabled fe_stid135_init_fft
* Re-add tbs6916 (16 tuner optical card)

## em28xx based cards
* em28xx crash on usb disconnect.
* em28xx crash on module unload

## m88rs6060 based cards
* m88rs6060: remove uneeeded reads
* tbs6904se: increase i2c speed (experimental)
* m88rs6060: faster spectrum scan (psyborg55)
* shortname for tbs6522

## tas2101 based cards
* tas2101: pause after activating demod, to return correct SNR
* tas2101: set proper lock flags
* tas2101: set timing lock status
* tas2101: increase constellation size
* tbs5990: set proper card_mac_address and rf_input.
* tbs5990: add bus address
* tbs5990: incorrect mac address read.

## si283 based cards
* si2183 patch
* si2183: report timing lock to avoid problems in user apps.
* Bug: si2183 sets incorrect frequency limits
* Bug: si2183 sets incorrect frequency limits
* tbs6504: ensure all frontends on the same adapter have same faked macaddress
* Workaround for tbs6504 not detecting stream_type correctly



# Changes in release-0.9.0

* Blindscan and spectrum upport for montage m88rs60606 based cards: tbs6902SE and tbs2904SE.
* Support m883s6060 module unloading
* stid135: BER is now reported properly othrough the PRE instead of POST error counters
* Improved correction of discontinuities in spectrum due to small errors in RF level
* stid135: detected multi-stream IDs are now accumalated internally
* Added DTV_BIT_RATE readout via dvbapi
* stid135: improper estimation of required  llr rate, causing rai multistream on 5.0W to not tune sometimes
  (green blocks in picture)
* stid135: changed default spectral resolution: slightly slower scan, but more narrow-band muxes found
* Add support for timing lock flag
* stv091x: return more correct data about currently tuned mux (instead of returning what user requested)


