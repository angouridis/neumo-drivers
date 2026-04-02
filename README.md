# Fork Notice

This is a fork of [deeptho/neumo-drivers](https://github.com/deeptho/neumo-drivers), which is not actively maintained. It was used in a personal project to fix compilation errors and improve compatibility with **Ubuntu 24.04.1 LTS** (GNU/Linux 6.8.0-106-generic x86_64) using **TBS 6903X** and **TBS 6904SE** cards.

**Use at your own risk.** No free support is provided whatsoever — I'm on a busy schedule.

---

# What is this?
This repository contains the neumo drivers for TBS dvb cards.
These drivers add support for advanced features such as spectrum acquisition, blindscan
and improved demuxing.

The drivers are based on the official TBS drivers, with specific neumo-patches for the following drivers:

* stid135: ts6909x and tbs6903x cards (v1 and v2)
* stv091x: tbs5927 tbs6908 tbs6903 tbs6983 tbs6522 tbs6983 tbs6903 tbs6909
* tas2101: tbs5990, tbs6904
* si2183 based cards: tbs6504, tbs5580, tbs6916
* m88rs6060 based cards: tbs6902SE and tbs904SE. On these cards there is not IQ-scan (constellation display)
to support blindscan, to fix bugs and to make improvements.

All other TBS cards should also be supported, but wihout the above mentioned advanced features.

## Supported PCI cards:
* tbs6910X tbs6910 tbs6910se tbs6916 tbs6216 tbs6909x
* tbs6909SE tbs6909 tbs6908 tbs6904 tbs6904X
* tbs6904SE tbs6902 tbs6902SE tbs6590 tbs6903 tbs6903x tbs6905
* tbs6508 tbs6522 tbs6522H tbs6504 tbs7901 tbs6528 tbs6912
* tbs6504H tbs6590X
* TBS6281 TBS6281SE TBS6205SE tbs6205 tbs6290 tbs6290se tbs7230
* tbs6209 tbs6209SE tbs6514 tbs6814 tbs6704
* tbs6302SE tbs6308 tbs6308X tbs6312X tbs6304 tbs6301 tbs6301se tbs6302x tbs6302T
* tbs6302RV tbs6304X tbs6304T tbs6304RV tbs6324 tbs6322 tbs690a  tbs6331
* tbs6900  tbs6004 tbs690b tbs6104 tbs6008 tbs6214 tbs6034 tbs6032 tbs6001

## Supported USB cards:
* tbs-qbox tbs-qbox2 tbs-qbox22 tbs-qboxs2 tbs-qbox2ci tbs5922se tbs5925 tbs5880
tbs5220 tbs5230 tbs5881 tbs5520 tbs5520se tbs5530 tbs5580 tbs5927 tbs5301 tbs5930 tbs5931 tbs5590

## Minimally suppored other cards
* Hauppage wintv DVB-C/DVB-T stick
* Astrometa DVB0C.DVB-T stick

The support relates to assigning a consistent MAC address to these devices.

# Important
These drivers should work with most linux DVB applications but to make use of the special
features you need `neumoDVB` or one of the `blindscan` apps found on githhub. Moreoever, you need
recent enough versions of these programs, as older versions will refuse to start, will crash, or will
not work properly


# Installation

### 1. Prerequisities

#### Ubuntu / Debian
Ensure you have the latest kernel headers and the **extra modules** package (required for core media symbols like `mc.ko` and `videobuf2` on modern kernels like 6.8):
```bash
sudo apt update
sudo apt install linux-headers-$(uname -r) linux-modules-extra-$(uname -r)
sudo apt install libproc-processtable-perl libelf-dev patchutils patch gcc make
```

#### Fedora
```bash
sudo dnf install -y kernel-devel patchutils ccache perl-File-Copy perl perl-Proc-ProcessTable
```

### 2. Build and Install

```bash
# Clean previous builds
make clean

# Compile
make -j$(nproc)

# Install modules
sudo make install

# Update dependencies
sudo depmod -a
```

### 3. Firmware
Download and extract the TBS tuner firmwares:
```bash
wget http://www.tbsdtv.com/download/document/linux/tbs-tuner-firmwares_v1.0.tar.bz2
sudo tar jxvf tbs-tuner-firmwares_v1.0.tar.bz2 -C /lib/firmware/
```

### 4. Loading Drivers
You can either reboot (recommended) or manually load the modules:
```bash
sudo modprobe mc
sudo modprobe videobuf2-core
sudo modprobe dvb-core
sudo modprobe tbsecp3  # or your specific card driver
```

---


# Checking that it works
```
    ls -al /dev/dvb/adapters
    #is should show at least one adapter. Otherwise no drivers have been loaded.

    cat /sys/module/dvb_core/info/version
    #this should show something like
       type = "neumo";  #neumo drivers have been loaded
       version = "1.7"; #version of the driver interface
       GIT-REV = "3269fab2c845";
       GIT-TAG = "";
       GIT-BRANCH = "deepthought-new"
```
# In case something goes wrong

Report problems on github. Howver, there is no point in doing that if you do not
provide enough information to actually find the cause of the problem. Apart from
a clear description of all commands you ran, and what actions you took before the
problem occured, you also need to provide information on driver versions and such.
The script `bug_report.py` helps to gather some useful data into a tar file.
At some point it will also ask for a password to invoke `sudo` to copy your kernel
logs. You are free to edit the produced files to remove sensitive data should it be present.


# Usage
These drivers were designed for use with neumoDVB to support advanced features provided by
some cards:

* Spectrum acquisition.
* Constellation plots.
* Blind scanning.
* Supporting cards with RF mixes, which allow multiple demods to connect to the same tuner
* Stable identification of cards even when they are inserted in different orders (USB device)
  or placed in different slots.
* Providing advanced information for the stid135 based cards through sysfs and allowing
  certain parameters to be set through sysfs for those cards.

To make use or optimal use of these features, application programs should adhere to a new
extended ``neumo-DVB'' interface, but the drivers were designed to be also backward compatable

## Using with neumoDVB

neumoDVB detects the presence of these drivers and then will exploit the advanced features;
if the drivers are not loaded if falls back to the DVB-apiV5 interface

## Using with existing programs developed for DVB-apiV5

In principle no changes are needed, and even without changes for stid135 based cards,
through the sysfs interface it is possible to obtain more information about
the cards and what they are doing. Also it is possible to slightly enhance using existing
programs, such as tvheadend.


## Additional information and configuration via sysfs
* Through /sys/module/stid135/... it is possible to figure out which adapter belongs to which
  card. There is no guarantee that the directory layout and content will have this specific
  format in future. So do not rely on it.
* Each card has one *temperature* property per stid135 chip, which contains the current temperature
  in Celcius.
* Each card has a *blindscan_always* property.
  By setting '/sys/module/stid135/card0/blindscan_always' to 1, the stid135 driver will blindscan
  muxes on that card (card0). For instance if TvHeadend thinks that a mux is QPSK and has a symbolrate
  of 20kS/s, but in reality it has PSK8 modulation and a symbolrate of 30kS/s, tuning will fail without this
  setting, but will succeed with it. The downside is that the web interface of TvHeadend will
  still show the erroneous parameters. The upside is that scanning becomes easier.

  Note that the proper way to set a sysfs parameter is (as an example)

  ``` echo 1 | sudo tee /sys/module/stid135/card0/blindscan_always```

* Each adapter (or rather: demodulator) has a *default_rf_in* property. The value of this
  is a number indicating the physical input to which the adapter will be connected by default,
  if applications do not use the neumo-DVB api. For example, on tbs6909X,
  the default value for /sys/module/stid135/card0/chip0/demodX/default_rf_in
  will be X modulo 8, which means that two demodulators are connected to each of the four
  RF inputs.

  In case you only have one cable connected to RF input 0, you can set all of the
  default_rf_in values to 0 and then configure TvHeadend to treat adapters 1...7 as slave adapters
  connected to master adapter 0. This then will allow simulatenous viewing or recording of
  channels spread over 8 different muxes in the same satellite band (e.g., horizontal Ku-low).

  Note that TvHeadend will probably have difficulties coping with that much data, so it is best
  to stick with a smaller number of slave tuners.

* /sys/module/dvb_core/demuxX/demux displays the internal state of the demuxer. It shows the hierarchy
  of streams.
* /sys/module/dvb_core/demuxX/dmxdev displays the internal state of the demuxer, but from the point
  of view of each open file descriptor.

## The neumoDVBapi

This API has been kept mostly compatible with the existing DVB-V5 api, both internally in
the kernel and externally towards user applications. However, the kernel-side
API is not binary compatile (yet) with DVB-apiV5 and requires recompilation of **all** DVB modules that
the user wants to use, even those not supporting neumoDVB.

User space applications should proceed as follows

* First check for the presence of */sys/module/dvb_core/info/version*. If this entry exists,
  it means that the neumoDVB drivers have been loaded, otherwise the application should fall back to
  DVB-apiV5, i.e., not use any additional features provided by nuemoDVB. The version number in that
  sysfs file can also be used to distinghuish between different versions of the drivers. Note that the
  neumoDVB api is not yet finalized.

* Then tune the frontend, which involved the existing DVB-V5 api ioctls, but with additional options,
  to retrieve spectral data (instead of tuning), or to retrieve constellation samples (along with tuning),
  to support blind tuning or blindscanning a satellite band. There is also an additional ioctl to
  exploit internal switches on some multi-input DVB-S2 cards. These ioctls allow connecting a specific
  internal frontend to one of four internal wide-band tuners.

* Then use the demux ioctls to extract the desired transport stream or section data. New icoctls
  have been added to select the desired embedded T2MI or STID streams.

### The neumoDVBapi frontend ioctl api

* *FE_GET_EXTENDED_INFO* Using this ioctl, get information about the installed cards and adapters,
  specificaly their names (for use in GUI and in log files) and their *MAC address*. The latter
  is a unique id, which can be used to associate configuration information. For instance, the adapter
  number of a specific adapter may differ from one boot to the next when a card has been removed,
  or a new one has been inserted. However, the MAC address will always remain the same. In case
  the card specific drivers do not provide a MAC address, neumoDVB will attemp to construct one
  that is unique based on in which PCIe slot it is inserted. If the card is later in an other slot
  the MAC address will change. For USB devices there are even fewer guarantees, and the card may appear
  at a new MAC address after removing and re-attaching it.

* *FE_SET_RF_INPUT* When tuning to a specific mux, first call this ioctl to connect
  a demodulator to a specific RF_INPUT, but also to synchronize secondary device (LNBs and switches)
  configuration when multiple demodulators connect to the same tuner and thus to the same LNB and switches.

  Being able to select a specific RF_INPUTs means that card operation becomes much more flexible.
  For instance, it is possible to connect more than 2 demodulators to the same input cable temporarily
  if the card has an internal switch. In case each input is connected to a different LNB for a different satellite,
  it is possible to receive up to 8 different muxes on TBS6909V2 or up to 16 muxes on TBS6916 on the same satellite.
  With the standard DVB-v5 api drivers only 2 would be possible, because of the choice to pair 2 tuners
  to each RF_INPUT in a fixed manner.

  Another problem not handled by the standard drivers is that a slave adapter should wait with tuning
  until it is certain that secondary equipment has been configured: switches need some time to power up,
  some types of LNBs (e.g, unicable) also need some time after power up before they respond to commands,
  and positioners need some time to rotate the dish. The neumo API leaves it to applications to work
  out the details, as this often needs configuration constants, or perhaps even custom software. Instead
  it expectes that exactly one of the connecting threads will performa all needed actions (sending diseqc
  commands, waiting appropriate amounts of time...) and will then inform the drivers when all of this is
  done. Until then, all other frontends trying to connect to the same rf_in through *FE_SET_RF_INPUT* ioctls
  will see these ioctls fail with a return value of *FE_RESERVATION_RETRY*. Those threads should wait a while
  and then retry the ioctl.

  The reason why this is needed is that many programs are multi hreaded. If the thread for the master tuner
  is delayed for some reason, it may happen that the master tuner has not yet finished sending DiSeqC commands
  when the slave tuner starts tuning and then slave tuning will fail. Of course it would be possible to prevent
  this at the application level, but neumoDVB handles it in the drivers.

  Concretely, when calling FE_SET_RF_INPUT, the application can specificy whether the demodulator will
  become master or slave. Typically an application will specify 'master' for the first tune, and 'slave'
  for subsequent tune's using the same RF_INPUT. An application can also specify 'master or slave'. In this
  case the drivers will make the adapter that makes the first ioctl the master and the other ones slaves.
  In all cases, when the drivers receive the ioctl, they will enforce proper execution of master requests
  before related slave requests can start.

  The ioctl FE_SET_RF_INPUT caller should specify

    * *rf_input*: the desired tuner and cable to connect to

    * *mode*: whether the tuner wants to become master (and agrees to configure LNB and switches), slave
     (agrees to not configure LNB and switches), or both.

    * *unicable_mode*: whether or not the secondary system attached to the LNB operates in unicable mode.
    In uncable mode, DiSeqC commands need to be sent even by slave tuners, to select specific user bands.
    This creates new opportunities for races between multiple frontends trying to program user bands
    in parallel.

    Also, the output voltage needs to be temporarily raised to 18 volt when unicable commands are sent,
    and must be 12 volt in all other cases. User programs activate new user bands in three steps:
    1) raising voltage to 18 volt;
    2) sending unicable DiSeqC unicable command;
    3) lowering voltage to 12 volt.
  
    This three step process is inherited from dvbapiV5, but creates additional opportunities
    for racing. For instance, internal locking prevents step 2 from overlapping with step 2 calls made
    by other frontends, but not prevent other threads from executing step 3 after the current thread
    has executed step 1. This wll then cause user band selection to fail

    Setting unicable_mode = 1 allows slave threads to raise voltages and send unicable DiSeqC commands,
    which it would otherwise prohibit. When unicable_mode active, also setting voltage to 18 VOLT
    will soft-fail with the return value FE_UNICABLE_DISEQC_RETRY when the voltage is already 18 volt,
    which implies that some other frontend is currently sending unicable DiSeqC commands.

    When the FE_SET_VOLTAGE ioctl returns  FE_UNICABLE_DISEQC_RETRY, the thread should retry the ioctl.
    When it succeeds instead, then the thread calling the ioctl will be the only one allowed to
    send unicable commands.

    * *owner*: a unique identifier for the calling application. Applications can only use resources (tuners,
     demodulators) if they are not in use by another application. Typically this should be the process id.

    * config_id*: a number which is incremented each time the application will reconfigure an RF_INPUT,
     which means: select a different LNB, or a different satellite band.

     Whenever the driver notices an update in config_id, it will wait for all demodulators to release
     resources reserved with an older config_id value before allowing FE_SET_RF_INPUT ioctl's (including
     the one using the new config_id for the first time) to succeed. This loosens the synchronisation
     requirements on the application. For instance the application can call FE_SET_RF_INPUT for a master
     and slave demodulator in parallel on two threads. If for some reason the master ioctl call is delayed
     until after the slave ioctl call, the driver will do the right thing: fail one or both of the calls
     and inform the calling threads (through the ioctl return value) that this failure is only temporary
     and that they should retry the call after waiting for a brief period.

  The ioctl's return value indicates

  * a result: master or slave. If the result is 'master', the adapter should proceed with setting
     voltages, tones, sending diseqc commands, and then finally perform a tune. If the result is
     'slave', then the caller can be certain that secondary equipment is now ready and the caller
     can start tuning its adapter.

  * or permanent failure (e.g., requesting a non-existing RF_INPUT)

  * or temporary failure: it is not yet possible to select the RF_INPUT, either because a master demodulator
     has not yet finished configuring secondary equipment (which the drivers can tell, see below),
     or because some other demodulators still need to release the tuner but have not done so. The calling
     thread can simply retry after sleeping e.g., 10 milliseconds, or the application can provide a
     locking mechanism to prevent this from happening (warning: this tends
     to be slower)

  If the calling thread receives a 'slave' result, it should proceed directly with tuning. If it
  receives a 'master result', it should make FE_SET_VOLTAGE, FE_SET_TONE and FE_DISEQC_SEND_MASTER_CMD
  commands as needed, and also respect any delays needed by the secondary equipment to
  correctly power up. At this stage the calling thread should tune the adapter, using FE_SET_PROPERTY,
  and include a DTV_SET_SEC_CONFIGURED property. This informs the drivers that the secondary  equipment
  is ready for use. From this stage on FE_SET_RF_INPUT ioctls for the same RF_INPUT will succeed, provided
  they use the current config_id value.

* The remainder of the tuning process is similar as with the standard DVB-v5 api, except that additional
  tuning properties have been added to indicate blind scanning and that additional properties are returned
  to indicate discovered modulation parameters:

  * *DTV_ALGORITHM*: specify the type of blind tuning to perform
  
  * *DTV_SEARCH_RANGE*: specify the frequency range to search during blind tuning
  
  * *DTV_ISI_LIST*: retrieve list of ISI codes (stream ids)
  
  * *DTV_PLS_SEARCH_LIST*: specify a ist of PLS scrambling modes/codes to test during scan
  
  * *DTV_PLS_SEARCH_RANGE*: specify a range of PLS scrambling modes/codes to test during scan
  
  * *DTV_SCAN_START_FREQUENCY*: specify the start of the frequency range to scan
  
  * DTV_SCAN_END_FREQUENCY*: specify the end of the frequency range to scan
  
  * *DTV_SCAN_RESOLUTION*: specify the frequency step for a range scan
  
  * *DTV_SCAN_FFT_SIZE*: specify the FFT size of frequency scan
  
  * *DTV_SCAN*: request blind scanning a range of frequencies
  
  * *DTV_SPECTRUM*: request starting a spectrum acquisition
  
  * *DTV_MAX_SYMBOL_RATE*: specify maximum allowed symbol rate during blindscan
  
  * *DTV_CONSTELLATION*: request constellation samples to be returned after tuning
  
  * *DTV_HEARTBEAT*: specify how frequently the API asks the drivers to check quality, strength, lock status...
     of currently tuned demods
  
  * *DTV_BITRATE*: request returning the bit rate of the received stream
  
  * *DTV_LOCKTIME*: request returning the time until first lock
  
  * *DTV_MATYPE_LIST*: request returning a list of present matypes and stream_ids
  
  * *DTV_RF_INPUT*: request returning the currently connected rf_input
  
  * *DTV_SET_SEC_CONFIGURED*: inform the driver that the frontend's secondary equipment (diseqc switches,
     power supply) has been properly configured and that the equipment is fully powered up and ready for
     use by slave demods.
  
  * *DTV_OUTPUT_BBFRAMES*: forc stid135 based cards to send bbframes to the demux api when multi-stream
     is received.

### The neumoDVB demux api

neumoDVB adds additional ioctls to the DVB-v5 API. These ioctls acitvate internal demuxing of
T2MI- or STID-encapsulated transport streams:

* STID-encapsulated streams are created by the stid135 chips on some TBS cards. When the chip receives
  data in a non-TS format, the chip encapsulates those in a single-PID transport stream. When the
  chip receives a multi-stream, the fronend usually specifies the desired transport stream by specifying its
  stream id (ISI).

  However, by specifying the bbframes_auto=1 as a module option to stid35.ko, or by
  sending the command DTV_OUTPUT_BBFRAMES in the FE_SET_PROPERTY frontend ioctl during tuning, this embedding
  can be forced. In that case the bbframes of **all** streams are embebedded into a single PID transport stream.
  This is useful to work around a hardware bug, which in rare cases prevents correct reception of multi-streams.
  It is also useful to receive and decode all streams simultaneously, while using only a single demod.

* T2MI-encapsulated streams are DVB-T2 transport streams encapsulated in a single PID and transmitted over
  DVB-S2 transport streams. Multiple such T2MI streams can be sent in the same DVB-S2 mux, embedded in different
  PIDs.

To make use of this functionality, user space programs may need to request  DTV_OUTPUT_BBFRAMES in the frontend
FE_SET_PROPERTY ioctl or using the module option bbframes_auto. Moreover, additional demux ioctls  need
to be used to internally demux embedded transport streams. In case the new demux ioctls are not used
(e.g., in legacy programs) neumoDVB tries to select a reasonable default:
When an ISI (stream_id) was specified during tuning (frontend ioctl FE_SET_PROPERTY), this ISI will be
used for programs not calling the DMX_SET_STID_STREAM demux ioctl. This will result in legacy programs
continuing to work ``as is''.

* *DMX_SET_STID_STREAM* Call this ioctl first to specify the ISI to demux in a multi-stream.
  Each open file descriptor of the demux device can only handle one transport stream. The selected ISI
  therefore cannot be changed and all remaining ioctls will apply to the selected stream. It is however
  possible to open the demux device multiple times in parallel and select a different ISI in each session.
  This allows all streams in a multi-stream mux to be received simultaneously. Also, if a stream_id
  was specifified during tuning, that stream_id (ISI) will be used if the DMX_SET_STID_STREAM is not
  called.

  It is illegal to call DMX_SET_STID_STREAM multiple times on the same open demux. It is also illegal
  to call DMX_SET_STID_STREAM after a DMX_SET_T2MI_STREAM, DMX_SET_PES_FILTER,
  DMX_SET_FILTER or DMX_ADD_PID ioctl. The only exception is immediately after a DMX_STOP call, which resets the
  internal state to the initial one.

* *DMX_SET_T2MI_STREAM* Call this ioctl first, or after DMX_SET_STID_STREAM, to specify which T2MI stream
  to demux internally. All remaining ioctls will now apply to the thus selected transport stream.
  In theory this selected transport stream can it self contain one or more T2MI streams, although this never
  occurs in practice. It is then allowed to call DMX_SET_T2MI_STREAM a second time to select the desired
  stream.

  Again, it is illegal to call DMX_SET_T2MI_STREAM after  DMX_SET_PES_FILTER, DMX_SET_FILTER or DMX_ADD_PID.

*  *DMX_SET_FILTER* and *DMX_SET_PES_FILTER*, *DMX_ADD_PID* behave as in DVB-V5 api. Speficically,
   DMX_SET_PES_FILTER is used to select a first PID to add to the output, and DMX_ADD_PID to add addional
   pids.


---

# Version History
For a detailed list of changes and release notes, see [versions.md](versions.md).

# Known problems
* si2183 based cards: blindscan cannot discover stream_ids in multistreams, unless it is given
 one of them when starting the blindscan. In that case, it will find the other ones


User space tools for using blindscan can be found at
https://github.com/deeptho/blindscan



